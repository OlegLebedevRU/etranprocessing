import base64
import logging
import secrets
import urllib.parse
from dataclasses import dataclass
from datetime import UTC, datetime

from cryptography import x509
from fastapi import Depends, HTTPException, Request
from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.database import get_db
from app.models import License, OrgStatus, Terminal, TerminalCertHistory
from app.services.cert_discovery import record_terminal_discovery

logger = logging.getLogger(__name__)

# Legacy menu clients (clsMenuCreator) authenticate with
# `Authorization: ClientCertificate <PEM|base64>` instead of TLS client cert.
# Only ListMenuFile accepts this fallback; payment/gategauge stay mTLS-only.
LEGACY_CLIENT_CERT_PATHS = frozenset({"/api/ListMenuFile"})


@dataclass(frozen=True, slots=True)
class TerminalLicenseState:
    license: License | None
    state: str  # "ok" | "error"


def parse_cert_subject(subject: str) -> dict[str, str]:
    """Parse X.509 subject string like 'CN=sn123,O=42' into dict."""
    result = {}
    for part in subject.split(","):
        part = part.strip()
        if "=" in part:
            key, value = part.split("=", 1)
            result[key.strip()] = value.strip()
    return result


def extract_cert_issuer(request: Request) -> str:
    """Extract Issuer DN from proxy headers or parsed client certificate."""
    for h in ("X-Client-Cert-Issuer-DN", "X-Client-Cert-Issuer", "X-SSL-Client-Issuer"):
        val = request.headers.get(h)
        if val:
            return val
    raw_cert = request.headers.get("X-SSL-Client-Cert")
    if raw_cert:
        try:
            cert_pem = urllib.parse.unquote(raw_cert)
            if "-----BEGIN CERTIFICATE-----" in cert_pem:
                cert = x509.load_pem_x509_certificate(cert_pem.encode("utf-8"))
                return cert.issuer.rfc4514_string()
        except Exception:  # noqa: BLE001, S110
            pass
    return ""


def extract_cert_not_valid_after(request: Request) -> datetime | None:
    """Extract certificate expiration date (not_valid_after) from client certificate or headers."""
    for h in (
        "X-Client-Cert-NotAfter",
        "X-Client-Cert-End",
        "X-SSL-Client-V-End",
        "X-Client-Cert-Valid-To",
    ):
        val = request.headers.get(h)
        if val:
            try:
                return datetime.fromisoformat(val)
            except Exception:  # noqa: BLE001, S110
                pass
    raw_cert = request.headers.get("X-SSL-Client-Cert")
    if raw_cert:
        try:
            cert_pem = urllib.parse.unquote(raw_cert)
            if "-----BEGIN CERTIFICATE-----" in cert_pem:
                cert = x509.load_pem_x509_certificate(cert_pem.encode("utf-8"))
                dt = getattr(cert, "not_valid_after_utc", None)
                if dt is None:
                    dt = cert.not_valid_after.replace(tzinfo=UTC)
                return dt
        except Exception:  # noqa: BLE001, S110
            pass
    return None


def _load_x509_from_payload(payload: str) -> x509.Certificate | None:
    """Load an X.509 cert from PEM text or base64(PEM|DER)."""
    text = urllib.parse.unquote(payload).strip()
    if not text:
        return None

    if "-----BEGIN CERTIFICATE-----" in text:
        try:
            return x509.load_pem_x509_certificate(text.encode("utf-8"))
        except ValueError:
            return None

    # Single-line PEM: scheme payload may collapse newlines to spaces.
    if "BEGIN CERTIFICATE" in text and "-----BEGIN CERTIFICATE-----" not in text:
        compact = text.replace(" ", "").replace("\t", "")
        rebuilt = compact.replace("-----BEGINCERTIFICATE-----", "")
        rebuilt = rebuilt.replace("-----ENDCERTIFICATE-----", "")
        pem = (
            "-----BEGIN CERTIFICATE-----\n"
            + "\n".join(rebuilt[i : i + 64] for i in range(0, len(rebuilt), 64))
            + "\n-----END CERTIFICATE-----\n"
        )
        try:
            return x509.load_pem_x509_certificate(pem.encode("utf-8"))
        except ValueError:
            return None

    token = text.split()[0]
    try:
        raw = base64.b64decode(token, validate=False)
    except ValueError, TypeError:
        return None
    if b"-----BEGIN CERTIFICATE-----" in raw:
        try:
            return x509.load_pem_x509_certificate(raw)
        except ValueError:
            return None
    try:
        return x509.load_der_x509_certificate(raw)
    except ValueError:
        return None


def parse_authorization_client_certificate(
    request: Request,
) -> x509.Certificate | None:
    """Parse legacy `Authorization: ClientCertificate <PEM|base64>` header.

    Used by older menu clients that send the certificate in the Authorization
    header instead of presenting it during the TLS handshake.
    """
    auth = request.headers.get("Authorization", "")
    if not auth:
        return None
    parts = auth.split(None, 1)
    if len(parts) != 2:
        return None
    scheme, payload = parts
    if scheme.lower() != "clientcertificate":
        return None
    return _load_x509_from_payload(payload)


def _cert_identity(cert: x509.Certificate) -> tuple[str, str, str, datetime]:
    """Return (subject, serial, issuer, not_after) in the header convention."""
    subject = cert.subject.rfc4514_string()
    serial = format(cert.serial_number, "X")
    issuer = cert.issuer.rfc4514_string()
    not_after = getattr(cert, "not_valid_after_utc", None)
    if not_after is None:
        not_after = cert.not_valid_after.replace(tzinfo=UTC)
    return subject, serial, issuer, not_after


async def get_current_terminal(
    request: Request,
    db: AsyncSession = Depends(get_db),
) -> Terminal:
    """Extract terminal identity from TLS / reverse proxy headers.

    Branching logic:
    1. If Issuer is 'iot.leo4.ru' (New CA issued by new backend):
       Strict validation by Terminal.sn == cn AND Terminal.cert_serial == cert_serial.
    2. Else (Legacy CA / SubCA / legacy certificates):
       Lookup terminal by OU (device_id) and O (org_id) (or L / kiosk_id).
       Auto-binds or updates cert_serial for that specific terminal on request.
    """
    endpoint = request.url.path
    client_ip = (
        request.headers.get("X-Real-IP")
        or request.headers.get("X-Forwarded-For")
        or (request.client.host if request.client else None)
    )

    subject = request.headers.get("X-Client-Cert-DN", "")
    if not subject:
        subject = request.headers.get("X-SSL-Client-Cert-Subject", "")

    cert_serial = request.headers.get("X-Client-Cert-Serial", "")

    auth_cert: x509.Certificate | None = None
    auth_issuer = ""
    auth_not_after: datetime | None = None
    if (not subject or not cert_serial) and endpoint in LEGACY_CLIENT_CERT_PATHS:
        auth_cert = parse_authorization_client_certificate(request)
        if auth_cert is not None:
            cert_subject, cert_serial_val, cert_issuer, cert_not_after = _cert_identity(
                auth_cert
            )
            if not subject:
                subject = cert_subject
            if not cert_serial:
                cert_serial = cert_serial_val
            auth_issuer = cert_issuer
            auth_not_after = cert_not_after

    if not subject or not cert_serial:
        logger.debug(f"Missing cert headers. DN='{subject}', Serial='{cert_serial}'")
        await record_terminal_discovery(
            db,
            sn=None,
            cert_serial=cert_serial or None,
            cert_dn=subject or None,
            is_valid=False,
            validation_status="missing_headers",
            endpoint=endpoint,
            client_ip=client_ip,
        )
        raise HTTPException(
            status_code=401, detail="Missing client certificate headers"
        )

    parsed = parse_cert_subject(subject)
    cn = parsed.get("CN", "")
    ou = parsed.get("OU", "")
    o = parsed.get("O", "")
    l_val = parsed.get("L", "")

    issuer = extract_cert_issuer(request) or auth_issuer
    is_new_ca = bool(issuer and "iot.leo4.ru" in issuer.lower())

    logger.debug(
        f"Cert Auth: Issuer='{issuer}', is_new_ca={is_new_ca}, CN='{cn}', OU='{ou}', O='{o}', L='{l_val}', Serial='{cert_serial}'"
    )

    if not cn and not ou and not l_val:
        await record_terminal_discovery(
            db,
            sn=None,
            cert_serial=cert_serial,
            cert_dn=subject,
            ou=ou or None,
            o=o or None,
            is_valid=False,
            validation_status="missing_cn",
            endpoint=endpoint,
            client_ip=client_ip,
        )
        raise HTTPException(
            status_code=401, detail="CN/identity not found in certificate subject"
        )

    if is_new_ca:
        # OpenSSL hex rendering can add a leading zero; compare the same numeric serial.
        if (
            not cert_serial
            or len(cert_serial) > 40
            or any(c not in "0123456789abcdefABCDEF" for c in cert_serial)
        ):
            raise HTTPException(401, "Invalid new CA certificate serial")
        serial_value = cert_serial.upper().lstrip("0") or "0"
        # Strict auth for new CA: sn (CN) + certificate serial, no identity fallback.
        result = await db.execute(
            select(Terminal).where(
                Terminal.sn == cn,
                func.ltrim(func.upper(Terminal.cert_serial), "0") == serial_value,
            )
        )
        terminal = result.scalar_one_or_none()

        if terminal:
            # Discovery uses the issuance representation; do not rewrite terminals.cert_serial here.
            cert_serial = terminal.cert_serial
            cert_not_valid_after = (
                extract_cert_not_valid_after(request) or auth_not_after
            )
            if (
                cert_not_valid_after
                and terminal.cert_not_valid_after != cert_not_valid_after
            ):
                terminal.cert_not_valid_after = cert_not_valid_after
            await record_terminal_discovery(
                db,
                sn=cn,
                cert_serial=cert_serial,
                cert_dn=subject,
                ou=ou or None,
                o=o or None,
                is_valid=True,
                validation_status="authenticated",
                terminal_id=terminal.id,
                db_cert_serial=terminal.cert_serial,
                endpoint=endpoint,
                client_ip=client_ip,
            )
            return terminal

        # Diagnosis for discovery log
        res_diag = await db.execute(select(Terminal).where(Terminal.sn == cn))
        db_term = res_diag.scalar_one_or_none()
        if db_term:
            val_status = "serial_mismatch"
            term_id = db_term.id
            db_serial = db_term.cert_serial
        else:
            val_status = "terminal_not_found"
            term_id = None
            db_serial = None

        await record_terminal_discovery(
            db,
            sn=cn,
            cert_serial=cert_serial,
            cert_dn=subject,
            ou=ou or None,
            o=o or None,
            is_valid=False,
            validation_status=val_status,
            terminal_id=term_id,
            db_cert_serial=db_serial,
            endpoint=endpoint,
            client_ip=client_ip,
        )
        raise HTTPException(
            status_code=401,
            detail=f"Terminal auth failed ({val_status}). CN='{cn}', Serial='{cert_serial}'",
        )

    # Legacy CA flow: match by OU (device_id) and O (org_id)
    matched_terminal = None
    if ou and ou.isdigit():
        dev_id = int(ou)
        conds = [Terminal.device_id == dev_id]
        if o and o.isdigit():
            conds.append(Terminal.org_id == int(o))
        res = await db.execute(select(Terminal).where(*conds))
        matched_terminal = res.scalar_one_or_none()

    if not matched_terminal and l_val and l_val.isdigit():
        kiosk_id = int(l_val)
        conds = [Terminal.id == kiosk_id]
        if o and o.isdigit():
            conds.append(Terminal.org_id == int(o))
        res = await db.execute(select(Terminal).where(*conds))
        matched_terminal = res.scalar_one_or_none()

    if not matched_terminal and cn:
        # Fallback if certificate had only CN and no OU/L
        res = await db.execute(select(Terminal).where(Terminal.sn == cn))
        matched_terminal = res.scalar_one_or_none()

    if matched_terminal:
        terminal = matched_terminal
        cert_not_valid_after = extract_cert_not_valid_after(request) or auth_not_after
        if (
            cert_not_valid_after
            and terminal.cert_not_valid_after != cert_not_valid_after
        ):
            terminal.cert_not_valid_after = cert_not_valid_after

        # Only auto-bind / update serial if terminal has no serial or current serial is also a legacy cert (<=20 hex chars).
        # Do not overwrite a new CA 40-char serial with a legacy cert serial.
        should_update_serial = not terminal.cert_serial or (
            terminal.cert_serial != cert_serial and len(terminal.cert_serial) <= 20
        )
        if should_update_serial:
            val_status = "auto_bound" if not terminal.cert_serial else "serial_updated"
            terminal.cert_serial = cert_serial
            db.add(
                TerminalCertHistory(
                    terminal_id=terminal.id,
                    cert_serial=cert_serial,
                    source="legacy_auth",
                )
            )
        else:
            val_status = "authenticated"

        await record_terminal_discovery(
            db,
            sn=cn or terminal.sn,
            cert_serial=cert_serial,
            cert_dn=subject,
            ou=ou or str(terminal.device_id),
            o=o or str(terminal.org_id),
            is_valid=True,
            validation_status=val_status,
            terminal_id=terminal.id,
            db_cert_serial=terminal.cert_serial,
            endpoint=endpoint,
            client_ip=client_ip,
        )
        try:
            await db.commit()
        except Exception as e:  # noqa: BLE001
            try:
                await db.rollback()
            except Exception:  # noqa: BLE001, S110
                pass
            logger.warning(
                f"Failed to commit discovery for legacy terminal {terminal.id}: {e}"
            )

        return terminal

    await record_terminal_discovery(
        db,
        sn=cn or None,
        cert_serial=cert_serial,
        cert_dn=subject,
        ou=ou or None,
        o=o or None,
        is_valid=False,
        validation_status="terminal_not_found",
        terminal_id=None,
        db_cert_serial=None,
        endpoint=endpoint,
        client_ip=client_ip,
    )
    raise HTTPException(
        status_code=401,
        detail=f"Legacy terminal not found for OU='{ou}', O='{o}', L='{l_val}'",
    )

    # Do NOT check is_active here — let get_terminal_license_state handle it
    # so the licensebilling endpoint can return proper XML state=error
    # instead of a JSON HTTPException.

    # OU is informational — log mismatch but don't block
    if ou:
        try:
            ou_device_id = int(ou)
            if terminal.device_id != ou_device_id:
                logger.debug(
                    f"OU mismatch: cert OU={ou_device_id}, db device_id={terminal.device_id} "
                    f"(terminal sn={cn})"
                )
        except ValueError:
            pass

    return terminal


async def check_license(
    terminal: Terminal,
    db: AsyncSession,
) -> License:
    """Check that terminal has a non-expired license and is active."""
    if not terminal.is_active:
        raise HTTPException(status_code=403, detail="Terminal is disabled")

    result = await db.execute(select(License).where(License.terminal_id == terminal.id))
    license_ = result.scalar_one_or_none()
    if not license_:
        raise HTTPException(status_code=403, detail="No license")

    if license_.expires_at < datetime.now(UTC):
        raise HTTPException(status_code=403, detail="License expired")

    return license_


async def check_org_status(
    terminal: Terminal,
    db: AsyncSession,
) -> OrgStatus:
    """Check that org is not blocked."""
    result = await db.execute(
        select(OrgStatus).where(OrgStatus.org_id == terminal.org_id)
    )
    org_status = result.scalar_one_or_none()
    if not org_status:
        return OrgStatus(org_id=terminal.org_id, status="active")

    if org_status.status == "blocked":
        raise HTTPException(status_code=403, detail="Organization is blocked")

    return org_status


async def get_terminal_license_state(
    terminal: Terminal = Depends(get_current_terminal),
    db: AsyncSession = Depends(get_db),
) -> TerminalLicenseState:
    """
    Compute the terminal license state for the terminal-facing API.
    Returns TerminalLicenseState with license (if found) and computed state.
    Classic only: no L4Desk subscription facts or server-side expiry grace.
    Exact expiry remains valid here; the terminal owns its three-day grace.
    Only the existing "blocked" org status denies; "inactive" is unchanged.
    License denies are returned as state=error, not HTTPException.
    """
    # Check org status
    org_res = await db.execute(
        select(OrgStatus).where(OrgStatus.org_id == terminal.org_id)
    )
    org_status = org_res.scalar_one_or_none()

    result = await db.execute(select(License).where(License.terminal_id == terminal.id))
    license_ = result.scalar_one_or_none()

    if not terminal.is_active or (org_status and org_status.status == "blocked"):
        return TerminalLicenseState(license=license_, state="error")

    if not license_ or license_.expires_at < datetime.now(UTC):
        return TerminalLicenseState(license=license_, state="error")

    return TerminalLicenseState(license=license_, state="ok")


async def require_service_auth(request: Request) -> str:
    """Verify service-to-service authentication token.

    Accepts:
      - Authorization: Bearer <token>
      - X-Service-Token: <token>
      - X-Internal-Service-Key: <token>

    If settings.service_auth_token or settings.internal_service_key is configured,
    the incoming token must match using constant-time comparison.
    If no service token is configured in environment, requests are permitted (for local dev/testing).
    """
    configured_token = settings.service_auth_token or settings.internal_service_key
    auth_header = request.headers.get("Authorization", "")
    token = ""
    if auth_header.startswith("Bearer "):
        token = auth_header[7:].strip()
    elif "X-Service-Token" in request.headers:
        token = request.headers["X-Service-Token"].strip()
    elif "X-Internal-Service-Key" in request.headers:
        token = request.headers["X-Internal-Service-Key"].strip()

    if configured_token and (
        not token or not secrets.compare_digest(token, configured_token)
    ):
        logger.warning(
            "Service auth failed for path=%s client_ip=%s",
            request.url.path,
            request.client.host if request.client else "unknown",
        )
        raise HTTPException(
            status_code=401,
            detail={
                "error": "unauthorized",
                "message": "Invalid or missing service authentication token",
                "error_code": "SERVICE_AUTH_FAILED",
            },
        )
    return token
