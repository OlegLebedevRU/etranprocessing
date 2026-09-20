from __future__ import annotations

import logging
from datetime import UTC, datetime, timedelta
from typing import Any

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.models import User
from app.models_l4desk import (
    FinBillingCycle,
    FinNotificationDelivery,
    L4DeskAuditEvent,
    L4DeskMembership,
)
from app.repositories.l4desk_repository import L4DeskRepository
from app.services.email_service import ServerlessEmailClient
from app.services.financial_core.cycles import FinBillingCycleService

logger = logging.getLogger(__name__)

TYPE_CYCLE_MINUS_7 = "cycle_minus_7"
TYPE_CYCLE_MINUS_3 = "cycle_minus_3"
TYPE_CYCLE_MINUS_1 = "cycle_minus_1"
TYPE_GRACE = "grace"
TYPE_BLOCKED = "blocked"


class FinNotificationService:
    """Idempotent cycle-bound email notifications (L4D-12-MB).

    Normative Logic:
    6. Уведомления renewal-7d/-3d/-1d, grace_started, blocked уникальны
       по tenant/cycle/type; retry не создаёт дубль.
    """

    @staticmethod
    async def schedule_notification_if_missing(
        db: AsyncSession,
        tenant_id: int,
        billing_cycle_id: int,
        notification_type: str,
        scheduled_at: datetime,
        correlation_id: str | None = None,
    ) -> FinNotificationDelivery:
        """Schedule a notification delivery record idempotently.

        Guaranteed unique by (tenant_id, billing_cycle_id, notification_type).
        """
        repo = L4DeskRepository(db)
        existing = await repo.get_notification_delivery(
            tenant_id=tenant_id,
            billing_cycle_id=billing_cycle_id,
            notification_type=notification_type,
        )
        if existing is not None:
            return existing

        corr_id = (
            correlation_id
            or f"notif-{tenant_id}-{billing_cycle_id}-{notification_type}"
        )

        delivery = FinNotificationDelivery(
            tenant_id=tenant_id,
            billing_cycle_id=billing_cycle_id,
            notification_type=notification_type,
            scheduled_at=scheduled_at,
            status="pending",
            attempts=0,
            sent_at=None,
            provider_message_id=None,
            last_error=None,
            correlation_id=corr_id,
        )
        db.add(delivery)
        await db.flush()
        logger.info(
            "Scheduled notification %s for tenant=%s cycle=%s (scheduled_at=%s)",
            notification_type,
            tenant_id,
            billing_cycle_id,
            scheduled_at.isoformat(),
        )
        return delivery

    @staticmethod
    async def check_and_schedule_cycle_notifications(
        db: AsyncSession,
        tenant_id: int,
        as_of: datetime | None = None,
    ) -> list[FinNotificationDelivery]:
        """Check current cycle boundaries and schedule due renewal/grace/blocked notifications."""
        as_of_dt = as_of or datetime.now(UTC)
        if as_of_dt.tzinfo is None:
            as_of_dt = as_of_dt.replace(tzinfo=UTC)

        profile = await FinBillingCycleService.ensure_billing_profile(db, tenant_id)
        if profile.anchor_at is None:
            # Free tier has no individual billing cycles -> no renewal notifications
            return []

        cycle = await FinBillingCycleService.get_or_create_cycle_for_timestamp(
            db, tenant_id, as_of_dt
        )
        if cycle is None:
            return []

        scheduled: list[FinNotificationDelivery] = []

        # 1. Renewal boundaries: 7d, 3d, 1d before cycle.ends_at
        t_minus_7 = cycle.ends_at - timedelta(days=7)
        t_minus_3 = cycle.ends_at - timedelta(days=3)
        t_minus_1 = cycle.ends_at - timedelta(days=1)

        if as_of_dt >= t_minus_7 and as_of_dt < cycle.ends_at:
            rec = await FinNotificationService.schedule_notification_if_missing(
                db=db,
                tenant_id=tenant_id,
                billing_cycle_id=cycle.id,
                notification_type=TYPE_CYCLE_MINUS_7,
                scheduled_at=as_of_dt,
            )
            scheduled.append(rec)

        if as_of_dt >= t_minus_3 and as_of_dt < cycle.ends_at:
            rec = await FinNotificationService.schedule_notification_if_missing(
                db=db,
                tenant_id=tenant_id,
                billing_cycle_id=cycle.id,
                notification_type=TYPE_CYCLE_MINUS_3,
                scheduled_at=as_of_dt,
            )
            scheduled.append(rec)

        if as_of_dt >= t_minus_1 and as_of_dt < cycle.ends_at:
            rec = await FinNotificationService.schedule_notification_if_missing(
                db=db,
                tenant_id=tenant_id,
                billing_cycle_id=cycle.id,
                notification_type=TYPE_CYCLE_MINUS_1,
                scheduled_at=as_of_dt,
            )
            scheduled.append(rec)

        # 2. Entitlement transitions: grace and blocked
        if profile.entitlement == "grace":
            rec = await FinNotificationService.schedule_notification_if_missing(
                db=db,
                tenant_id=tenant_id,
                billing_cycle_id=cycle.id,
                notification_type=TYPE_GRACE,
                scheduled_at=as_of_dt,
            )
            scheduled.append(rec)
        elif profile.entitlement == "blocked":
            rec = await FinNotificationService.schedule_notification_if_missing(
                db=db,
                tenant_id=tenant_id,
                billing_cycle_id=cycle.id,
                notification_type=TYPE_BLOCKED,
                scheduled_at=as_of_dt,
            )
            scheduled.append(rec)

        return scheduled

    @staticmethod
    async def get_tenant_owner_email(db: AsyncSession, tenant_id: int) -> str | None:
        """Find authoritative email address for the tenant owner."""
        # 1. Look for owner membership
        stmt = (
            select(User.username)
            .join(L4DeskMembership, User.id == L4DeskMembership.user_id)
            .where(
                L4DeskMembership.tenant_id == tenant_id,
                L4DeskMembership.is_owner.is_(True),
            )
        )
        res = await db.execute(stmt)
        email = res.scalar_one_or_none()
        if email and "@" in email:
            return email.strip().lower()

        # 2. Fallback: any user belonging to this tenant/org
        fallback_stmt = select(User.username).where(
            User.org_id == tenant_id,
            User.is_active.is_(True),
        )
        fallback_res = await db.execute(fallback_stmt)
        fallback_email = fallback_res.scalar_one_or_none()
        if fallback_email and "@" in fallback_email:
            return fallback_email.strip().lower()

        return None

    @staticmethod
    async def dispatch_pending_notifications(
        db: AsyncSession,
        email_client: Any = None,
        as_of: datetime | None = None,
    ) -> list[FinNotificationDelivery]:
        """Dispatch due pending/failed notifications via email provider.

        Handles retries, audit logging, and provider failures cleanly.
        """
        as_of_dt = as_of or datetime.now(UTC)
        if as_of_dt.tzinfo is None:
            as_of_dt = as_of_dt.replace(tzinfo=UTC)

        repo = L4DeskRepository(db)
        pending = await repo.list_pending_notification_deliveries(
            as_of=as_of_dt, limit=50
        )
        if not pending:
            return []

        client = email_client or ServerlessEmailClient()
        dispatched: list[FinNotificationDelivery] = []

        for item in pending:
            # Query associated cycle to format text
            stmt_cycle = select(FinBillingCycle).where(
                FinBillingCycle.id == item.billing_cycle_id
            )
            cycle = (await db.execute(stmt_cycle)).scalar_one_or_none()

            recipient = await FinNotificationService.get_tenant_owner_email(
                db, item.tenant_id
            )
            if not recipient:
                item.attempts += 1
                item.last_error = (
                    f"No recipient email found for tenant {item.tenant_id}"
                )
                if item.attempts >= settings.l4desk_notification_max_retries:
                    item.status = "failed"
                await db.flush()
                continue

            ends_at_str = (
                cycle.ends_at.strftime("%Y-%m-%d %H:%M UTC")
                if cycle
                else "в конце цикла"
            )
            deadline_str = (
                cycle.grace_deadline.strftime("%Y-%m-%d %H:%M UTC")
                if cycle and cycle.grace_deadline
                else "в течение 3 дней"
            )

            if item.notification_type == TYPE_CYCLE_MINUS_7:
                subject = "L4Desk: Напоминание о продлении подписки (осталось 7 дней)"
                body = (
                    f"Здравствуйте!\n\n"
                    f"Напоминаем, что текущий расчетный период L4Desk заканчивается {ends_at_str}.\n"
                    f"Рекомендуем заблаговременно проверить и пополнить баланс лицевого счета для бесперебойной работы терминалов.\n\n"
                    f"Команда L4Desk"
                )
            elif item.notification_type == TYPE_CYCLE_MINUS_3:
                subject = "L4Desk: Напоминание о продлении подписки (осталось 3 дня)"
                body = (
                    f"Здравствуйте!\n\n"
                    f"До окончания текущего расчетного периода L4Desk осталось 3 дня ({ends_at_str}).\n"
                    f"Пожалуйста, проверьте баланс для продления обслуживания терминалов.\n\n"
                    f"Команда L4Desk"
                )
            elif item.notification_type == TYPE_CYCLE_MINUS_1:
                subject = "L4Desk: Напоминание о продлении подписки (остался 1 день)"
                body = (
                    f"Здравствуйте!\n\n"
                    f"Завтра ({ends_at_str}) заканчивается расчетный период L4Desk.\n"
                    f"Во избежание перебоев в удаленном управлении рекомендуем пополнить баланс сегодня.\n\n"
                    f"Команда L4Desk"
                )
            elif item.notification_type == TYPE_GRACE:
                subject = "L4Desk: Предоставлен льготный период оплаты"
                body = (
                    f"Здравствуйте!\n\n"
                    f"На вашем счете недостаточно средств. Вам предоставлена отсрочка до {deadline_str}.\n"
                    f"Пожалуйста, пополните баланс до указанной даты, чтобы не допустить приостановки удаленного управления.\n\n"
                    f"Команда L4Desk"
                )
            elif item.notification_type == TYPE_BLOCKED:
                subject = "L4Desk: Удаленное управление приостановлено"
                body = (
                    "Здравствуйте!\n\n"
                    "В связи с задолженностью и истечением льготного периода доступ к удаленному управлению терминалами приостановлен.\n"
                    "Для возобновления работы пополните баланс лицевого счета.\n\n"
                    "Команда L4Desk"
                )
            else:
                subject = "L4Desk: Уведомление"
                body = "Информационное сообщение сервиса L4Desk."

            item.status = "sending"
            await db.flush()

            try:
                res = await client.send_email(
                    device_id=f"tenant-{item.tenant_id}",
                    recipients=[recipient],
                    subject=subject,
                    message=body,
                )
                item.status = "sent"
                item.sent_at = datetime.now(UTC)
                item.attempts += 1
                item.provider_message_id = str(
                    res.get("postbox_message_id")
                    or res.get("message_id")
                    or f"msg-{item.id}"
                )
                item.last_error = None
                await db.flush()

                # Audit event
                audit = L4DeskAuditEvent(
                    tenant_id=item.tenant_id,
                    actor="notification_worker",
                    event_type="notification_delivered",
                    subject_type="fin_notification_delivery",
                    subject_id=str(item.id),
                    operation_id=f"notif-sent-{item.id}",
                    correlation_id=item.correlation_id,
                    outcome="success",
                    details={
                        "type": item.notification_type,
                        "recipient": recipient,
                        "provider_message_id": item.provider_message_id,
                    },
                    occurred_at=datetime.now(UTC),
                )
                db.add(audit)
                await db.flush()
                dispatched.append(item)

            except Exception as exc:  # noqa: BLE001
                logger.warning(
                    "Failed to deliver notification %s to %s: %s",
                    item.id,
                    recipient,
                    exc,
                )
                item.attempts += 1
                item.last_error = str(exc)[:500]
                if item.attempts >= settings.l4desk_notification_max_retries:
                    item.status = "failed"
                else:
                    item.status = "pending"
                await db.flush()

                audit = L4DeskAuditEvent(
                    tenant_id=item.tenant_id,
                    actor="notification_worker",
                    event_type="notification_failed",
                    subject_type="fin_notification_delivery",
                    subject_id=str(item.id),
                    operation_id=f"notif-fail-{item.id}-{item.attempts}",
                    correlation_id=item.correlation_id,
                    outcome="failed",
                    details={
                        "type": item.notification_type,
                        "recipient": recipient,
                        "attempts": item.attempts,
                        "error": str(exc)[:500],
                    },
                    occurred_at=datetime.now(UTC),
                )
                db.add(audit)
                await db.flush()
                dispatched.append(item)

        return dispatched
