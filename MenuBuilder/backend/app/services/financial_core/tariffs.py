from __future__ import annotations

import logging
from datetime import UTC, datetime

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models_l4desk import FinTariffVersion
from app.services.financial_core.exceptions import (
    FinTariffNotFoundError,
    FinValidationError,
)
from app.services.financial_core.schemas import FinTariffVersionCreate

logger = logging.getLogger(__name__)

DEFAULT_TARIFF_VERSION = "v1.0"
DEFAULT_EFFECTIVE_FROM = datetime(2026, 1, 1, 0, 0, 0, tzinfo=UTC)
DEFAULT_TERMINAL_MONTH_KOPECKS = 10000
DEFAULT_HOURLY_RATE_KOPECKS = 100
DEFAULT_FREE_DAILY_SECONDS = 7200


class FinTariffService:
    """Service for managing versioned immutable tariff snapshots."""

    @staticmethod
    async def ensure_default_tariff(db: AsyncSession) -> FinTariffVersion:
        """Ensure the baseline default tariff snapshot (v1.0) exists."""
        stmt = select(FinTariffVersion).where(
            FinTariffVersion.version == DEFAULT_TARIFF_VERSION
        )
        res = await db.execute(stmt)
        existing = res.scalar_one_or_none()
        if existing is not None:
            return existing

        now_dt = datetime.now(UTC)
        tariff = FinTariffVersion(
            version=DEFAULT_TARIFF_VERSION,
            effective_from=DEFAULT_EFFECTIVE_FROM,
            terminal_month_kopecks=DEFAULT_TERMINAL_MONTH_KOPECKS,
            hourly_rate_kopecks=DEFAULT_HOURLY_RATE_KOPECKS,
            free_daily_seconds=DEFAULT_FREE_DAILY_SECONDS,
            actor="system:seed",
            correlation_id="init-seed",
            created_at=now_dt,
        )
        db.add(tariff)
        await db.flush()
        logger.info(
            "Created baseline tariff version %s effective from %s",
            tariff.version,
            tariff.effective_from,
        )
        return tariff

    @staticmethod
    async def get_effective_tariff(
        db: AsyncSession, as_of: datetime | None = None
    ) -> FinTariffVersion:
        """Resolve the effective tariff version for a specific point in time."""
        target_dt = as_of or datetime.now(UTC)
        stmt = (
            select(FinTariffVersion)
            .where(FinTariffVersion.effective_from <= target_dt)
            .order_by(FinTariffVersion.effective_from.desc())
            .limit(1)
        )
        res = await db.execute(stmt)
        tariff = res.scalar_one_or_none()

        if tariff is None:
            # Fallback: ensure default baseline tariff exists
            tariff = await FinTariffService.ensure_default_tariff(db)
            if tariff.effective_from > target_dt:
                # If target timestamp is before default effective_from, the earliest tariff still applies
                stmt_earliest = (
                    select(FinTariffVersion)
                    .order_by(FinTariffVersion.effective_from.asc())
                    .limit(1)
                )
                res_earliest = await db.execute(stmt_earliest)
                earliest = res_earliest.scalar_one_or_none()
                if earliest:
                    return earliest
                raise FinTariffNotFoundError(
                    f"No effective tariff found for timestamp {target_dt}"
                )

        return tariff

    @staticmethod
    async def get_tariff_by_id(
        db: AsyncSession, tariff_id: int
    ) -> FinTariffVersion | None:
        """Fetch tariff version by primary key."""
        stmt = select(FinTariffVersion).where(FinTariffVersion.id == tariff_id)
        res = await db.execute(stmt)
        return res.scalar_one_or_none()

    @staticmethod
    async def create_tariff_version(
        db: AsyncSession, req: FinTariffVersionCreate
    ) -> FinTariffVersion:
        """Create a new immutable tariff version."""
        stmt = select(FinTariffVersion).where(
            (FinTariffVersion.version == req.version)
            | (FinTariffVersion.effective_from == req.effective_from)
        )
        res = await db.execute(stmt)
        conflict = res.scalar_one_or_none()
        if conflict is not None:
            raise FinValidationError(
                f"Tariff version {req.version} or effective_from {req.effective_from} already exists"
            )

        now_dt = datetime.now(UTC)
        tariff = FinTariffVersion(
            version=req.version,
            effective_from=req.effective_from,
            terminal_month_kopecks=req.terminal_month_kopecks,
            hourly_rate_kopecks=req.hourly_rate_kopecks,
            free_daily_seconds=req.free_daily_seconds,
            actor=req.actor,
            correlation_id=req.correlation_id,
            created_at=now_dt,
        )
        db.add(tariff)
        await db.flush()
        logger.info(
            "Created new tariff version %s id=%s effective from %s",
            tariff.version,
            tariff.id,
            tariff.effective_from,
        )
        return tariff

    @staticmethod
    async def list_tariffs(db: AsyncSession) -> list[FinTariffVersion]:
        """List all tariff versions ordered by effective date."""
        stmt = select(FinTariffVersion).order_by(FinTariffVersion.effective_from.asc())
        res = await db.execute(stmt)
        return list(res.scalars().all())
