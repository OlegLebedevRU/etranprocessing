"""Tests for legacy device companion PFX certificate endpoint."""

import base64
from datetime import UTC, datetime, timedelta
from unittest.mock import AsyncMock, patch

import pytest
from cryptography import x509
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID
from httpx import ASGITransport, AsyncClient

from app.database import get_db
from app.dependencies import get_current_terminal
from app.main import app
from app.models import Terminal
from app.services.ca import CAResponse
from app.services.companion_cert import generate_key_and_csr
from app.utils.pfx import load_pfx_bundle


@pytest.fixture(autouse=True)
def cleanup_overrides():
    yield
    app.dependency_overrides.clear()


@pytest.fixture
def anyio_backend():
    return "asyncio"


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


def test_generate_key_and_csr():
    terminal = Terminal(
        id=42,
        device_id=9876,
        sn="a4b0009876c12345d270826",
        cert_serial="SERIAL123",
        org_id=77,
        is_active=True,
    )

    private_key_pem, csr_pem = generate_key_and_csr(terminal)

    assert "BEGIN PRIVATE KEY" in private_key_pem
    assert "BEGIN CERTIFICATE REQUEST" in csr_pem

    csr = x509.load_pem_x509_csr(csr_pem.encode("utf-8"), default_backend())
    assert csr.is_signature_valid

    cn = csr.subject.get_attributes_for_oid(NameOID.COMMON_NAME)[0].value
    assert cn == "a4b0009876c12345d270826"

    ou = csr.subject.get_attributes_for_oid(NameOID.ORGANIZATIONAL_UNIT_NAME)[0].value
    assert ou == "9876"

    o = csr.subject.get_attributes_for_oid(NameOID.ORGANIZATION_NAME)[0].value
    assert o == "77"

    loc = csr.subject.get_attributes_for_oid(NameOID.LOCALITY_NAME)[0].value
    assert loc == "42"

    st = csr.subject.get_attributes_for_oid(NameOID.STATE_OR_PROVINCE_NAME)[0].value
    assert st == "msk"

    c = csr.subject.get_attributes_for_oid(NameOID.COUNTRY_NAME)[0].value
    assert c == "ru"

    email = csr.subject.get_attributes_for_oid(NameOID.EMAIL_ADDRESS)[0].value
    assert email == "1.terminal@forpay.ru"


@pytest.mark.anyio
async def test_map_legacy_crt_success():
    terminal = Terminal(
        id=10,
        device_id=5001,
        sn="a4b0005001c99999d270826",
        cert_serial="ORIGINAL_SERIAL",
        cert_not_valid_after=datetime.now(UTC) + timedelta(days=120),
        org_id=5,
        is_active=True,
    )

    mock_db = AsyncMock()
    app.dependency_overrides[get_current_terminal] = lambda: terminal
    app.dependency_overrides[get_db] = lambda: mock_db

    with patch(
        "app.services.companion_cert.sign_csr",
        new=AsyncMock(side_effect=_mock_sign_csr),
    ) as mock_sign:
        transport = ASGITransport(app=app)
        async with AsyncClient(transport=transport, base_url="http://test") as client:
            resp = await client.get("/api/devices/map_legacy_crt/")

            assert resp.status_code == 200
            data = resp.json()

            assert data["sn"] == terminal.sn
            assert data["device_id"] == terminal.device_id
            assert data["valid_days"] in (119, 120)
            assert data["pfx_password"].startswith("pfx-5001-")

            # Validate PFX container
            pfx_bytes = base64.b64decode(data["pfx"])
            key, cert, cas = load_pfx_bundle(pfx_bytes, password=data["pfx_password"])
            assert key is not None
            assert cert is not None
            assert len(cas) == 1

            # Ensure sign_csr was called with X-CN / cn and remaining exp_days
            mock_sign.assert_called_once()
            call_kwargs = mock_sign.call_args.kwargs
            assert call_kwargs["cn"] == terminal.sn
            assert call_kwargs["sign"] == "temporary"
            assert call_kwargs["exp_days"] in (119, 120)

    # Ensure DB was untouched (stateless operation)
    assert terminal.cert_serial == "ORIGINAL_SERIAL"


@pytest.mark.anyio
async def test_map_legacy_crt_v1_alias():
    terminal = Terminal(
        id=11,
        device_id=5002,
        sn="a4b0005002c88888d270826",
        cert_serial="ORIGINAL_SERIAL_2",
        cert_not_valid_after=None,  # default fallback 365
        org_id=5,
        is_active=True,
    )

    mock_db = AsyncMock()
    app.dependency_overrides[get_current_terminal] = lambda: terminal
    app.dependency_overrides[get_db] = lambda: mock_db

    with patch(
        "app.services.companion_cert.sign_csr",
        new=AsyncMock(side_effect=_mock_sign_csr),
    ):
        transport = ASGITransport(app=app)
        async with AsyncClient(transport=transport, base_url="http://test") as client:
            resp = await client.get("/api/v1/devices/map_legacy_crt/")

            assert resp.status_code == 200
            data = resp.json()
            assert data["sn"] == terminal.sn
            assert data["device_id"] == 5002
            assert data["valid_days"] == 365


@pytest.mark.anyio
async def test_map_legacy_crt_unauthenticated():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.get("/api/devices/map_legacy_crt/")
        assert resp.status_code == 401
