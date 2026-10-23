"""PB-owned verified public recipient registry; no private keys or vault access."""

import urllib.parse

from cryptography import x509
from cryptography.exceptions import InvalidSignature, UnsupportedAlgorithm
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID
from fastapi import HTTPException, Request

from app.models import Terminal
from app.services.ca_trust import verify_terminal_chain
from app.services.terminal_certificate import verified_terminal_identity

MAX_PEM_BYTES = 8192


def validate_recipient(pem: str, terminal: Terminal) -> x509.Certificate:
    if not pem or len(pem.encode()) > MAX_PEM_BYTES:
        raise ValueError("recipient_unavailable")
    certificates = x509.load_pem_x509_certificates(pem.encode())
    if len(certificates) != 1:
        raise ValueError("recipient_unavailable")
    cert = certificates[0]
    verify_terminal_chain(cert)
    cn = cert.subject.get_attributes_for_oid(NameOID.COMMON_NAME)
    key = cert.public_key()
    if (
        len(cn) != 1
        or str(cn[0].value) != terminal.sn
        or not terminal.cert_serial
        or int(terminal.cert_serial, 16) != cert.serial_number
        or not isinstance(key, rsa.RSAPublicKey)
        or key.key_size not in (2048, 3072, 4096)
    ):
        raise ValueError("recipient_unavailable")
    try:
        if cert.extensions.get_extension_for_class(x509.BasicConstraints).value.ca:
            raise ValueError("recipient_unavailable")
    except x509.ExtensionNotFound:
        # Existing root-issued leaves predate the new issuing profile.
        pass
    return cert


def observed_recipient(request: Request, terminal: Terminal) -> str | None:
    """Optional additive collection. Invalid evidence never changes legacy auth."""
    raw = request.headers.get("X-SSL-Client-Cert", "")
    if not raw or len(raw) > MAX_PEM_BYTES * 3:
        return None
    try:
        sn, serial = verified_terminal_identity(request)
        if sn != terminal.sn or int(serial, 16) != int(terminal.cert_serial or "0", 16):
            return None
        cert = validate_recipient(urllib.parse.unquote(raw), terminal)
        return cert.public_bytes(serialization.Encoding.PEM).decode("ascii")
    except (
        HTTPException,
        ValueError,
        TypeError,
        InvalidSignature,
        UnsupportedAlgorithm,
        x509.ExtensionNotFound,
    ):
        return None


def recipient_response(terminal: Terminal) -> dict[str, object]:
    try:
        cert = validate_recipient(terminal.credential_recipient_pem or "", terminal)
    except (
        ValueError,
        TypeError,
        InvalidSignature,
        UnsupportedAlgorithm,
        x509.ExtensionNotFound,
    ) as exc:
        raise HTTPException(409, "credential_recipient_unavailable") from exc
    return {
        "sn": terminal.sn,
        "device_id": terminal.device_id,
        "cert_serial": terminal.cert_serial,
        "recipient_cert_sha256": cert.fingerprint(hashes.SHA256()).hex(),
        "certificate_pem": cert.public_bytes(serialization.Encoding.PEM).decode(
            "ascii"
        ),
        "not_valid_after": cert.not_valid_after_utc,
    }
