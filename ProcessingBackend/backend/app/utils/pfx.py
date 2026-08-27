"""PKCS#12 (PFX) container serialization and deserialization utilities."""

from typing import cast

from cryptography import x509
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives.serialization import (
    BestAvailableEncryption,
    NoEncryption,
    load_pem_private_key,
    pkcs12,
)


def create_pfx_bundle(
    private_key_pem: str,
    cert_pem: str,
    ca_pem: str | None = None,
    password: str = "",
    friendly_name: bytes = b"leo4_device_companion",
) -> bytes:
    """Pack private key, leaf certificate, and optional CA certificate into a PFX (PKCS#12) bundle.

    Args:
        private_key_pem: PEM-encoded RSA/ECC private key.
        cert_pem: PEM-encoded leaf X.509 certificate.
        ca_pem: Optional PEM-encoded CA certificate.
        password: PFX protection password. If empty, NoEncryption is used.
        friendly_name: Alias name for the certificate and key within the bundle.

    Returns:
        Binary DER-encoded PKCS#12 bundle bytes.
    """
    key = load_pem_private_key(
        private_key_pem.encode("utf-8"),
        password=None,
        backend=default_backend(),
    )
    cert = x509.load_pem_x509_certificate(
        cert_pem.encode("utf-8"),
        backend=default_backend(),
    )

    cas: list[x509.Certificate] | None = None
    if ca_pem and ca_pem.strip():
        ca_cert = x509.load_pem_x509_certificate(
            ca_pem.encode("utf-8"),
            backend=default_backend(),
        )
        cas = [ca_cert]

    encryption = (
        BestAvailableEncryption(password.encode("utf-8"))
        if password
        else NoEncryption()
    )

    return pkcs12.serialize_key_and_certificates(
        name=friendly_name,
        key=cast(pkcs12.PKCS12PrivateKeyTypes, key),
        cert=cert,
        cas=cas,
        encryption_algorithm=encryption,
    )


def load_pfx_bundle(
    pfx_bytes: bytes,
    password: str = "",
):
    """Load PKCS#12 container and return (private_key, certificate, additional_certificates)."""
    pw_bytes = password.encode("utf-8") if password else None
    return pkcs12.load_key_and_certificates(
        pfx_bytes,
        password=pw_bytes,
        backend=default_backend(),
    )
