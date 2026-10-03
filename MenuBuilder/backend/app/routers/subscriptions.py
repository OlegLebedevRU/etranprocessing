"""User-facing subscriptions and resource usage, independent of the old subledger."""

import secrets
from datetime import UTC, date, datetime, timedelta
from typing import Any

from fastapi import APIRouter, Depends, Header, HTTPException, Request
from pydantic import BaseModel, Field
from sqlalchemy import String, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import get_current_user, require_superuser, require_tenant_context
from app.config import settings
from app.database import get_db
from app.models_l4desk import (
    FinPayment,
    FinUsageDaily,
    L4DeskAuditEvent,
    L4DeskTerminal,
)
from app.services.subscription_payments import (
    SubscriptionItem,
    SubscriptionOrderRequest,
    create_order,
    order_snapshot,
    payment_read,
    quote,
    sync_order,
)
from app.services.subscriptions import (
    list_subscriptions,
    lock_tenant,
    record_terms,
    subscription_context,
)
from app.services.yookassa import YooKassaError, is_ip_trusted

router = APIRouter(tags=["subscriptions"])


class SubscriptionCorrection(BaseModel):
    paid_until: datetime | None
    expected_paid_until: datetime | None = None
    reason: str = Field(min_length=10, max_length=500)
    operation_id: str = Field(min_length=16, max_length=128)


@router.post("/admin/subscriptions/{terminal_id}/correct")
async def correct_subscription(
    terminal_id: int,
    body: SubscriptionCorrection,
    user: dict = Depends(require_superuser),
    db: AsyncSession = Depends(get_db),
):
    if not user.get("is_superuser") and user.get("role_id") != 1:
        raise HTTPException(403, "Корректировка доступна только суперпользователю")
    terminal = await db.get(L4DeskTerminal, terminal_id)
    if terminal is None or terminal.deleted_at is not None:
        raise HTTPException(404, "Терминал не найден")
    await lock_tenant(db, terminal.tenant_id)
    terminal = await db.get(L4DeskTerminal, terminal_id, populate_existing=True)
    if terminal is None or terminal.deleted_at is not None:
        raise HTTPException(404, "Терминал не найден")
    previous = await db.scalar(
        select(L4DeskAuditEvent).where(
            L4DeskAuditEvent.tenant_id == terminal.tenant_id,
            L4DeskAuditEvent.event_type == "subscription.corrected",
            L4DeskAuditEvent.operation_id == body.operation_id,
        )
    )
    proposed = body.paid_until
    if proposed is not None:
        if proposed.tzinfo is None:
            raise HTTPException(422, "Дата должна содержать часовой пояс")
        proposed = proposed.astimezone(UTC)
    if previous is not None:
        if (
            previous.subject_id != str(terminal_id)
            or (previous.details or {}).get("paid_until")
            != (proposed.isoformat() if proposed else None)
            or (previous.details or {}).get("reason") != body.reason
        ):
            raise HTTPException(409, "Ключ операции уже использован")
        return previous.details
    profile, free_id = await subscription_context(db, terminal.tenant_id)
    if profile is None or free_id == terminal_id:
        raise HTTPException(
            409, "Корректируется только подписка дополнительного терминала L4Desk"
        )
    old = terminal.paid_until
    if (
        "expected_paid_until" in body.model_fields_set
        and old != body.expected_paid_until
    ):
        raise HTTPException(
            409, "Срок изменился. Обновите данные перед корректировкой."
        )
    terminal.paid_until = proposed
    actor = str(user.get("username") or user.get("sub"))
    details = record_terms(
        db,
        terminal,
        timezone=profile.timezone,
        actor=actor,
        operation_id=body.operation_id,
        reason=body.reason,
    )
    details["old_paid_until"] = old.isoformat() if old else None
    db.add(
        L4DeskAuditEvent(
            tenant_id=terminal.tenant_id,
            actor=actor,
            event_type="subscription.corrected",
            subject_type="terminal",
            subject_id=str(terminal_id),
            operation_id=body.operation_id,
            correlation_id=body.operation_id,
            outcome="applied",
            details=details,
        )
    )
    await db.commit()
    return details


class QuoteRequest(BaseModel):
    items: list[SubscriptionItem] = Field(min_length=1, max_length=50)


def tenant(user: dict[str, Any], target: int | None = None) -> int:
    if target is not None:
        if not user.get("is_superuser"):
            raise HTTPException(403, "Только администратор может выбрать организацию")
        return target
    org_id = int(user.get("org_id") or 0)
    if org_id <= 0:
        raise HTTPException(403, "Выберите организацию")
    return org_id


@router.get("/subscriptions")
async def summary(
    org_id: int | None = None,
    user: dict = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    items = await list_subscriptions(db, tenant(user, org_id))
    return {
        "payments_enabled": settings.is_yookassa_enabled,
        "month_price_kopecks": settings.subscription_month_price_kopecks,
        "items": items,
        "can_create": len(items) < 3
        or any(i.state == "active" and not i.is_free for i in items),
    }


@router.post("/subscriptions/quote")
async def subscription_quote(
    body: QuoteRequest,
    user: dict = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
):
    if user.get("role_id") == 4:
        raise HTTPException(403, "Недостаточно прав для покупки")
    return await quote(db, tenant(user), body.items)


@router.post("/subscriptions/orders")
async def checkout(
    body: SubscriptionOrderRequest,
    user: dict = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
):
    if user.get("role_id") == 4:
        raise HTTPException(403, "Недостаточно прав для покупки")
    email = str(user.get("email") or user.get("username") or "")
    if "@" not in email:
        raise HTTPException(422, "Для оплаты укажите email в профиле")
    try:
        return await create_order(
            db, tenant(user), body, str(user.get("username") or user.get("sub")), email
        )
    except YooKassaError as exc:
        raise HTTPException(
            502, "Платёж пока не подтверждён. Повторите проверку этого же заказа."
        ) from exc


@router.get("/subscriptions/orders")
async def orders(
    user: dict = Depends(require_tenant_context), db: AsyncSession = Depends(get_db)
):
    org_id = tenant(user)
    rows = (
        await db.execute(
            select(FinPayment, L4DeskAuditEvent)
            .join(
                L4DeskAuditEvent,
                L4DeskAuditEvent.subject_id == FinPayment.id.cast(String),
            )
            .where(
                FinPayment.tenant_id == org_id,
                L4DeskAuditEvent.tenant_id == org_id,
                L4DeskAuditEvent.event_type == "subscription.order",
            )
            .order_by(FinPayment.id.desc())
            .limit(50)
        )
    ).all()
    return [payment_read(p, event.details or {}) for p, event in rows]


@router.post("/subscriptions/orders/{payment_id}/check")
async def check_order(
    payment_id: int,
    user: dict = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
):
    if user.get("role_id") == 4:
        raise HTTPException(403, "Недостаточно прав")
    try:
        return await sync_order(db, payment_id, tenant(user))
    except YooKassaError as exc:
        raise HTTPException(
            502, "Платёж пока не подтверждён. Повторите проверку позже."
        ) from exc


@router.post("/finance/yookassa/webhook")
async def webhook(
    request: Request,
    body: dict,
    db: AsyncSession = Depends(get_db),
    secret: str | None = Header(None, alias="X-YooKassa-Webhook-Secret"),
):
    if settings.yookassa_webhook_secret and not secrets.compare_digest(
        secret or "", settings.yookassa_webhook_secret
    ):
        raise HTTPException(403, "Неверный источник уведомления")
    if settings.yookassa_ip_filter_enabled and not is_ip_trusted(
        request.client.host if request.client else None, settings.yookassa_trusted_ips
    ):
        raise HTTPException(403, "Неверный источник уведомления")
    obj = body.get("object")
    provider_id = obj.get("id") if isinstance(obj, dict) else None
    if not isinstance(provider_id, str) or not provider_id:
        return {"status": "ignored"}
    payment = await db.scalar(
        select(FinPayment).where(FinPayment.provider_payment_id == provider_id)
    )
    if payment is None:
        # Unknown/old notifications cannot grant months. Reconciliation of uncertain
        # create requests uses the same provider idempotence key.
        return {"status": "ignored"}
    try:
        await order_snapshot(db, payment)
    except HTTPException as exc:
        if exc.status_code == 410:
            return {"status": "ignored"}
        raise
    try:
        return await sync_order(db, payment.id)
    except YooKassaError as exc:
        raise HTTPException(502, "Не удалось сверить платёж") from exc


@router.get("/usage")
async def usage(
    start: date | None = None,
    end: date | None = None,
    terminal_id: int | None = None,
    user: dict = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
):
    org_id = tenant(user)
    end = end or datetime.now(UTC).date()
    start = start or end - timedelta(days=29)
    if start > end or (end - start).days > 366:
        raise HTTPException(422, "Выберите период не длиннее года")
    stmt = select(FinUsageDaily).where(
        FinUsageDaily.tenant_id == org_id, FinUsageDaily.local_date.between(start, end)
    )
    if terminal_id is not None:
        stmt = stmt.where(FinUsageDaily.terminal_id == terminal_id)
    rows = (await db.scalars(stmt.order_by(FinUsageDaily.local_date))).all()
    result = {
        (r.terminal_id, str(r.local_date)): {
            "terminal_id": r.terminal_id,
            "date": str(r.local_date),
            "video_seconds": r.video_seconds,
            "console_seconds": r.console_seconds,
        }
        for r in rows
    }
    late = (
        await db.scalars(
            select(L4DeskAuditEvent).where(
                L4DeskAuditEvent.tenant_id == org_id,
                L4DeskAuditEvent.event_type == "duration.recorded",
                L4DeskAuditEvent.occurred_at
                >= datetime.combine(start, datetime.min.time(), tzinfo=UTC)
                - timedelta(days=2),
            )
        )
    ).all()
    for event in late:
        d = event.details or {}
        if not d.get("historical_posted") or not str(start) <= d.get(
            "local_date", ""
        ) <= str(end):
            continue
        tid = int(event.subject_id)
        if terminal_id is not None and tid != terminal_id:
            continue
        key = (tid, d["local_date"])
        target = result.setdefault(
            key,
            {
                "terminal_id": tid,
                "date": d["local_date"],
                "video_seconds": 0,
                "console_seconds": 0,
            },
        )
        target["video_seconds"] += d.get("video_seconds", 0)
        target["console_seconds"] += d.get("console_seconds", 0)
    return list(result.values())


@router.api_route(
    "/finance/{path:path}",
    methods=["GET", "POST", "PUT", "PATCH", "DELETE"],
    include_in_schema=False,
)
async def frozen_finance(path: str, user: dict = Depends(get_current_user)):
    raise HTTPException(
        410,
        "Балансная модель закрыта. Используйте подписки и статистику использования.",
    )
