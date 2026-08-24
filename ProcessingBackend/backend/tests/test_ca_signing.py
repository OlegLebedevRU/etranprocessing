"""Tests for CA CSR signing flow and SAN fields generation."""

import sys
import urllib.parse
from datetime import UTC, datetime, timedelta
from pathlib import Path

import pytest
from cryptography import x509
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID

# Add parent directory of backend (ProcessingBackend) to sys.path to import ca_sign_csr
ca_dir = str(Path(__file__).resolve().parent.parent.parent)
if ca_dir not in sys.path:
    sys.path.insert(0, ca_dir)

from ca_sign_csr import (  # pyright: ignore[reportMissingImports]
    _extract_device_code,
    sign_csr_from_headers,
)


@pytest.fixture
def ca_credentials(tmp_path, monkeypatch):
    """Generate temporary CA key and certificate for testing."""
    key = rsa.generate_private_key(65537, 2048, default_backend())
    subject = x509.Name(
        [
            x509.NameAttribute(NameOID.COMMON_NAME, "Test CA"),
            x509.NameAttribute(NameOID.ORGANIZATION_NAME, "Test Org"),
        ]
    )
    now = datetime.now(UTC).replace(tzinfo=None)
    cert = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(subject)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now)
        .not_valid_after(now + timedelta(days=10))
        .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
        .sign(key, hashes.SHA256(), default_backend())
    )

    ca_cert_path = tmp_path / "ca.crt"
    ca_key_path = tmp_path / "ca.key"
    certs_dir = tmp_path / "certs"
    certs_dir.mkdir(exist_ok=True)

    ca_cert_path.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    ca_key_path.write_bytes(
        key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        )
    )

    monkeypatch.setenv("CA_CERT_PATH", str(ca_cert_path))
    monkeypatch.setenv("CA_KEY_PATH", str(ca_key_path))
    monkeypatch.setenv("CA_CERTS_DIR", str(certs_dir) + "/")

    return ca_cert_path, ca_key_path


def _generate_csr(
    cn: str = "A1B2C3D4E5F6", device_id: int = 4625, org_id: int = 223
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
    builder = (
        x509.CertificateSigningRequestBuilder()
        .subject_name(subject)
        .sign(key, hashes.SHA256(), default_backend())
    )
    return builder.public_bytes(serialization.Encoding.PEM).decode("utf-8")


def test_extract_device_code():
    # Standard format: a2b0004625c18468d030726 -> 0004625
    assert _extract_device_code("a2b0004625c18468d030726") == "0004625"
    assert _extract_device_code("a4b0006208c93036d070826") == "0006208"
    assert _extract_device_code("a4b6208c93036d070826") == "0006208"
    assert _extract_device_code("a3b0000000c10221d290825") == "0000000"

    # Fallback with device_id
    assert _extract_device_code("custom_sn", 4625) == "0004625"
    assert _extract_device_code("", 42) == "0000042"
    assert _extract_device_code("", 0) == "0000000"


def test_sign_csr_with_san_fields(ca_credentials):
    csr_pem = _generate_csr(cn="A1B2C3D4E5F6", device_id=4625)
    device_sn = "a2b0004625c18468d030726"
    sign_val = "A1B2C3D4E5F67890"

    event = {
        "headers": {
            "X-Ssl-Client-Csr": urllib.parse.quote(csr_pem),
            "X-Ssl-Client-Exp-Days": "365",
            "X-Sign": sign_val,
            "X-Cn": device_sn,
        }
    }

    result = sign_csr_from_headers(event, None)
    assert result["statusCode"] == 200

    body = result["body"]
    assert body["sn"] == device_sn
    assert body["device_id"] == 4625

    cert_pem = urllib.parse.unquote(body["cert"])
    cert = x509.load_pem_x509_certificate(cert_pem.encode("utf-8"), default_backend())

    # Check CN override
    cn = cert.subject.get_attributes_for_oid(NameOID.COMMON_NAME)[0].value
    assert cn == device_sn

    # Check SAN extension
    san = cert.extensions.get_extension_for_class(x509.SubjectAlternativeName)
    uris = [
        str(u) for u in san.value.get_values_for_type(x509.UniformResourceIdentifier)
    ]
    dns_names = [str(d) for d in san.value.get_values_for_type(x509.DNSName)]

    # 1. Device SN as URI
    assert device_sn in uris
    # 2. Hostname as DNS: leo4-0004625.local
    assert "leo4-0004625.local" in dns_names
    # 3. Sign as URI
    assert f"urn:sign:{sign_val}" in uris
