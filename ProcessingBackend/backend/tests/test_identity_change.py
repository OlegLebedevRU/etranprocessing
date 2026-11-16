"""Identity changes: tenant isolation, durable issuance and failure-only diagnostics."""

from datetime import UTC, datetime, timedelta
from types import SimpleNamespace
from unittest.mock import AsyncMock, MagicMock
from uuid import uuid4

import pytest
from etranprocessing_db.models.device_events import TerminalIdentityChange

from app.models import CertificatePin, Terminal
from app.routers import certificates
from app.services import identity_change
from tests.test_certificate_pin_contract import _generate_csr, _mock_sign_csr


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.fixture
def context():
    terminal = Terminal(
        id=101,
        device_id=4625,
        sn="a2b0004625c18468d030726",
        org_id=1,
        cert_serial="A" * 40,
        is_active=True,
    )
    pin = CertificatePin(
        id=51,
        pin="fixture-pin",
        terminal_id=101,
        org_id=1,
        status="pending",
        purpose="setup",
        expires_at=datetime.now(UTC) + timedelta(hours=1),
    )
    change = TerminalIdentityChange(
        terminal_id=101,
        operation_id=uuid4(),
        source_org_id=1,
        target_org_id=2,
        old_serial=terminal.cert_serial,
        status="pending",
        requested_by="fixture-admin",
    )
    db = AsyncMock()
    db.add = MagicMock()
    db.scalar.return_value = None
    result = MagicMock()
    result.scalar_one_or_none.return_value = None
    db.execute.return_value = result
    return terminal, pin, change, db


@pytest.mark.anyio
async def test_check_returns_target_without_switching(context, monkeypatch):
    terminal, pin, change, db = context
    monkeypatch.setattr(
        certificates, "_find_terminal_by_pin", AsyncMock(return_value=(terminal, pin))
    )
    monkeypatch.setattr(
        identity_change, "pending_change", AsyncMock(return_value=change)
    )
    response = await certificates._handle_check({"pin": pin.pin}, db)
    assert "O=2,OU=4625" in response.body.decode("cp1251")
    assert terminal.org_id == 1 and terminal.cert_serial == "A" * 40
    db.commit.assert_not_awaited()


@pytest.mark.anyio
@pytest.mark.parametrize("csr_org,success", [(1, False), (2, True)])
async def test_setup_switches_only_for_target_csr_even_if_iot_fails(
    context, monkeypatch, csr_org, success
):
    terminal, pin, change, db = context
    monkeypatch.setattr(
        certificates, "_find_terminal_by_pin", AsyncMock(return_value=(terminal, pin))
    )
    monkeypatch.setattr(
        identity_change, "pending_change", AsyncMock(return_value=change)
    )
    signer = AsyncMock(side_effect=_mock_sign_csr)
    monkeypatch.setattr(certificates, "sign_csr", signer)
    sync = AsyncMock(return_value=False)
    monkeypatch.setattr(identity_change, "sync_iot", sync)
    csr = _generate_csr(org_id=csr_org)
    response = await certificates._handle_setup(
        {"pin": pin.pin}, SimpleNamespace(body=AsyncMock(return_value=csr.encode())), db
    )
    if not success:
        assert "Error" in response.body.decode("cp1251")
        signer.assert_not_awaited()
        assert terminal.org_id == 1 and pin.status == "pending"
        return
    assert "<Result>OK</Result>" in response.body.decode("cp1251")
    assert terminal.org_id == 2 and pin.status == "used"
    assert change.status == "sync_pending" and change.old_serial == "A" * 40
    assert change.new_serial == terminal.cert_serial and change.certificate_response
    sync.assert_awaited_once_with(db, terminal.id, change.operation_id)
    license_updates = [
        call.args[0]
        for call in db.execute.call_args_list
        if str(call.args[0]).startswith("UPDATE licenses")
    ]
    assert len(license_updates) == 1
    params = license_updates[0].compile().params
    assert params["balance"] == 0 and params["monthly_price_override_minor"] is None
    assert "expires_at" not in params
    assert not any(
        "UPDATE payments" in str(call.args[0]) for call in db.execute.call_args_list
    )


@pytest.mark.anyio
async def test_transfer_response_survives_worker_restart_and_is_csr_bound(context):
    _, _, change, db = context
    csr = _generate_csr(org_id=2)
    import hashlib

    from cryptography import x509
    from cryptography.hazmat.primitives import serialization

    change.status = "sync_pending"
    change.issued_at = datetime.now(UTC)
    change.csr_sha256 = hashlib.sha256(
        x509.load_pem_x509_csr(csr.encode()).public_bytes(serialization.Encoding.DER)
    ).hexdigest()
    change.certificate_response = "fixture-pkcs7-response"
    db.execute.return_value.scalar_one_or_none.return_value = change
    certificates._recent_setup_cache.clear()
    response = await certificates._check_setup_retry(
        "fixture-pin", "", db, csr.encode()
    )
    assert response and "fixture-pkcs7-response" in response.body.decode()
    assert (
        await certificates._check_setup_retry(
            "fixture-pin", "", db, _generate_csr(org_id=2).encode()
        )
        is None
    )
    change.issued_at -= timedelta(minutes=16)
    assert (
        await certificates._check_setup_retry("fixture-pin", "", db, csr.encode())
        is None
    )


@pytest.mark.anyio
async def test_old_serial_incident_is_deduplicated_and_unrelated_serial_is_not_reported(
    context, monkeypatch
):
    terminal, _, change, db = context
    change.status = "completed"
    change.new_serial = "B" * 40
    db.scalar.return_value = change
    record = AsyncMock()
    monkeypatch.setattr(identity_change, "add_incident", record)
    identity_change._rejected_samples.clear()
    await identity_change.record_old_certificate(db, terminal, "0" + "a" * 40)
    await identity_change.record_old_certificate(db, terminal, "A" * 40)
    await identity_change.record_old_certificate(db, terminal, "C" * 40)
    record.assert_awaited_once()
    assert record.call_args.args[2] == "identity_old_certificate"


@pytest.mark.anyio
async def test_successful_terminal_auth_does_not_query_identity_or_create_incidents(
    monkeypatch,
):
    from app.dependencies import get_current_terminal
    from tests.test_cert_serial_auto_bind import _make_mock_request

    terminal = Terminal(
        id=1671,
        device_id=773,
        sn="a4b0000773c12345d210826",
        cert_serial="52B8E528000400002E35",
        org_id=1,
        is_active=True,
    )
    db = AsyncMock()
    db.add = MagicMock()
    exact = MagicMock()
    exact.scalar_one_or_none.return_value = terminal
    discovery = MagicMock()
    discovery.scalar_one_or_none.return_value = None
    db.execute.side_effect = [exact, discovery]
    incident = AsyncMock()
    monkeypatch.setattr(identity_change, "record_old_certificate", incident)
    request = _make_mock_request(
        path="/api/licensebilling",
        dn=f"CN={terminal.sn},OU=773,O=1",
        serial=terminal.cert_serial,
        issuer="CN=iot.leo4.ru",
    )
    assert await get_current_terminal(request, db) is terminal
    incident.assert_not_awaited()
    db.scalar.assert_not_awaited()
    db.get.assert_not_awaited()
    assert db.execute.await_count == 2  # existing terminal lookup + discovery only


@pytest.mark.anyio
async def test_legacy_cn_fallback_cannot_reuse_previous_tenant_certificate(monkeypatch):
    from fastapi import HTTPException

    from app import dependencies
    from tests.test_cert_serial_auto_bind import _make_mock_request

    terminal = Terminal(
        id=1671, device_id=773, sn="fixture-sn", cert_serial="B" * 40, org_id=2
    )
    db = AsyncMock()
    db.add = MagicMock()
    old_org = MagicMock()
    old_org.scalar_one_or_none.return_value = None
    by_sn = MagicMock()
    by_sn.scalar_one_or_none.return_value = terminal
    db.execute.side_effect = [old_org, by_sn]
    incident = AsyncMock()
    monkeypatch.setattr(identity_change, "record_old_certificate", incident)
    monkeypatch.setattr(dependencies, "record_terminal_discovery", AsyncMock())
    request = _make_mock_request(
        path="/api/licensebilling",
        dn="CN=fixture-sn,OU=773,O=1",
        serial="123456",
        issuer="CN=SubCA",
    )
    with pytest.raises(HTTPException) as exc:
        await dependencies.get_current_terminal(request, db)
    assert exc.value.status_code == 401 and "tenant_mismatch" in str(exc.value.detail)
    incident.assert_awaited_once_with(db, terminal, "123456")
    assert terminal.cert_serial == "B" * 40


@pytest.mark.anyio
@pytest.mark.parametrize("provider", ["success", "unavailable", "wrong_operation"])
async def test_iot_sync_contract_and_actionable_failure(context, monkeypatch, provider):
    import json

    import httpx

    terminal, _, change, db = context
    terminal.org_id = 2
    terminal.cert_serial = "B" * 40
    change.status = "sync_pending"
    change.new_serial = terminal.cert_serial
    db.scalar.return_value = terminal
    db.get.return_value = change
    monkeypatch.setattr(
        identity_change.settings,
        "identity_change_iot_url",
        "https://iot.example.invalid",
    )
    monkeypatch.setattr(
        identity_change.settings, "identity_change_iot_key", "fixture-service-key"
    )
    actual_client = httpx.AsyncClient
    calls = []

    def handle(request):
        calls.append(request)
        assert request.url.path == "/api/internal/v1/provisioning/identity-transfers"
        assert request.headers["X-Internal-Service-Key"] == "fixture-service-key"
        payload = json.loads(request.content)
        assert payload == {
            "operation_id": str(change.operation_id),
            "terminal_id": terminal.id,
            "device_id": terminal.device_id,
            "sn": terminal.sn,
            "source_org_id": 1,
            "target_org_id": 2,
        }
        if provider == "unavailable":
            return httpx.Response(503)
        return httpx.Response(
            200,
            json={
                "operation_id": str(change.operation_id)
                if provider == "success"
                else str(uuid4()),
                "status": "completed",
                "org_id": 2,
                "sn": terminal.sn,
            },
        )

    monkeypatch.setattr(
        identity_change.httpx,
        "AsyncClient",
        lambda **kwargs: actual_client(transport=httpx.MockTransport(handle), **kwargs),
    )
    incident = AsyncMock()
    monkeypatch.setattr(identity_change, "add_incident", incident)
    completed = await identity_change.sync_iot(db, terminal.id, change.operation_id)
    assert completed is (provider == "success")
    assert terminal.org_id == 2  # failed sync never silently rolls back issued identity
    if completed:
        assert change.status == "completed"
        assert await identity_change.sync_iot(db, terminal.id, change.operation_id)
        assert len(calls) == 1
        incident.assert_not_awaited()
    else:
        assert change.status == "sync_pending"
        incident.assert_awaited_once()
        db.commit.assert_awaited_once()


@pytest.mark.anyio
async def test_previous_tenant_payment_id_cannot_be_replayed_as_new_owner():
    from app.models import Payment
    from app.services.payment_service import PaymentService

    db = AsyncMock()
    result = MagicMock()
    result.scalar_one_or_none.return_value = Payment(
        paym_id=1, paym_ext_id="fixture-old-payment", org_id=1, terminal_id=1
    )
    db.execute.return_value = result
    service = PaymentService(db)
    with pytest.raises(ValueError, match="another terminal identity"):
        await service.create_payment(
            Terminal(id=1, org_id=2),
            tsp_code=7,
            amount=100,
            paym_ext_id="fixture-old-payment",
            params={},
        )
    assert db.execute.await_count == 1
