import logging
from dataclasses import dataclass
from datetime import UTC, datetime

from fastapi import Depends, HTTPException, Request
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.database import get_db
from app.models import License, OrgStatus, Terminal, TerminalCertHistory

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

    subject = request.headers.get("X-Client-Cert-DN", "")
    if not subject:
        subject = request.headers.get("X-SSL-Client-Cert-Subject", "")

    cert_serial = request.headers.get("X-Client-Cert-Serial", "")

    if not subject or not cert_serial:
        logger.warning(f"Missing cert headers. DN='{subject}', Serial='{cert_serial}'")
        raise HTTPException(
            status_code=401, detail="Missing client certificate headers"
        )

    parsed = parse_cert_subject(subject)
    cn = parsed.get("CN", "")
    ou = parsed.get("OU", "")

    logger.info(
        f"Cert DN: '{subject}', CN: '{cn}', OU: '{ou}', Serial: '{cert_serial}'"
    )

    if not cn:
        raise HTTPException(
            status_code=401, detail="CN not found in certificate subject"
        )

    # Primary auth: sn (CN) + cert_serial — both must match
    result = await db.execute(
        select(Terminal).where(
            Terminal.sn == cn,
            Terminal.cert_serial == cert_serial,
        )
    )
    terminal = result.scalar_one_or_none()

    if not terminal:
        is_licensebilling = request.url.path.startswith(
            "/api/licensebilling"
        ) or request.url.path.startswith("/licensebilling")
        if is_licensebilling and settings.auto_set_cert_serial_on_licensebilling:
            result = await db.execute(
                select(Terminal).where(
                    Terminal.sn == cn,
                    (Terminal.cert_serial.is_(None)) | (Terminal.cert_serial == ""),
                )
            )
            terminal = result.scalar_one_or_none()
            if terminal:
                logger.info(
                    f"Auto-populating cert_serial for terminal {terminal.sn} (id={terminal.id}) with '{cert_serial}' from licensebilling request"
                )
                terminal.cert_serial = cert_serial
                db.add(
                    TerminalCertHistory(
                        terminal_id=terminal.id,
                        cert_serial=cert_serial,
                        source="licensebilling",
                    )
                )
                await db.commit()

    if not terminal:
        logger.warning(
            f"Terminal not found by sn+serial. CN='{cn}', Serial='{cert_serial}'"
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
                logger.warning(
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
