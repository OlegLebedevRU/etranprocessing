from __future__ import annotations

import logging
from datetime import UTC, datetime
from typing import Any

from fastapi import APIRouter, Depends, HTTPException, status
from pydantic import BaseModel, Field
from sqlalchemy import desc, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import get_current_user
from app.database import get_db
from app.models_l4desk import L4DeskAuditEvent

logger = logging.getLogger(__name__)

router = APIRouter(tags=["mcp"])


class McpWaitlistRequest(BaseModel):
    use_case: str = Field(..., min_length=2, max_length=128)
    note: str | None = Field(None, max_length=500)
    contact_email: str | None = Field(None, max_length=128)


class McpWaitlistResponse(BaseModel):
    status: str
    tenant_id: int
    use_case: str
    contact_email: str | None = None
    registered_at: str


class McpWaitlistStatusResponse(BaseModel):
    registered: bool
    tenant_id: int | None = None
    use_case: str | None = None
    registered_at: str | None = None
    note: str | None = None


@router.post(
    "/api/v1/mcp/waitlist",
    response_model=McpWaitlistResponse,
    status_code=status.HTTP_201_CREATED,
)
@router.post(
    "/api/mcp/waitlist",
    response_model=McpWaitlistResponse,
    status_code=status.HTTP_201_CREATED,
)
async def join_mcp_waitlist(
    body: McpWaitlistRequest,
    user: dict[str, Any] = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
) -> McpWaitlistResponse:
    """Record tenant interest and register for MCP early access waitlist."""
    org_id = user.get("org_id")
    if org_id is None or int(org_id) <= 0:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Active organization context required",
        )
    tenant_id = int(org_id)
    actor = str(
        user.get("sub") or user.get("username") or f"user-{user.get('user_id')}"
    )
    contact_email = body.contact_email or user.get("email") or ""

    audit_entry = L4DeskAuditEvent(
        tenant_id=tenant_id,
        actor=actor,
        event_type="mcp_waitlist_signup",
        subject_type="tenant",
        subject_id=str(tenant_id),
        operation_id=f"mcp-waitlist-{tenant_id}-{int(datetime.now(UTC).timestamp())}",
        correlation_id=f"corr-mcp-{tenant_id}",
        outcome="success",
        details={
            "use_case": body.use_case,
            "note": body.note,
            "contact_email": contact_email,
            "source": "l4desk_ui",
        },
    )
    db.add(audit_entry)
    await db.commit()

    logger.info(
        "Tenant %d registered for MCP waitlist (actor: %s, use_case: %s)",
        tenant_id,
        actor,
        body.use_case,
    )

    return McpWaitlistResponse(
        status="registered",
        tenant_id=tenant_id,
        use_case=body.use_case,
        contact_email=contact_email or None,
        registered_at=datetime.now(UTC).isoformat(),
    )


@router.get(
    "/api/v1/mcp/waitlist/status",
    response_model=McpWaitlistStatusResponse,
    status_code=status.HTTP_200_OK,
)
@router.get(
    "/api/mcp/waitlist/status",
    response_model=McpWaitlistStatusResponse,
    status_code=status.HTTP_200_OK,
)
async def get_mcp_waitlist_status(
    user: dict[str, Any] = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
) -> McpWaitlistStatusResponse:
    """Check whether the current tenant is already registered on the MCP waitlist."""
    org_id = user.get("org_id")
    if org_id is None or int(org_id) <= 0:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Active organization context required",
        )
    tenant_id = int(org_id)

    stmt = (
        select(L4DeskAuditEvent)
        .where(
            L4DeskAuditEvent.tenant_id == tenant_id,
            L4DeskAuditEvent.event_type == "mcp_waitlist_signup",
        )
        .order_by(desc(L4DeskAuditEvent.id))
        .limit(1)
    )
    result = await db.execute(stmt)
    entry = result.scalar_one_or_none()

    if entry is None:
        return McpWaitlistStatusResponse(registered=False, tenant_id=tenant_id)

    details = entry.details or {}
    return McpWaitlistStatusResponse(
        registered=True,
        tenant_id=tenant_id,
        use_case=details.get("use_case"),
        note=details.get("note"),
        registered_at=(entry.occurred_at.isoformat() if entry.occurred_at else None),
    )
