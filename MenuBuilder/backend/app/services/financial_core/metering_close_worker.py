from __future__ import annotations

import asyncio
import contextlib
import logging
from datetime import UTC, date, datetime, timedelta

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.database import async_session
from app.models_l4desk import FinUsageDaily
from app.services.financial_core.metering import FinMeteringService
from app.services.financial_core.timezones import resolve_timezone

logger = logging.getLogger(__name__)


class FinMeteringCloseWorker:
    """Post completed local days for an explicit tenant allowlist."""

    def __init__(
        self,
        tenant_ids: list[int] | None = None,
        interval_sec: float | None = None,
        grace_sec: int | None = None,
    ) -> None:
        self.tenant_ids = (
            tenant_ids
            if tenant_ids is not None
            else settings.l4desk_metering_close_worker_tenant_ids
        )
        self.interval_sec = (
            interval_sec
            if interval_sec is not None
            else settings.l4desk_metering_close_worker_interval_sec
        )
        self.grace_sec = (
            grace_sec
            if grace_sec is not None
            else settings.l4desk_metering_close_grace_sec
        )
        self._running = False

    async def run_single_tick(
        self, db: AsyncSession, as_of: datetime | None = None
    ) -> dict[str, int]:
        """Close eligible days; an empty allowlist cannot post any tenant."""
        if not self.tenant_ids:
            return {"days_closed": 0, "rows_posted": 0}

        now = as_of or datetime.now(UTC)
        if now.tzinfo is None:
            now = now.replace(tzinfo=UTC)
        cutoff = now - timedelta(seconds=self.grace_sec)

        stmt = (
            select(FinUsageDaily)
            .where(
                FinUsageDaily.tenant_id.in_(self.tenant_ids),
                FinUsageDaily.ledger_transaction_id.is_(None),
                FinUsageDaily.posted_kopecks > 0,
            )
            .order_by(FinUsageDaily.local_date, FinUsageDaily.tenant_id)
        )
        result = await db.execute(stmt)
        rows = result.scalars().all()
        day_ready: dict[tuple[int, date], bool] = {}
        for row in rows:
            key = (row.tenant_id, row.local_date)
            ready = (
                row.local_date
                < cutoff.astimezone(resolve_timezone(row.timezone)).date()
            )
            day_ready[key] = day_ready.get(key, True) and ready
        eligible_days = {key for key, ready in day_ready.items() if ready}

        rows_posted = 0
        for tenant_id, local_date in sorted(eligible_days):
            closed = await FinMeteringService.close_and_post_daily_usage(
                db, tenant_id=tenant_id, local_date=local_date
            )
            rows_posted += sum(row.ledger_transaction_id is not None for row in closed)

        if eligible_days:
            await db.commit()
        return {"days_closed": len(eligible_days), "rows_posted": rows_posted}

    async def run_worker(self) -> None:
        self._running = True
        logger.info(
            "Starting FinMeteringCloseWorker for %d tenants",
            len(self.tenant_ids),
        )
        while self._running:
            try:
                async with async_session() as db:
                    result = await self.run_single_tick(db)
                    if result["rows_posted"]:
                        logger.info(
                            "Metering close posted %d rows", result["rows_posted"]
                        )
            except asyncio.CancelledError:
                break
            except Exception:
                logger.exception("FinMeteringCloseWorker tick failed")

            with contextlib.suppress(asyncio.CancelledError):
                await asyncio.sleep(max(1.0, self.interval_sec))
        logger.info("FinMeteringCloseWorker stopped")

    def stop(self) -> None:
        self._running = False


metering_close_worker = FinMeteringCloseWorker()
