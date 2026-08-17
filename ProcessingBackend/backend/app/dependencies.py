import logging
from datetime import UTC, datetime

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
        logger.warning(
            f"Terminal not found by sn+serial. CN='{cn}', Serial='{cert_serial}'"
        )
        raise HTTPException(
            status_code=401,
            detail=f"Terminal not found. CN='{cn}', Serial='{cert_serial}'",
        )

    if not terminal.is_active:
        raise HTTPException(status_code=403, detail="Terminal is deactivated")

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
