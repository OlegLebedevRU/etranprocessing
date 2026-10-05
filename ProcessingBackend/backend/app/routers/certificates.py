"""Certificates router — legacy-compatible check/setup endpoints.

Terminal calls:
  GET  /api/certificates/?function=check&pin=...&tosign=...
  POST /api/certificates/?function=setup&pin=...&cpserial=...   body=PKCS10

Auth is PIN-based (not nginx headers). PIN is pre-allocated in certificate_pins table.
"""

import asyncio
import base64
import hashlib
import os
import urllib.parse
from datetime import UTC, datetime, timedelta
from functools import lru_cache
from pathlib import Path
from typing import Literal

from cryptography import x509
from cryptography.exceptions import InvalidSignature, UnsupportedAlgorithm
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import serialization
from cryptography.utils import int_to_bytes
from cryptography.x509.oid import NameOID
from fastapi import APIRouter, Depends, HTTPException, Request, Response, status
from sqlalchemy import and_, exists, or_, select, text, update
from sqlalchemy.engine import Row
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.database import get_db
from app.dependencies import require_service_auth
from app.logging_config import cert_logger
from app.models import (
    CertificatePin,
    L4DeskAuditEvent,
    L4DeskTerminal,
    Terminal,
    TerminalCertHistory,
)
from app.schemas.certificates import (
    IssueCertificatePinRequest,
    IssueCertificatePinResponse,
    RenewalEnrollRequest,
    RenewalPinRequest,
    RenewalPinResponse,
)
from app.services.ca import sign_csr
from app.services.cert_billing import (
    compute_pin_expiry,
    generate_unique_pin,
    mask_pin,
)
from app.services.certificate_permission import validate_pin_permission
from app.services.leo4proxy_policy import subscription_allowance

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
# PIN lookup & safe retry cache
# ---------------------------------------------------------------------------

_recent_setup_cache: dict[str, tuple[datetime, str, str]] = {}


def _cleanup_setup_cache(now: datetime) -> None:
    """Evict cache items older than 30 minutes."""
    if len(_recent_setup_cache) > 200:
        expired_keys = [
            k
            for k, (cached_at, _, _) in _recent_setup_cache.items()
            if now - cached_at > timedelta(minutes=30)
        ]
        for k in expired_keys:
            _recent_setup_cache.pop(k, None)


async def _check_setup_retry(
    pin: str, cpserial: str, _db: AsyncSession
) -> Response | None:
    """Safely handle setup retry if network dropped after PIN was marked used."""
    if pin not in _recent_setup_cache:
        return None

    cached_at, cached_cpserial, pkcs7_b64 = _recent_setup_cache[pin]
    now = datetime.now(UTC)
    _cleanup_setup_cache(now)

    if now - cached_at > timedelta(minutes=15):
        _recent_setup_cache.pop(pin, None)
        return None

    if cpserial and cached_cpserial and cpserial.lower() != cached_cpserial.lower():
        return None

    cert_logger.info(
        "SETUP RETRY: returning cached certificate response for pin=%s",
        mask_pin(pin),
    )
    return ok_response(pkcs7_b64)


async def _find_terminal_by_pin(
    pin: str,
    db: AsyncSession,
    for_update: bool = False,
    purpose: str = "setup",
    include_used: bool = False,
) -> Row[tuple[Terminal, CertificatePin]] | None:
    """Look up terminal via certificate_pins table. Returns (terminal, pin_row) or None.

    Lazily expires a `pending` PIN whose TTL has passed (marks it `expired` and
    treats it as not found), instead of relying on a separate sweep job.
    Uses pessimistic row locking when for_update=True to prevent race conditions.
    """
    stmt = (
        select(Terminal, CertificatePin)
        .join(CertificatePin, CertificatePin.terminal_id == Terminal.id)
        .where(CertificatePin.pin == pin, CertificatePin.purpose == purpose)
    )
    stmt = stmt.where(
        CertificatePin.status.in_(["pending", "used"])
        if include_used
        else CertificatePin.status == "pending"
    )
    if for_update:
        stmt = stmt.with_for_update(of=CertificatePin)

    result = await db.execute(stmt)
    row = result.one_or_none()
    if not row:
        return None

    _terminal, cert_pin = row
    if (cert_pin.purpose or "setup") != purpose:
        return None
    if cert_pin.expires_at <= datetime.now(UTC):
        if cert_pin.status == "pending":
            cert_pin.status = "expired"
            await db.commit()
        cert_logger.info("PIN expired: pin=%s", mask_pin(pin))
        return None

    return row


def _parse_ca_datetime(value: str) -> datetime | None:
    """Parse the CA's not_valid_after string into an aware datetime, if possible."""
    if not value:
        return None
    try:
        parsed = datetime.fromisoformat(value)
    except ValueError:
        cert_logger.warning("Unable to parse CA not_valid_after: %s", value)
        return None
    return parsed if parsed.tzinfo else parsed.replace(tzinfo=UTC)


# ---------------------------------------------------------------------------
# Main entry point
# ---------------------------------------------------------------------------


@lru_cache(maxsize=1)
def _renewal_ca() -> x509.Certificate:
    # Public trust anchor packaged with PB; not learned from client headers or a network response.
    return x509.load_pem_x509_certificate(
        (Path(__file__).parents[1] / "certs" / "iot_leo4_ca.crt").read_bytes()
    )


def _renewal_identity(request: Request) -> tuple[str, str]:
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
        ca = _renewal_ca()
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


async def _verify_renewal_owner(
    terminal: Terminal,
    identity: tuple[str, str],
    db: AsyncSession,
    recovery: CertificatePin | None = None,
) -> None:
    sn, serial = identity
    accepted = {terminal.cert_serial}
    if recovery:
        current_issuance = await db.scalar(
            select(
                exists().where(
                    TerminalCertHistory.pin_id == recovery.id,
                    TerminalCertHistory.terminal_id == terminal.id,
                    TerminalCertHistory.cert_serial == terminal.cert_serial,
                )
            )
        )
        if not current_issuance:
            raise HTTPException(409, "renewal_recovery_superseded")
        accepted.add(recovery.renewal_auth_serial)
    accepted_values = {value.upper().lstrip("0") for value in accepted if value}
    if terminal.sn != sn or serial.upper().lstrip("0") not in accepted_values:
        raise HTTPException(401, "renewal_identity_mismatch")
    if not terminal.is_active or not await subscription_allowance(db, terminal):
        raise HTTPException(403, "terminal_renewal_not_allowed")


@router.post("/renew")
async def renew_certificate(
    payload: RenewalEnrollRequest, request: Request, db: AsyncSession = Depends(get_db)
) -> Response:
    identity = _renewal_identity(request)
    async with asyncio.timeout(60):
        params = {
            "pin": payload.pin,
            "cpserial": payload.cpserial,
            "v": "26",
            "tosign": payload.tosign,
        }
        if payload.function == "check":
            return await _handle_check(params, db, identity)
        if not payload.csr:
            raise HTTPException(422, "csr_required")
        return await _handle_setup(params, request, db, identity, payload.csr.encode())


@router.post("/renewal-pins", response_model=RenewalPinResponse, status_code=201)
async def issue_renewal_pin(
    payload: RenewalPinRequest,
    db: AsyncSession = Depends(get_db),
    _auth: str = Depends(require_service_auth),
) -> RenewalPinResponse:
    if not (settings.service_auth_token or settings.internal_service_key):
        raise HTTPException(503, "renewal_service_auth_not_configured")
    terminal = (
        await db.execute(
            select(Terminal)
            .where(
                Terminal.id == payload.terminal_id,
                Terminal.org_id == payload.tenant_id,
                Terminal.sn == payload.sn,
            )
            .with_for_update(of=Terminal)
        )
    ).scalar_one_or_none()
    if terminal is None:
        raise HTTPException(404, "terminal_not_found")
    if not terminal.is_active or not await subscription_allowance(db, terminal):
        raise HTTPException(403, "terminal_renewal_not_allowed")
    now = datetime.now(UTC)
    if (
        not terminal.cert_serial
        or len(terminal.cert_serial) <= 20
        or terminal.cert_not_valid_after is None
        or terminal.cert_not_valid_after <= now
    ):
        raise HTTPException(409, "live_new_ca_certificate_required")
    paid_item = await validate_pin_permission(
        db,
        terminal,
        "renew",
        payload.order_item_id,
        admin_override=payload.admin_override,
    )
    current_issuance = exists().where(
        TerminalCertHistory.pin_id == CertificatePin.id,
        TerminalCertHistory.terminal_id == terminal.id,
        TerminalCertHistory.cert_serial == terminal.cert_serial,
    )
    reusable = (
        select(CertificatePin)
        .where(
            CertificatePin.terminal_id == terminal.id,
            CertificatePin.org_id == terminal.org_id,
            CertificatePin.purpose == "renew",
            CertificatePin.expires_at > now + timedelta(seconds=120),
            or_(
                CertificatePin.status == "pending",
                and_(
                    CertificatePin.status == "used",
                    CertificatePin.used_at > now - timedelta(minutes=15),
                    CertificatePin.renewal_response.is_not(None),
                    current_issuance,
                ),
            ),
        )
        .order_by(CertificatePin.id.desc())
        .limit(1)
    )
    if payload.pin_id:
        reusable = reusable.where(CertificatePin.id == payload.pin_id)
    if paid_item is not None:
        reusable = reusable.where(CertificatePin.order_item_id == paid_item.id)
    pin = (await db.execute(reusable)).scalar_one_or_none()
    if pin is None and payload.pin_id:
        raise HTTPException(409, "renewal_pin_unavailable")
    if pin is None:
        if (
            paid_item is not None
            and await db.scalar(
                select(CertificatePin.id).where(
                    CertificatePin.order_item_id == paid_item.id
                )
            )
            is not None
        ):
            raise HTTPException(410, "certificate_paid_permission_consumed_or_expired")
        pin = CertificatePin(
            pin=await generate_unique_pin(db),
            terminal_id=terminal.id,
            org_id=terminal.org_id,
            purpose="renew",
            created_by="menubuilder",
            creation_source="tenant",
            payment_required=paid_item is not None,
            order_item_id=paid_item.id if paid_item else None,
            status="pending",
            expires_at=compute_pin_expiry(now),
        )
        db.add(pin)
        await db.flush()
        await db.commit()
    return RenewalPinResponse(
        pin_id=pin.id,
        tenant_id=terminal.org_id,
        terminal_id=terminal.id,
        sn=terminal.sn,
        pin=pin.pin,
        expires_at=pin.expires_at,
    )


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


async def _handle_check(
    params: dict, db: AsyncSession, renewal_identity: tuple[str, str] | None = None
) -> Response:
    """PIN → terminal → return DN + sign."""
    pin = params.get("pin", "").strip()
    tosign = params.get("tosign", "")

    if not pin:
        return error_response("PIN не указан", code=2)

    found = await _find_terminal_by_pin(
        pin, db, purpose="renew" if renewal_identity else "setup"
    )
    if not found:
        pin_check = await db.execute(
            select(CertificatePin).where(CertificatePin.pin == pin)
        )
        existing_pin = pin_check.scalar_one_or_none()
        if existing_pin:
            if existing_pin.status == "used":
                cert_logger.warning("CHECK: pin already used: %s", mask_pin(pin))
                return error_response("Пин-код уже использован", code=2)
            elif (
                existing_pin.status == "expired"
                or existing_pin.expires_at <= datetime.now(UTC)
            ):
                cert_logger.warning("CHECK: pin expired: %s", mask_pin(pin))
                return error_response("Пин-код просрочен", code=2)

        cert_logger.warning("CHECK: pin not found: %s", mask_pin(pin))
        return error_response("Пин-код не существует", code=2)

    terminal, _pin_row = found
    if renewal_identity:
        await _verify_renewal_owner(terminal, renewal_identity, db)

    # sign = MD5(decode_base64(tosign) + SignKey)
    sign = ""
    if tosign:
        try:
            decoded = base64.b64decode(tosign).decode(ENCODING, errors="replace")
            sign = (
                hashlib.md5((decoded + SIGN_KEY).encode(ENCODING)).hexdigest().upper()
            )
        except ValueError, UnicodeDecodeError:
            cert_logger.warning(
                "CHECK: failed to decode/tosign for pin=%s", mask_pin(pin)
            )

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
        mask_pin(pin),
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


async def _handle_setup(
    params: dict,
    request: Request,
    db: AsyncSession,
    renewal_identity: tuple[str, str] | None = None,
    csr_body: bytes | None = None,
) -> Response:
    """PIN lookup → CA sign → PKCS#7 chain → terminal."""
    pin = params.get("pin", "").strip()
    cpserial = params.get("cpserial", "")

    if not pin:
        return error_response("PIN не указан", code=2)

    found = await _find_terminal_by_pin(
        pin,
        db,
        for_update=True,
        purpose="renew" if renewal_identity else "setup",
        include_used=bool(renewal_identity),
    )
    if not found:
        retry_resp = (
            None if renewal_identity else await _check_setup_retry(pin, cpserial, db)
        )
        if retry_resp:
            return retry_resp

        pin_check = await db.execute(
            select(CertificatePin).where(CertificatePin.pin == pin)
        )
        existing_pin = pin_check.scalar_one_or_none()
        if existing_pin:
            if existing_pin.status == "used":
                cert_logger.warning("SETUP: pin already used: %s", mask_pin(pin))
                return error_response("Пин-код уже использован", code=2)
            elif (
                existing_pin.status == "expired"
                or existing_pin.expires_at <= datetime.now(UTC)
            ):
                cert_logger.warning("SETUP: pin expired: %s", mask_pin(pin))
                return error_response("Пин-код просрочен", code=2)

        cert_logger.warning("SETUP: pin not found: %s", mask_pin(pin))
        return error_response("Пин-код не существует", code=2)

    terminal, cert_pin = found
    if renewal_identity:
        terminal = (
            await db.execute(
                select(Terminal)
                .where(Terminal.id == terminal.id)
                .with_for_update(of=Terminal)
                .execution_options(populate_existing=True)
            )
        ).scalar_one()
        await _verify_renewal_owner(
            terminal,
            renewal_identity,
            db,
            cert_pin if cert_pin.status == "used" else None,
        )

    body = csr_body if csr_body is not None else await request.body()
    if not body:
        return error_response("PKCS10 не найден в теле запроса", code=4)

    # Terminal sends raw Base64 (no PEM headers). Wrap for CA.
    pkcs10_raw = body.decode("utf-8", errors="replace")
    if "-----BEGIN" not in pkcs10_raw:
        pkcs10_pem = f"-----BEGIN CERTIFICATE REQUEST-----\n{pkcs10_raw.strip()}\n-----END CERTIFICATE REQUEST-----\n"
    else:
        pkcs10_pem = pkcs10_raw

    # 1. Parse CSR and verify cryptographic signature
    try:
        csr = x509.load_pem_x509_csr(
            pkcs10_pem.encode("utf-8"), backend=default_backend()
        )
    except Exception as e:  # noqa: BLE001
        cert_logger.warning(
            "SETUP: Invalid CSR format for pin=%s: %s", mask_pin(pin), e
        )
        return error_response(f"Неверный формат CSR: {e}", code=4)

    if not csr.is_signature_valid:
        cert_logger.warning("SETUP: CSR signature is invalid for pin=%s", mask_pin(pin))
        return error_response("Недействительная подпись CSR", code=4)

    csr_hash = hashlib.sha256(csr.public_bytes(serialization.Encoding.DER)).hexdigest()
    if renewal_identity and cert_pin.status == "used":
        if (
            cert_pin.renewal_csr_sha256 == csr_hash
            and cert_pin.renewal_response
            and cert_pin.used_at
            and datetime.now(UTC) < cert_pin.used_at + timedelta(minutes=15)
        ):
            return ok_response(cert_pin.renewal_response)
        raise HTTPException(409, "renewal_pin_consumed")

    # 2. Check CSR mismatch against terminal identity
    # Check OU (device_id) if present
    ou_attrs = csr.subject.get_attributes_for_oid(NameOID.ORGANIZATIONAL_UNIT_NAME)
    if ou_attrs:
        ou_val = str(ou_attrs[0].value)
        digits = "".join(filter(str.isdigit, ou_val))
        if digits and int(digits) != 0 and int(digits) != terminal.device_id:
            cert_logger.warning(
                "SETUP: CSR mismatch OU=%s does not match terminal device_id=%d",
                ou_val,
                terminal.device_id,
            )
            return error_response("Несоответствие данных CSR терминалу", code=4)

    # Check O (org_id) if present
    o_attrs = csr.subject.get_attributes_for_oid(NameOID.ORGANIZATION_NAME)
    if o_attrs:
        o_val = str(o_attrs[0].value)
        digits = "".join(filter(str.isdigit, o_val))
        if digits and int(digits) != 0 and int(digits) != terminal.org_id:
            cert_logger.warning(
                "SETUP: CSR mismatch O=%s does not match terminal org_id=%d",
                o_val,
                terminal.org_id,
            )
            return error_response("Несоответствие данных CSR терминалу", code=4)

    # Check CN if present: In legacy flow, CN is either cpserial (sign) or terminal.sn
    cn_attrs = csr.subject.get_attributes_for_oid(NameOID.COMMON_NAME)
    if cn_attrs:
        csr_cn = str(cn_attrs[0].value).strip()
        expected_cns = {terminal.sn.lower(), terminal.sn.upper()}
        if cpserial:
            expected_cns.add(cpserial.lower())
            expected_cns.add(cpserial.upper())
        if csr_cn.lower() not in expected_cns:
            cert_logger.warning(
                "SETUP: CSR mismatch CN='%s' does not match terminal.sn='%s' or cpserial='%s'",
                csr_cn,
                terminal.sn,
                cpserial,
            )
            return error_response("Несоответствие данных CSR терминалу", code=4)

    cert_logger.info(
        "SETUP: pin=%s, sn=%s, cpserial=%s", mask_pin(pin), terminal.sn, cpserial
    )

    try:
        if renewal_identity:
            remaining = (cert_pin.expires_at - datetime.now(UTC)).total_seconds()
            if remaining <= 0:
                raise HTTPException(410, "renewal_pin_expired")
            async with asyncio.timeout(min(remaining, 60)):
                ca_result = await sign_csr(
                    csr_pem=pkcs10_pem, sign=cpserial, cn=terminal.sn
                )
        else:
            ca_result = await sign_csr(
                csr_pem=pkcs10_pem, sign=cpserial, cn=terminal.sn
            )
    except HTTPException:
        raise
    except Exception as e:  # noqa: BLE001
        cert_logger.error("SETUP: CA failed: %s", e, exc_info=True)
        return error_response(f"Ошибка выпуска сертификата: {e}", code=1)

    if renewal_identity and cert_pin.expires_at <= datetime.now(UTC):
        raise HTTPException(410, "renewal_pin_expired")

    not_valid_after = _parse_ca_datetime(ca_result.not_valid_after)

    terminal.cert_serial = ca_result.serial_number
    terminal.cert_not_valid_after = not_valid_after
    cert_pin.status = "used"
    cert_pin.used_at = datetime.now(UTC)
    pkcs7_b64 = _build_pkcs7_chain(ca_result.cert_pem, ca_result.ca_pem)
    if renewal_identity:
        cert_pin.renewal_auth_serial = renewal_identity[1]
        cert_pin.renewal_csr_sha256 = csr_hash
        cert_pin.renewal_response = pkcs7_b64
    db.add(
        TerminalCertHistory(
            terminal_id=terminal.id,
            cert_serial=ca_result.serial_number,
            not_valid_after=not_valid_after,
            pin_id=cert_pin.id,
            source="renew" if renewal_identity else "setup",
        )
    )

    # Update L4DeskTerminal if it exists
    l4_res = await db.execute(
        select(L4DeskTerminal).where(L4DeskTerminal.terminal_id == terminal.id)
    )
    l4_term = l4_res.scalar_one_or_none()
    if l4_term:
        l4_term.pin_state = "consumed"
        l4_term.certificate_reference = ca_result.serial_number

    # Audit event (masked pin, never plain)
    audit = L4DeskAuditEvent(
        tenant_id=terminal.org_id,
        actor="terminal",
        event_type="certificate_renewed"
        if renewal_identity
        else "certificate_enrolled",
        subject_type="terminal",
        subject_id=str(terminal.id),
        operation_id=None,
        correlation_id=f"cpserial:{cpserial}",
        outcome="success",
        details={
            "sn": terminal.sn,
            "pin_masked": mask_pin(pin),
            "cert_serial": ca_result.serial_number,
            "cpserial": cpserial,
            "valid_until": ca_result.not_valid_after,
        },
    )
    db.add(audit)
    await db.commit()

    cert_logger.info(
        "SETUP OK: pin=%s, serial=%s, valid_until=%s",
        mask_pin(pin),
        ca_result.serial_number,
        ca_result.not_valid_after,
    )

    if not renewal_identity:
        _recent_setup_cache[pin] = (datetime.now(UTC), cpserial, pkcs7_b64)
    return ok_response(pkcs7_b64)


# ---------------------------------------------------------------------------
# Additive Service-Auth Certificate PIN Contract (L4D-06A-PB)
# ---------------------------------------------------------------------------


@router.post(
    "/pins/issue",
    response_model=IssueCertificatePinResponse,
    status_code=status.HTTP_201_CREATED,
    summary="Issue or replay one-time certificate PIN (L4D-06A-PB)",
)
@router.post(
    "/pins",
    response_model=IssueCertificatePinResponse,
    status_code=status.HTTP_201_CREATED,
    include_in_schema=False,
)
async def issue_certificate_pin_endpoint(
    payload: IssueCertificatePinRequest,
    response: Response,
    _auth: str = Depends(require_service_auth),
    db: AsyncSession = Depends(get_db),
) -> IssueCertificatePinResponse:
    if settings.product_scope_split_enabled:
        # Serialize the provider's operation replay before reading its audit fact.
        for identity in (
            f"pb-pin-operation:{payload.operation_id}",
            f"pb-terminal-pin:{payload.terminal_id}",
        ):
            await db.execute(
                text("SELECT pg_advisory_xact_lock(hashtextextended(:identity, 0))"),
                {"identity": identity},
            )
    # 1. Check idempotency first: has this operation_id already been executed?
    audit_res = await db.execute(
        select(L4DeskAuditEvent).where(
            L4DeskAuditEvent.operation_id == payload.operation_id,
            L4DeskAuditEvent.event_type == "certificate_pin_issued",
        )
    )
    existing_audit = audit_res.scalar_one_or_none()

    if existing_audit:
        details = existing_audit.details or {}
        # Verify identical parameters
        if (
            existing_audit.tenant_id != payload.tenant_id
            or existing_audit.subject_id != str(payload.terminal_id)
            or details.get("sn") != payload.sn
        ):
            raise HTTPException(
                status_code=status.HTTP_409_CONFLICT,
                detail={
                    "error": "operation_id_conflict",
                    "message": f"operation_id '{payload.operation_id}' was already executed with different parameters",
                    "error_code": "OPERATION_ID_CONFLICT",
                    "operation_id": payload.operation_id,
                },
            )

        # Idempotent replay: find existing CertificatePin
        pin_id = details.get("pin_id")
        cert_pin = None
        if pin_id:
            pin_res = await db.execute(
                select(CertificatePin).where(CertificatePin.id == pin_id)
            )
            cert_pin = pin_res.scalar_one_or_none()
        if not cert_pin:
            pin_res = await db.execute(
                select(CertificatePin).where(
                    CertificatePin.terminal_id == payload.terminal_id,
                    CertificatePin.created_by == f"op:{payload.operation_id[:90]}",
                )
            )
            cert_pin = pin_res.scalar_one_or_none()

        now = datetime.now(UTC)
        pin_val: str | None = None
        masked_val = str(details.get("pin_masked", "***"))
        current_status: Literal["issued", "consumed", "expired"] = "expired"

        if cert_pin:
            masked_val = mask_pin(cert_pin.pin)
            if cert_pin.status == "pending":
                if cert_pin.expires_at <= now:
                    cert_pin.status = "expired"
                    await db.commit()
                    current_status = "expired"
                else:
                    current_status = "issued"
                    pin_val = cert_pin.pin
            elif cert_pin.status == "used":
                current_status = "consumed"
            else:
                current_status = "expired"
            exp_time = cert_pin.expires_at
            created_time = cert_pin.created_at or now
        else:
            exp_str = details.get("expires_at")
            exp_time = datetime.fromisoformat(exp_str) if exp_str else now
            created_time = existing_audit.occurred_at

        cert_logger.info(
            "PIN REPLAY: op=%s terminal=%d sn=%s pin=%s status=%s",
            payload.operation_id,
            payload.terminal_id,
            payload.sn,
            masked_val,
            current_status,
        )
        response.status_code = status.HTTP_200_OK
        return IssueCertificatePinResponse(
            operation_id=payload.operation_id,
            correlation_id=payload.correlation_id or existing_audit.correlation_id,
            tenant_id=payload.tenant_id,
            terminal_id=payload.terminal_id,
            sn=payload.sn,
            pin=pin_val,
            pin_masked=masked_val,
            status=current_status,
            expires_at=exp_time,
            created_at=created_time,
            replayed=True,
        )

    # 2. Lookup terminal for new issuance
    term_res = await db.execute(
        select(Terminal)
        .where(Terminal.id == payload.terminal_id)
        .with_for_update(of=Terminal)
    )
    terminal = term_res.scalar_one_or_none()
    if not terminal:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail={
                "error": "terminal_not_found",
                "message": f"Terminal {payload.terminal_id} not found",
                "error_code": "TERMINAL_NOT_FOUND",
                "operation_id": payload.operation_id,
            },
        )

    # 3. Ownership binding
    if terminal.org_id != payload.tenant_id:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail={
                "error": "tenant_ownership_mismatch",
                "message": f"Terminal {payload.terminal_id} does not belong to tenant {payload.tenant_id}",
                "error_code": "TENANT_OWNERSHIP_MISMATCH",
                "operation_id": payload.operation_id,
            },
        )

    # 4. Serial number binding
    if terminal.sn != payload.sn:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail={
                "error": "serial_number_mismatch",
                "message": f"Provided SN '{payload.sn}' does not match terminal SN '{terminal.sn}'",
                "error_code": "SERIAL_NUMBER_MISMATCH",
                "operation_id": payload.operation_id,
            },
        )

    now = datetime.now(UTC)
    paid_item = await validate_pin_permission(
        db, terminal, "setup", payload.order_item_id
    )
    cert_pin = None
    if paid_item is not None:
        cert_pin = await db.scalar(
            select(CertificatePin).where(
                CertificatePin.order_item_id == paid_item.id,
                CertificatePin.terminal_id == terminal.id,
                CertificatePin.org_id == terminal.org_id,
                CertificatePin.purpose == "setup",
            )
        )
        if (
            cert_pin is None
            or cert_pin.status != "pending"
            or cert_pin.expires_at <= now
        ):
            raise HTTPException(410, "certificate_paid_permission_consumed_or_expired")
        pin_value, expires_at = cert_pin.pin, cert_pin.expires_at
    else:
        # Purpose isolation: ordinary setup never expires a queued renewal.
        await db.execute(
            update(CertificatePin)
            .where(
                CertificatePin.terminal_id == terminal.id,
                CertificatePin.org_id == terminal.org_id,
                CertificatePin.status == "pending",
                CertificatePin.purpose == "setup",
            )
            .values(status="expired")
        )
        pin_value = await generate_unique_pin(db)
        expires_at = compute_pin_expiry(now=now, ttl_seconds=payload.ttl_seconds)
        cert_pin = CertificatePin(
            pin=pin_value,
            terminal_id=terminal.id,
            org_id=terminal.org_id,
            created_by=f"op:{payload.operation_id[:90]}",
            creation_source="tenant",
            payment_required=False,
            status="pending",
            purpose="setup",
            expires_at=expires_at,
        )
        db.add(cert_pin)
        await db.flush()

    # Update L4DeskTerminal if it exists
    l4_res = await db.execute(
        select(L4DeskTerminal).where(L4DeskTerminal.terminal_id == terminal.id)
    )
    l4_term = l4_res.scalar_one_or_none()
    if l4_term:
        l4_term.pin_state = "issued"

    # Audit event (NEVER plain PIN!)
    audit = L4DeskAuditEvent(
        tenant_id=terminal.org_id,
        actor=payload.actor or "system",
        event_type="certificate_pin_issued",
        subject_type="terminal",
        subject_id=str(terminal.id),
        operation_id=payload.operation_id,
        correlation_id=payload.correlation_id or payload.operation_id,
        outcome="success",
        details={
            "sn": terminal.sn,
            "pin_id": cert_pin.id,
            "pin_masked": mask_pin(pin_value),
            "expires_at": expires_at.isoformat(),
            "ttl_seconds": payload.ttl_seconds,
        },
    )
    db.add(audit)
    await db.commit()

    cert_logger.info(
        "PIN ISSUED: op=%s terminal=%d sn=%s pin=%s status=issued",
        payload.operation_id,
        terminal.id,
        terminal.sn,
        mask_pin(pin_value),
    )

    response.status_code = status.HTTP_201_CREATED
    return IssueCertificatePinResponse(
        operation_id=payload.operation_id,
        correlation_id=payload.correlation_id,
        tenant_id=terminal.org_id,
        terminal_id=terminal.id,
        sn=terminal.sn,
        pin=pin_value,
        pin_masked=mask_pin(pin_value),
        status="issued",
        expires_at=expires_at,
        created_at=cert_pin.created_at or now,
        replayed=False,
    )


@router.get(
    "/pins/by-operation/{operation_id}",
    response_model=IssueCertificatePinResponse,
    summary="Query certificate PIN status by operation_id",
)
async def get_pin_by_operation_endpoint(
    operation_id: str,
    _auth: str = Depends(require_service_auth),
    db: AsyncSession = Depends(get_db),
) -> IssueCertificatePinResponse:
    audit_res = await db.execute(
        select(L4DeskAuditEvent).where(
            L4DeskAuditEvent.operation_id == operation_id,
            L4DeskAuditEvent.event_type == "certificate_pin_issued",
        )
    )
    audit = audit_res.scalar_one_or_none()
    if not audit:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail={
                "error": "operation_not_found",
                "message": f"Operation '{operation_id}' not found",
                "error_code": "OPERATION_NOT_FOUND",
                "operation_id": operation_id,
            },
        )

    details = audit.details or {}
    pin_id = details.get("pin_id")
    cert_pin = None
    if pin_id:
        pin_res = await db.execute(
            select(CertificatePin).where(CertificatePin.id == pin_id)
        )
        cert_pin = pin_res.scalar_one_or_none()
    if not cert_pin:
        pin_res = await db.execute(
            select(CertificatePin).where(
                CertificatePin.created_by == f"op:{operation_id[:90]}"
            )
        )
        cert_pin = pin_res.scalar_one_or_none()

    term_id = int(audit.subject_id)
    term_res = await db.execute(select(Terminal).where(Terminal.id == term_id))
    terminal = term_res.scalar_one_or_none()
    sn = terminal.sn if terminal else str(details.get("sn", ""))
    tenant_id = audit.tenant_id or (terminal.org_id if terminal else 0)

    now = datetime.now(UTC)
    pin_val: str | None = None
    masked_val = str(details.get("pin_masked", "***"))
    current_status: Literal["issued", "consumed", "expired"] = "expired"

    if cert_pin:
        masked_val = mask_pin(cert_pin.pin)
        if cert_pin.status == "pending":
            if cert_pin.expires_at <= now:
                cert_pin.status = "expired"
                await db.commit()
                current_status = "expired"
            else:
                current_status = "issued"
                pin_val = cert_pin.pin
        elif cert_pin.status == "used":
            current_status = "consumed"
        else:
            current_status = "expired"
        exp_time = cert_pin.expires_at
        created_time = cert_pin.created_at or now
    else:
        exp_str = details.get("expires_at")
        exp_time = datetime.fromisoformat(exp_str) if exp_str else now
        created_time = audit.occurred_at

    return IssueCertificatePinResponse(
        operation_id=operation_id,
        correlation_id=audit.correlation_id,
        tenant_id=tenant_id,
        terminal_id=term_id,
        sn=sn,
        pin=pin_val,
        pin_masked=masked_val,
        status=current_status,
        expires_at=exp_time,
        created_at=created_time,
        replayed=True,
    )


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
