"""Terminal renewal reminders stored in the existing audit outbox, without cycles."""

import hashlib
import logging
from datetime import UTC, datetime, timedelta

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.models import User
from app.models_l4desk import L4DeskAuditEvent, L4DeskMembership
from app.services.email_service import ServerlessEmailClient
from app.services.subscriptions import (
    TerminalSubscription,
    list_subscriptions,
    lock_tenant,
)

logger = logging.getLogger(__name__)


def reminder_kind(state: TerminalSubscription, now: datetime) -> str | None:
    if state.is_free or state.paid_until is None:
        return None
    if state.state in {"grace", "expired"}:
        return state.state
    if state.state != "active":
        return None
    days = (state.paid_until - now).total_seconds() / 86400
    for threshold in (1, 3, 7):
        if days <= threshold:
            return f"renew_{threshold}"
    return None


async def notify_tenant(db: AsyncSession, tenant_id: int, client=None) -> None:
    if (
        not settings.is_yookassa_enabled
        or not settings.l4desk_email_notifications_enabled
    ):
        return
    now = datetime.now(UTC)
    await lock_tenant(db, tenant_id)
    states = await list_subscriptions(db, tenant_id)
    for state in states:
        kind = reminder_kind(state, now)
        if kind is None or state.paid_until is None:
            continue
        operation = hashlib.sha256(
            f"{tenant_id}:{state.terminal_id}:{state.paid_until}:{kind}".encode()
        ).hexdigest()
        existing = await db.scalar(
            select(L4DeskAuditEvent.id).where(
                L4DeskAuditEvent.tenant_id == tenant_id,
                L4DeskAuditEvent.event_type == "subscription.reminder",
                L4DeskAuditEvent.operation_id == operation,
            )
        )
        if existing is None:
            db.add(
                L4DeskAuditEvent(
                    tenant_id=tenant_id,
                    actor="subscription_worker",
                    event_type="subscription.reminder",
                    subject_type="terminal",
                    subject_id=str(state.terminal_id),
                    operation_id=operation,
                    correlation_id=operation,
                    outcome="pending",
                    details={
                        "kind": kind,
                        "paid_until": state.paid_until.isoformat(),
                        "attempts": 0,
                    },
                )
            )
    await db.commit()
    pending = list(
        await db.scalars(
            select(L4DeskAuditEvent)
            .where(
                L4DeskAuditEvent.tenant_id == tenant_id,
                L4DeskAuditEvent.event_type == "subscription.reminder",
                L4DeskAuditEvent.outcome.in_(["pending", "sending"]),
            )
            .order_by(L4DeskAuditEvent.id)
            .limit(50)
        )
    )
    ids = [event.id for event in pending]
    await db.commit()
    for event_id in ids:
        await lock_tenant(db, tenant_id)
        event = await db.get(L4DeskAuditEvent, event_id, populate_existing=True)
        if event is None or event.outcome not in {"pending", "sending"}:
            await db.commit()
            continue
        details = dict(event.details or {})
        claimed = details.get("claimed_at")
        if (
            event.outcome == "sending"
            and claimed
            and now - datetime.fromisoformat(claimed) < timedelta(minutes=10)
        ):
            await db.commit()
            continue
        state = next(
            (
                s
                for s in await list_subscriptions(db, tenant_id)
                if str(s.terminal_id) == event.subject_id
            ),
            None,
        )
        if (
            state is None
            or not state.paid_until
            or state.paid_until.isoformat() != details.get("paid_until")
            or reminder_kind(state, now) != details.get("kind")
        ):
            event.outcome = "superseded"
            await db.commit()
            continue
        email = await db.scalar(
            select(User.username)
            .join(L4DeskMembership, L4DeskMembership.user_id == User.id)
            .where(
                L4DeskMembership.tenant_id == tenant_id,
                L4DeskMembership.is_owner.is_(True),
            )
            .limit(1)
        )
        if not email or "@" not in email:
            event.outcome = "failed"
            event.details = {**details, "error": "Owner email is missing"}
            await db.commit()
            continue
        details.update(
            attempts=details.get("attempts", 0) + 1, claimed_at=now.isoformat()
        )
        event.outcome, event.details = "sending", details
        name, paid, grace = state.name, state.paid_until, state.grace_until
        await db.commit()  # No database lock during email provider IO.
        outcome, error = "sent", None
        try:
            await (client or ServerlessEmailClient()).send_email(
                device_id=f"subscription-{event_id}",
                recipients=[email],
                subject=f"L4Desk: подписка терминала {name}",
                message=f"Терминал: {name}. Подписка оплачена до {paid:%d.%m.%Y %H:%M UTC}. "
                f"Доступ сохраняется до {grace:%d.%m.%Y %H:%M UTC}. "
                "Откройте раздел «Подписки», выберите терминал и продлите срок. Автосписаний нет.",
            )
        except Exception as exc:  # noqa: BLE001 — persist bounded delivery retry after provider failure.
            logger.warning(
                "Subscription reminder %s delivery failed: %s",
                event_id,
                type(exc).__name__,
            )
            outcome = (
                "failed"
                if details["attempts"] >= settings.l4desk_notification_max_retries
                else "pending"
            )
            error = type(exc).__name__
        await lock_tenant(db, tenant_id)
        event = await db.get(L4DeskAuditEvent, event_id, populate_existing=True)
        if event is not None:
            event.outcome, event.details = outcome, {**details, "error": error}
        await db.commit()
