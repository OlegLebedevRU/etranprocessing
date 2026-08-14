import logging
import re
from datetime import datetime, timezone

from fastapi import Depends, HTTPException, Request
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.models import License, OrgStatus, Terminal

logger = logging.getLogger(__name__)


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
    Nginx-mutual forwards:
    - X-Client-Cert-DN: 'CN=serial_number,O=org_id'
    - X-Client-Cert-Serial: hex serial number (e.g. '607E7D40000400002BE9')
    - X-SSL-Client-Verify: SUCCESS/FAILED/NONE
    """

    # Try DN-based lookup first (CN = terminal SN)
    subject = request.headers.get("X-Client-Cert-DN", "")
    if not subject:
        subject = request.headers.get("X-SSL-Client-Cert-Subject", "")

    cert_serial = request.headers.get("X-Client-Cert-Serial", "")
    logger.info(f"Cert DN: '{subject}', Serial: '{cert_serial}'")

    # Strategy 1: Look up by OU (terminal number) from DN
    if subject:
        parsed = parse_cert_subject(subject)
        ou = parsed.get("OU", "")
        if ou:
            try:
                device_id = int(ou)
                result = await db.execute(select(Terminal).where(Terminal.device_id == device_id))
                terminal = result.scalar_one_or_none()
                if terminal and terminal.is_active:
                    return terminal
            except ValueError:
                pass

    # Strategy 2: Look up by CN from DN
    if subject:
        parsed = parse_cert_subject(subject)
        cn = parsed.get("CN", "")
        if cn:
            result = await db.execute(select(Terminal).where(Terminal.sn == cn))
            terminal = result.scalar_one_or_none()
            if terminal and terminal.is_active:
                return terminal

    # Strategy 3: Look up by cert serial
    if cert_serial:
        result = await db.execute(select(Terminal).where(Terminal.cert_serial == cert_serial))
        terminal = result.scalar_one_or_none()
        if terminal and terminal.is_active:
            return terminal

    # Not found
    logger.warning(f"Terminal not found. DN='{subject}', Serial='{cert_serial}', headers={dict(request.headers)}")
    raise HTTPException(status_code=401, detail=f"Terminal not found. DN='{subject}', Serial='{cert_serial}'")


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

    if license_.expires_at < datetime.now(timezone.utc):
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
