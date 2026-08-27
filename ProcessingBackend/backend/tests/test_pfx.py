"""Unit tests for PKCS#12 (PFX) container serialization/deserialization."""

from datetime import UTC, datetime, timedelta

import pytest
from cryptography import x509
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID

from app.utils.pfx import create_pfx_bundle, load_pfx_bundle


def _create_test_cert_and_key():
    key = rsa.generate_private_key(65537, 2048, default_backend())
    subject = x509.Name(
        [
            x509.NameAttribute(NameOID.COMMON_NAME, "test-leaf"),
            x509.NameAttribute(NameOID.ORGANIZATION_NAME, "Test Corp"),
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
        .not_valid_after(now + timedelta(days=30))
        .sign(key, hashes.SHA256(), default_backend())
    )

    key_pem = key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption(),
    ).decode("utf-8")

    cert_pem = cert.public_bytes(serialization.Encoding.PEM).decode("utf-8")
    return key_pem, cert_pem


def test_create_and_load_pfx_with_password():
    key_pem, cert_pem = _create_test_cert_and_key()
    _, ca_pem = _create_test_cert_and_key()
    password = "secret-pfx-pass"

    pfx_bytes = create_pfx_bundle(
        private_key_pem=key_pem,
        cert_pem=cert_pem,
        ca_pem=ca_pem,
        password=password,
    )

    assert isinstance(pfx_bytes, bytes)
    assert len(pfx_bytes) > 0

    loaded_key, loaded_cert, loaded_cas = load_pfx_bundle(pfx_bytes, password=password)

    assert loaded_key is not None
    assert loaded_cert is not None
    assert len(loaded_cas) == 1
    assert (
        loaded_cert.subject.get_attributes_for_oid(NameOID.COMMON_NAME)[0].value
        == "test-leaf"
    )


def test_pfx_wrong_password_fails():
    key_pem, cert_pem = _create_test_cert_and_key()
    password = "correct-password"

    pfx_bytes = create_pfx_bundle(
        private_key_pem=key_pem,
        cert_pem=cert_pem,
        password=password,
    )

    with pytest.raises(Exception):  # noqa: B017
        load_pfx_bundle(pfx_bytes, password="wrong-password")


def test_create_and_load_pfx_without_password():
    key_pem, cert_pem = _create_test_cert_and_key()

    pfx_bytes = create_pfx_bundle(
        private_key_pem=key_pem,
        cert_pem=cert_pem,
        password="",
    )

    loaded_key, loaded_cert, loaded_cas = load_pfx_bundle(pfx_bytes, password="")
    assert loaded_key is not None
    assert loaded_cert is not None
    assert len(loaded_cas) == 0
