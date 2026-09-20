from __future__ import annotations

import hashlib
import logging
import math
from datetime import UTC, date, datetime, timedelta

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models_l4desk import FinBillingProfile, FinUsageDaily, L4DeskTenantProfile
from app.services.financial_core.accounts import FinAccountService
from app.services.financial_core.posting import FinPostingService
from app.services.financial_core.schemas import (
    FinPostingEntryRequest,
    FinPostingRequest,
)
from app.services.financial_core.tariffs import FinTariffService
from app.services.financial_core.terminals import FinTerminalService
from app.services.financial_core.timezones import resolve_timezone

logger = logging.getLogger(__name__)


def split_interval_by_local_days(
    start_utc: datetime, end_utc: datetime, tz_name: str
) -> list[tuple[date, int]]:
    """Split an interval [start_utc, end_utc) across local midnight boundaries of tz_name.

    Invariants:
    - sum(duration_seconds) == round((end_utc - start_utc).total_seconds())
    - DST safe (uses exact timezone offsets at each local midnight).
    """
    if start_utc.tzinfo is None:
        start_utc = start_utc.replace(tzinfo=UTC)
    if end_utc.tzinfo is None:
        end_utc = end_utc.replace(tzinfo=UTC)

    if start_utc >= end_utc:
        return []

    tz = resolve_timezone(tz_name)
    start_local = start_utc.astimezone(tz)
    end_local = end_utc.astimezone(tz)

    chunks: list[tuple[date, int]] = []
    curr_local = start_local

    while curr_local < end_local:
        curr_date = curr_local.date()
        next_day = curr_date + timedelta(days=1)
        next_midnight_local = datetime(
            next_day.year, next_day.month, next_day.day, 0, 0, 0, tzinfo=tz
        )
        next_boundary = min(end_local, next_midnight_local)

        slice_start_utc = curr_local.astimezone(UTC)
        slice_end_utc = next_boundary.astimezone(UTC)
        seconds = round((slice_end_utc - slice_start_utc).total_seconds())

        if seconds > 0:
            chunks.append((curr_date, seconds))

        curr_local = next_boundary

    return chunks


def calculate_daily_amounts(
    billable_seconds: int, hourly_rate_kopecks: int
) -> tuple[int, int, int, int]:
    """Calculate paid hours and kopecks according to general tariff formula.

    Normative Logic:
    5. paid_hours = ceil(billable_seconds / 3600), current rate 100 kopecks/hour.
       Округлять aggregate один раз в сутки.
    6. Общая формула будущего тарифа хранит calculated, затем
       posted = floor(calculated / 100) * 100,
       discarded = calculated - posted;
       calculated = posted + discarded, discarded не переносится и не проводится.

    Invariants:
    - calculated == posted + discarded
    - 0 <= discarded < 100
    - posted >= 0 and posted % 100 == 0
    """
    paid_hours = math.ceil(billable_seconds / 3600) if billable_seconds > 0 else 0
    calculated = paid_hours * hourly_rate_kopecks
    posted = (calculated // 100) * 100
    discarded = calculated - posted
    return paid_hours, calculated, posted, discarded


def calculate_daily_metrics(
    video_sec: int,
    console_sec: int,
    is_free: bool,
    free_daily_seconds: int,
    hourly_rate_kopecks: int,
) -> dict[str, int]:
    """Calculate source, free, billable seconds, hours, and kopecks for a single day.

    Normative Logic:
    4. Локальные сутки tenant делят active->closed intervals.
       Для free terminal: billable_seconds = max(0, console + video - 7200);
       для остальных free = 0. Console/video не пересекаются.
    """
    source_sec = video_sec + console_sec
    if is_free:
        free_sec = min(source_sec, free_daily_seconds)
        billable_sec = max(0, source_sec - free_daily_seconds)
    else:
        free_sec = 0
        billable_sec = source_sec

    paid_hours, calc, posted, discarded = calculate_daily_amounts(
        billable_sec, hourly_rate_kopecks
    )

    return {
        "source_seconds": source_sec,
        "video_seconds": video_sec,
        "console_seconds": console_sec,
        "free_seconds": free_sec,
        "billable_seconds": billable_sec,
        "rounded_billable_hours": paid_hours,
        "rate_kopecks": hourly_rate_kopecks,
        "calculated_kopecks": calc,
        "posted_kopecks": posted,
        "discarded_kopecks": discarded,
    }


class FinMeteringService:
    """Service for daily usage metering, midnight session splitting, and late-event delta corrections."""

    @staticmethod
    async def record_session_usage(
        db: AsyncSession,
        tenant_id: int,
        terminal_id: int,
        session_type: str,
        start_utc: datetime,
        end_utc: datetime,
        event_id: str,
        tz_name: str | None = None,
        actor: str = "metering_worker",
        correlation_id: str = "",
    ) -> list[FinUsageDaily]:
        """Record session usage across local calendar days with delta handling for posted days.

        Normative Logic:
        4. Локальные сутки tenant делят active->closed intervals.
           Console/video не пересекаются.
        76. FinUsageDaily - один оригинальный расчёт на естественный ключ (terminal_id, local_date).
           До posting consumer может пересчитывать строку под блокировкой.
           После posting оригинал не меняется. Delta calculation сохраняется в FinLedgerTransaction
           новой adjustment транзакцией с corrects_transaction_id.
        """
        effective_tz = tz_name
        if not effective_tz:
            tz_stmt = select(L4DeskTenantProfile.timezone).where(
                L4DeskTenantProfile.tenant_id == tenant_id
            )
            res_tz = await db.execute(tz_stmt)
            effective_tz = res_tz.scalar_one_or_none()
        if not effective_tz:
            prof_stmt = select(FinBillingProfile.anchor_timezone).where(
                FinBillingProfile.tenant_id == tenant_id
            )
            res_prof = await db.execute(prof_stmt)
            effective_tz = res_prof.scalar_one_or_none() or "UTC"

        chunks = split_interval_by_local_days(start_utc, end_utc, effective_tz)
        rows: list[FinUsageDaily] = []

        is_free = await FinTerminalService.is_terminal_free(db, tenant_id, terminal_id)

        for local_date, sec in chunks:
            stmt = (
                select(FinUsageDaily)
                .where(
                    FinUsageDaily.terminal_id == terminal_id,
                    FinUsageDaily.local_date == local_date,
                )
                .with_for_update()
            )
            res = await db.execute(stmt)
            row = res.scalar_one_or_none()

            date_dt = datetime(
                local_date.year, local_date.month, local_date.day, 12, 0, 0, tzinfo=UTC
            )
            tariff = await FinTariffService.get_effective_tariff(db, date_dt)

            if row is None:
                # First event for this day: create open usage row
                v_sec = sec if session_type == "video" else 0
                c_sec = sec if session_type == "console" else 0
                metrics = calculate_daily_metrics(
                    v_sec,
                    c_sec,
                    is_free,
                    tariff.free_daily_seconds,
                    tariff.hourly_rate_kopecks,
                )
                source_hash = hashlib.sha256(
                    f"{terminal_id}:{local_date}:{event_id}".encode()
                ).hexdigest()

                new_row = FinUsageDaily(
                    tenant_id=tenant_id,
                    terminal_id=terminal_id,
                    local_date=local_date,
                    timezone=effective_tz,
                    tariff_version_id=tariff.id,
                    source_seconds=metrics["source_seconds"],
                    video_seconds=metrics["video_seconds"],
                    console_seconds=metrics["console_seconds"],
                    free_seconds=metrics["free_seconds"],
                    billable_seconds=metrics["billable_seconds"],
                    rounded_billable_hours=metrics["rounded_billable_hours"],
                    rate_kopecks=metrics["rate_kopecks"],
                    calculated_kopecks=metrics["calculated_kopecks"],
                    posted_kopecks=metrics["posted_kopecks"],
                    discarded_kopecks=metrics["discarded_kopecks"],
                    source_project="MenuBuilder",
                    source_event_id=event_id,
                    source_events_hash=source_hash,
                    archive_batch_id=None,
                    ledger_transaction_id=None,
                    actor=actor,
                    correlation_id=correlation_id
                    or f"usage-{terminal_id}-{local_date}",
                    posted_at=None,
                )
                db.add(new_row)
                await db.flush()
                rows.append(new_row)

            elif row.ledger_transaction_id is None:
                # Row exists and is OPEN (not yet posted to ledger): update under lock
                v_sec = row.video_seconds + (sec if session_type == "video" else 0)
                c_sec = row.console_seconds + (sec if session_type == "console" else 0)
                metrics = calculate_daily_metrics(
                    v_sec,
                    c_sec,
                    is_free,
                    tariff.free_daily_seconds,
                    tariff.hourly_rate_kopecks,
                )

                row.source_seconds = metrics["source_seconds"]
                row.video_seconds = metrics["video_seconds"]
                row.console_seconds = metrics["console_seconds"]
                row.free_seconds = metrics["free_seconds"]
                row.billable_seconds = metrics["billable_seconds"]
                row.rounded_billable_hours = metrics["rounded_billable_hours"]
                row.rate_kopecks = metrics["rate_kopecks"]
                row.calculated_kopecks = metrics["calculated_kopecks"]
                row.posted_kopecks = metrics["posted_kopecks"]
                row.discarded_kopecks = metrics["discarded_kopecks"]
                row.source_events_hash = hashlib.sha256(
                    f"{row.source_events_hash}:{event_id}".encode()
                ).hexdigest()

                await db.flush()
                rows.append(row)

            else:
                # Row is ALREADY POSTED: original row is IMMUTABLE!
                # Calculate delta and post an adjustment transaction
                new_v = row.video_seconds + (sec if session_type == "video" else 0)
                new_c = row.console_seconds + (sec if session_type == "console" else 0)
                new_metrics = calculate_daily_metrics(
                    new_v,
                    new_c,
                    is_free,
                    tariff.free_daily_seconds,
                    tariff.hourly_rate_kopecks,
                )
                delta_posted = new_metrics["posted_kopecks"] - row.posted_kopecks

                if delta_posted != 0:
                    (
                        settlement_acc,
                        _,
                    ) = await FinAccountService.ensure_tenant_settlement_account(
                        db, tenant_id
                    )
                    revenue_acc = await FinAccountService.get_system_account(
                        db, "usage_revenue"
                    )
                    if revenue_acc is None:
                        sys_accs = await FinAccountService.ensure_system_accounts(db)
                        revenue_acc = sys_accs["usage_revenue"]

                    adj_hash = hashlib.sha256(
                        f"adj:{row.id}:{event_id}:{delta_posted}".encode()
                    ).hexdigest()
                    snapshot = {
                        "original_usage_id": row.id,
                        "terminal_id": terminal_id,
                        "local_date": str(local_date),
                        "old_seconds": row.source_seconds,
                        "new_seconds": new_metrics["source_seconds"],
                        "old_posted_kopecks": row.posted_kopecks,
                        "new_posted_kopecks": new_metrics["posted_kopecks"],
                        "delta_kopecks": delta_posted,
                        "late_event_id": event_id,
                        "tariff_version_id": tariff.id,
                    }

                    if delta_posted > 0:
                        entries = [
                            FinPostingEntryRequest(
                                account_id=settlement_acc.id,
                                debit_kopecks=delta_posted,
                                credit_kopecks=0,
                            ),
                            FinPostingEntryRequest(
                                account_id=revenue_acc.id,
                                debit_kopecks=0,
                                credit_kopecks=delta_posted,
                            ),
                        ]
                    else:
                        refund_amount = abs(delta_posted)
                        entries = [
                            FinPostingEntryRequest(
                                account_id=revenue_acc.id,
                                debit_kopecks=refund_amount,
                                credit_kopecks=0,
                            ),
                            FinPostingEntryRequest(
                                account_id=settlement_acc.id,
                                debit_kopecks=0,
                                credit_kopecks=refund_amount,
                            ),
                        ]

                    adj_req = FinPostingRequest(
                        tenant_id=tenant_id,
                        operation_id=f"op-usage-adj-{row.id}-{event_id[:16]}",
                        kind="adjustment",
                        source_project="MenuBuilder",
                        source_type="daily_adjustment",
                        source_id=f"daily-{terminal_id}-{local_date.isoformat()}-adj-{event_id[:16]}",
                        source_event_id=event_id,
                        source_events_hash=adj_hash,
                        corrects_transaction_id=row.ledger_transaction_id,
                        calculation_snapshot=snapshot,
                        actor=actor,
                        correlation_id=correlation_id
                        or f"usage-adj-{terminal_id}-{local_date}",
                        entries=entries,
                    )
                    await FinPostingService.post_transaction(db, adj_req)
                    logger.info(
                        "Posted delta adjustment %s kopecks for usage_id=%s date=%s",
                        delta_posted,
                        row.id,
                        local_date,
                    )
                rows.append(row)

        return rows

    @staticmethod
    async def close_and_post_daily_usage(
        db: AsyncSession,
        tenant_id: int,
        local_date: date,
        terminal_id: int | None = None,
        actor: str = "metering_worker",
    ) -> list[FinUsageDaily]:
        """Close open daily usage rows for tenant and date, posting ledger transactions for positive amounts."""
        stmt = (
            select(FinUsageDaily)
            .where(
                FinUsageDaily.tenant_id == tenant_id,
                FinUsageDaily.local_date == local_date,
                FinUsageDaily.ledger_transaction_id.is_(None),
            )
            .with_for_update()
        )
        if terminal_id is not None:
            stmt = stmt.where(FinUsageDaily.terminal_id == terminal_id)

        res = await db.execute(stmt)
        open_rows = list(res.scalars().all())

        posted_rows: list[FinUsageDaily] = []
        settlement_acc, _ = await FinAccountService.ensure_tenant_settlement_account(
            db, tenant_id
        )
        revenue_acc = await FinAccountService.get_system_account(db, "usage_revenue")
        if revenue_acc is None:
            sys_accs = await FinAccountService.ensure_system_accounts(db)
            revenue_acc = sys_accs["usage_revenue"]

        for row in open_rows:
            if row.posted_kopecks > 0:
                post_req = FinPostingRequest(
                    tenant_id=row.tenant_id,
                    operation_id=f"op-usage-{row.terminal_id}-{row.local_date.isoformat()}",
                    kind="usage",
                    source_project="MenuBuilder",
                    source_type="daily",
                    source_id=f"daily-{row.terminal_id}-{row.local_date.isoformat()}",
                    source_event_id=row.source_event_id,
                    source_events_hash=row.source_events_hash,
                    actor=actor,
                    correlation_id=row.correlation_id,
                    entries=[
                        FinPostingEntryRequest(
                            account_id=settlement_acc.id,
                            debit_kopecks=row.posted_kopecks,
                            credit_kopecks=0,
                        ),
                        FinPostingEntryRequest(
                            account_id=revenue_acc.id,
                            debit_kopecks=0,
                            credit_kopecks=row.posted_kopecks,
                        ),
                    ],
                )
                tx = await FinPostingService.post_transaction(db, post_req)
                row.ledger_transaction_id = tx.id
                row.posted_at = tx.posted_at
            else:
                # 0 kopecks: no ledger transaction posted per contract
                pass

            await db.flush()
            posted_rows.append(row)

        return posted_rows
