"""MenuBuilder side of the internal organization reservation contract."""

import httpx
import pytest
from fastapi import HTTPException

from app.services.iot_client import IotPlatformClient


@pytest.mark.anyio
async def test_reservation_sends_internal_auth_and_accepts_replay(monkeypatch):
    original_client = httpx.AsyncClient
    seen: list[httpx.Request] = []

    def respond(request: httpx.Request) -> httpx.Response:
        seen.append(request)
        return httpx.Response(
            200,
            json={
                "operation_id": "l4desk-registration:7",
                "org_id": 1001,
                "replayed": True,
            },
        )

    monkeypatch.setattr(
        httpx,
        "AsyncClient",
        lambda **kwargs: original_client(
            transport=httpx.MockTransport(respond), **kwargs
        ),
    )
    client = IotPlatformClient(base_url="http://app1:8000", service_token="test-key")

    assert (
        await client.reserve_org_id(
            operation_id="l4desk-registration:7", minimum_org_id=1002
        )
        == 1001
    )
    assert seen[0].url.path == "/api/internal/v1/provisioning/organizations/reserve"
    assert seen[0].headers["X-Internal-Service-Key"] == "test-key"
    assert seen[0].read().decode() == (
        '{"operation_id":"l4desk-registration:7","minimum_org_id":1002}'
    )


@pytest.mark.anyio
async def test_reservation_fails_closed_without_internal_credentials():
    client = IotPlatformClient(base_url="http://app1:8000", service_token="")
    with pytest.raises(HTTPException) as exc:
        await client.reserve_org_id(
            operation_id="l4desk-registration:8", minimum_org_id=1
        )
    assert exc.value.status_code == 503
    assert exc.value.detail == {"code": "org_allocator_unavailable"}


@pytest.mark.anyio
async def test_reservation_preserves_explicit_id_conflict(monkeypatch):
    original_client = httpx.AsyncClient
    monkeypatch.setattr(
        httpx,
        "AsyncClient",
        lambda **kwargs: original_client(
            transport=httpx.MockTransport(
                lambda request: httpx.Response(
                    409, json={"detail": {"code": "org_id_already_in_use"}}
                )
            ),
            **kwargs,
        ),
    )
    client = IotPlatformClient(base_url="http://app1:8000", service_token="test-key")
    with pytest.raises(HTTPException) as exc:
        await client.reserve_org_id(
            operation_id="l4desk-admin-org:8", minimum_org_id=1, requested_org_id=4
        )
    assert exc.value.status_code == 409
    assert exc.value.detail == {"code": "org_id_already_in_use"}
