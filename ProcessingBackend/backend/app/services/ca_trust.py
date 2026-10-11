"""Pinned public terminal CA hierarchy; never learn trust from a request."""

from datetime import UTC, datetime
from functools import lru_cache
from pathlib import Path

from cryptography import x509
from cryptography.x509.oid import ExtendedKeyUsageOID, ExtensionOID, NameOID

from app.config import settings

CERTS_DIR = Path(__file__).parents[1] / "certs"


@lru_cache(maxsize=1)
def terminal_root() -> x509.Certificate:
    roots = x509.load_pem_x509_certificates(
        (CERTS_DIR / "iot_leo4_ca.crt").read_bytes()
    )
    matches = [
        cert
        for cert in roots
        if cert.subject == cert.issuer
        and cert.subject.get_attributes_for_oid(NameOID.COMMON_NAME)
        == [x509.NameAttribute(NameOID.COMMON_NAME, "iot.leo4.ru")]
    ]
    if len(matches) != 1:
        raise ValueError("Exactly one terminal root is required")
    return matches[0]


@lru_cache(maxsize=1)
def terminal_issuer() -> x509.Certificate:
    path = (
        Path(settings.ca_issuing_cert_path)
        if settings.ca_issuing_cert_path
        else CERTS_DIR / "terminal_issuing_ca_01.crt"
    )
    certs = x509.load_pem_x509_certificates(path.read_bytes())
    if len(certs) != 1:
        raise ValueError("Exactly one issuing certificate is required")
    return certs[0]


def validate_ca(cert: x509.Certificate, now: datetime) -> None:
    if not cert.not_valid_before_utc <= now < cert.not_valid_after_utc:
        raise ValueError("Terminal CA is not currently valid")
    bc = cert.extensions.get_extension_for_class(x509.BasicConstraints)
    if not bc.critical or not bc.value.ca:
        raise ValueError("Terminal CA requires critical CA basic constraints")
    try:
        usage = cert.extensions.get_extension_for_class(x509.KeyUsage).value
    except x509.ExtensionNotFound:
        # The existing offline root predates explicit KeyUsage.
        usage = None
    if usage is not None and not usage.key_cert_sign:
        raise ValueError("Terminal CA cannot sign certificates")
    allowed_critical = {
        ExtensionOID.BASIC_CONSTRAINTS,
        ExtensionOID.KEY_USAGE,
        ExtensionOID.EXTENDED_KEY_USAGE,
    }
    if any(ext.critical and ext.oid not in allowed_critical for ext in cert.extensions):
        raise ValueError("Unsupported critical CA constraint")
    try:
        eku = cert.extensions.get_extension_for_class(x509.ExtendedKeyUsage).value
    except x509.ExtensionNotFound:
        eku = None
    if eku is not None and ExtendedKeyUsageOID.CLIENT_AUTH not in eku:
        raise ValueError("Terminal CA excludes client authentication")


def validate_issuer(
    issuer: x509.Certificate, root: x509.Certificate, now: datetime
) -> None:
    validate_ca(root, now)
    validate_ca(issuer, now)
    root.verify_directly_issued_by(root)
    issuer.verify_directly_issued_by(root)
    root_bc = root.extensions.get_extension_for_class(x509.BasicConstraints).value
    issuer_bc = issuer.extensions.get_extension_for_class(x509.BasicConstraints).value
    if root_bc.path_length is not None and root_bc.path_length < 1:
        raise ValueError("Root does not permit an issuing CA")
    if issuer_bc.path_length != 0 or issuer.subject == root.subject:
        raise ValueError("Issuing CA must be a distinct terminal-only CA")
    ku = issuer.extensions.get_extension_for_class(x509.KeyUsage)
    if not ku.critical or not ku.value.key_cert_sign or not ku.value.crl_sign:
        raise ValueError("Issuing CA requires certificate and CRL signing usage")


def is_terminal_issuing_dn(issuer_dn: str) -> bool:
    try:
        name = x509.Name.from_rfc4514_string(issuer_dn)
    except ValueError:
        return False
    # Classify by CN, then cryptographically verify the exact pinned issuer.
    # A forged variant must be rejected, never routed to legacy OU fallback.
    return name.get_attributes_for_oid(NameOID.COMMON_NAME) == (
        terminal_issuer().subject.get_attributes_for_oid(NameOID.COMMON_NAME)
    )


def verify_terminal_chain(
    cert: x509.Certificate, root: x509.Certificate | None = None
) -> None:
    root = root if root is not None else terminal_root()
    now = datetime.now(UTC)
    validate_ca(root, now)
    if not cert.not_valid_before_utc <= now < cert.not_valid_after_utc:
        raise ValueError("Terminal certificate is not currently valid")
    if cert.issuer == root.subject:
        cert.verify_directly_issued_by(root)
        return  # Existing root-issued profile remains compatible.
    issuer = terminal_issuer()
    validate_issuer(issuer, root, now)
    cert.verify_directly_issued_by(issuer)
    bc = cert.extensions.get_extension_for_class(x509.BasicConstraints).value
    ku = cert.extensions.get_extension_for_class(x509.KeyUsage).value
    eku = cert.extensions.get_extension_for_class(x509.ExtendedKeyUsage).value
    if bc.ca or not ku.digital_signature or ku.key_cert_sign or ku.crl_sign:
        raise ValueError("Invalid terminal certificate usage")
    if ExtendedKeyUsageOID.CLIENT_AUTH not in eku:
        raise ValueError("Terminal certificate excludes client authentication")
    allowed_critical = {
        ExtensionOID.BASIC_CONSTRAINTS,
        ExtensionOID.KEY_USAGE,
    }
    if any(ext.critical and ext.oid not in allowed_critical for ext in cert.extensions):
        raise ValueError("Unsupported critical terminal constraint")
