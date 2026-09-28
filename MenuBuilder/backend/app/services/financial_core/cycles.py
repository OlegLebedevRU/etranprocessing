from __future__ import annotations

import calendar
import logging
from datetime import UTC, datetime, timedelta

from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models_l4desk import FinBillingCycle, FinBillingProfile, L4DeskTenantProfile
from app.services.financial_core.exceptions import FinValidationError
from app.services.financial_core.timezones import resolve_timezone

logger = logging.getLogger(__name__)


class FinBillingCycleService:
    """Service for individual billing cycles, anchor calculation, and grace intervals."""

    @staticmethod
    def calculate_add_months(
        anchor_dt: datetime, n_months: int, anchor_day: int, tz_name: str
    ) -> datetime:
        """Add n_months to anchor_dt in tz_name following the rule of the last existing day.

        Invariants:
        - Day 31 in Jan -> Feb 28 (or 29 in leap year), Mar 31, Apr 30.
        - Day 29 in leap Feb -> Feb 28 in non-leap year, restored in leap year.
        - Exact local time of day in tz_name is preserved and converted to UTC.
        - DST safe.
        """
        tz = resolve_timezone(tz_name)
        anchor_local = anchor_dt.astimezone(tz)

        total_months = anchor_local.year * 12 + (anchor_local.month - 1) + n_months
        target_year = total_months // 12
        target_month = (total_months % 12) + 1

        max_days = calendar.monthrange(target_year, target_month)[1]
        target_day = min(anchor_day, max_days)

        target_local = datetime(
            target_year,
            target_month,
            target_day,
            anchor_local.hour,
            anchor_local.minute,
            anchor_local.second,
            anchor_local.microsecond,
            tzinfo=tz,
        )
        return target_local.astimezone(UTC)

    @staticmethod
    def calculate_cycle_boundaries(
        anchor_dt: datetime, anchor_day: int, sequence: int, tz_name: str
    ) -> tuple[datetime, datetime, datetime]:
        """Calculate [starts_at, ends_at) and grace_deadline for cycle sequence.

        Invariants:
        - starts_at < grace_deadline < ends_at
        - grace_deadline = starts_at + 3 calendar days in tenant timezone.
        """
        starts_at = FinBillingCycleService.calculate_add_months(
            anchor_dt, sequence, anchor_day, tz_name
        )
        ends_at = FinBillingCycleService.calculate_add_months(
            anchor_dt, sequence + 1, anchor_day, tz_name
        )

        tz = resolve_timezone(tz_name)
        local_start = starts_at.astimezone(tz)
        local_grace = local_start + timedelta(days=3)
        grace_deadline = local_grace.astimezone(UTC)

        return starts_at, ends_at, grace_deadline

    @staticmethod
    async def ensure_billing_profile(
        db: AsyncSession, tenant_id: int
    ) -> FinBillingProfile:
        """Ensure a FinBillingProfile exists for the tenant."""
        stmt = select(FinBillingProfile).where(FinBillingProfile.tenant_id == tenant_id)
        res = await db.execute(stmt)
        profile = res.scalar_one_or_none()
        if profile is not None:
            return profile

        now_dt = datetime.now(UTC)
        profile = FinBillingProfile(
            tenant_id=tenant_id,
            anchor_at=None,
            anchor_day=None,
            anchor_timezone=None,
            first_payment_transaction_id=None,
            entitlement="free",
            entitlement_changed_at=now_dt,
        )
        db.add(profile)
        await db.flush()
        logger.info("Initialized billing profile for tenant=%s (free tier)", tenant_id)
        return profile

    @staticmethod
    async def initialize_anchor_from_payment(
        db: AsyncSession,
        tenant_id: int,
        payment_tx_id: int,
        paid_at: datetime,
        timezone: str | None = None,
    ) -> tuple[FinBillingProfile, FinBillingCycle]:
        """Atomically initialize anchor from the first successful payment.

        Normative Logic:
        1. Первый успешный платёж задаст anchor; месяцы — add_months(anchor,n) с правилом последнего существующего дня.
        2. Поздняя оплата или online НЕ сдвигает существующий anchor.
        """
        stmt = (
            select(FinBillingProfile)
            .where(FinBillingProfile.tenant_id == tenant_id)
            .with_for_update()
        )
        res = await db.execute(stmt)
        profile = res.scalar_one_or_none()

        if profile is None:
            profile = await FinBillingCycleService.ensure_billing_profile(db, tenant_id)
            # Re-lock
            res = await db.execute(stmt)
            profile = res.scalar_one()

        if profile.anchor_at is not None:
            logger.info(
                "Tenant %s already has anchor_at=%s, anchor not shifted",
                tenant_id,
                profile.anchor_at,
            )
            # Fetch current cycle
            cycle = await FinBillingCycleService.get_or_create_cycle_for_timestamp(
                db, tenant_id, paid_at
            )
            if cycle is None:
                stmt_c = (
                    select(FinBillingCycle)
                    .where(FinBillingCycle.tenant_id == tenant_id)
                    .order_by(FinBillingCycle.sequence.desc())
                    .limit(1)
                )
                res_c = await db.execute(stmt_c)
                cycle = res_c.scalar_one()
            return profile, cycle

        effective_tz = timezone or profile.anchor_timezone
        if not effective_tz:
            tz_stmt = select(L4DeskTenantProfile.timezone).where(
                L4DeskTenantProfile.tenant_id == tenant_id
            )
            res_tz = await db.execute(tz_stmt)
            effective_tz = res_tz.scalar_one_or_none() or "UTC"

        try:
            tz = resolve_timezone(effective_tz)
        except Exception as e:
            raise FinValidationError(
                f"Invalid timezone for anchor: {effective_tz}"
            ) from e

        local_paid = paid_at.astimezone(tz)
        anchor_day = local_paid.day

        profile.anchor_at = paid_at
        profile.anchor_day = anchor_day
        profile.anchor_timezone = effective_tz
        profile.first_payment_transaction_id = payment_tx_id
        profile.entitlement = "active"
        profile.entitlement_changed_at = paid_at

        starts_at, ends_at, grace_deadline = (
            FinBillingCycleService.calculate_cycle_boundaries(
                paid_at, anchor_day, 0, effective_tz
            )
        )

        cycle = FinBillingCycle(
            tenant_id=tenant_id,
            sequence=0,
            starts_at=starts_at,
            ends_at=ends_at,
            grace_deadline=grace_deadline,
            timezone=effective_tz,
            closed_at=None,
        )
        db.add(cycle)
        await db.flush()

        logger.info(
            "Initialized anchor for tenant=%s: anchor_at=%s, day=%s, tz=%s, cycle 0 [%s, %s)",
            tenant_id,
            paid_at,
            anchor_day,
            effective_tz,
            starts_at,
            ends_at,
        )
        return profile, cycle

    @staticmethod
    async def get_or_create_cycle_for_timestamp(
        db: AsyncSession, tenant_id: int, timestamp: datetime
    ) -> FinBillingCycle | None:
        """Find or sequentially create billing cycle for timestamp."""
        stmt_prof = select(FinBillingProfile).where(
            FinBillingProfile.tenant_id == tenant_id
        )
        res_prof = await db.execute(stmt_prof)
        profile = res_prof.scalar_one_or_none()

        if profile is None or profile.anchor_at is None or profile.anchor_day is None:
            return None

        # Check existing cycle
        stmt_cycle = select(FinBillingCycle).where(
            FinBillingCycle.tenant_id == tenant_id,
            FinBillingCycle.starts_at <= timestamp,
            FinBillingCycle.ends_at > timestamp,
        )
        res_cycle = await db.execute(stmt_cycle)
        existing = res_cycle.scalar_one_or_none()
        if existing is not None:
            return existing

        if timestamp < profile.anchor_at:
            # Event occurred before anchor, return sequence 0 cycle if exists
            stmt_zero = select(FinBillingCycle).where(
                FinBillingCycle.tenant_id == tenant_id,
                FinBillingCycle.sequence == 0,
            )
            res_zero = await db.execute(stmt_zero)
            return res_zero.scalar_one_or_none()

        anchor_dt = profile.anchor_at
        anchor_day = profile.anchor_day
        tz_name = profile.anchor_timezone or "UTC"

        # Find maximum existing sequence
        stmt_max = select(func.coalesce(func.max(FinBillingCycle.sequence), -1)).where(
            FinBillingCycle.tenant_id == tenant_id
        )
        res_max = await db.execute(stmt_max)
        max_seq = int(res_max.scalar_one())

        curr_seq = max(0, max_seq + 1)
        target_cycle: FinBillingCycle | None = None

        while True:
            s_at, e_at, g_dl = FinBillingCycleService.calculate_cycle_boundaries(
                anchor_dt, anchor_day, curr_seq, tz_name
            )

            # Check if cycle already exists
            stmt_chk = select(FinBillingCycle).where(
                FinBillingCycle.tenant_id == tenant_id,
                FinBillingCycle.sequence == curr_seq,
            )
            res_chk = await db.execute(stmt_chk)
            chk = res_chk.scalar_one_or_none()

            if chk is None:
                c = FinBillingCycle(
                    tenant_id=tenant_id,
                    sequence=curr_seq,
                    starts_at=s_at,
                    ends_at=e_at,
                    grace_deadline=g_dl,
                    timezone=tz_name,
                    closed_at=None,
                )
                db.add(c)
                await db.flush()
                if s_at <= timestamp < e_at:
                    target_cycle = c
            else:
                if chk.starts_at <= timestamp < chk.ends_at:
                    target_cycle = chk

            if s_at <= timestamp < e_at:
                break
            if s_at > timestamp:
                break
            curr_seq += 1

        return target_cycle

    @staticmethod
    async def get_current_cycle(
        db: AsyncSession, tenant_id: int, as_of: datetime | None = None
    ) -> FinBillingCycle | None:
        """Get the active billing cycle for tenant as of timestamp."""
        now_dt = as_of or datetime.now(UTC)
        return await FinBillingCycleService.get_or_create_cycle_for_timestamp(
            db, tenant_id, now_dt
        )
