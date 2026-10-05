"""Pinned trust and proof of TLS client possession for new agent APIs."""

import urllib.parse
from datetime import UTC, datetime
from functools import lru_cache
from pathlib import Path

from cryptography import x509
from cryptography.exceptions import InvalidSignature, UnsupportedAlgorithm
from cryptography.x509.oid import NameOID
from fastapi import HTTPException, Request


@lru_cache(maxsize=1)
def terminal_ca() -> x509.Certificate:
    # Public trust anchor packaged with PB; not learned from client headers or a network response.
    return x509.load_pem_x509_certificate(
        (Path(__file__).parents[1] / "certs" / "iot_leo4_ca.crt").read_bytes()
    )


def verified_terminal_identity(
    request: Request, *, ca: x509.Certificate | None = None
) -> tuple[str, str]:
    """Verify this route's client signature without changing the legacy TLS trust configuration."""
    verified = request.headers.get("X-Client-Cert-Verified", "")
    # Nginx optional_no_ca authenticates possession in TLS, then PB verifies the trust anchor.
    # These headers must be overwritten by the dedicated ingress location.
    if verified != "SUCCESS" and not verified.startswith("FAILED:"):
        raise HTTPException(401, "verified_certificate_required")
    try:
        cert = x509.load_pem_x509_certificate(
            urllib.parse.unquote(request.headers.get("X-SSL-Client-Cert", "")).encode()
        )
        ca = ca if ca is not None else terminal_ca()
        cert.verify_directly_issued_by(ca)
        issuer = cert.issuer.get_attributes_for_oid(NameOID.COMMON_NAME)
        subject = cert.subject.get_attributes_for_oid(NameOID.COMMON_NAME)
        now = datetime.now(UTC)
        if (
            len(issuer) != 1
            or issuer[0].value != "iot.leo4.ru"
            or len(subject) != 1
            or not cert.not_valid_before_utc <= now < cert.not_valid_after_utc
            or not ca.not_valid_before_utc <= now < ca.not_valid_after_utc
        ):
            raise ValueError("invalid renewal certificate")
        serial = request.headers.get("X-Client-Cert-Serial", "").upper()
        if not serial or serial.lstrip("0") != format(cert.serial_number, "X").lstrip(
            "0"
        ):
            raise ValueError("serial header mismatch")
        return str(subject[0].value), serial
    except (ValueError, TypeError, InvalidSignature, UnsupportedAlgorithm) as exc:
        raise HTTPException(401, "live_new_ca_certificate_required") from exc
