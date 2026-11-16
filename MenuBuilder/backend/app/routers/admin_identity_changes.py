"""Superuser-owned intent; PB alone issues and switches certificate identity."""

from datetime import UTC, datetime
from uuid import UUID

import httpx
from etranprocessing_db.models.device_events import TerminalIdentityChange
from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, Field
from sqlalchemy import select, update
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import require_superuser
from app.config import settings
from app.database import get_db
from app.models import CertificatePin, Org, Terminal
from app.services.product_scope import validate_terminal_transfer


async def require_identity_superuser(user: dict = Depends(require_superuser)) -> dict:
    if not user.get("is_superuser"):
        raise HTTPException(403, "Только суперпользователь")
    return user


router = APIRouter(
    prefix="/api/admin/terminals",
    tags=["admin-identity"],
    dependencies=[Depends(require_identity_superuser)],
)


class StartIdentityChange(BaseModel):
    target_org_id: int = Field(gt=0)
    operation_id: UUID


def read_change(change: TerminalIdentityChange | None) -> dict | None:
    if change is None:
        return None
    return {
        "operation_id": str(change.operation_id),
        "source_org_id": change.source_org_id,
        "target_org_id": change.target_org_id,
        "status": change.status,
        "requested_by": change.requested_by,
        "created_at": change.created_at,
        "issued_at": change.issued_at,
        "completed_at": change.completed_at,
    }


async def locked_terminal(db: AsyncSession, terminal_id: int) -> Terminal:
    terminal = await db.scalar(
        select(Terminal)
        .where(Terminal.id == terminal_id)
        .with_for_update(of=Terminal)
        .execution_options(populate_existing=True)
    )
    if terminal is None:
        raise HTTPException(404, "Терминал не найден")
    return terminal


async def cancel_setup_pins(db: AsyncSession, terminal_id: int) -> None:
    await db.execute(
        update(CertificatePin)
        .where(
            CertificatePin.terminal_id == terminal_id,
            CertificatePin.purpose == "setup",
            CertificatePin.status == "pending",
        )
        .values(status="cancelled")
    )


@router.get("/{terminal_id}/identity-change")
async def get_identity_change(
    terminal_id: int,
    operation_id: UUID | None = None,
    db: AsyncSession = Depends(get_db),
):
    if await db.get(Terminal, terminal_id) is None:
        raise HTTPException(404, "Терминал не найден")
    query = select(TerminalIdentityChange).where(
        TerminalIdentityChange.terminal_id == terminal_id
    )
    if operation_id:
        query = query.where(TerminalIdentityChange.operation_id == operation_id)
    return read_change(
        await db.scalar(
            query.order_by(TerminalIdentityChange.created_at.desc()).limit(1)
        )
    )


@router.post("/{terminal_id}/identity-change")
async def start_identity_change(
    terminal_id: int,
    body: StartIdentityChange,
    user: dict = Depends(require_identity_superuser),
    db: AsyncSession = Depends(get_db),
):
    terminal = await locked_terminal(db, terminal_id)
    existing = await db.get(
        TerminalIdentityChange, body.operation_id, populate_existing=True
    )
    if existing and existing.operation_id == body.operation_id:
        if (
            existing.terminal_id != terminal_id
            or existing.target_org_id != body.target_org_id
        ):
            raise HTTPException(409, "Операция уже связана с другим тенантом")
        return read_change(existing)
    active = await db.scalar(
        select(TerminalIdentityChange).where(
            TerminalIdentityChange.terminal_id == terminal_id,
            TerminalIdentityChange.status.in_(["pending", "sync_pending"]),
        )
    )
    if active:
        raise HTTPException(
            409, "У терминала уже есть незавершённая смена идентичности"
        )
    if terminal.org_id == body.target_org_id:
        raise HTTPException(409, "Выбран текущий тенант")
    target = await db.get(Org, body.target_org_id)
    source = await db.get(Org, terminal.org_id)
    if target is None or source is None:
        raise HTTPException(404, "Тенант не найден")
    if not target.is_active:
        raise HTTPException(409, "Целевой тенант отключён")
    if (
        source.site_mode != "classic"
        or target.site_mode != "classic"
        or terminal.l4desk_subscription_enabled
    ):
        raise HTTPException(409, "Смена идентичности доступна только Classic → Classic")
    await validate_terminal_transfer(db, terminal, target.org_id)
    await cancel_setup_pins(db, terminal_id)
    change = TerminalIdentityChange(
        terminal_id=terminal_id,
        operation_id=body.operation_id,
        source_org_id=terminal.org_id,
        target_org_id=target.org_id,
        old_serial=(terminal.cert_serial.upper().lstrip("0") or "0")
        if terminal.cert_serial
        else None,
        status="pending",
        requested_by=str(user.get("username") or "superuser")[:100],
        created_at=datetime.now(UTC),
    )
    db.add(change)
    await db.commit()
    return read_change(change)


@router.delete("/{terminal_id}/identity-change/{operation_id}")
async def cancel_identity_change(
    terminal_id: int, operation_id: UUID, db: AsyncSession = Depends(get_db)
):
    await locked_terminal(db, terminal_id)
    change = await db.get(TerminalIdentityChange, operation_id, populate_existing=True)
    if change is None or change.terminal_id != terminal_id:
        raise HTTPException(409, "Операция уже изменилась")
    if change.status == "cancelled":
        return read_change(change)
    if change.status != "pending":
        raise HTTPException(
            409,
            "После выдачи сертификата отмена невозможна; используйте повтор синхронизации",
        )
    await cancel_setup_pins(db, terminal_id)
    change.status = "cancelled"
    await db.commit()
    return read_change(change)


@router.post("/{terminal_id}/identity-change/{operation_id}/retry")
async def retry_identity_change(
    terminal_id: int, operation_id: UUID, db: AsyncSession = Depends(get_db)
):
    change = await db.get(TerminalIdentityChange, operation_id)
    if change is None or change.terminal_id != terminal_id:
        raise HTTPException(409, "Операция уже изменилась")
    if change.status not in ("sync_pending", "completed"):
        raise HTTPException(409, "Сертификат ещё не выдан")
    if not settings.processing_backend_effective_token:
        raise HTTPException(503, "Сервис синхронизации не настроен")
    try:
        async with httpx.AsyncClient(timeout=15) as client:
            response = await client.post(
                settings.processing_backend_effective_url.rstrip("/")
                + f"/api/certificates/identity-changes/{terminal_id}/retry",
                headers={
                    "X-Internal-Service-Key": settings.processing_backend_effective_token
                },
                params={"operation_id": str(operation_id)},
            )
            response.raise_for_status()
            payload = response.json()
            completed = isinstance(payload, dict) and payload.get("completed") is True
    except (httpx.HTTPError, ValueError) as exc:
        raise HTTPException(503, "Не удалось повторить синхронизацию с IoT") from exc
    return {"completed": completed}
