import logging
import urllib.parse
from dataclasses import dataclass
from datetime import UTC, datetime

from cryptography import x509
from fastapi import Depends, HTTPException, Request
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.models import License, OrgStatus, Terminal, TerminalCertHistory
from app.services.cert_discovery import record_terminal_discovery

logger = logging.getLogger(__name__)


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

    issuer = extract_cert_issuer(request)
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
        # Strict auth for new CA: sn (CN) + cert_serial
        result = await db.execute(
            select(Terminal).where(
                Terminal.sn == cn,
                Terminal.cert_serial == cert_serial,
            )
        )
        terminal = result.scalar_one_or_none()

        if terminal:
            cert_not_valid_after = extract_cert_not_valid_after(request)
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
        cert_not_valid_after = extract_cert_not_valid_after(request)
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
    """Check that terminal has an active, non-expired license."""
    result = await db.execute(
        select(License).where(
            License.terminal_id == terminal.id,
            License.is_active == True,
        )
    )
    license_ = result.scalar_one_or_none()
    if not license_:
        raise HTTPException(status_code=403, detail="No active license")

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
    Never raises HTTPException — state is always computed, not thrown.
    """
    if not terminal.is_active:
        result = await db.execute(
            select(License).where(
                License.terminal_id == terminal.id,
                License.is_active == True,
            )
        )
        license_ = result.scalar_one_or_none()
        return TerminalLicenseState(license=license_, state="error")

    # Check org status
    result = await db.execute(
        select(OrgStatus).where(OrgStatus.org_id == terminal.org_id)
    )
    org_status = result.scalar_one_or_none()
    if org_status and org_status.status == "blocked":
        result = await db.execute(
            select(License).where(
                License.terminal_id == terminal.id,
                License.is_active == True,
            )
        )
        license_ = result.scalar_one_or_none()
        return TerminalLicenseState(license=license_, state="error")

    # Find active license
    result = await db.execute(
        select(License).where(
            License.terminal_id == terminal.id,
            License.is_active == True,
        )
    )
    license_ = result.scalar_one_or_none()

    if not license_:
        return TerminalLicenseState(license=None, state="error")

    # Check expiry — renewal_enabled=false alone does NOT cause state=error
    if license_.expires_at < datetime.now(UTC):
        return TerminalLicenseState(license=license_, state="error")

    return TerminalLicenseState(license=license_, state="ok")
