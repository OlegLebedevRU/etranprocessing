"""Certificates router — legacy-compatible check/setup endpoints.

Terminal calls:
  GET  /api/certificates/?function=check&pin=...&tosign=...
  POST /api/certificates/?function=setup&pin=...&cpserial=...   body=PKCS10

Auth is PIN-based (not nginx headers). PIN is pre-allocated in certificate_pins table.
"""

import base64
import hashlib
import os
from datetime import UTC, datetime

from cryptography import x509
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import serialization
from cryptography.utils import int_to_bytes
from fastapi import APIRouter, Depends, Request
from fastapi.responses import Response
from sqlalchemy import select
from sqlalchemy.engine import Row
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.logging_config import cert_logger
from app.models import CertificatePin, Terminal
from app.services.ca import sign_csr

router = APIRouter()

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

SIGN_KEY = os.environ["SIGN_KEY"]
LEGACY_PROVIDER = "Microsoft Enhanced Cryptographic Provider v1.0"
CNG_PROVIDER = "Microsoft Software Key Storage Provider"

ENCODING = "windows-1251"

# DN fields (legacy format)
DN_STATE = "msk"
DN_COUNTRY = "ru"
DN_EMAIL = "1.terminal@forpay.ru"

# PKCS#7 OIDs
OID_DATA = "1.2.840.113549.1.7.1"
OID_SIGNED_DATA = "1.2.840.113549.1.7.2"

# DER tags
TAG_INTEGER = 0x02
TAG_OID = 0x06
TAG_SEQUENCE = 0x30
TAG_SET = 0x31
TAG_CONTEXT = 0xA0


# ---------------------------------------------------------------------------
# XML helpers (legacy format, windows-1251 encoding)
# ---------------------------------------------------------------------------


def xml_response(content: str) -> Response:
    return Response(
        content=f'<?xml version="1.0" encoding="{ENCODING}"?>\n{content}',
        media_type="text/xml",
    )


def ok_response(inner_xml: str, code: int = 0) -> Response:
    return xml_response(
        f"<Response><Result>OK</Result><code>{code}</code>"
        f"<CERTDATA>{inner_xml}</CERTDATA></Response>"
    )


def error_response(description: str, code: int = 1) -> Response:
    return xml_response(
        f"<Response><Result>Error</Result><code>{code}</code>"
        f"<Description>{description}</Description></Response>"
    )


# ---------------------------------------------------------------------------
# PIN lookup
# ---------------------------------------------------------------------------


async def _find_terminal_by_pin(
    pin: str, db: AsyncSession
) -> Row[tuple[Terminal, CertificatePin]] | None:
    """Look up terminal via certificate_pins table. Returns (terminal, pin_row) or None."""
    result = await db.execute(
        select(Terminal, CertificatePin)
        .join(CertificatePin, CertificatePin.terminal_id == Terminal.id)
        .where(CertificatePin.pin == pin, CertificatePin.status == "pending")
    )
    row = result.one_or_none()
    return row if row else None


# ---------------------------------------------------------------------------
# Main entry point
# ---------------------------------------------------------------------------


@router.get("")
@router.post("")
@router.get("/")
@router.post("/")
@router.get("/Dispatcher.ashx")
@router.post("/Dispatcher.ashx")
async def certificates_handler(request: Request, db: AsyncSession = Depends(get_db)):
    """Routes by ?function= param — same as legacy Dispatcher.ashx."""
    params = dict(request.query_params)

    body = await request.body()
    if body:
        from urllib.parse import parse_qs, unquote_plus

        raw = unquote_plus(body.decode(ENCODING, errors="replace"))
        for k, v in parse_qs(raw, keep_blank_values=True).items():
            params[k] = v[0]

    function = params.get("function", "").lower()

    if function == "check":
        return await _handle_check(params, db)
    elif function in ("setup", "usercert_setup"):
        return await _handle_setup(params, request, db)
    else:
        return error_response(f"Unknown function: {function}")


# ---------------------------------------------------------------------------
# CHECK
# ---------------------------------------------------------------------------


async def _handle_check(params: dict, db: AsyncSession) -> Response:
    """PIN → terminal → return DN + sign."""
    pin = params.get("pin", "").strip()
    tosign = params.get("tosign", "")

    if not pin:
        return error_response("PIN не указан", code=2)

    found = await _find_terminal_by_pin(pin, db)
    if not found:
        cert_logger.warning("CHECK: pin not found: %s", pin)
        return error_response("Пин-код не существует", code=2)

    terminal, _pin_row = found

    # sign = MD5(decode_base64(tosign) + SignKey)
    sign = ""
    if tosign:
        try:
            decoded = base64.b64decode(tosign).decode(ENCODING, errors="replace")
            sign = (
                hashlib.md5((decoded + SIGN_KEY).encode(ENCODING)).hexdigest().upper()
            )
        except ValueError, UnicodeDecodeError:
            cert_logger.warning("CHECK: failed to decode/tosign for pin=%s", pin)

    dn = (
        f"CN={terminal.sn}"
        f",O={terminal.org_id}"
        f",OU={terminal.device_id}"
        f",S={DN_STATE},C={DN_COUNTRY}"
        f",L={terminal.id}"
        f",E={DN_EMAIL}"
    )

    v = params.get("v", "")
    prov = CNG_PROVIDER if v == "26" else LEGACY_PROVIDER

    cert_logger.info(
        "CHECK: pin=%s, sn=%s, device=%d, org=%d, v=%s",
        pin,
        terminal.sn,
        terminal.device_id,
        terminal.org_id,
        v,
    )

    inner = (
        f"<catype>SubCA</catype>"
        f"<prov>{prov}</prov>"
        f"<dn>{dn}</dn>"
        f"<pin>{pin}</pin>"
        f"<sign>{sign}</sign>"
    )
    return ok_response(inner)


# ---------------------------------------------------------------------------
# SETUP
# ---------------------------------------------------------------------------


async def _handle_setup(params: dict, request: Request, db: AsyncSession) -> Response:
    """PIN lookup → CA sign → PKCS#7 chain → terminal."""
    pin = params.get("pin", "").strip()
    cpserial = params.get("cpserial", "")

    if not pin:
        return error_response("PIN не указан", code=2)

    found = await _find_terminal_by_pin(pin, db)
    if not found:
        cert_logger.warning("SETUP: pin not found: %s", pin)
        return error_response("Пин-код не существует", code=2)

    terminal, cert_pin = found

    body = await request.body()
    if not body:
        return error_response("PKCS10 не найден в теле запроса", code=4)

    # Terminal sends raw Base64 (no PEM headers). Wrap for CA.
    pkcs10_raw = body.decode("utf-8", errors="replace")
    if "-----BEGIN" not in pkcs10_raw:
        pkcs10_pem = f"-----BEGIN CERTIFICATE REQUEST-----\n{pkcs10_raw.strip()}\n-----END CERTIFICATE REQUEST-----\n"
    else:
        pkcs10_pem = pkcs10_raw

    cert_logger.info("SETUP: pin=%s, sn=%s, cpserial=%s", pin, terminal.sn, cpserial)

    try:
        ca_result = await sign_csr(
            csr_pem=pkcs10_pem,
            sign=cpserial,
            cn=terminal.sn,
        )
    except Exception as e:  # noqa: BLE001
        cert_logger.error("SETUP: CA failed: %s", e, exc_info=True)
        return error_response(f"Ошибка выпуска сертификата: {e}", code=1)

    terminal.cert_serial = ca_result.serial_number
    cert_pin.status = "used"
    cert_pin.used_at = datetime.now(UTC)
    await db.commit()

    cert_logger.info(
        "SETUP OK: pin=%s, serial=%s, valid_until=%s",
        pin,
        ca_result.serial_number,
        ca_result.not_valid_after,
    )

    pkcs7_b64 = _build_pkcs7_chain(ca_result.cert_pem, ca_result.ca_pem)
    return ok_response(pkcs7_b64)


# ---------------------------------------------------------------------------
# PKCS#7 degenerate (certificates-only) builder
# ---------------------------------------------------------------------------


def _der_len(data: bytes) -> bytes:
    """DER length encoding."""
    n = len(data)
    if n < 0x80:
        return bytes([n])
    elif n < 0x100:
        return bytes([0x81, n])
    else:
        return bytes([0x82, (n >> 8) & 0xFF, n & 0xFF])


def _der_tlv(tag: int, value: bytes) -> bytes:
    """DER TLV (tag + length + value)."""
    return bytes([tag]) + _der_len(value) + value


def _der_int(n: int) -> bytes:
    """DER INTEGER."""
    if n < 0:
        raise ValueError("Negative integer")
    b = int_to_bytes(n)
    if b[0] & 0x80:
        b = b"\x00" + b
    return _der_tlv(TAG_INTEGER, b)


def _der_seq(*parts: bytes) -> bytes:
    return _der_tlv(TAG_SEQUENCE, b"".join(parts))


def _der_set(*parts: bytes) -> bytes:
    return _der_tlv(TAG_SET, b"".join(parts))


def _der_ctx(tag_num: int, value: bytes) -> bytes:
    return _der_tlv(TAG_CONTEXT | tag_num, value)


def _der_oid(dotted: str) -> bytes:
    """DER OID from dotted notation."""
    parts = [int(x) for x in dotted.split(".")]
    body = bytes([40 * parts[0] + parts[1]])
    for p in parts[2:]:
        if p < 0x80:
            body += bytes([p])
        else:
            chunks = [p & 0x7F]
            p >>= 7
            while p:
                chunks.append(0x80 | (p & 0x7F))
                p >>= 7
            body += bytes(reversed(chunks))
    return _der_tlv(TAG_OID, body)


def _build_pkcs7_chain(cert_pem: str, ca_pem: str) -> str:
    """Build PKCS#7 degenerate (certificates-only) from leaf + CA PEM certs.

    Returns raw Base64 matching Windows CA CR_OUT_CHAIN format.
    ContentInfo { SignedData { certificates[], no signers } }
    """
    leaf = x509.load_pem_x509_certificate(cert_pem.encode(), default_backend())
    ca = x509.load_pem_x509_certificate(ca_pem.encode(), default_backend())

    certs_der = leaf.public_bytes(serialization.Encoding.DER) + ca.public_bytes(
        serialization.Encoding.DER
    )

    signed_data = _der_seq(
        _der_int(1),  # version
        _der_set(),  # digestAlgorithms (empty)
        _der_seq(_der_oid(OID_DATA)),  # encapContentInfo (empty data)
        _der_ctx(0, certs_der),  # certificates [0]
        _der_set(),  # signerInfos (empty)
    )

    content_info = _der_seq(
        _der_oid(OID_SIGNED_DATA),
        _der_ctx(0, signed_data),
    )

    return base64.b64encode(content_info).decode("ascii")
