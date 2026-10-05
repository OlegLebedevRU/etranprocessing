"""Verified subscription purchases with atomic, idempotent terminal extensions."""

import hashlib
import json
from datetime import UTC, datetime
from decimal import Decimal, InvalidOperation
from typing import Any, Literal

from fastapi import HTTPException
from pydantic import BaseModel, Field, field_validator
from sqlalchemy import String, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.models_l4desk import FinPayment, L4DeskAuditEvent, L4DeskTerminal
from app.services.subscriptions import (
    add_months,
    list_subscriptions,
    lock_tenant,
    record_terms,
    subscription_context,
)
from app.services.yookassa import get_yookassa_client


class SubscriptionItem(BaseModel):
    terminal_id: int = Field(gt=0, strict=True)
    months: Literal[1, 3, 6, 12]

    @field_validator("months", mode="before")
    @classmethod
    def validate_months(cls, value):
        if type(value) is not int:
            raise ValueError("Срок указывается целым числом месяцев")
        return value


class SubscriptionOrderRequest(BaseModel):
    items: list[SubscriptionItem] = Field(min_length=1, max_length=50)
    operation_id: str = Field(min_length=16, max_length=128)
    quote_hash: str


def snapshot_hash(snapshot: dict[str, Any]) -> str:
    return hashlib.sha256(
        json.dumps(
            snapshot, sort_keys=True, ensure_ascii=False, separators=(",", ":")
        ).encode()
    ).hexdigest()


async def quote(
    db: AsyncSession, tenant_id: int, items: list[SubscriptionItem]
) -> dict[str, Any]:
    states = {s.terminal_id: s for s in await list_subscriptions(db, tenant_id)}
    profile, _ = await subscription_context(db, tenant_id)
    if profile is None:
        raise HTTPException(403, "Организация не подключена к L4Desk")
    if len({i.terminal_id for i in items}) != len(items):
        raise HTTPException(422, "Терминал указан несколько раз")
    snapshot: dict[str, Any] = {
        "version": 1,
        "purpose": "subscription",
        "tenant_id": tenant_id,
        "timezone": profile.timezone,
        "price_kopecks": settings.subscription_month_price_kopecks,
        "items": [
            {"terminal_id": i.terminal_id, "months": i.months}
            for i in sorted(items, key=lambda i: i.terminal_id)
        ],
    }
    for item in items:
        state = states.get(item.terminal_id)
        if state is None or state.is_free or state.state == "admin_disabled":
            raise HTTPException(
                409, "Выберите дополнительные терминалы, не отключённые администратором"
            )
    snapshot["amount_kopecks"] = (
        sum(i.months for i in items) * settings.subscription_month_price_kopecks
    )
    return {
        "snapshot": snapshot,
        "quote_hash": snapshot_hash(snapshot),
        "available": settings.is_yookassa_enabled,
        "preview": [
            {
                "terminal_id": i.terminal_id,
                "paid_until": add_months(
                    max(
                        datetime.now(UTC),
                        states[i.terminal_id].paid_until or datetime.now(UTC),
                    ),
                    i.months,
                    profile.timezone,
                ),
            }
            for i in items
        ],
    }


async def order_snapshot(db: AsyncSession, payment: FinPayment) -> dict[str, Any]:
    event = await db.scalar(
        select(L4DeskAuditEvent)
        .where(
            L4DeskAuditEvent.tenant_id == payment.tenant_id,
            L4DeskAuditEvent.event_type == "subscription.order",
            L4DeskAuditEvent.subject_id == str(payment.id),
        )
        .order_by(L4DeskAuditEvent.id)
        .limit(1)
    )
    if event is None or not event.details:
        raise HTTPException(410, "Этот платёж относится к прежней финансовой модели")
    return event.details


def payment_read(payment: FinPayment, snapshot: dict[str, Any]) -> dict[str, Any]:
    return {
        "id": payment.id,
        "status": payment.status,
        "amount_kopecks": payment.amount_kopecks,
        "confirmation_url": payment.confirmation_url,
        "items": snapshot["items"],
        "created_at": payment.created_at,
    }


async def create_order(
    db: AsyncSession,
    tenant_id: int,
    request: SubscriptionOrderRequest,
    actor: str,
    email: str,
) -> dict[str, Any]:
    if not settings.is_yookassa_enabled:
        raise HTTPException(
            409,
            detail={"code": "payments_disabled", "message": "Оплата пока недоступна"},
        )
    await lock_tenant(db, tenant_id)
    existing = await db.scalar(
        select(FinPayment)
        .where(
            FinPayment.tenant_id == tenant_id,
            FinPayment.operation_id == request.operation_id,
        )
        .with_for_update()
    )
    if existing is not None:
        snapshot = await order_snapshot(db, existing)
        proposed = [
            {"terminal_id": i.terminal_id, "months": i.months}
            for i in sorted(request.items, key=lambda i: i.terminal_id)
        ]
        if (
            proposed != snapshot["items"]
            or request.quote_hash != snapshot["quote_hash"]
        ):
            raise HTTPException(409, "Ключ заказа уже использован для другой корзины")
        payment = existing
    else:
        result = await quote(db, tenant_id, request.items)
        if result["quote_hash"] != request.quote_hash:
            raise HTTPException(
                409, "Стоимость изменилась. Обновите корзину перед оплатой."
            )
        snapshot = {
            **result["snapshot"],
            "quote_hash": result["quote_hash"],
            "email": email,
        }
        payment = FinPayment(
            tenant_id=tenant_id,
            operation_id=request.operation_id,
            provider="yookassa",
            status="pending",
            amount_kopecks=snapshot["amount_kopecks"],
            currency="RUB",
            actor=actor,
            correlation_id=request.operation_id,
        )
        db.add(payment)
        await db.flush()
        rubles = f"{snapshot['amount_kopecks'] // 100}.{snapshot['amount_kopecks'] % 100:02d}"
        provider_request = {
            "amount": {"value": rubles, "currency": "RUB"},
            "capture": True,
            "confirmation": {
                "type": "redirect",
                "return_url": settings.yookassa_return_url_base.rstrip("/")
                + "/licenses",
            },
            "description": "Подписка L4Desk на дополнительные терминалы",
            "metadata": {
                "purpose": "subscription",
                "order_id": str(payment.id),
                "tenant_id": str(tenant_id),
                "quote_hash": snapshot["quote_hash"],
            },
        }
        if settings.yookassa_receipt_enabled:
            receipt = {
                "customer": {"email": email},
                "items": [
                    {
                        "description": settings.yookassa_item_description,
                        "quantity": "1.00",
                        "amount": {"value": rubles, "currency": "RUB"},
                        "vat_code": settings.yookassa_vat_code,
                        "payment_subject": settings.yookassa_payment_subject,
                        "payment_mode": settings.yookassa_payment_mode,
                    }
                ],
            }
            if settings.yookassa_tax_system_code is not None:
                receipt["tax_system_code"] = settings.yookassa_tax_system_code
            provider_request["receipt"] = receipt
        snapshot["provider_request"] = provider_request
        db.add(
            L4DeskAuditEvent(
                tenant_id=tenant_id,
                actor=actor,
                event_type="subscription.order",
                subject_type="payment",
                subject_id=str(payment.id),
                operation_id=request.operation_id,
                correlation_id=request.operation_id,
                outcome="pending",
                details=snapshot,
            )
        )
    payment_id = payment.id
    await db.commit()  # provider IO never holds the tenant lock
    return await sync_order(db, payment_id, tenant_id)


async def sync_order(
    db: AsyncSession, payment_id: int, tenant_id: int | None = None
) -> dict[str, Any]:
    payment = await db.get(FinPayment, payment_id)
    if payment is None or (tenant_id is not None and payment.tenant_id != tenant_id):
        raise HTTPException(404, "Платёж не найден")
    snapshot = await order_snapshot(db, payment)
    if payment.status in {"succeeded", "canceled"}:
        return payment_read(payment, snapshot)
    owner_id = payment.tenant_id
    operation_id = payment.operation_id
    provider_id = payment.provider_payment_id
    await db.commit()
    client = get_yookassa_client()
    if provider_id:
        remote = await client.get_payment(provider_id)
    else:
        remote = await client.create_payment(
            amount_kopecks=snapshot["amount_kopecks"],
            idempotence_key=hashlib.sha256(
                f"subscription:{owner_id}:{operation_id}".encode()
            ).hexdigest(),
            return_url=settings.yookassa_return_url_base.rstrip("/") + "/licenses",
            description="Подписка L4Desk на дополнительные терминалы",
            customer_email=snapshot["email"],
            metadata={
                "purpose": "subscription",
                "order_id": str(payment_id),
                "tenant_id": str(owner_id),
                "quote_hash": snapshot["quote_hash"],
            },
            request_snapshot=snapshot["provider_request"],
        )
    await lock_tenant(db, owner_id)
    payment = await db.scalar(
        select(FinPayment)
        .where(FinPayment.id == payment_id)
        .with_for_update()
        .execution_options(populate_existing=True)
    )
    if payment is None:
        raise HTTPException(404, "Платёж не найден")
    if payment.status in {"succeeded", "canceled"}:
        return payment_read(payment, snapshot)
    metadata = remote.get("metadata") or {}
    amount = remote.get("amount") or {}
    try:
        amount_matches = (
            Decimal(str(amount.get("value", "0"))) * 100 == payment.amount_kopecks
        )
    except InvalidOperation:
        amount_matches = False
    if (
        remote.get("id") != (provider_id or remote.get("id"))
        or not remote.get("id")
        or metadata.get("purpose") != "subscription"
        or metadata.get("order_id") != str(payment_id)
        or metadata.get("tenant_id") != str(owner_id)
        or metadata.get("quote_hash") != snapshot["quote_hash"]
        or amount.get("currency") != "RUB"
        or not amount_matches
    ):
        raise HTTPException(409, "Подтверждение платежа не соответствует заказу")
    payment.provider_payment_id = remote["id"]
    payment.confirmation_url = (remote.get("confirmation") or {}).get(
        "confirmation_url"
    )
    payment.receipt_snapshot = remote.get("receipt")
    status = remote.get("status")
    if status == "succeeded":
        if remote.get("paid") is not True or not remote.get("captured_at"):
            raise HTTPException(409, "Платёж ещё не подтверждён")
        try:
            paid_at = datetime.fromisoformat(remote["captured_at"])
        except (ValueError, TypeError) as exc:
            raise HTTPException(409, "Некорректное время оплаты") from exc
        if paid_at.tzinfo is None:
            raise HTTPException(409, "Некорректное время оплаты")
        extensions = []
        for item in snapshot["items"]:
            terminal = await db.scalar(
                select(L4DeskTerminal)
                .where(
                    L4DeskTerminal.tenant_id == owner_id,
                    L4DeskTerminal.terminal_id == item["terminal_id"],
                )
                .with_for_update()
                .execution_options(populate_existing=True)
            )
            if terminal is None or terminal.deleted_at is not None:
                raise HTTPException(
                    409, "Оплаченный терминал удалён; требуется проверка заказа"
                )
            if settings.product_scope_split_enabled:
                from app.models import Terminal

                runtime = await db.get(Terminal, terminal.runtime_terminal_id)
                if (
                    runtime is None
                    or runtime.org_id != owner_id
                    or not runtime.l4desk_subscription_enabled
                ):
                    raise HTTPException(
                        409,
                        "Терминал больше не участвует в подписке; требуется сверка платежа",
                    )
            old = terminal.paid_until
            new_paid_until = add_months(
                max(paid_at, old or paid_at), item["months"], snapshot["timezone"]
            )
            terminal.paid_until = new_paid_until
            record_terms(
                db,
                terminal,
                timezone=snapshot["timezone"],
                actor="payment_verification",
                operation_id=operation_id,
            )
            extensions.append(
                {
                    "terminal_id": terminal.terminal_id,
                    "old_paid_until": old.isoformat() if old else None,
                    "paid_until": new_paid_until.isoformat(),
                }
            )
        payment.status = "succeeded"
        payment.verified_at = datetime.now(UTC)
        payment.succeeded_at = paid_at
        db.add(
            L4DeskAuditEvent(
                tenant_id=owner_id,
                actor="payment_verification",
                event_type="subscription.applied",
                subject_type="payment",
                subject_id=str(payment_id),
                operation_id=operation_id,
                correlation_id=payment.correlation_id,
                outcome="succeeded",
                details={"items": extensions, "timezone": snapshot["timezone"]},
            )
        )
    elif status == "canceled":
        payment.status = "canceled"
    elif status in {"pending", "waiting_for_capture"}:
        payment.status = status
    else:
        raise HTTPException(409, "Неизвестное состояние платежа")
    await db.commit()
    return payment_read(payment, snapshot)


async def check_pending_orders(db: AsyncSession, tenant_id: int) -> None:
    rows = (
        await db.execute(
            select(FinPayment, L4DeskAuditEvent)
            .join(
                L4DeskAuditEvent,
                L4DeskAuditEvent.subject_id == FinPayment.id.cast(String),
            )
            .where(
                FinPayment.tenant_id == tenant_id,
                FinPayment.status.in_(["pending", "waiting_for_capture"]),
                L4DeskAuditEvent.tenant_id == tenant_id,
                L4DeskAuditEvent.event_type == "subscription.order",
            )
        )
    ).all()
    if rows:
        raise HTTPException(
            409,
            "Сначала дождитесь проверки текущего платежа; удаление может изменить оплачиваемую корзину",
        )
