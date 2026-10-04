import json
from datetime import UTC, datetime, timedelta
from types import SimpleNamespace
from unittest.mock import AsyncMock, MagicMock
from uuid import UUID

import httpx
import pytest
from fastapi import HTTPException, Response
from sqlalchemy.dialects import postgresql

from app.models import Terminal
from app.routers import certificate_renewal as flow


@pytest.fixture
def anyio_backend():
    return "asyncio"


def terminal():
    return Terminal(
        id=3,
        org_id=1,
        device_id=773,
        sn="fixture-sn",
        is_active=True,
        cert_serial="A" * 40,
        cert_not_valid_after=datetime.now(UTC) + timedelta(days=30),
    )


@pytest.mark.parametrize("seconds", [121, 125, 180, 86400, 604800, 44640 * 60])
def test_queued_deadline_never_exceeds_pin(seconds):
    now = datetime.now(UTC)
    pin = flow.ProviderPin(
        pin_id=8,
        tenant_id=1,
        terminal_id=3,
        sn="fixture-sn",
        pin="000000",
        expires_at=now + timedelta(seconds=seconds),
    )
    body = flow.queue_body(773, pin, now)
    assert body["ttl"] * 60 <= seconds
    assert body["payload"]["dt"][0]["ttl_sec"] == 120
    assert "000000" not in repr(pin)
    assert "task_id" not in body and "operation_id" not in body


@pytest.mark.parametrize("seconds", [-1, 0, 60, 120])
def test_reject_insufficient_pin_lifetime(seconds):
    now = datetime.now(UTC)
    pin = flow.ProviderPin(
        pin_id=8,
        tenant_id=1,
        terminal_id=3,
        sn="fixture-sn",
        pin="000000",
        expires_at=now + timedelta(seconds=seconds),
    )
    with pytest.raises(HTTPException) as error:
        flow.queue_body(773, pin, now)
    assert error.value.status_code == 410


@pytest.mark.anyio
async def test_offline_queue_and_lost_answer_manual_retry_same_pin(monkeypatch):
    target = terminal()
    calls = []
    queued_count = 0
    real_client = httpx.AsyncClient

    async def transport(request):
        nonlocal queued_count
        body = json.loads(request.content)
        calls.append((request.url.path, body))
        if request.url.path.endswith("renewal-pins"):
            assert body.get("pin_id") in (None, 8)
            return httpx.Response(
                201,
                json={
                    "pin_id": 8,
                    "tenant_id": 1,
                    "terminal_id": 3,
                    "sn": "fixture-sn",
                    "pin": "000000",
                    "expires_at": (datetime.now(UTC) + timedelta(hours=24)).isoformat(),
                },
            )
        queued_count += 1
        if queued_count == 1:
            raise httpx.ReadTimeout("fixture loss", request=request)
        return httpx.Response(
            200, json={"id": "135a4120-9ba6-4f6c-8cac-4baf5df8f1df", "created_at": 1}
        )

    monkeypatch.setattr(
        flow.httpx,
        "AsyncClient",
        lambda **kw: real_client(transport=httpx.MockTransport(transport), **kw),
    )
    monkeypatch.setattr(flow, "owned_terminal", AsyncMock(return_value=target))
    monkeypatch.setattr(flow, "admission", AsyncMock(return_value=None))
    monkeypatch.setattr(
        flow,
        "ProcessingBackendPinClient",
        lambda: SimpleNamespace(
            base_url="http://localhost", service_token="test", _headers=dict
        ),
    )
    monkeypatch.setattr(flow.iot_client, "base_url", "http://localhost")
    monkeypatch.setattr(flow.iot_client, "service_token", "test")
    monkeypatch.setattr(
        flow.iot_client,
        "get_console_device",
        AsyncMock(
            return_value={"sn": "fixture-sn", "connection": {"is_online": False}}
        ),
    )
    db = SimpleNamespace(add=MagicMock(), commit=AsyncMock())
    user = {"org_id": 1, "sub": "fixture"}
    with pytest.raises(HTTPException) as lost:
        await flow.order_renewal(773, flow.RenewalOrderRequest(), Response(), user, db)
    assert lost.value.status_code == 502 and lost.value.detail["pin_id"] == 8
    assert "000000" not in json.dumps(lost.value.detail)
    assert not db.add.called
    result = await flow.order_renewal(
        773, flow.RenewalOrderRequest(pin_id=8), Response(), user, db
    )
    assert (
        result.task_id == UUID("135a4120-9ba6-4f6c-8cac-4baf5df8f1df")
        and "000000" not in result.model_dump_json()
    )
    assert calls[-2][1]["pin_id"] == 8 and calls[-1][1]["method_code"] == 7011
    audit = db.add.call_args.args[0]
    assert audit.operation_id == str(result.task_id) and "000000" not in json.dumps(
        audit.details
    )
    assert db.commit.await_count == 1


@pytest.mark.anyio
async def test_status_does_not_confirm_issuance_without_new_identity(monkeypatch):
    target = terminal()
    monkeypatch.setattr(flow, "owned_terminal", AsyncMock(return_value=target))
    monkeypatch.setattr(flow, "admission", AsyncMock(return_value=None))
    pin = SimpleNamespace(
        id=8,
        status="used",
        expires_at=datetime.now(UTC) + timedelta(hours=1),
        used_at=datetime.now(UTC),
    )
    queries = []

    async def scalar(query):
        queries.append(
            str(
                query.compile(
                    dialect=postgresql.dialect(), compile_kwargs={"literal_binds": True}
                )
            )
        )
        return pin if len(queries) == 1 else None

    db = SimpleNamespace(scalar=scalar)
    result = await flow.renewal_status(773, Response(), {"org_id": 1}, db)
    assert (
        result["status"] == "issued_waiting_identity" and result["observed_at"] is None
    )
    assert (
        "terminal_cert_discovery.cert_serial" in queries[1]
        and "is_valid IS true" in queries[1]
    )
    assert "pin" not in result and "renewal_response" not in result


@pytest.mark.anyio
async def test_admission_admin_expiry_and_subscription(monkeypatch):
    target = terminal()
    monkeypatch.setattr(
        flow, "check_terminal", AsyncMock(return_value=SimpleNamespace(allowed=True))
    )
    assert await flow.admission(target, MagicMock()) is None
    target.is_active = False
    assert await flow.admission(target, MagicMock())
    target.is_active = True
    target.cert_not_valid_after = datetime.now(UTC) - timedelta(seconds=1)
    assert await flow.admission(target, MagicMock())
    target.cert_not_valid_after = datetime.now(UTC) + timedelta(days=1)
    monkeypatch.setattr(
        flow,
        "check_terminal",
        AsyncMock(return_value=SimpleNamespace(allowed=False, reason="unpaid")),
    )
    assert await flow.admission(target, MagicMock()) == "unpaid"
