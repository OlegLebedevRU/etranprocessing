"""Addressed session shutdown and reconciliation of unfinished subscription orders."""

import asyncio
import logging

from sqlalchemy import String, or_, select

from app.config import settings
from app.database import async_session
from app.models import Terminal
from app.models_l4desk import (
    FinPayment,
    L4DeskAuditEvent,
    L4DeskRemoteSession,
    L4DeskTenantProfile,
)
from app.services.remote_session_stop import RemoteSessionStopService
from app.services.subscription_notifications import notify_tenant
from app.services.subscription_payments import sync_order
from app.services.subscriptions import list_subscriptions

logger = logging.getLogger(__name__)
_last_payment_id = 0


async def reconcile_sessions() -> None:
    async with async_session() as db:
        stmt = select(L4DeskTenantProfile.tenant_id)
        if settings.product_scope_split_enabled:
            stmt = stmt.union(
                select(L4DeskRemoteSession.tenant_id).where(
                    L4DeskRemoteSession.state.in_(
                        ["reserved", "start_requested", "active", "stop_requested"]
                    )
                )
            )
        tenants = list(await db.scalars(stmt))
    for tenant_id in tenants:
        try:
            async with async_session() as db:
                profile = await db.get(L4DeskTenantProfile, tenant_id)
                states = (
                    await list_subscriptions(db, tenant_id, include_deleted=True)
                    if profile
                    else []
                )
                for state in states:
                    if not state.allowed:
                        await RemoteSessionStopService.stop_sessions_for_blocked_tenant(
                            db,
                            tenant_id,
                            terminal_id=state.terminal_id,
                            actor="subscription_worker",
                        )
                if settings.product_scope_split_enabled:
                    inactive = await db.scalars(
                        select(Terminal.id)
                        .join(
                            L4DeskRemoteSession,
                            L4DeskRemoteSession.terminal_id == Terminal.id,
                        )
                        .where(
                            Terminal.org_id == tenant_id,
                            Terminal.is_active.is_(False)
                            if profile is not None
                            else or_(
                                Terminal.is_active.is_(False),
                                Terminal.l4desk_subscription_enabled.is_(True),
                            ),
                            L4DeskRemoteSession.tenant_id == tenant_id,
                            L4DeskRemoteSession.state.in_(
                                ["reserved", "start_requested", "active"]
                            ),
                        )
                        .distinct()
                    )
                    for terminal_id in inactive:
                        await RemoteSessionStopService.stop_sessions_for_blocked_tenant(
                            db,
                            tenant_id,
                            terminal_id=terminal_id,
                            actor="administrative_stop_worker",
                        )
                await RemoteSessionStopService.process_stop_outbox(
                    db, tenant_id=tenant_id
                )
                await db.commit()
                if profile is not None:
                    await notify_tenant(db, tenant_id)
        except Exception:
            logger.exception(
                "Subscription session reconciliation failed for tenant %s", tenant_id
            )


async def reconcile_payments() -> None:
    global _last_payment_id
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


async def run_tick() -> None:
    await reconcile_sessions()
    await reconcile_payments()


async def _run_loop(callback) -> None:
    while True:
        try:
            await callback()
        except Exception:
            logger.exception("Subscription worker tick failed")
        await asyncio.sleep(settings.subscription_worker_interval_seconds)


async def run_worker() -> None:
    # A slow payment provider cannot delay administrative/subscription session checks.
    async with asyncio.TaskGroup() as group:
        group.create_task(_run_loop(reconcile_sessions))
        group.create_task(_run_loop(reconcile_payments))
