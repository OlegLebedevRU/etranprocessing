from __future__ import annotations

import asyncio
import contextlib
import logging
from datetime import UTC, datetime
from typing import Any

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.database import async_session
from app.models_l4desk import FinBillingProfile
from app.services.financial_core.entitlement import (
    ENTITLEMENT_BLOCKED,
    FinEntitlementService,
)
from app.services.financial_core.notifications import FinNotificationService
from app.services.financial_core.stop_outbox import FinStopOutboxService

logger = logging.getLogger(__name__)


class FinEntitlementWorker:
    """Periodic worker for cycle boundaries, grace expiration, stop outbox, and notifications.

    Normative Logic:
    - Periodic entitlement evaluation across active tenants
    - Cycle renewal notifications: -7d, -3d, -1d
    - Grace & blocked transition notifications
    - Session termination upon blocked status
    - Outbox retry for session stops and email delivery
    """

    def __init__(
        self,
        interval_sec: float | None = None,
        email_client: Any = None,
        iot_adapter: Any = None,
    ) -> None:
        self.interval_sec = (
            interval_sec
            if interval_sec is not None
            else settings.l4desk_entitlement_worker_interval_sec
        )
        self.email_client = email_client
        self.iot_adapter = iot_adapter
        self._running = False

    async def run_single_tick(
        self,
        db: AsyncSession,
        as_of: datetime | None = None,
        email_client: Any = None,
        iot_adapter: Any = None,
    ) -> dict[str, Any]:
        """Execute one complete evaluation and reconciliation pass across all tenants."""
        as_of_dt = as_of or datetime.now(UTC)
        if as_of_dt.tzinfo is None:
            as_of_dt = as_of_dt.replace(tzinfo=UTC)

        client_email = email_client or self.email_client
        client_iot = iot_adapter or self.iot_adapter

        stmt = select(FinBillingProfile.tenant_id).order_by(
            FinBillingProfile.tenant_id.asc()
        )
        res = await db.execute(stmt)
        tenant_ids = list(res.scalars().all())

        evaluated_tenants = 0
        blocked_tenants = 0
        scheduled_notifications = 0
        stopped_sessions = 0

        for t_id in tenant_ids:
            # 1. Evaluate entitlement
            status = await FinEntitlementService.get_tenant_entitlement_status(
                db, t_id, as_of=as_of_dt
            )
            evaluated_tenants += 1

            # 2. Schedule cycle notifications (-7d, -3d, -1d, grace, blocked)
            notifs = (
                await FinNotificationService.check_and_schedule_cycle_notifications(
                    db, t_id, as_of=as_of_dt
                )
            )
            scheduled_notifications += len(notifs)

            # 3. If blocked, terminate active sessions
            if status.state == ENTITLEMENT_BLOCKED:
                blocked_tenants += 1
                stops = await FinStopOutboxService.stop_sessions_for_blocked_tenant(
                    db, t_id, iot_adapter=client_iot
                )
                stopped_sessions += len(stops)

        # 4. Dispatch pending notifications
        dispatched_notifs = await FinNotificationService.dispatch_pending_notifications(
            db, email_client=client_email, as_of=as_of_dt
        )

        # 5. Process stop outbox retries
        outbox_stops = await FinStopOutboxService.process_stop_outbox(
            db, iot_adapter=client_iot
        )

        await db.commit()

        return {
            "as_of": as_of_dt.isoformat(),
            "tenants_evaluated": evaluated_tenants,
            "tenants_blocked": blocked_tenants,
            "notifications_scheduled": scheduled_notifications,
            "notifications_dispatched": len(dispatched_notifs),
            "sessions_stopped": stopped_sessions,
            "outbox_retries_processed": len(outbox_stops),
        }

    async def run_worker(self) -> None:
        """Continuous background execution loop."""
        self._running = True
        logger.info(
            "Starting FinEntitlementWorker with interval=%.1fs",
            self.interval_sec,
        )
        while self._running:
            try:
                async with async_session() as db:
                    await self.run_single_tick(db)
            except asyncio.CancelledError:
                break
            except Exception as exc:  # noqa: BLE001
                logger.error("Unhandled error in FinEntitlementWorker: %s", exc)

            with contextlib.suppress(asyncio.CancelledError):
                await asyncio.sleep(self.interval_sec)

        logger.info("FinEntitlementWorker stopped.")

    def stop(self) -> None:
        self._running = False


entitlement_worker = FinEntitlementWorker()
