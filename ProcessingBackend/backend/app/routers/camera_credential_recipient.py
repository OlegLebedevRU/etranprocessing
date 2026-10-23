"""Read-only, tenant-scoped credential recipient API. Service auth only."""

from fastapi import APIRouter, Depends, HTTPException, Query, Response
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.database import get_db
from app.dependencies import require_service_auth
from app.models import L4DeskTerminal, Org, OrgStatus, Terminal
from app.services.camera_credential_recipient import recipient_response

router = APIRouter(prefix="/api/certificates/terminals", tags=["certificates"])


@router.get("/{device_id}/credential-recipient")
async def get_credential_recipient(
    device_id: int,
    response: Response,
    org_id: int = Query(gt=0),
    db: AsyncSession = Depends(get_db),
    _auth: str = Depends(require_service_auth),
) -> dict[str, object]:
    if not (settings.service_auth_token or settings.internal_service_key):
        raise HTTPException(503, "credential_recipient_service_auth_not_configured")
    terminal = await db.scalar(
        select(Terminal).where(
            Terminal.device_id == device_id, Terminal.org_id == org_id
        )
    )
    if terminal is None:
        raise HTTPException(404, "credential_recipient_not_found")
    org = await db.scalar(select(Org).where(Org.org_id == org_id))
    status = await db.scalar(select(OrgStatus).where(OrgStatus.org_id == org_id))
    l4desk = await db.scalar(
        select(L4DeskTerminal).where(L4DeskTerminal.runtime_terminal_id == terminal.id)
    )
    if (
        not terminal.is_active
        or org is None
        or not org.is_active
        or status is not None
        and status.status == "blocked"
        or l4desk is not None
        and (
            l4desk.tenant_id != org_id
            or l4desk.deleted_at is not None
            or l4desk.user_disabled_at is not None
        )
    ):
        raise HTTPException(403, "credential_recipient_disabled")
    response.headers["Cache-Control"] = "no-store"
    return recipient_response(terminal)
