import logging
from dataclasses import dataclass
from datetime import UTC, datetime

from fastapi import Depends, HTTPException, Request
from sqlalchemy import or_, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
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


async def get_current_terminal(
    request: Request,
    db: AsyncSession = Depends(get_db),
) -> Terminal:
    """
    Extract terminal identity from nginx TLS headers.
    Authentication by sn (CN from cert) + cert_serial — both must match.
    OU is optional for quick device_id lookup but not used for auth.

    Nginx-mutual forwards:
    - X-Client-Cert-DN: 'emailAddress=...,CN=A99D2F...,OU=773,O=1,L=...,ST=...,C=ru'
    - X-Client-Cert-Serial: hex serial without colons (e.g. '52B8E528000400002E2D')
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

    logger.debug(
        f"Cert DN: '{subject}', CN: '{cn}', OU: '{ou}', O: '{o}', L: '{l_val}', Serial: '{cert_serial}'"
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
            status_code=401, detail="CN not found in certificate subject"
        )

    # 1. Primary auth: sn (CN) + cert_serial — both match
    result = await db.execute(
        select(Terminal).where(
            Terminal.sn == cn,
            Terminal.cert_serial == cert_serial,
        )
    )
    terminal = result.scalar_one_or_none()

    if terminal:
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
    else:
        is_allowed_autobind = (
            endpoint.startswith(
                (
                    "/api/licensebilling",
                    "/licensebilling",
                    "/api/gategauge",
                    "/GateGauge",
                    "/api/techgate",
                    "/techgate",
                )
            )
            or settings.auto_set_cert_serial_on_licensebilling
        )
        if is_allowed_autobind:
            conditions = []
            if cn:
                conditions.append(Terminal.sn == cn)
            if l_val and l_val.isdigit():
                if o and o.isdigit():
                    conditions.append(
                        (Terminal.id == int(l_val)) & (Terminal.org_id == int(o))
                    )
                else:
                    conditions.append(Terminal.id == int(l_val))
            if ou and ou.isdigit() and o and o.isdigit():
                conditions.append(
                    (Terminal.device_id == int(ou)) & (Terminal.org_id == int(o))
                )

            if conditions:
                res = await db.execute(
                    select(Terminal).where(
                        or_(*conditions),
                        (Terminal.cert_serial.is_(None)) | (Terminal.cert_serial == ""),
                    )
                )
                matched_terminal = res.scalar_one_or_none()
            else:
                matched_terminal = None

            if matched_terminal:
                terminal = matched_terminal
                if endpoint.startswith(("/api/licensebilling", "/licensebilling")):
                    source = "licensebilling"
                elif endpoint.startswith(("/api/gategauge", "/GateGauge")):
                    source = "gategauge"
                elif endpoint.startswith(("/api/techgate", "/techgate")):
                    source = "techgate"
                else:
                    source = "auto_bind"

                logger.info(
                    f"Auto-populating cert_serial for terminal {terminal.sn} (id={terminal.id}, dev={terminal.device_id}) with '{cert_serial}' from {source} request"
                )
                terminal.cert_serial = cert_serial
                if cn and (not terminal.sn or terminal.sn.isdigit()):
                    terminal.sn = cn

                db.add(
                    TerminalCertHistory(
                        terminal_id=terminal.id,
                        cert_serial=cert_serial,
                        source=source,
                    )
                )
                await record_terminal_discovery(
                    db,
                    sn=cn,
                    cert_serial=cert_serial,
                    cert_dn=subject,
                    ou=ou or None,
                    o=o or None,
                    is_valid=True,
                    validation_status="auto_bound",
                    terminal_id=terminal.id,
                    db_cert_serial=cert_serial,
                    endpoint=endpoint,
                    client_ip=client_ip,
                )
                await db.commit()

    if not terminal:
        # Check if terminal exists with different serial for diagnostics
        res = await db.execute(select(Terminal).where(Terminal.sn == cn))
        db_term = res.scalar_one_or_none()
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

        logger.debug(
            f"Terminal validation failed ({val_status}). CN='{cn}', Serial='{cert_serial}', DB_Serial='{db_serial}'"
        )
        raise HTTPException(
            status_code=401,
            detail=f"Terminal not found. CN='{cn}', Serial='{cert_serial}'",
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


@dataclass(frozen=True, slots=True)
class JwtUser:
    username: str
    org_id: int


async def get_current_user_jwt(
    request: Request,
) -> JwtUser:
    """Extract user identity from headers set by nginx after JWT validation.

    SECURITY: nginx validates the JWT and extracts claims using
    auth_jwt_extract_var_claims + explicit proxy_set_header. This prevents
    client header spoofing because nginx removes all client-supplied headers
    before setting proxy_set_header directives.

    The backend trusts these headers as the authoritative source of identity
    because nginx has already verified the JWT signature.
    """
    username = request.headers.get("jwt-sub", "")
    if not username:
        raise HTTPException(status_code=401, detail="Missing jwt-sub header")

    org_id_str = request.headers.get("jwt-org", "")
    if not org_id_str:
        raise HTTPException(status_code=401, detail="Missing jwt-org header")

    try:
        org_id = int(org_id_str)
    except ValueError:
        raise HTTPException(status_code=401, detail="Invalid jwt-org header")

    return JwtUser(username=username, org_id=org_id)
