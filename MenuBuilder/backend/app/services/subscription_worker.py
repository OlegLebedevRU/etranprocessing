"""Addressed session shutdown and reconciliation of unfinished subscription orders."""

import asyncio
import logging

from sqlalchemy import String, select

from app.config import settings
from app.database import async_session
from app.models_l4desk import FinPayment, L4DeskAuditEvent, L4DeskTenantProfile
from app.services.remote_session_stop import RemoteSessionStopService
from app.services.subscription_notifications import notify_tenant
from app.services.subscription_payments import sync_order
from app.services.subscriptions import list_subscriptions

logger = logging.getLogger(__name__)
_last_payment_id = 0


async def run_tick() -> None:
    global _last_payment_id
    async with async_session() as db:
        tenants = list(await db.scalars(select(L4DeskTenantProfile.tenant_id)))
    for tenant_id in tenants:
        try:
            async with async_session() as db:
                states = await list_subscriptions(db, tenant_id, include_deleted=True)
                for state in states:
                    if not state.allowed:
                        await RemoteSessionStopService.stop_sessions_for_blocked_tenant(
                            db,
                            tenant_id,
                            terminal_id=state.terminal_id,
                            actor="subscription_worker",
                        )
                await RemoteSessionStopService.process_stop_outbox(
                    db, tenant_id=tenant_id
                )
                await db.commit()
                await notify_tenant(db, tenant_id)
        except Exception:
            logger.exception(
                "Subscription session reconciliation failed for tenant %s", tenant_id
            )
    async with async_session() as db:
        pending = list(
            await db.scalars(
                select(FinPayment.id)
                .join(
                    L4DeskAuditEvent,
                    L4DeskAuditEvent.subject_id == FinPayment.id.cast(String),
                )
                .where(
                    L4DeskAuditEvent.event_type == "subscription.order",
                    FinPayment.status.in_(["pending", "waiting_for_capture"]),
                    FinPayment.id > _last_payment_id,
                )
                .order_by(FinPayment.id)
                .limit(50)
            )
        )
    # Rotate even after provider failures so a stuck order cannot starve later ones.
    _last_payment_id = pending[-1] if pending else 0
    for payment_id in pending:
        try:
            async with async_session() as db:
                await sync_order(db, payment_id)
        except Exception:
            logger.exception(
                "Subscription order %s is awaiting provider reconciliation", payment_id
            )


async def run_worker() -> None:
    while True:
        try:
            await run_tick()
        except Exception:
            logger.exception("Subscription worker tick failed")
        await asyncio.sleep(settings.subscription_worker_interval_seconds)
