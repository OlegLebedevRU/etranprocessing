import json
from types import SimpleNamespace
from unittest.mock import AsyncMock

import httpx
import pytest
from fastapi import FastAPI, HTTPException

from app.auth import get_current_user
from app.database import get_db
from app.routers import mcp_proxy
from app.schemas.mcp_events import UserEventFilters, UserEventPage
from app.services.iot_client import IotPlatformClient, iot_client


def page():
    return {
        "items": [
            {
                "id": 10,
                "device_id": 773,
                "event_type_code": 999,
                "dev_event_id": 7,
                "created_at": "2026-10-01T12:00:00Z",
                "dev_timestamp": "2026-10-01T11:59:59Z",
                "payload": {
                    "300": [
                        {
                            "446": '{"result":"ok"}',
                            "447": -2,
                            "448": "12345678-1234-1234-1234-123456789abc",
                        }
                    ]
                },
            }
        ],
        "next_after_event_id": 10,
        "has_more": False,
    }


@pytest.fixture
def history_app(monkeypatch):
    app = FastAPI()
    app.include_router(mcp_proxy.router, prefix="/api")
    user = {"token_type": "api_token", "role_id": 1, "org_id": 1, "is_superuser": True}

    async def auth():
        return user

    async def db():
        yield AsyncMock()

    app.dependency_overrides[get_current_user] = auth
    app.dependency_overrides[get_db] = db
    access = AsyncMock(return_value=SimpleNamespace(sn="SN773", org_id=1))
    device = AsyncMock(
        return_value={
            "device_id": 773,
            "sn": "SN773",
            "connection": {"svc_connect": False},
        }
    )
    search = AsyncMock(return_value=UserEventPage.model_validate(page()))
    monkeypatch.setattr(mcp_proxy, "_verify_device_access", access)
    monkeypatch.setattr(iot_client, "get_console_device", device)
    monkeypatch.setattr(iot_client, "search_user_events", search)
    return app, user, access, device, search


@pytest.mark.anyio
async def test_history_offline_and_next_page_recheck_access(history_app):
    app, _, access, device, search = history_app
    async with httpx.AsyncClient(
        transport=httpx.ASGITransport(app=app), base_url="http://test"
    ) as client:
        first = await client.get(
            "/api/mcp/events/773", headers={"Authorization": "Bearer test"}
        )
        assert first.status_code == 200
        assert first.json() == page()
        access.return_value = SimpleNamespace(sn="SN773", org_id=2)
        second = await client.get(
            "/api/mcp/events/773?after_event_id=10",
            headers={"Authorization": "Bearer test"},
        )
        assert second.status_code == 403
    assert access.await_count == 2
    assert device.await_count == search.await_count == 1
    assert search.await_args.args[:2] == (773, 1)


@pytest.mark.anyio
@pytest.mark.parametrize(
    "change,status",
    [
        ("jwt", 401),
        ("role", 403),
        ("no_org", 403),
        ("mismatch", 409),
        ("missing", 409),
        ("network", 503),
        ("revoked", 401),
    ],
)
async def test_history_denies_invalid_context(history_app, change, status):
    app, user, _access, device, search = history_app
    if change == "jwt":
        user["token_type"] = "tenant"
    if change == "role":
        user["role_id"] = 4
    if change == "no_org":
        user["org_id"] = 0
    if change == "mismatch":
        device.return_value["sn"] = "OTHER"
    if change == "missing":
        device.return_value = None
    if change == "network":
        device.side_effect = httpx.ConnectError("offline")
    if change == "revoked":

        async def revoked():
            raise HTTPException(status_code=401, detail="revoked")

        app.dependency_overrides[get_current_user] = revoked
    async with httpx.AsyncClient(
        transport=httpx.ASGITransport(app=app), base_url="http://test"
    ) as client:
        result = await client.get(
            "/api/mcp/events/773", headers={"Authorization": "Bearer test"}
        )
    assert result.status_code == status
    search.assert_not_awaited()


@pytest.mark.anyio
@pytest.mark.parametrize(
    "query",
    [
        "org_id=2",
        "limit=101",
        "limit=0",
        "events_include=75",
        "correlation_id=bad",
        "correlation_id=12345678123412341234123456789abc",
        "after_event_id=0",
        "created_from=2026-10-01T12:00:00",
        "created_from=2026-10-02T00:00:00Z&created_to=2026-10-01T00:00:00Z",
    ],
)
async def test_history_invalid_filters(history_app, query):
    app, _, access, _, search = history_app
    async with httpx.AsyncClient(
        transport=httpx.ASGITransport(app=app), base_url="http://test"
    ) as client:
        result = await client.get(
            "/api/mcp/events/773?" + query, headers={"Authorization": "Bearer test"}
        )
    assert result.status_code == 422
    access.assert_not_awaited()
    search.assert_not_awaited()


@pytest.mark.anyio
async def test_internal_client_headers_filters_payload(monkeypatch):
    seen = []

    def handle(request):
        seen.append(request)
        return httpx.Response(200, json=page())

    real_client = httpx.AsyncClient
    monkeypatch.setattr(
        httpx,
        "AsyncClient",
        lambda **kwargs: real_client(transport=httpx.MockTransport(handle), **kwargs),
    )
    client = IotPlatformClient(base_url="http://test", service_token="fixture-key")
    result = await client.search_user_events(
        773,
        1,
        UserEventFilters(
            correlation_id="12345678-1234-1234-1234-123456789ABC",
            events_include=[991, 999],
            after_event_id=9,
            created_from="2026-10-01T14:00:00+03:00",
            limit=2,
        ),
    )
    req = seen[0]
    assert req.url.path == "/api/internal/v1/device-events/search"
    assert req.headers["X-Org-Id"] == "1"
    assert "X-Role" not in req.headers and "Authorization" not in req.headers
    assert req.headers["X-Internal-Service-Key"] == "fixture-key"
    assert req.url.params.get_list("events_include") == ["991", "999"]
    assert req.url.params["correlation_id"].endswith("9abc")
    assert req.url.params["created_from"] == "2026-10-01T11:00:00Z"
    assert result.items[0].payload == page()["items"][0]["payload"]


@pytest.mark.anyio
@pytest.mark.parametrize(
    "failure", ["timeout", "http", "json", "wrong_device", "wrong_code", "too_many"]
)
async def test_internal_failure_is_not_empty_history(monkeypatch, failure):
    def handle(request):
        if failure == "timeout":
            raise httpx.ReadTimeout("timeout", request=request)
        if failure == "http":
            return httpx.Response(403, json={"detail": "secret detail"})
        if failure == "json":
            return httpx.Response(200, text="broken")
        data = page()
        if failure == "wrong_device":
            data["items"][0]["device_id"] = 774
        if failure == "wrong_code":
            data["items"][0]["event_type_code"] = 75
        if failure == "too_many":
            data["items"] *= 2
        return httpx.Response(200, content=json.dumps(data))

    real_client = httpx.AsyncClient
    monkeypatch.setattr(
        httpx,
        "AsyncClient",
        lambda **kwargs: real_client(transport=httpx.MockTransport(handle), **kwargs),
    )
    client = IotPlatformClient(base_url="http://test", service_token="fixture-key")
    with pytest.raises(HTTPException) as err:
        await client.search_user_events(773, 1, UserEventFilters(limit=1))
    assert err.value.status_code == 503
    assert err.value.detail == "IoT event history unavailable"
