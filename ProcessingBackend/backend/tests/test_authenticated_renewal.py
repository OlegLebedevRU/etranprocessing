"""Renewal authorization and lost-response recovery, without a real CA or DB."""

import urllib.parse
from datetime import UTC, datetime, timedelta
from unittest.mock import AsyncMock, MagicMock

import pytest
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID
from httpx import ASGITransport, AsyncClient
from sqlalchemy.dialects import postgresql

from app.database import get_db
from app.main import app
from app.models import CertificatePin, Terminal
from app.routers import certificates as module
from tests.test_certificate_pin_contract import _generate_csr, _mock_sign_csr

SERIAL = "4" * 40
SN = "fixture-sn"


@pytest.fixture
def anyio_backend():
    return "asyncio"


def identity_headers(*, expired=False, issuer="iot.leo4.ru", sn=SN, serial=SERIAL):
    key = rsa.generate_private_key(65537, 2048)
    now = datetime.now(UTC)
    cert = (
        x509.CertificateBuilder()
        .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, sn)]))
        .issuer_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, issuer)]))
        .public_key(key.public_key())
        .serial_number(int(serial, 16))
        .not_valid_before(now - timedelta(days=2))
        .not_valid_after(now + timedelta(days=-1 if expired else 1))
        .sign(key, hashes.SHA256())
    )
    return {
        "X-Client-Cert-Verified": "SUCCESS",
        "X-Client-Cert-Serial": serial,
        "X-SSL-Client-Cert": urllib.parse.quote(
            cert.public_bytes(serialization.Encoding.PEM).decode()
        ),
    }


@pytest.fixture
def renewal(monkeypatch):
    terminal = Terminal(
        id=101,
        org_id=1,
        device_id=773,
        sn=SN,
        is_active=True,
        cert_serial=SERIAL,
        cert_not_valid_after=datetime.now(UTC) + timedelta(days=1),
    )
    pin = CertificatePin(
        id=1,
        terminal_id=101,
        org_id=1,
        pin="123456",
        purpose="renew",
        status="pending",
        expires_at=datetime.now(UTC) + timedelta(hours=24),
    )
    db = MagicMock()
    db.execute = AsyncMock()
    db.commit = AsyncMock()
    result = MagicMock()
    result.scalar_one.return_value = terminal
    result.scalar_one_or_none.return_value = None
    db.execute.return_value = result
    db.scalar = AsyncMock(return_value=True)
    lookup = AsyncMock(return_value=(terminal, pin))
    monkeypatch.setattr(module, "_find_terminal_by_pin", lookup)
    monkeypatch.setattr(module, "subscription_allowance", AsyncMock(return_value=True))
    signer = AsyncMock(side_effect=_mock_sign_csr)
    monkeypatch.setattr(module, "sign_csr", signer)

    async def dependency():
        yield db

    app.dependency_overrides[get_db] = dependency
    yield terminal, pin, db, signer, lookup
    app.dependency_overrides.clear()


@pytest.mark.anyio
@pytest.mark.parametrize(
    "failure",
    ["missing", "unverified", "expired", "legacy", "wrong_sn", "wrong_serial"],
)
async def test_renewal_identity_matrix(renewal, failure):
    _, _, _, signer, _ = renewal
    headers = identity_headers(
        expired=failure == "expired",
        issuer="SubCA" if failure == "legacy" else "iot.leo4.ru",
        sn="another-sn" if failure == "wrong_sn" else SN,
        serial="5" * 40 if failure == "wrong_serial" else SERIAL,
    )
    if failure == "missing":
        headers = {}
    if failure == "unverified":
        headers["X-Client-Cert-Verified"] = "NONE"
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        response = await client.post(
            "/api/certificates/renew",
            headers=headers,
            json={"function": "check", "pin": "123456"},
        )
    assert response.status_code == 401
    signer.assert_not_awaited()


@pytest.mark.anyio
async def test_same_csr_recovery_survives_cache_reset(renewal):
    terminal, pin, db, signer, _ = renewal
    headers = identity_headers()
    csr = _generate_csr(cn=SN, device_id=773, org_id=1)
    body = {"function": "setup", "pin": "123456", "csr": csr}
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        first = await client.post("/api/certificates/renew", headers=headers, json=body)
        assert first.status_code == 200 and "<Result>OK</Result>" in first.text
        locked_sql = str(
            db.execute.call_args_list[0].args[0].compile(dialect=postgresql.dialect())
        )
        # PostgreSQL cannot lock the nullable, eagerly joined terminal type.
        assert "LEFT OUTER JOIN terminal_types" in locked_sql
        assert locked_sql.endswith("FOR UPDATE OF terminals")
        serial = terminal.cert_serial
        assert (
            serial != SERIAL
            and pin.status == "used"
            and pin.renewal_auth_serial == SERIAL
        )
        module._recent_setup_cache.clear()
        replay = await client.post(
            "/api/certificates/renew", headers=headers, json=body
        )
        assert replay.content == first.content
        assert terminal.cert_serial == serial
        assert signer.await_count == 1 and db.commit.await_count == 1
        # A new CSR from a duplicate task does not reuse or issue another certificate.
        body["csr"] = _generate_csr(cn=SN, device_id=773, org_id=1)
        rejected = await client.post(
            "/api/certificates/renew", headers=headers, json=body
        )
        assert rejected.status_code == 409 and signer.await_count == 1


@pytest.mark.anyio
@pytest.mark.parametrize("failure", ["inactive", "subscription", "recovery_deadline"])
async def test_changed_rights_and_recovery_deadline(renewal, monkeypatch, failure):
    terminal, pin, _, signer, _ = renewal
    if failure == "inactive":
        terminal.is_active = False
    if failure == "subscription":
        monkeypatch.setattr(
            module, "subscription_allowance", AsyncMock(return_value=False)
        )
    if failure == "recovery_deadline":
        pin.status = "used"
        pin.used_at = datetime.now(UTC) - timedelta(minutes=16)
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        response = await client.post(
            "/api/certificates/renew",
            headers=identity_headers(),
            json={
                "function": "setup",
                "pin": "123456",
                "csr": _generate_csr(cn=SN, device_id=773, org_id=1),
            },
        )
    assert response.status_code == (409 if failure == "recovery_deadline" else 403)
    signer.assert_not_awaited()


def test_renewal_proxy_is_explicit_and_service_pin_route_is_private():
    from tests.test_nginx_routing_contract import LEGACY_NGINX_CONFIG

    text = LEGACY_NGINX_CONFIG.read_text(encoding="utf-8")
    location = text.split("location = /api/certificates/renew {", 1)[1].split(
        "proxy_buffering off;", 1
    )[0]
    assert "if ($ssl_client_verify != SUCCESS)" in location
    assert "X-Client-Cert-Serial $ssl_client_serial" in location
    assert "X-SSL-Client-Cert $ssl_client_escaped_cert" in location
    assert "location = /api/certificates/renewal-pins { return 404; }" in text


@pytest.mark.anyio
@pytest.mark.parametrize("function", ["check", "setup"])
async def test_renewal_pin_cannot_pass_anonymous_flow(monkeypatch, function):
    pin = CertificatePin(
        purpose="renew",
        status="pending",
        pin="123456",
        expires_at=datetime.now(UTC) + timedelta(hours=1),
    )
    db = MagicMock()
    result = MagicMock()
    result.one_or_none.return_value = (Terminal(id=1), pin)
    result.scalar_one_or_none.return_value = pin
    db.execute = AsyncMock(return_value=result)
    signer = AsyncMock()
    monkeypatch.setattr(module, "sign_csr", signer)

    async def dependency():
        yield db

    app.dependency_overrides[get_db] = dependency
    try:
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            response = await client.post(
                f"/api/certificates/?function={function}&pin=123456"
            )
        assert "<Result>Error</Result>" in response.text
        signer.assert_not_awaited()
    finally:
        app.dependency_overrides.clear()


def test_policy_expiry_uses_already_loaded_certificate_date():
    from app.services.leo4proxy_policy import get_leo4proxy_policy

    terminal = Terminal(
        sn=SN,
        is_active=True,
        cert_not_valid_after=datetime.now(UTC) - timedelta(seconds=1),
    )
    response = get_leo4proxy_policy(terminal)
    assert not response.mqtt_rtp_allowed and response.outgoing_https_allowed
    assert response.stop_facts == ["certificate_expired"]


@pytest.mark.anyio
@pytest.mark.parametrize(
    "pin_id,status", [(None, "pending"), (8, "pending"), (None, "used"), (8, "used")]
)
async def test_provider_reuses_unfinished_pin_without_new_outbox(
    monkeypatch, pin_id, status
):
    from app.schemas.certificates import RenewalPinRequest

    target = Terminal(
        id=3,
        org_id=1,
        sn=SN,
        is_active=True,
        cert_serial=SERIAL,
        cert_not_valid_after=datetime.now(UTC) + timedelta(days=1),
    )
    pin = CertificatePin(
        id=8,
        pin="000000",
        terminal_id=3,
        org_id=1,
        purpose="renew",
        status=status,
        expires_at=datetime.now(UTC) + timedelta(hours=1),
    )
    first = MagicMock()
    first.scalar_one_or_none.return_value = target
    second = MagicMock()
    second.scalar_one_or_none.return_value = pin
    db = MagicMock()
    db.execute = AsyncMock(side_effect=[first, second])
    db.commit = AsyncMock()
    monkeypatch.setattr(module.settings, "internal_service_key", "test-internal")
    monkeypatch.setattr(module, "subscription_allowance", AsyncMock(return_value=True))
    result = await module.issue_renewal_pin(
        RenewalPinRequest(tenant_id=1, terminal_id=3, sn=SN, pin_id=pin_id), db, "test"
    )
    assert (
        result.pin_id == 8
        and result.pin == "000000"
        and not db.add.called
        and not db.commit.called
    )
    locked_sql = str(
        db.execute.call_args_list[0].args[0].compile(dialect=postgresql.dialect())
    )
    assert "LEFT OUTER JOIN terminal_types" in locked_sql
    assert locked_sql.endswith("FOR UPDATE OF terminals")
    query = str(
        db.execute.call_args.args[0].compile(
            dialect=postgresql.dialect(), compile_kwargs={"literal_binds": True}
        )
    )
    assert (
        "purpose = 'renew'" in query
        and "terminal_cert_history.cert_serial" in query
        and "used_at >" in query
    )


@pytest.mark.anyio
async def test_recovery_cannot_restore_superseded_serial(monkeypatch):
    target = Terminal(id=3, org_id=1, sn=SN, is_active=True, cert_serial=SERIAL)
    pin = CertificatePin(id=8, renewal_auth_serial="5" * 40)
    db = MagicMock()
    db.scalar = AsyncMock(return_value=False)
    with pytest.raises(Exception) as denied:
        await module._verify_renewal_owner(target, (SN, "5" * 40), db, pin)
    assert denied.value.status_code == 409
