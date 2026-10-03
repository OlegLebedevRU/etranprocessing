"""Technical session durations. No prices, quota, balance or ledger mutations."""

import hashlib
from datetime import UTC, datetime

from sqlalchemy import select
from sqlalchemy.dialects.postgresql import insert
from sqlalchemy.ext.asyncio import AsyncSession

from app.models_l4desk import (
    FinTariffVersion,
    FinUsageDaily,
    L4DeskAuditEvent,
    L4DeskTenantProfile,
    L4DeskTerminal,
)
from app.services.resource_time import split_interval_by_local_days


class UsageService:
    @staticmethod
    async def record_session_usage(
        db: AsyncSession,
        *,
        tenant_id: int,
        terminal_id: int,
        session_type: str,
        start_utc: datetime,
        end_utc: datetime,
        event_id: str,
        actor: str = "session_usage",
        correlation_id: str = "",
    ) -> list[FinUsageDaily]:
        if session_type not in {"video", "console"}:
            raise ValueError("Unsupported usage type")
        terminal = await db.scalar(
            select(L4DeskTerminal)
            .where(
                L4DeskTerminal.terminal_id == terminal_id,
                L4DeskTerminal.tenant_id == tenant_id,
            )
            .with_for_update()
        )
        if terminal is None:
            raise ValueError("Unknown tenant terminal")
        timezone = (
            await db.scalar(
                select(L4DeskTenantProfile.timezone).where(
                    L4DeskTenantProfile.tenant_id == tenant_id,
                )
            )
            or "UTC"
        )
        tariff = await db.scalar(
            select(FinTariffVersion).order_by(FinTariffVersion.id).limit(1)
        )
        if tariff is None:
            # The old table requires a tariff FK. This row is only a schema adapter.
            await db.execute(
                insert(FinTariffVersion)
                .values(
                    version="duration-only-v1",
                    effective_from=datetime(1970, 1, 1, tzinfo=UTC),
                    terminal_month_kopecks=0,
                    hourly_rate_kopecks=0,
                    free_daily_seconds=0,
                    actor="duration_adapter",
                    correlation_id="duration-adapter-v1",
                )
                .on_conflict_do_nothing()
            )
            tariff = await db.scalar(
                select(FinTariffVersion).order_by(FinTariffVersion.id).limit(1)
            )
            if tariff is None:
                raise RuntimeError("Missing technical usage schema adapter")
        rows: list[FinUsageDaily] = []
        for local_date, seconds in split_interval_by_local_days(
            start_utc, end_utc, timezone
        ):
            operation = hashlib.sha256(
                f"{terminal_id}:{local_date}:{event_id}".encode()
            ).hexdigest()
            replay = await db.scalar(
                select(L4DeskAuditEvent.id).where(
                    L4DeskAuditEvent.tenant_id == tenant_id,
                    L4DeskAuditEvent.event_type == "duration.recorded",
                    L4DeskAuditEvent.operation_id == operation,
                )
            )
            if replay is not None:
                continue
            row = await db.scalar(
                select(FinUsageDaily)
                .where(
                    FinUsageDaily.terminal_id == terminal_id,
                    FinUsageDaily.local_date == local_date,
                )
                .with_for_update()
            )
            if row is None:
                row = FinUsageDaily(
                    tenant_id=tenant_id,
                    terminal_id=terminal_id,
                    local_date=local_date,
                    timezone=timezone,
                    tariff_version_id=tariff.id,
                    source_seconds=0,
                    video_seconds=0,
                    console_seconds=0,
                    free_seconds=0,
                    billable_seconds=0,
                    rounded_billable_hours=0,
                    rate_kopecks=0,
                    calculated_kopecks=0,
                    posted_kopecks=0,
                    discarded_kopecks=0,
                    source_project="MenuBuilder",
                    source_event_id=event_id,
                    source_events_hash=operation,
                    actor=actor,
                    correlation_id=correlation_id or operation,
                )
                db.add(row)
            video = seconds if session_type == "video" else 0
            console = seconds if session_type == "console" else 0
            # Posted historical rows are immutable. Late duration stays in audit and is
            # included by the usage API; there is no monetary adjustment.
            if row.ledger_transaction_id is None:
                row.video_seconds += video
                row.console_seconds += console
                row.source_seconds += seconds
                row.free_seconds += (
                    seconds  # satisfy the old seconds CHECK, not a quota
                )
                row.source_events_hash = hashlib.sha256(
                    f"{row.source_events_hash}:{operation}".encode()
                ).hexdigest()
            db.add(
                L4DeskAuditEvent(
                    tenant_id=tenant_id,
                    actor=actor,
                    event_type="duration.recorded",
                    subject_type="terminal",
                    subject_id=str(terminal_id),
                    operation_id=operation,
                    correlation_id=correlation_id or operation,
                    outcome="recorded",
                    details={
                        "local_date": str(local_date),
                        "video_seconds": video,
                        "console_seconds": console,
                        "historical_posted": row.ledger_transaction_id is not None,
                    },
                )
            )
            await db.flush()
            rows.append(row)
        return rows
