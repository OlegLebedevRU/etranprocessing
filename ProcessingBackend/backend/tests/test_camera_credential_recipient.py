"""Ephemeral public certificates test trusted collection and scoped API refusal."""

import urllib.parse
from datetime import UTC, datetime, timedelta
from unittest.mock import AsyncMock

import pytest
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, rsa
from cryptography.x509.oid import NameOID
from fastapi import HTTPException
from httpx import ASGITransport, AsyncClient
from starlette.requests import Request

from app.config import settings
from app.database import get_db
from app.main import app
from app.models import L4DeskTerminal, Org, OrgStatus, Terminal
from app.services import ca_trust, terminal_certificate
from app.services.camera_credential_recipient import (
    observed_recipient,
    recipient_response,
)


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.fixture
def registry(monkeypatch):
    now = datetime.now(UTC)
    ca_key = rsa.generate_private_key(65537, 2048)
    ca_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "fixture-ca")])
    ca = (
        x509.CertificateBuilder()
        .subject_name(ca_name)
        .issuer_name(ca_name)
        .public_key(ca_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - timedelta(days=2))
        .not_valid_after(now + timedelta(days=2))
        .add_extension(x509.BasicConstraints(ca=True, path_length=None), True)
        .sign(ca_key, hashes.SHA256())
    )
    monkeypatch.setattr(ca_trust, "terminal_root", lambda: ca)
    monkeypatch.setattr(terminal_certificate, "terminal_ca", lambda: ca)

    def leaf(
        *,
        sn="fixture-sn",
        serial=12345,
        expired=False,
        unsupported=False,
        forged=False,
        bits=2048,
    ):
        key = (
            ec.generate_private_key(ec.SECP256R1())
            if unsupported
            else rsa.generate_private_key(65537, bits)
        )
        name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, sn)])
        cert = (
            x509.CertificateBuilder()
            .subject_name(name)
            .issuer_name(ca_name)
            .public_key(key.public_key())
            .serial_number(serial)
            .not_valid_before(now - timedelta(days=2))
            .not_valid_after(now + timedelta(days=-1 if expired else 1))
            .sign(
                rsa.generate_private_key(65537, 2048) if forged else ca_key,
                hashes.SHA256(),
            )
        )
        return cert.public_bytes(serialization.Encoding.PEM).decode()

    terminal = Terminal(
        id=101,
        device_id=701,
        org_id=1,
        sn="fixture-sn",
        cert_serial="3039",
        is_active=True,
    )
    return terminal, leaf


def request(pem, *, verified="SUCCESS", serial="3039", authorization=False):
    headers = [(b"x-client-cert-serial", serial.encode())]
    if authorization:
        headers.append(
            (
                b"authorization",
                ("ClientCertificate " + urllib.parse.quote(pem)).encode(),
            )
        )
    else:
        headers.extend(
            [
                (b"x-client-cert-verified", verified.encode()),
                (b"x-ssl-client-cert", urllib.parse.quote(pem).encode()),
            ]
        )
    return Request(
        {"type": "http", "method": "GET", "path": "/api/gategauge", "headers": headers}
    )


@pytest.mark.parametrize("bits", [2048, 3072, 4096])
def test_verified_mtls_collects_only_current_leaf(registry, bits):
    terminal, leaf = registry
    pem = leaf(bits=bits)
    assert observed_recipient(request(pem), terminal) == pem
    terminal.credential_recipient_pem = pem
    result = recipient_response(terminal)
    assert (
        result["sn"] == terminal.sn and len(str(result["recipient_cert_sha256"])) == 64
    )
    terminal.cert_serial = "303A"
    with pytest.raises(HTTPException) as error:
        recipient_response(terminal)
    assert error.value.status_code == 409


@pytest.mark.parametrize(
    "kwargs",
    [
        {"sn": "other"},
        {"serial": 12346},
        {"expired": True},
        {"unsupported": True},
        {"forged": True},
        {"bits": 1024},
    ],
)
def test_bad_recipient_never_promoted(registry, kwargs):
    terminal, leaf = registry
    terminal.credential_recipient_pem = leaf(**kwargs)
    assert (
        observed_recipient(request(terminal.credential_recipient_pem), terminal) is None
    )
    with pytest.raises(HTTPException):
        recipient_response(terminal)


@pytest.mark.parametrize(
    "kwargs",
    [
        {"verified": "NONE"},
        {"verified": ""},
        {"serial": "303A"},
        {"authorization": True},
    ],
)
def test_unverified_headers_authorization_not_possession(registry, kwargs):
    terminal, leaf = registry
    assert observed_recipient(request(leaf(), **kwargs), terminal) is None


def test_oversize_multiple_leaf(registry):
    terminal, leaf = registry
    for pem in (leaf() * 2, "x" * 8193):
        assert observed_recipient(request(pem), terminal) is None
        terminal.credential_recipient_pem = pem
        with pytest.raises(HTTPException):
            recipient_response(terminal)


@pytest.mark.anyio
async def test_api_auth_tenant_disable_and_no_store(registry, monkeypatch):
    terminal, leaf = registry
    terminal.credential_recipient_pem = leaf()
    # Random service credential remains only in process memory.
    import secrets

    token = secrets.token_urlsafe(32)
    monkeypatch.setattr(settings, "service_auth_token", token)
    monkeypatch.setattr(settings, "internal_service_key", "")
    db = AsyncMock()

    async def override():
        yield db

    app.dependency_overrides[get_db] = override
    try:
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            url = "/api/certificates/terminals/701/credential-recipient?org_id=1"
            assert (await client.get(url)).status_code == 401
            headers = {"X-Internal-Service-Key": token}
            db.scalar.side_effect = [None]
            assert (
                await client.get(url.replace("org_id=1", "org_id=2"), headers=headers)
            ).status_code == 404
            db.scalar.side_effect = [
                terminal,
                Org(org_id=1, is_active=True),
                None,
                None,
            ]
            response = await client.get(url, headers=headers)
            assert (
                response.status_code == 200
                and response.headers["cache-control"] == "no-store"
            )
            assert set(response.json()) == {
                "sn",
                "device_id",
                "cert_serial",
                "recipient_cert_sha256",
                "certificate_pem",
                "not_valid_after",
            }
            db.scalar.side_effect = [
                terminal,
                Org(org_id=1, is_active=True),
                OrgStatus(org_id=1, status="blocked"),
                None,
            ]
            assert (await client.get(url, headers=headers)).status_code == 403
            db.scalar.side_effect = [
                terminal,
                Org(org_id=1, is_active=True),
                None,
                L4DeskTerminal(tenant_id=1, user_disabled_at=datetime.now(UTC)),
            ]
            assert (await client.get(url, headers=headers)).status_code == 403
            terminal.is_active = False
            db.scalar.side_effect = [
                terminal,
                Org(org_id=1, is_active=True),
                None,
                None,
            ]
            assert (await client.get(url, headers=headers)).status_code == 403
            monkeypatch.setattr(settings, "service_auth_token", "")
            assert (await client.get(url)).status_code == 503
    finally:
        app.dependency_overrides.pop(get_db, None)
