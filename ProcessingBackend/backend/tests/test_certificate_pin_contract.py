"""Contract, security, idempotency, race, replay, and backward compatibility tests

for Certificate PIN and CSR/X.509 issuance (L4D-06A-PB).
"""

import base64
import xml.etree.ElementTree as ET
from datetime import UTC, datetime, timedelta
from typing import Any
from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from cryptography import x509
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID
from httpx import ASGITransport, AsyncClient

from app.config import settings
from app.database import get_db
from app.main import app
from app.models import (
    CertificatePin,
    L4DeskAuditEvent,
    L4DeskTerminal,
    Terminal,
    TerminalCertHistory,
)
from app.routers.certificates import _find_terminal_by_pin, _recent_setup_cache
from app.services.ca import CAResponse


@pytest.fixture(autouse=True)
def cleanup():
    app.dependency_overrides.clear()
    _recent_setup_cache.clear()
    orig_token = settings.service_auth_token
    orig_key = settings.internal_service_key
    yield
    app.dependency_overrides.clear()
    _recent_setup_cache.clear()
    settings.service_auth_token = orig_token
    settings.internal_service_key = orig_key


@pytest.fixture
def anyio_backend():
    return "asyncio"


def _generate_csr(
    cn: str = "a2b0004625c18468d030726",
    device_id: int = 4625,
    org_id: int = 1,
    corrupt_signature: bool = False,
) -> str:
    key = rsa.generate_private_key(65537, 2048, default_backend())
    subject = x509.Name(
        [
            x509.NameAttribute(NameOID.COMMON_NAME, cn),
            x509.NameAttribute(NameOID.ORGANIZATION_NAME, str(org_id)),
            x509.NameAttribute(NameOID.ORGANIZATIONAL_UNIT_NAME, str(device_id)),
            x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME, "msk"),
            x509.NameAttribute(NameOID.COUNTRY_NAME, "ru"),
            x509.NameAttribute(NameOID.LOCALITY_NAME, "42"),
            x509.NameAttribute(NameOID.EMAIL_ADDRESS, "1.terminal@forpay.ru"),
        ]
    )
    builder = x509.CertificateSigningRequestBuilder().subject_name(subject)
    csr = builder.sign(key, hashes.SHA256(), default_backend())
    csr_pem = csr.public_bytes(serialization.Encoding.PEM).decode("utf-8")

    if corrupt_signature:
        # Mutate the base64 content so the signature fails cryptographic verification
        lines = csr_pem.strip().split("\n")
        body_lines = lines[1:-1]
        if body_lines:
            last = list(body_lines[-1])
            last[0] = "A" if last[0] != "A" else "B"
            body_lines[-1] = "".join(last)
            csr_pem = "\n".join([lines[0]] + body_lines + [lines[-1]]) + "\n"

    return csr_pem


async def _mock_sign_csr(
    csr_pem: str, sign: str = "", cn: str = "", exp_days: int | None = None
) -> CAResponse:
    csr = x509.load_pem_x509_csr(csr_pem.encode("utf-8"), default_backend())
    ca_key = rsa.generate_private_key(65537, 2048, default_backend())
    ca_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "Test CA")])
    now = datetime.now(UTC).replace(tzinfo=None)
    days = exp_days or 365
    cert = (
        x509.CertificateBuilder()
        .subject_name(csr.subject)
        .issuer_name(ca_name)
        .public_key(csr.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now)
        .not_valid_after(now + timedelta(days=days))
        .sign(ca_key, hashes.SHA256(), default_backend())
    )
    ca_cert = (
        x509.CertificateBuilder()
        .subject_name(ca_name)
        .issuer_name(ca_name)
        .public_key(ca_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now)
        .not_valid_after(now + timedelta(days=days))
        .sign(ca_key, hashes.SHA256(), default_backend())
    )
    return CAResponse(
        cert_pem=cert.public_bytes(serialization.Encoding.PEM).decode("utf-8"),
        ca_pem=ca_cert.public_bytes(serialization.Encoding.PEM).decode("utf-8"),
        serial_number=format(cert.serial_number, "X"),
        not_valid_after=(now + timedelta(days=days)).strftime("%Y-%m-%d %H:%M:%S"),
    )


class MockDatabase:
    """In-memory simulation of SQLAlchemy AsyncSession for certificate PIN contract tests."""

    def __init__(self):
        self.terminals: dict[int, Terminal] = {}
        self.pins: list[CertificatePin] = []
        self.audit_events: list[L4DeskAuditEvent] = []
        self.l4_terminals: dict[int, L4DeskTerminal] = {}
        self.cert_history: list[TerminalCertHistory] = []
        self._next_pin_id = 1
        self._next_audit_id = 1
        self.commit_count = 0
        self.flush_count = 0

    def add(self, obj: Any):
        if isinstance(obj, CertificatePin):
            if not getattr(obj, "id", None):
                obj.id = self._next_pin_id
                self._next_pin_id += 1
            if not getattr(obj, "created_at", None):
                obj.created_at = datetime.now(UTC)
            self.pins.append(obj)
        elif isinstance(obj, L4DeskAuditEvent):
            if not getattr(obj, "id", None):
                obj.id = self._next_audit_id
                self._next_audit_id += 1
            if not getattr(obj, "occurred_at", None):
                obj.occurred_at = datetime.now(UTC)
            self.audit_events.append(obj)
        elif isinstance(obj, TerminalCertHistory):
            self.cert_history.append(obj)

    async def flush(self):
        self.flush_count += 1

    async def commit(self):
        self.commit_count += 1

    async def rollback(self):
        pass

    async def execute(self, statement: Any, *args, **kwargs):
        class ExecResult:
            def __init__(self, items):
                self._items = items

            def scalar_one_or_none(self):
                return self._items[0] if self._items else None

            def one_or_none(self):
                return self._items[0] if self._items else None

            def scalars(self):
                class ScalarsResult:
                    def __init__(self, it):
                        self._it = it

                    def all(self):
                        return self._it

                return ScalarsResult(self._items)

        sql_str = str(statement)
        try:
            params = statement.compile().params
        except Exception:  # noqa: BLE001
            params = {}

        # 1. Update CertificatePin status="expired"
        if "UPDATE certificate_pins" in sql_str:
            for p in self.pins:
                if p.status == "pending":
                    p.status = "expired"
            return ExecResult([])

        # 2. Select Terminal by id
        if "FROM terminals" in sql_str and "JOIN certificate_pins" not in sql_str:
            matching_terms = list(self.terminals.values())
            for k, v in params.items():
                if "id" in k.lower():
                    matching_terms = [t for t in matching_terms if t.id == v]
            return ExecResult(matching_terms)

        # 3. Join Terminal and CertificatePin
        if (
            "JOIN certificate_pins" in sql_str
            or "FROM terminals, certificate_pins" in sql_str
            or "certificate_pins JOIN terminals" in sql_str
            or ("terminals" in sql_str and "certificate_pins" in sql_str)
        ):
            pin_val = None
            status_val = None
            for k, v in params.items():
                if "pin" in k.lower():
                    pin_val = v
                elif "status" in k.lower():
                    status_val = v
            matches = []
            for p in self.pins:
                if (
                    (pin_val is None or p.pin == pin_val)
                    and (status_val is None or p.status == status_val)
                    and p.terminal_id in self.terminals
                ):
                    matches.append((self.terminals[p.terminal_id], p))
            return ExecResult(matches)

        # 4. Select CertificatePin by pin / id
        if "FROM certificate_pins" in sql_str:
            matching_pins = list(self.pins)
            for k, v in params.items():
                if "pin" in k.lower():
                    matching_pins = [p for p in matching_pins if p.pin == v]
                elif "id" in k.lower():
                    matching_pins = [p for p in matching_pins if p.id == v]
            return ExecResult(matching_pins)

        # 5. L4DeskAuditEvent
        if "FROM l4desk_audit_events" in sql_str:
            matching_audits = list(self.audit_events)
            for k, v in params.items():
                if "operation_id" in k.lower():
                    matching_audits = [
                        a for a in matching_audits if a.operation_id == v
                    ]
            return ExecResult(matching_audits)

        # 6. L4DeskTerminal
        if "FROM l4desk_terminals" in sql_str:
            matching_l4 = list(self.l4_terminals.values())
            for k, v in params.items():
                if "terminal_id" in k.lower():
                    matching_l4 = [lt for lt in matching_l4 if lt.terminal_id == v]
            return ExecResult(matching_l4)

        return ExecResult([])


@pytest.fixture
def test_env():
    db = MockDatabase()
    terminal = Terminal(
        id=101,
        device_id=4625,
        sn="a2b0004625c18468d030726",
        cert_serial=None,
        org_id=1,
        is_active=True,
    )
    db.terminals[terminal.id] = terminal

    async def override_get_db():
        yield db

    app.dependency_overrides[get_db] = override_get_db
    return db, terminal


# ---------------------------------------------------------------------------
# Provider Contract & Idempotency Tests
# ---------------------------------------------------------------------------


@pytest.mark.anyio
async def test_issue_pin_success(test_env):
    db, terminal = test_env
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        req_data = {
            "operation_id": "op-test-100",
            "correlation_id": "corr-test-100",
            "tenant_id": 1,
            "terminal_id": 101,
            "sn": "a2b0004625c18468d030726",
            "ttl_seconds": 3600,
            "actor": "menubuilder-test",
        }
        resp = await client.post("/api/certificates/pins/issue", json=req_data)

        assert resp.status_code == 201
        data = resp.json()

        assert data["operation_id"] == "op-test-100"
        assert data["correlation_id"] == "corr-test-100"
        assert data["tenant_id"] == 1
        assert data["terminal_id"] == 101
        assert data["sn"] == "a2b0004625c18468d030726"
        assert data["status"] == "issued"
        assert data["replayed"] is False
        assert len(data["pin"]) == 6
        assert data["pin"].isdigit()
        assert data["pin_masked"] == f"***{data['pin'][-3:]}"
        assert "expires_at" in data

        # Check DB state
        assert len(db.pins) == 1
        pin_row = db.pins[0]
        assert pin_row.pin == data["pin"]
        assert pin_row.status == "pending"
        assert pin_row.terminal_id == 101
        assert pin_row.created_by == "op:op-test-100"

        # Check Audit event
        assert len(db.audit_events) == 1
        audit = db.audit_events[0]
        assert audit.event_type == "certificate_pin_issued"
        assert audit.operation_id == "op-test-100"
        assert audit.outcome == "success"
        # SECURITY REQUIREMENT: Plaintext PIN must NEVER be stored in audit details!
        assert "pin" not in audit.details
        assert audit.details["pin_masked"] == data["pin_masked"]
        assert audit.details["sn"] == terminal.sn


@pytest.mark.anyio
async def test_issue_pin_idempotent_replay(test_env):
    db, _terminal = test_env
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        req_data = {
            "operation_id": "op-idempotent-200",
            "correlation_id": "corr-200",
            "tenant_id": 1,
            "terminal_id": 101,
            "sn": "a2b0004625c18468d030726",
            "ttl_seconds": 7200,
        }
        # First call
        resp1 = await client.post("/api/certificates/pins/issue", json=req_data)
        assert resp1.status_code == 201
        data1 = resp1.json()
        assert data1["replayed"] is False
        first_pin = data1["pin"]

        # Second call (replay) with identical payload
        resp2 = await client.post("/api/certificates/pins/issue", json=req_data)
        assert resp2.status_code == 200
        data2 = resp2.json()

        assert data2["replayed"] is True
        assert data2["operation_id"] == "op-idempotent-200"
        assert data2["pin"] == first_pin
        assert data2["pin_masked"] == data1["pin_masked"]
        assert data2["status"] == "issued"
        assert data2["expires_at"] == data1["expires_at"]

        # Idempotency guarantee: NO second PIN created in DB!
        assert len(db.pins) == 1


@pytest.mark.anyio
async def test_issue_pin_reused_operation_id_conflict(test_env):
    _db, _terminal = test_env
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        req1 = {
            "operation_id": "op-conflict-1",
            "tenant_id": 1,
            "terminal_id": 101,
            "sn": "a2b0004625c18468d030726",
        }
        resp1 = await client.post("/api/certificates/pins/issue", json=req1)
        assert resp1.status_code == 201

        # Reusing operation_id with conflicting payload (e.g. different terminal_id or sn)
        req2 = {
            "operation_id": "op-conflict-1",
            "tenant_id": 1,
            "terminal_id": 999,  # different terminal
            "sn": "a2b0004625c18468d030726",
        }
        resp2 = await client.post("/api/certificates/pins/issue", json=req2)
        assert resp2.status_code in (404, 409)

        # Now test with existing terminal but different SN
        req3 = {
            "operation_id": "op-conflict-1",
            "tenant_id": 1,
            "terminal_id": 101,
            "sn": "a9b9999999c11111d222222",  # different SN
        }
        resp3 = await client.post("/api/certificates/pins/issue", json=req3)
        assert resp3.status_code == 409
        err = resp3.json()["detail"]
        assert err["error_code"] == "OPERATION_ID_CONFLICT"


@pytest.mark.anyio
async def test_issue_pin_ownership_mismatch(test_env):
    _db, _terminal = test_env
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        req = {
            "operation_id": "op-mismatch-1",
            "tenant_id": 999,  # Terminal 101 belongs to org 1!
            "terminal_id": 101,
            "sn": "a2b0004625c18468d030726",
        }
        resp = await client.post("/api/certificates/pins/issue", json=req)
        assert resp.status_code == 403
        err = resp.json()["detail"]
        assert err["error_code"] == "TENANT_OWNERSHIP_MISMATCH"


@pytest.mark.anyio
async def test_issue_pin_sn_mismatch(test_env):
    _db, _terminal = test_env
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        req = {
            "operation_id": "op-sn-mismatch-1",
            "tenant_id": 1,
            "terminal_id": 101,
            "sn": "wrong_sn_12345",
        }
        resp = await client.post("/api/certificates/pins/issue", json=req)
        assert resp.status_code == 400
        err = resp.json()["detail"]
        assert err["error_code"] == "SERIAL_NUMBER_MISMATCH"


@pytest.mark.anyio
async def test_issue_pin_terminal_not_found(test_env):
    db, _terminal = test_env
    # Clear terminals to simulate not found
    db.terminals.clear()
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        req = {
            "operation_id": "op-not-found-1",
            "tenant_id": 1,
            "terminal_id": 999,
            "sn": "a2b0004625c18468d030726",
        }
        resp = await client.post("/api/certificates/pins/issue", json=req)
        assert resp.status_code == 404
        err = resp.json()["detail"]
        assert err["error_code"] == "TERMINAL_NOT_FOUND"


# ---------------------------------------------------------------------------
# Security & Service Authentication Tests
# ---------------------------------------------------------------------------


@pytest.mark.anyio
async def test_service_auth_security(test_env):
    _db, _terminal = test_env
    settings.service_auth_token = "secret-service-token-42"
    transport = ASGITransport(app=app)

    async with AsyncClient(transport=transport, base_url="http://test") as client:
        req = {
            "operation_id": "op-auth-test-1",
            "tenant_id": 1,
            "terminal_id": 101,
            "sn": "a2b0004625c18468d030726",
        }

        # 1. No auth headers -> 401
        resp_no_auth = await client.post("/api/certificates/pins/issue", json=req)
        assert resp_no_auth.status_code == 401
        assert resp_no_auth.json()["detail"]["error_code"] == "SERVICE_AUTH_FAILED"

        # 2. Invalid Bearer token -> 401
        resp_bad_bearer = await client.post(
            "/api/certificates/pins/issue",
            json=req,
            headers={"Authorization": "Bearer wrong-token"},
        )
        assert resp_bad_bearer.status_code == 401

        # 3. Valid Bearer token -> 201
        resp_valid_bearer = await client.post(
            "/api/certificates/pins/issue",
            json=req,
            headers={"Authorization": "Bearer secret-service-token-42"},
        )
        assert resp_valid_bearer.status_code == 201

        # 4. Valid X-Service-Token header -> 200 (replay)
        resp_valid_x_token = await client.post(
            "/api/certificates/pins/issue",
            json=req,
            headers={"X-Service-Token": "secret-service-token-42"},
        )
        assert resp_valid_x_token.status_code == 200


@pytest.mark.anyio
async def test_lookup_pin_by_operation_id(test_env):
    _db, _terminal = test_env
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        # 1. Issue a PIN
        req = {
            "operation_id": "op-lookup-99",
            "tenant_id": 1,
            "terminal_id": 101,
            "sn": "a2b0004625c18468d030726",
        }
        resp1 = await client.post("/api/certificates/pins/issue", json=req)
        assert resp1.status_code == 201

        # 2. Lookup existing operation_id
        resp_lookup = await client.get(
            "/api/certificates/pins/by-operation/op-lookup-99"
        )
        assert resp_lookup.status_code == 200
        data = resp_lookup.json()
        assert data["operation_id"] == "op-lookup-99"
        assert data["terminal_id"] == 101
        assert data["status"] == "issued"
        assert data["replayed"] is True

        # 3. Lookup non-existent operation_id
        resp_unknown = await client.get(
            "/api/certificates/pins/by-operation/nonexistent-op"
        )
        assert resp_unknown.status_code == 404
        assert resp_unknown.json()["detail"]["error_code"] == "OPERATION_NOT_FOUND"


# ---------------------------------------------------------------------------
# Terminal-Facing Legacy Compatibility (CHECK / SETUP) Tests
# ---------------------------------------------------------------------------


@pytest.mark.anyio
async def test_terminal_facing_check_flow(test_env):
    db, _terminal = test_env
    pin_row = CertificatePin(
        id=1,
        pin="773773",
        terminal_id=101,
        org_id=1,
        status="pending",
        expires_at=datetime.now(UTC) + timedelta(hours=24),
    )
    db.pins.append(pin_row)

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        tosign_b64 = base64.b64encode(b"sysinfo_test_payload").decode("ascii")
        resp = await client.get(
            f"/api/certificates/?function=check&pin=773773&tosign={tosign_b64}"
        )
        assert resp.status_code == 200
        assert 'encoding="windows-1251"' in resp.text

        root = ET.fromstring(resp.text)
        assert root.findtext("Result") == "OK"
        certdata = root.find("CERTDATA")
        assert certdata is not None
        assert certdata.findtext("catype") == "SubCA"
        assert certdata.findtext("pin") == "773773"
        assert "CN=a2b0004625c18468d030726" in (certdata.findtext("dn") or "")


@pytest.mark.anyio
async def test_terminal_facing_setup_success(test_env):
    db, terminal = test_env
    pin_row = CertificatePin(
        id=1,
        pin="773773",
        terminal_id=101,
        org_id=1,
        status="pending",
        expires_at=datetime.now(UTC) + timedelta(hours=24),
    )
    db.pins.append(pin_row)

    csr_pem = _generate_csr(
        cn="a2b0004625c18468d030726", device_id=4625, org_id=1, corrupt_signature=False
    )
    cpserial = "B2B829B03880E5FAAFC143A2F2D357F0"

    with patch(
        "app.routers.certificates.sign_csr", new=AsyncMock(side_effect=_mock_sign_csr)
    ):
        transport = ASGITransport(app=app)
        async with AsyncClient(transport=transport, base_url="http://test") as client:
            resp = await client.post(
                f"/api/certificates/?function=setup&pin=773773&cpserial={cpserial}",
                content=csr_pem.encode("utf-8"),
            )
            assert resp.status_code == 200
            root = ET.fromstring(resp.text)
            assert root.findtext("Result") == "OK"
            assert root.findtext("code") == "0"
            certdata = root.findtext("CERTDATA")
            assert certdata is not None
            # Validate Base64 PKCS#7 payload
            p7_bytes = base64.b64decode(certdata)
            assert len(p7_bytes) > 100

            # Verify terminal state updated
            assert terminal.cert_serial is not None
            assert pin_row.status == "used"
            assert pin_row.used_at is not None

            # Verify TerminalCertHistory was created
            assert len(db.cert_history) == 1
            assert db.cert_history[0].terminal_id == 101

            # Verify audit event for enrollment
            assert any(a.event_type == "certificate_enrolled" for a in db.audit_events)


@pytest.mark.anyio
async def test_terminal_facing_setup_network_drop_safe_retry(test_env):
    db, _terminal = test_env
    pin_row = CertificatePin(
        id=1,
        pin="773773",
        terminal_id=101,
        org_id=1,
        status="pending",
        expires_at=datetime.now(UTC) + timedelta(hours=24),
    )
    db.pins.append(pin_row)

    csr_pem = _generate_csr(cn="a2b0004625c18468d030726", device_id=4625, org_id=1)
    cpserial = "B2B829B03880E5FAAFC143A2F2D357F0"

    with patch(
        "app.routers.certificates.sign_csr", new=AsyncMock(side_effect=_mock_sign_csr)
    ) as mock_sign:
        transport = ASGITransport(app=app)
        async with AsyncClient(transport=transport, base_url="http://test") as client:
            # 1. First setup succeeds
            resp1 = await client.post(
                f"/api/certificates/?function=setup&pin=773773&cpserial={cpserial}",
                content=csr_pem.encode("utf-8"),
            )
            assert resp1.status_code == 200
            p7_first = ET.fromstring(resp1.text).findtext("CERTDATA")
            assert mock_sign.call_count == 1

            # 2. Terminal retries setup due to simulated network drop (PIN is already 'used'!)
            resp2 = await client.post(
                f"/api/certificates/?function=setup&pin=773773&cpserial={cpserial}",
                content=csr_pem.encode("utf-8"),
            )
            assert resp2.status_code == 200
            root2 = ET.fromstring(resp2.text)
            assert root2.findtext("Result") == "OK"
            p7_second = root2.findtext("CERTDATA")
            # Returns the same certificate chain from safe retry cache without re-signing!
            assert p7_second == p7_first
            assert mock_sign.call_count == 1


@pytest.mark.anyio
async def test_terminal_facing_setup_csr_mismatch_device_id(test_env):
    db, _terminal = test_env
    pin_row = CertificatePin(
        id=1,
        pin="773773",
        terminal_id=101,
        org_id=1,
        status="pending",
        expires_at=datetime.now(UTC) + timedelta(hours=24),
    )
    db.pins.append(pin_row)

    # CSR with mismatched device_id (OU=9999 instead of 4625)
    mismatched_csr = _generate_csr(
        cn="a2b0004625c18468d030726", device_id=9999, org_id=1
    )
    cpserial = "B2B829B03880E5FAAFC143A2F2D357F0"

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post(
            f"/api/certificates/?function=setup&pin=773773&cpserial={cpserial}",
            content=mismatched_csr.encode("utf-8"),
        )
        assert resp.status_code == 200
        root = ET.fromstring(resp.text)
        assert root.findtext("Result") == "Error"
        assert root.findtext("code") == "4"
        assert "Несоответствие данных CSR" in (root.findtext("Description") or "")


@pytest.mark.anyio
async def test_terminal_facing_setup_csr_mismatch_cn(test_env):
    db, _terminal = test_env
    pin_row = CertificatePin(
        id=1,
        pin="773773",
        terminal_id=101,
        org_id=1,
        status="pending",
        expires_at=datetime.now(UTC) + timedelta(hours=24),
    )
    db.pins.append(pin_row)

    # CSR with CN of a completely different terminal and not matching cpserial
    mismatched_csr = _generate_csr(
        cn="a9b0009999c00000d000000", device_id=4625, org_id=1
    )
    cpserial = "B2B829B03880E5FAAFC143A2F2D357F0"

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post(
            f"/api/certificates/?function=setup&pin=773773&cpserial={cpserial}",
            content=mismatched_csr.encode("utf-8"),
        )
        assert resp.status_code == 200
        root = ET.fromstring(resp.text)
        assert root.findtext("Result") == "Error"
        assert root.findtext("code") == "4"
        assert "Несоответствие данных CSR" in (root.findtext("Description") or "")


@pytest.mark.anyio
async def test_terminal_facing_setup_invalid_csr_signature(test_env):
    db, _terminal = test_env
    pin_row = CertificatePin(
        id=1,
        pin="773773",
        terminal_id=101,
        org_id=1,
        status="pending",
        expires_at=datetime.now(UTC) + timedelta(hours=24),
    )
    db.pins.append(pin_row)

    # Corrupted CSR signature
    corrupted_csr = _generate_csr(
        cn="a2b0004625c18468d030726",
        device_id=4625,
        org_id=1,
        corrupt_signature=True,
    )
    cpserial = "B2B829B03880E5FAAFC143A2F2D357F0"

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post(
            f"/api/certificates/?function=setup&pin=773773&cpserial={cpserial}",
            content=corrupted_csr.encode("utf-8"),
        )
        assert resp.status_code == 200
        root = ET.fromstring(resp.text)
        assert root.findtext("Result") == "Error"
        assert root.findtext("code") == "4"
        desc = root.findtext("Description") or ""
        assert "Недействительная подпись CSR" in desc or "Неверный формат CSR" in desc


@pytest.mark.anyio
async def test_terminal_facing_expired_pin(test_env):
    db, _terminal = test_env
    pin_row = CertificatePin(
        id=1,
        pin="773773",
        terminal_id=101,
        org_id=1,
        status="pending",
        expires_at=datetime.now(UTC) - timedelta(hours=1),  # Expired!
    )
    db.pins.append(pin_row)

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        # Check expired PIN
        resp_check = await client.get("/api/certificates/?function=check&pin=773773")
        assert resp_check.status_code == 200
        root_check = ET.fromstring(resp_check.text)
        assert root_check.findtext("Result") == "Error"
        assert root_check.findtext("code") == "2"
        assert "просрочен" in (root_check.findtext("Description") or "")


@pytest.mark.anyio
async def test_terminal_facing_used_pin_no_cache(test_env):
    db, _terminal = test_env
    pin_row = CertificatePin(
        id=1,
        pin="773773",
        terminal_id=101,
        org_id=1,
        status="used",
        expires_at=datetime.now(UTC) + timedelta(hours=24),
        used_at=datetime.now(UTC) - timedelta(hours=2),
    )
    db.pins.append(pin_row)

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        # Check used PIN
        resp = await client.get("/api/certificates/?function=check&pin=773773")
        assert resp.status_code == 200
        root = ET.fromstring(resp.text)
        assert root.findtext("Result") == "Error"
        assert root.findtext("code") == "2"
        assert "использован" in (root.findtext("Description") or "")


@pytest.mark.anyio
async def test_row_locking_concurrency_protection():
    """Verify that _find_terminal_by_pin includes with_for_update(of=CertificatePin)."""
    mock_db = AsyncMock()
    exec_result = MagicMock()
    exec_result.one_or_none.return_value = None
    mock_db.execute.return_value = exec_result

    await _find_terminal_by_pin("123456", mock_db, for_update=True)

    assert mock_db.execute.called
    stmt = mock_db.execute.call_args[0][0]
    sql_compiled = str(stmt)
    assert "FOR UPDATE" in sql_compiled
