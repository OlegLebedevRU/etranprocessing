"""Superuser-only device incident inbox; reading never acknowledges an event."""

from datetime import UTC, datetime
from typing import Literal

from etranprocessing_db.models.device_events import DeviceAdminEvent
from fastapi import APIRouter, Depends, HTTPException, Query
from pydantic import BaseModel, Field
from sqlalchemy import func, or_, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.models import Terminal
from app.routers.admin_identity_changes import (
    require_identity_superuser as require_superuser,
)

router = APIRouter(
    prefix="/api/admin/device-events",
    tags=["admin-device-events"],
    dependencies=[Depends(require_superuser)],
)


class EventUpdate(BaseModel):
    status: Literal["new", "in_progress", "closed"]
    comment: str = Field(default="", max_length=4000)


@router.get("/count")
async def count_events(db: AsyncSession = Depends(get_db)):
    count = await db.scalar(
        select(func.count())
        .select_from(DeviceAdminEvent)
        .where(DeviceAdminEvent.status == "new")
    )
    return {"new_count": count or 0}


@router.get("")
async def list_events(
    state: Literal["new", "in_progress", "closed"] | None = None,
    event_type: str | None = None,
    org_id: int | None = None,
    terminal_id: int | None = None,
    page: int = Query(1, ge=1),
    page_size: int = Query(50, ge=1, le=100),
    db: AsyncSession = Depends(get_db),
):
    filters = []
    if state:
        filters.append(DeviceAdminEvent.status == state)
    if event_type:
        filters.append(DeviceAdminEvent.event_type == event_type)
    if terminal_id is not None:
        filters.append(DeviceAdminEvent.terminal_id == terminal_id)
    if org_id is not None:
        filters.append(
            or_(
                DeviceAdminEvent.details["source_org_id"].as_integer() == org_id,
                DeviceAdminEvent.details["target_org_id"].as_integer() == org_id,
            )
        )
    base = (
        select(DeviceAdminEvent, Terminal.sn, Terminal.device_id, Terminal.org_id)
        .join(Terminal, Terminal.id == DeviceAdminEvent.terminal_id)
        .where(*filters)
    )
    total = await db.scalar(select(func.count()).select_from(base.subquery()))
    rows = (
        await db.execute(
            base.order_by(
                DeviceAdminEvent.created_at.desc(), DeviceAdminEvent.id.desc()
            )
            .offset((page - 1) * page_size)
            .limit(page_size)
        )
    ).all()
    return {
        "total": total or 0,
        "items": [
            {
                "id": event.id,
                "terminal_id": event.terminal_id,
                "operation_id": str(event.operation_id),
                "event_type": event.event_type,
                "status": event.status,
                "details": event.details,
                "created_at": event.created_at,
                "updated_at": event.updated_at,
                "updated_by": event.updated_by,
                "comment": event.comment,
                "sn": sn,
                "device_id": device_id,
                "org_id": org_id,
            }
            for event, sn, device_id, org_id in rows
        ],
    }


@router.patch("/{event_id}")
async def update_event(
    event_id: int,
    body: EventUpdate,
    user: dict = Depends(require_superuser),
    db: AsyncSession = Depends(get_db),
):
    event = await db.get(DeviceAdminEvent, event_id, with_for_update=True)
    if event is None:
        raise HTTPException(404, "Событие не найдено")
    if (event.status == "new") != (body.status == "new"):
        await db.execute(select(func.pg_notify("portal_views_v1", "admin-events")))
    event.status = body.status
    event.comment = body.comment.strip()
    event.updated_by = str(user.get("username") or user.get("id") or "superuser")[:100]
    event.updated_at = datetime.now(UTC)
    await db.commit()
    return {"id": event.id, "status": event.status}
