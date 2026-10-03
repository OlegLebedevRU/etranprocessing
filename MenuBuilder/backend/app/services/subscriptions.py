"""Terminal subscriptions. Session durations never affect entitlement or price."""

import calendar
from datetime import UTC, datetime, timedelta
from typing import Literal

from fastapi import HTTPException
from pydantic import BaseModel
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.models import Org, Terminal
from app.models_l4desk import L4DeskAuditEvent, L4DeskTenantProfile, L4DeskTerminal
from app.services.resource_time import resolve_timezone


class TerminalSubscription(BaseModel):
    terminal_id: int
    device_id: int | None = None
    name: str
    state: Literal[
        "free",
        "active",
        "grace",
        "unpaid",
        "expired",
        "payments_disabled",
        "admin_disabled",
        "deleted",
    ]
    allowed: bool
    paid_until: datetime | None = None
    grace_until: datetime | None = None
    reason: str
    action: str | None = None
    is_free: bool = False


def add_months(at: datetime, months: int, timezone: str) -> datetime:
    if months not in (1, 3, 6, 12):
        raise ValueError("Choose 1, 3, 6 or 12 months")
    local = at.astimezone(resolve_timezone(timezone))
    year, month = divmod(local.year * 12 + local.month - 1 + months, 12)
    return local.replace(
        year=year,
        month=month + 1,
        day=min(local.day, calendar.monthrange(year, month + 1)[1]),
    ).astimezone(UTC)


def subscription_status(
    terminal: L4DeskTerminal,
    runtime: Terminal,
    *,
    free_id: int | None,
    timezone: str,
    enabled: bool,
    now: datetime | None = None,
    terms: dict | None = None,
) -> TerminalSubscription:
    at = now or datetime.now(UTC)
    paid = terminal.paid_until
    if paid is not None and paid.tzinfo is None:
        paid = paid.replace(tzinfo=UTC)
    grace = (
        (paid.astimezone(resolve_timezone(timezone)) + timedelta(days=3)).astimezone(
            UTC
        )
        if paid
        else None
    )
    is_free = terminal.terminal_id == free_id
    if paid and terms and terms.get("paid_until") == paid.isoformat():
        grace = datetime.fromisoformat(terms["grace_until"])
    state: Literal[
        "free",
        "active",
        "grace",
        "unpaid",
        "expired",
        "payments_disabled",
        "admin_disabled",
        "deleted",
    ]
    action = None
    if terminal.deleted_at is not None:
        state, reason, allowed = "deleted", "Терминал удалён", False
    elif not runtime.is_active:
        state, reason, allowed = (
            "admin_disabled",
            "Отключён администратором. Оплата не отменяет отключение.",
            False,
        )
    elif is_free:
        state, reason, allowed = "free", "Бесплатно, без ограничения времени", True
    elif not enabled:
        state, reason, allowed = (
            "payments_disabled",
            "Платные подключения пока недоступны",
            False,
        )
    elif paid is None:
        state, reason, allowed, action = (
            "unpaid",
            "Требуется подписка",
            False,
            "Подключить",
        )
    elif at < paid:
        state, reason, allowed, action = "active", "Подписка оплачена", True, "Продлить"
    elif grace is not None and at < grace:
        state, reason, allowed, action = (
            "grace",
            "Продлите подписку, чтобы сохранить доступ",
            True,
            "Продлить",
        )
    else:
        state, reason, allowed, action = (
            "expired",
            "Подписка закончилась",
            False,
            "Восстановить доступ",
        )
    return TerminalSubscription(
        terminal_id=terminal.terminal_id,
        device_id=runtime.device_id,
        name=runtime.note or runtime.sn,
        state=state,
        allowed=allowed,
        paid_until=paid,
        grace_until=grace,
        reason=reason,
        action=action,
        is_free=is_free,
    )


async def lock_tenant(db: AsyncSession, tenant_id: int) -> None:
    if (
        await db.scalar(
            select(Org.org_id).where(Org.org_id == tenant_id).with_for_update()
        )
        is None
    ):
        raise HTTPException(404, "Организация не найдена")


async def subscription_context(
    db: AsyncSession, tenant_id: int
) -> tuple[L4DeskTenantProfile | None, int | None]:
    profile = await db.get(L4DeskTenantProfile, tenant_id)
    if profile is None:
        return None, None
    free_id = await db.scalar(
        select(L4DeskTerminal.terminal_id)
        .where(
            L4DeskTerminal.tenant_id == tenant_id,
            L4DeskTerminal.deleted_at.is_(None),
        )
        .order_by(L4DeskTerminal.ordinal)
        .limit(1)
    )
    return profile, free_id


async def list_subscriptions(
    db: AsyncSession, tenant_id: int, *, include_deleted: bool = False
) -> list[TerminalSubscription]:
    profile, free_id = await subscription_context(db, tenant_id)
    if profile is None:
        raise HTTPException(403, "Подписки доступны организациям L4Desk")
    stmt = (
        select(L4DeskTerminal, Terminal)
        .join(Terminal, Terminal.id == L4DeskTerminal.runtime_terminal_id)
        .where(L4DeskTerminal.tenant_id == tenant_id)
        .order_by(L4DeskTerminal.ordinal)
    )
    if not include_deleted:
        stmt = stmt.where(L4DeskTerminal.deleted_at.is_(None))
    rows = (await db.execute(stmt)).all()
    terms = await subscription_terms(db, tenant_id)
    return [
        subscription_status(
            t,
            r,
            free_id=free_id,
            timezone=profile.timezone,
            enabled=settings.is_yookassa_enabled,
            terms=terms.get(t.terminal_id),
        )
        for t, r in rows
    ]


async def check_terminal(
    db: AsyncSession, tenant_id: int, runtime: Terminal
) -> TerminalSubscription | None:
    profile, free_id = await subscription_context(db, tenant_id)
    if profile is None:
        return (
            None  # Classic technical session records do not opt a tenant into billing.
        )
    terminal = await db.scalar(
        select(L4DeskTerminal).where(
            L4DeskTerminal.tenant_id == tenant_id,
            L4DeskTerminal.terminal_id == runtime.id,
        )
    )
    if terminal is None:
        raise HTTPException(403, "Терминал не подключён к организации L4Desk")
    return subscription_status(
        terminal,
        runtime,
        free_id=free_id,
        timezone=profile.timezone,
        enabled=settings.is_yookassa_enabled,
        terms=(await subscription_terms(db, tenant_id, terminal.terminal_id)).get(
            terminal.terminal_id
        ),
    )


async def subscription_terms(
    db: AsyncSession, tenant_id: int, terminal_id: int | None = None
) -> dict[int, dict]:
    """Keep the paid period's calendar grace stable when the tenant timezone changes."""
    stmt = (
        select(L4DeskAuditEvent)
        .where(
            L4DeskAuditEvent.tenant_id == tenant_id,
            L4DeskAuditEvent.event_type == "subscription.terms",
        )
        .order_by(L4DeskAuditEvent.id.desc())
    )
    if terminal_id is not None:
        stmt = stmt.where(L4DeskAuditEvent.subject_id == str(terminal_id)).limit(1)
    result = {}
    for event in await db.scalars(stmt):
        result.setdefault(int(event.subject_id), event.details or {})
    return result


def record_terms(
    db: AsyncSession,
    terminal: L4DeskTerminal,
    *,
    timezone: str,
    actor: str,
    operation_id: str,
    reason: str = "",
) -> dict:
    paid = terminal.paid_until
    grace = (
        (paid.astimezone(resolve_timezone(timezone)) + timedelta(days=3)).astimezone(
            UTC
        )
        if paid
        else None
    )
    details = {
        "paid_until": paid.isoformat() if paid else None,
        "grace_until": grace.isoformat() if grace else None,
        "timezone": timezone,
        "reason": reason,
    }
    db.add(
        L4DeskAuditEvent(
            tenant_id=terminal.tenant_id,
            actor=actor,
            event_type="subscription.terms",
            subject_type="terminal",
            subject_id=str(terminal.terminal_id),
            operation_id=operation_id,
            correlation_id=operation_id,
            outcome="applied",
            details=details,
        )
    )
    return details


async def check_creation_limit(db: AsyncSession, tenant_id: int) -> None:
    profile = await db.get(L4DeskTenantProfile, tenant_id)
    if profile is None:
        return
    await lock_tenant(db, tenant_id)
    statuses = await list_subscriptions(db, tenant_id)
    if len(statuses) >= 3 and not any(
        t.state == "active" and not t.is_free for t in statuses
    ):
        raise HTTPException(
            409,
            detail={
                "code": "terminal_limit_reached",
                "message": "Без активной подписки можно создать три терминала: один бесплатный и два для подготовки.",
            },
        )
