"""Fallback auth via Authorization: ClientCertificate for ListMenuFile.

Legacy menu clients (clsMenuCreator) send the client certificate in the
Authorization header instead of presenting it during the TLS handshake.
Only /api/ListMenuFile accepts this fallback; payment stays mTLS-only.
"""

import base64
from datetime import UTC, datetime, timedelta
from unittest.mock import AsyncMock, MagicMock

import pytest
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID
from fastapi import HTTPException
from starlette.requests import Request

from app.dependencies import (
    LEGACY_CLIENT_CERT_PATHS,
    get_current_terminal,
    parse_authorization_client_certificate,
)
from app.models import Terminal

SERIAL = 0x6A7EFDFE000400002C39
SERIAL_HEX = format(SERIAL, "X")


@pytest.fixture
def anyio_backend():
    return "asyncio"


def _make_cert() -> x509.Certificate:
    key = rsa.generate_private_key(65537, 2048)
    now = datetime.now(UTC)
    subject = x509.Name(
        [
            x509.NameAttribute(NameOID.COMMON_NAME, "8941E46F1800D4D151864979E80FC72B"),
            x509.NameAttribute(NameOID.ORGANIZATIONAL_UNIT_NAME, "283"),
            x509.NameAttribute(NameOID.ORGANIZATION_NAME, "424"),
            x509.NameAttribute(NameOID.LOCALITY_NAME, "1135"),
        ]
    )
    return (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "SubCA")]))
        .public_key(key.public_key())
        .serial_number(SERIAL)
        .not_valid_before(now - timedelta(days=2))
        .not_valid_after(now + timedelta(days=30))
        .sign(key, hashes.SHA256())
    )


def _make_request(path: str, authorization: str | None) -> Request:
    headers = []
    if authorization is not None:
        headers.append((b"authorization", authorization.encode("utf-8")))
    scope = {
        "type": "http",
        "method": "GET",
        "path": path,
        "raw_path": path.encode("ascii"),
        "query_string": b"",
        "headers": headers,
        "client": ("178.47.170.22", 12345),
    }
    return Request(scope)


def _terminal() -> Terminal:
    return Terminal(
        id=1135,
        device_id=283,
        sn="a4b0000283c34652d210826",
        cert_serial=SERIAL_HEX,
        org_id=424,
        is_active=True,
    )


def _mock_db_with_terminal(terminal: Terminal) -> AsyncMock:
    mock_db = AsyncMock()
    mock_db.add = MagicMock()
    exec_term = MagicMock()
    exec_term.scalar_one_or_none.return_value = terminal
    exec_disc = MagicMock()
    exec_disc.scalar_one_or_none.return_value = None
    mock_db.execute.side_effect = [exec_term, exec_disc]
    return mock_db


def test_parse_authorization_pem():
    cert = _make_cert()
    pem = cert.public_bytes(serialization.Encoding.PEM).decode()
    req = _make_request("/api/ListMenuFile", f"ClientCertificate {pem}")
    parsed = parse_authorization_client_certificate(req)
    assert parsed is not None
    assert parsed.serial_number == SERIAL


def test_parse_authorization_base64_der():
    cert = _make_cert()
    der_b64 = base64.b64encode(cert.public_bytes(serialization.Encoding.DER)).decode()
    req = _make_request("/api/ListMenuFile", f"clientCertificate {der_b64}")
    parsed = parse_authorization_client_certificate(req)
    assert parsed is not None
    assert parsed.serial_number == SERIAL


def test_parse_authorization_base64_pem():
    cert = _make_cert()
    pem_b64 = base64.b64encode(cert.public_bytes(serialization.Encoding.PEM)).decode()
    req = _make_request("/api/ListMenuFile", f"clientCertificate {pem_b64}")
    parsed = parse_authorization_client_certificate(req)
    assert parsed is not None
    assert parsed.serial_number == SERIAL


def test_parse_authorization_ignores_other_schemes():
    req = _make_request("/api/ListMenuFile", "Bearer sometoken")
    assert parse_authorization_client_certificate(req) is None
    req = _make_request("/api/ListMenuFile", "Basic dXNlcjpwYXNz")
    assert parse_authorization_client_certificate(req) is None


def test_listmenu_paths_allow_legacy_auth():
    assert "/api/ListMenuFile" in LEGACY_CLIENT_CERT_PATHS
    assert "/api/licensebilling" not in LEGACY_CLIENT_CERT_PATHS


@pytest.mark.anyio
async def test_list_menu_auth_via_authorization_pem():
    """ListMenuFile accepts Authorization: ClientCertificate when TLS headers absent."""
    cert = _make_cert()
    pem = cert.public_bytes(serialization.Encoding.PEM).decode()
    req = _make_request("/api/ListMenuFile", f"ClientCertificate {pem}")
    terminal = _terminal()
    mock_db = _mock_db_with_terminal(terminal)

    res = await get_current_terminal(req, mock_db)
    assert res is terminal
    mock_db.commit.assert_awaited()


@pytest.mark.anyio
async def test_list_menu_auth_via_authorization_base64_der():
    cert = _make_cert()
    der_b64 = base64.b64encode(cert.public_bytes(serialization.Encoding.DER)).decode()
    req = _make_request("/api/ListMenuFile", f"clientCertificate {der_b64}")
    terminal = _terminal()
    mock_db = _mock_db_with_terminal(terminal)

    res = await get_current_terminal(req, mock_db)
    assert res is terminal


@pytest.mark.anyio
async def test_payment_rejects_authorization_only():
    """Payment must stay mTLS-only: Authorization fallback is ListMenuFile-scoped."""
    cert = _make_cert()
    pem = cert.public_bytes(serialization.Encoding.PEM).decode()
    req = _make_request("/api/payment/etran.ashx", f"ClientCertificate {pem}")
    mock_db = AsyncMock()
    mock_db.add = MagicMock()
    mock_db.execute.return_value = MagicMock()

    with pytest.raises(HTTPException) as exc:
        await get_current_terminal(req, mock_db)
    assert exc.value.status_code == 401
    assert "Missing client certificate headers" in str(exc.value.detail)


@pytest.mark.anyio
async def test_list_menu_still_requires_some_identity():
    req = _make_request("/api/ListMenuFile", None)
    mock_db = AsyncMock()
    mock_db.add = MagicMock()
    mock_db.execute.return_value = MagicMock()

    with pytest.raises(HTTPException) as exc:
        await get_current_terminal(req, mock_db)
    assert exc.value.status_code == 401


@pytest.mark.anyio
async def test_list_menu_rejects_garbage_authorization():
    req = _make_request("/api/ListMenuFile", "clientCertificate not-a-cert")
    mock_db = AsyncMock()
    mock_db.add = MagicMock()
    mock_db.execute.return_value = MagicMock()

    with pytest.raises(HTTPException) as exc:
        await get_current_terminal(req, mock_db)
    assert exc.value.status_code == 401
