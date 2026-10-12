"""Issuing hierarchy, terminal profile, trust failures and chain containers."""

import base64
import ipaddress
import urllib.parse
from datetime import UTC, datetime, timedelta
from unittest.mock import AsyncMock, MagicMock
from uuid import uuid4

import pytest
from cryptography import x509
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives.serialization import pkcs7, pkcs12
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID
from fastapi import HTTPException, Response
from starlette.requests import Request

from app.config import Settings, settings
from app.dependencies import get_current_terminal
from app.models import Terminal
from app.routers import file_manager as fm
from app.routers.certificates import _build_pkcs7_chain
from app.schemas.file_manager import AgentHello
from app.services import ca, ca_trust, local_ca
from app.services.file_manager_readiness import FM_REQUIRED_CAPABILITIES
from app.services.terminal_certificate import verified_terminal_identity
from app.utils.pfx import create_pfx_bundle

SN = "a4b0000773c82116d210826"
PASSWORD = "fixture-only-password"


def test_local_signer_is_default_and_external_requires_explicit_selection(monkeypatch):
    monkeypatch.delenv("CA_SIGNER_BACKEND", raising=False)
    assert Settings(_env_file=None).ca_signer_backend == "local"
    assert (
        Settings(ca_signer_backend="external", _env_file=None).ca_signer_backend
        == "external"
    )


@pytest.fixture
def anyio_backend():
    return "asyncio"


def ca_certificate(subject, key, issuer, signing_key, *, path_length, days=3650):
    now = datetime.now(UTC)
    return (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(issuer)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - timedelta(days=2))
        .not_valid_after(now + timedelta(days=days))
        .add_extension(
            x509.BasicConstraints(ca=True, path_length=path_length), critical=True
        )
        .add_extension(
            x509.KeyUsage(False, False, False, False, False, True, True, False, False),
            critical=True,
        )
        .sign(signing_key, hashes.SHA256())
    )


@pytest.fixture(scope="module")
def hierarchy(tmp_path_factory):
    directory = tmp_path_factory.mktemp("issuer")
    root_key = rsa.generate_private_key(65537, 2048)
    root_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "iot.leo4.ru")])
    root = ca_certificate(root_name, root_key, root_name, root_key, path_length=None)
    key = rsa.generate_private_key(65537, 3072)
    issuer_name = x509.Name(
        [x509.NameAttribute(NameOID.COMMON_NAME, "Terminal Issuing CA 01")]
    )
    issuer = ca_certificate(
        issuer_name, key, root_name, root_key, path_length=0, days=100
    )
    (directory / "iot_leo4_ca.crt").write_bytes(
        root.public_bytes(serialization.Encoding.PEM)
    )
    (directory / "terminal_issuing_ca_01.crt").write_bytes(
        issuer.public_bytes(serialization.Encoding.PEM)
    )
    (directory / "issuer.key").write_bytes(
        key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.BestAvailableEncryption(PASSWORD.encode()),
        )
    )
    # This is the exact named format of the operator-provided password file.
    (directory / "password.env").write_text(f"ISSUER_KEY_PASS={PASSWORD}\n")
    return directory, root, root_key, issuer, key


@pytest.fixture
def configured(hierarchy, monkeypatch):
    directory = hierarchy[0]
    monkeypatch.setattr(ca_trust, "CERTS_DIR", directory)
    monkeypatch.setattr(settings, "ca_signer_backend", "local")
    monkeypatch.setattr(settings, "ca_issuing_cert_path", "")
    monkeypatch.setattr(settings, "ca_issuing_key_path", str(directory / "issuer.key"))
    monkeypatch.setattr(
        settings, "ca_issuing_key_password_file", str(directory / "password.env")
    )
    for cached in (
        ca_trust.terminal_root,
        ca_trust.terminal_issuer,
        local_ca.issuing_key,
    ):
        cached.cache_clear()
    yield hierarchy
    for cached in (
        ca_trust.terminal_root,
        ca_trust.terminal_issuer,
        local_ca.issuing_key,
    ):
        cached.cache_clear()


def csr_pair(*, request_ca=False, arbitrary_san=False):
    key = rsa.generate_private_key(65537, 2048)
    subject = x509.Name(
        [
            x509.NameAttribute(NameOID.COMMON_NAME, "csr-sign"),
            x509.NameAttribute(NameOID.ORGANIZATION_NAME, "1"),
            x509.NameAttribute(NameOID.ORGANIZATIONAL_UNIT_NAME, "773"),
            x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME, "msk"),
            x509.NameAttribute(NameOID.COUNTRY_NAME, "ru"),
            x509.NameAttribute(NameOID.LOCALITY_NAME, "1"),
            x509.NameAttribute(NameOID.EMAIL_ADDRESS, "1.terminal@forpay.ru"),
        ]
    )
    builder = x509.CertificateSigningRequestBuilder().subject_name(subject)
    if request_ca:
        builder = builder.add_extension(
            x509.BasicConstraints(ca=True, path_length=0), critical=True
        )
    if arbitrary_san:
        builder = builder.add_extension(
            x509.SubjectAlternativeName([x509.DNSName("foreign.example")]),
            critical=False,
        )
    csr = builder.sign(key, hashes.SHA256())
    return key, csr.public_bytes(serialization.Encoding.PEM).decode(), subject


def request_for(
    cert,
    *,
    serial=None,
    verified="FAILED:unable to get local issuer certificate",
    subject=None,
):
    headers = {
        "X-Client-Cert-Verified": verified,
        "X-Client-Cert-Serial": serial or format(cert.serial_number, "X"),
        "X-Client-Cert-DN": subject or cert.subject.rfc4514_string(),
        "X-Client-Cert-Issuer-DN": cert.issuer.rfc4514_string(),
        "X-SSL-Client-Cert": urllib.parse.quote(
            cert.public_bytes(serialization.Encoding.PEM).decode()
        ),
    }
    return Request(
        {
            "type": "http",
            "path": "/api/leo4proxy/policy",
            "headers": [
                (key.lower().encode(), value.encode()) for key, value in headers.items()
            ],
        }
    )


def directly_root_signed(cert, configured):
    builder = (
        x509.CertificateBuilder()
        .subject_name(cert.subject)
        .issuer_name(configured[1].subject)
        .public_key(cert.public_key())
        .serial_number(cert.serial_number)
        .not_valid_before(cert.not_valid_before_utc)
        .not_valid_after(cert.not_valid_after_utc)
    )
    for ext in cert.extensions:
        builder = builder.add_extension(ext.value, ext.critical)
    return builder.sign(configured[2], hashes.SHA256())


@pytest.mark.anyio
@pytest.mark.parametrize("mode", ["issuer", "root", "untrusted"])
async def test_fm_registration_uses_verified_chain_not_immediate_issuer_name(
    configured, monkeypatch, mode
):
    cert = local_ca.sign_terminal_csr(csr_pair()[1], "fixture-sign", SN, 365)
    if mode == "root":
        cert = directly_root_signed(cert, configured)
    request = request_for(cert)
    request.scope["path"] = "/api/file-manager/v1/agent/hello"
    instance = uuid4()
    request.scope["headers"].append((b"x-fm-agent-instance-id", str(instance).encode()))
    if mode == "untrusted":
        request.scope["headers"] = [
            (name, b"NONE" if name == b"x-client-cert-verified" else value)
            for name, value in request.scope["headers"]
        ]
    terminal = Terminal(
        id=1,
        org_id=1,
        device_id=773,
        sn=SN,
        is_active=True,
        cert_serial=format(cert.serial_number, "X"),
    )
    lookup = AsyncMock(return_value=terminal)
    monkeypatch.setattr(fm, "get_current_terminal", lookup)
    db = AsyncMock()
    if mode == "untrusted":
        with pytest.raises(HTTPException) as error:
            await fm.fm_terminal(request, db)
        assert error.value.status_code == 401
        lookup.assert_not_awaited()
        db.execute.assert_not_awaited()
        return
    authenticated = await fm.fm_terminal(request, db)
    body = AgentHello(
        agent_instance_id=instance,
        agent_version="1.13.34",
        protocol_version=2,
        capabilities=sorted(FM_REQUIRED_CAPABILITIES),
        filesystem_ready=True,
    )
    result = await fm.hello(body, request, Response(), authenticated, db)
    assert result.available and result.state == "ready"
    db.commit.assert_awaited_once()
    values = db.execute.call_args.args[0].compile().params
    assert values["cert_serial"] == terminal.cert_serial


def test_profile_matches_terminal_773_and_one_day_backdating(configured):
    _, csr, subject = csr_pair(arbitrary_san=True)
    before = datetime.now(UTC)
    cert = local_ca.sign_terminal_csr(csr, "fixture-sign", SN, 365)
    after = datetime.now(UTC)
    assert cert.subject == x509.Name(
        [
            x509.NameAttribute(NameOID.COMMON_NAME, SN)
            if attr.oid == NameOID.COMMON_NAME
            else attr
            for attr in subject
        ]
    )
    assert (
        before - timedelta(days=1, seconds=1)
        <= cert.not_valid_before_utc
        <= after - timedelta(days=1)
    )
    # Requested duration starts at issuance; capped by remaining issuer lifetime.
    assert cert.not_valid_after_utc == configured[3].not_valid_after_utc
    assert len(format(cert.serial_number, "X")) == 40
    assert len(cert.extensions) == 4  # Exact existing leaf extension set, no SKI/AKI.
    ku = cert.extensions.get_extension_for_class(x509.KeyUsage)
    assert (
        ku.critical
        and ku.value.digital_signature
        and ku.value.content_commitment
        and ku.value.key_encipherment
    )
    assert not ku.value.key_cert_sign and not ku.value.crl_sign
    eku = cert.extensions.get_extension_for_class(x509.ExtendedKeyUsage).value
    assert list(eku) == [
        ExtendedKeyUsageOID.SERVER_AUTH,
        ExtendedKeyUsageOID.CLIENT_AUTH,
    ]
    san = list(
        cert.extensions.get_extension_for_class(x509.SubjectAlternativeName).value
    )
    assert san == [
        x509.UniformResourceIdentifier(SN),
        x509.UniformResourceIdentifier("urn:leo4:terminal:0000773"),
        *[
            x509.DNSName(f"leo4-0000773.{suffix}")
            for suffix in ("device.leo4.ru", "term.leo4.ru", "internal", "local")
        ],
        x509.DNSName("localhost"),
        x509.IPAddress(ipaddress.ip_address("127.0.0.1")),
        x509.IPAddress(ipaddress.ip_address("::1")),
        x509.UniformResourceIdentifier("urn:sign:fixture-sign"),
    ]
    ca_trust.verify_terminal_chain(cert)
    assert verified_terminal_identity(request_for(cert)) == (
        SN,
        format(cert.serial_number, "X"),
    )


@pytest.mark.parametrize("days", [0, -1, 367])
def test_invalid_validity_is_rejected(configured, days):
    with pytest.raises(ValueError, match="validity"):
        local_ca.sign_terminal_csr(csr_pair()[1], "", SN, days)


@pytest.mark.parametrize("sn", ["", "bad/name", "terminal\n", "не-SN"])
def test_invalid_sn_is_rejected(configured, sn):
    with pytest.raises(ValueError, match="SN"):
        local_ca.sign_terminal_csr(csr_pair()[1], "", sn, 365)


def test_requested_ca_privileges_are_rejected(configured):
    with pytest.raises(ValueError, match="CA privileges"):
        local_ca.sign_terminal_csr(csr_pair(request_ca=True)[1], "", SN, 365)


def test_bad_password_is_rejected_without_leaking_value(
    configured, monkeypatch, tmp_path
):
    path = tmp_path / "password.env"
    path.write_text("ISSUER_KEY_PASS=wrong-fixture-password\n")
    monkeypatch.setattr(settings, "ca_issuing_key_password_file", str(path))
    with pytest.raises(ValueError, match="Cannot decrypt") as exc:
        local_ca.issuing_key()
    assert "wrong-fixture-password" not in str(exc.value)


def test_unencrypted_key_is_rejected(configured, monkeypatch, tmp_path):
    path = tmp_path / "unencrypted.key"
    path.write_bytes(
        configured[4].private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        )
    )
    monkeypatch.setattr(settings, "ca_issuing_key_path", str(path))
    with pytest.raises(ValueError, match="Cannot decrypt"):
        local_ca.issuing_key()


def test_mismatched_key_is_rejected(configured, monkeypatch, tmp_path):
    path = tmp_path / "wrong.key"
    path.write_bytes(
        rsa.generate_private_key(65537, 3072).private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.BestAvailableEncryption(PASSWORD.encode()),
        )
    )
    monkeypatch.setattr(settings, "ca_issuing_key_path", str(path))
    with pytest.raises(ValueError, match="does not match"):
        local_ca.issuing_key()


def test_old_direct_root_certificate_remains_valid(configured):
    _, root, root_key, _, _ = configured
    key = rsa.generate_private_key(65537, 2048)
    now = datetime.now(UTC)
    cert = (
        x509.CertificateBuilder()
        .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, SN)]))
        .issuer_name(root.subject)
        .public_key(key.public_key())
        .serial_number(123)
        .not_valid_before(now - timedelta(days=1))
        .not_valid_after(now + timedelta(days=1))
        .sign(root_key, hashes.SHA256())
    )
    assert verified_terminal_identity(request_for(cert)) == (SN, "7B")


@pytest.mark.parametrize("mutation", ["serial", "none", "missing_pem"])
def test_identity_requires_tls_possession_and_matching_serial(configured, mutation):
    cert = local_ca.sign_terminal_csr(csr_pair()[1], "", SN, 365)
    request = request_for(
        cert,
        serial="BAD" if mutation == "serial" else None,
        verified="NONE" if mutation == "none" else "SUCCESS",
    )
    if mutation == "missing_pem":
        request.scope["headers"] = [
            (key, value)
            for key, value in request.scope["headers"]
            if key != b"x-ssl-client-cert"
        ]
    with pytest.raises(HTTPException) as exc:
        verified_terminal_identity(request)
    assert exc.value.status_code == 401


@pytest.mark.anyio
async def test_local_signer_and_pkcs7_pfx_include_complete_chain(
    configured, monkeypatch
):
    key, csr, _ = csr_pair()
    external = MagicMock(side_effect=AssertionError("External signer must not be used"))
    monkeypatch.setattr(ca.httpx, "AsyncClient", external)
    response = await ca.sign_csr(csr, "fixture-sign", SN)
    leaf = x509.load_pem_x509_certificate(response.cert_pem.encode())
    chain = x509.load_pem_x509_certificates(response.ca_pem.encode())
    assert chain == [configured[3], configured[1]]
    assert response.serial_number == format(leaf.serial_number, "X")
    assert datetime.fromisoformat(response.not_valid_after) == leaf.not_valid_after_utc
    container = base64.b64decode(_build_pkcs7_chain(response.cert_pem, response.ca_pem))
    assert set(pkcs7.load_der_pkcs7_certificates(container)) == {leaf, *chain}
    pfx = create_pfx_bundle(
        key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        ).decode(),
        response.cert_pem,
        response.ca_pem,
        "fixture",
    )
    _, pfx_leaf, pfx_chain = pkcs12.load_key_and_certificates(pfx, b"fixture")
    assert pfx_leaf == leaf and set(pfx_chain) == set(chain)
    external.assert_not_called()


@pytest.mark.anyio
@pytest.mark.parametrize("mode", ["issuer", "root"])
@pytest.mark.parametrize(
    "path",
    [
        "/api/payment/",
        "/api/techgate/",
        "/api/gategauge/",
        "/api/licensebilling/",
        "/licensebilling/",
        "/api/ListMenuFile",
        "/api/leo4proxy/policy",
        "/api/devices/map_legacy_crt/",
        "/api/v1/devices/map_legacy_crt/",
        "/api/file-manager/v1/agent/hello",
    ],
)
async def test_new_issuer_uses_strict_sn_serial_without_autobind(
    configured, monkeypatch, mode, path
):
    cert = local_ca.sign_terminal_csr(csr_pair()[1], "", SN, 365)
    if mode == "root":
        cert = directly_root_signed(cert, configured)
    serial = format(cert.serial_number, "X")
    terminal = Terminal(
        id=773, org_id=1, device_id=773, sn=SN, cert_serial=serial, is_active=True
    )
    db = AsyncMock()
    result = MagicMock()
    result.scalar_one_or_none.return_value = terminal
    db.execute.return_value = result
    monkeypatch.setattr("app.dependencies.record_terminal_discovery", AsyncMock())
    request = request_for(cert)
    request.scope["path"] = path
    assert await get_current_terminal(request, db) is terminal
    params = db.execute.call_args.args[0].compile().params
    assert SN in params.values() and serial in params.values()
    assert terminal.cert_serial == serial
    db.add.assert_not_called()


@pytest.mark.anyio
async def test_spoofed_new_issuer_cannot_fall_back_to_legacy(configured):
    cert = local_ca.sign_terminal_csr(csr_pair()[1], "", SN, 365)
    request = request_for(cert, subject="CN=other,OU=773,O=1")
    db = AsyncMock()
    with pytest.raises(HTTPException, match="certificate_identity_mismatch"):
        await get_current_terminal(request, db)
    db.execute.assert_not_awaited()


@pytest.mark.parametrize(
    "fault", ["expired", "future", "foreign_key", "ca_leaf", "missing_client_auth"]
)
def test_invalid_leaf_chain_is_rejected(configured, fault):
    _, _, _, issuer, key = configured
    now = datetime.now(UTC)
    peer_key = rsa.generate_private_key(65537, 2048)
    cert = (
        x509.CertificateBuilder()
        .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, SN)]))
        .issuer_name(issuer.subject)
        .public_key(peer_key.public_key())
        .serial_number(123)
        .not_valid_before(
            now + timedelta(days=1) if fault == "future" else now - timedelta(days=2)
        )
        .not_valid_after(
            now - timedelta(days=1) if fault == "expired" else now + timedelta(days=2)
        )
        .add_extension(
            x509.BasicConstraints(ca=fault == "ca_leaf", path_length=None),
            critical=True,
        )
        .add_extension(
            x509.KeyUsage(True, True, True, False, False, False, False, False, False),
            critical=True,
        )
        .add_extension(
            x509.ExtendedKeyUsage(
                [
                    ExtendedKeyUsageOID.SERVER_AUTH
                    if fault == "missing_client_auth"
                    else ExtendedKeyUsageOID.CLIENT_AUTH
                ]
            ),
            critical=False,
        )
        .sign(peer_key if fault == "foreign_key" else key, hashes.SHA256())
    )
    with pytest.raises(HTTPException) as exc:
        verified_terminal_identity(request_for(cert))
    assert exc.value.status_code == 401


@pytest.mark.parametrize(
    "fault", ["expired", "future", "foreign_root", "bad_path_length"]
)
def test_invalid_issuer_is_rejected(configured, monkeypatch, tmp_path, fault):
    _, root, root_key, issuer, key = configured
    now = datetime.now(UTC)
    signing_key = (
        rsa.generate_private_key(65537, 2048) if fault == "foreign_root" else root_key
    )
    candidate = (
        x509.CertificateBuilder()
        .subject_name(issuer.subject)
        .issuer_name(root.subject)
        .public_key(key.public_key())
        .serial_number(123)
        .not_valid_before(
            now + timedelta(days=1) if fault == "future" else now - timedelta(days=2)
        )
        .not_valid_after(
            now - timedelta(days=1) if fault == "expired" else now + timedelta(days=2)
        )
        .add_extension(
            x509.BasicConstraints(
                ca=True, path_length=1 if fault == "bad_path_length" else 0
            ),
            critical=True,
        )
        .add_extension(
            x509.KeyUsage(False, False, False, False, False, True, True, False, False),
            critical=True,
        )
        .sign(signing_key, hashes.SHA256())
    )
    path = tmp_path / "invalid-issuer.crt"
    path.write_bytes(candidate.public_bytes(serialization.Encoding.PEM))
    monkeypatch.setattr(settings, "ca_issuing_cert_path", str(path))
    with pytest.raises((ValueError, InvalidSignature)) as exc:
        local_ca.issuing_key()
    # Loading never reaches key decryption when trust/validity fails.
    assert "Cannot decrypt" not in str(exc.value)


@pytest.mark.anyio
async def test_local_failure_never_falls_back_to_external(configured, monkeypatch):
    external = MagicMock(side_effect=AssertionError("External fallback is forbidden"))
    monkeypatch.setattr(ca.httpx, "AsyncClient", external)
    with pytest.raises(ValueError, match="validity"):
        await ca.sign_csr(csr_pair()[1], "", SN, exp_days=0)
    external.assert_not_called()


@pytest.mark.anyio
async def test_listmenu_authorization_contract_accepts_verified_new_chain(
    configured, monkeypatch
):
    cert = local_ca.sign_terminal_csr(csr_pair()[1], "", SN, 365)
    serial = format(cert.serial_number, "X")
    terminal = Terminal(
        id=773, org_id=1, device_id=773, sn=SN, cert_serial=serial, is_active=True
    )
    token = base64.b64encode(cert.public_bytes(serialization.Encoding.DER)).decode()
    request = Request(
        {
            "type": "http",
            "path": "/api/ListMenuFile",
            "headers": [(b"authorization", ("ClientCertificate " + token).encode())],
        }
    )
    db = AsyncMock()
    result = MagicMock()
    result.scalar_one_or_none.return_value = terminal
    db.execute.return_value = result
    monkeypatch.setattr("app.dependencies.record_terminal_discovery", AsyncMock())
    assert await get_current_terminal(request, db) is terminal
