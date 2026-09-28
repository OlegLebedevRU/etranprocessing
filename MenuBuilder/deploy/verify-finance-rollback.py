"""Explicitly invoked release probe: existing test tenant, mock stop, outer rollback.

Run against the published backend image in a disposable container. Never start
application workers. Service-level commits release savepoints only; the outer
transaction is rolled back even on an assertion failure. No provider is called.
PostgreSQL sequence values may advance, but no test business rows survive.
"""

import asyncio
import json
import uuid
from datetime import UTC, datetime, timedelta
from typing import Any

from sqlalchemy import func, select, text
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import engine
from app.models_l4desk import (
    FinBalanceProjection,
    FinBillingCycle,
    FinLedgerTransaction,
    FinReconciliationRun,
    FinTerminalMonthlyCharge,
    L4DeskAuditEvent,
    L4DeskRemoteSession,
    L4DeskTerminal,
)
from app.repositories.l4desk_repository import L4DeskRepository
from app.services.financial_core.cycles import FinBillingCycleService
from app.services.financial_core.entitlement import FinEntitlementService
from app.services.financial_core.reconciliation import FinReconciliationService
from app.services.financial_core.schemas import FinReconciliationRequest
from app.services.financial_core.stop_outbox import FinStopOutboxService
from app.services.financial_core.terminals import FinTerminalService
from app.services.remote_session_metering import initial_metering_cursor

TENANT = 10000


class MockStop:
    def __init__(self, expected_epoch: str) -> None:
        self.expected_epoch = expected_epoch
        self.calls = 0

    async def stop_remote_session(self, **kwargs: Any) -> dict[str, str]:
        assert kwargs["session_id"] == self.expected_epoch
        assert kwargs["reason"] == "entitlement_blocked"
        self.calls += 1
        return {"status": "closed"}


async def snapshot(db: AsyncSession) -> tuple[int, ...]:
    balance = await db.scalar(
        select(FinBalanceProjection.balance_kopecks).where(
            FinBalanceProjection.tenant_id == TENANT
        )
    )
    assert balance is not None
    tx_count = await db.scalar(
        select(func.count())
        .select_from(FinLedgerTransaction)
        .where(FinLedgerTransaction.tenant_id == TENANT)
    )
    session_count = await db.scalar(
        select(func.count())
        .select_from(L4DeskRemoteSession)
        .where(L4DeskRemoteSession.tenant_id == TENANT)
    )
    counts = [balance, int(tx_count or 0), int(session_count or 0)]
    for model in (
        FinBillingCycle,
        FinTerminalMonthlyCharge,
        FinReconciliationRun,
        L4DeskAuditEvent,
    ):
        count = await db.scalar(
            select(func.count()).select_from(model).where(model.tenant_id == TENANT)
        )
        counts.append(int(count or 0))
    return tuple(counts)


async def main() -> None:
    started = datetime.now(UTC)
    operation = f"18e-rollback-{uuid.uuid4().hex}"
    evidence: dict[str, Any] = {"tenant_id": TENANT, "provider": "mock"}
    async with engine.connect() as connection:
        outer = await connection.begin()
        try:
            async with AsyncSession(
                bind=connection,
                expire_on_commit=False,
                join_transaction_mode="create_savepoint",
            ) as db:
                await db.execute(text("SET LOCAL lock_timeout = '3s'"))
                await db.execute(text("SET LOCAL statement_timeout = '30s'"))
                before = await snapshot(db)
                active = await db.scalar(
                    select(func.count())
                    .select_from(L4DeskRemoteSession)
                    .where(
                        L4DeskRemoteSession.tenant_id == TENANT,
                        L4DeskRemoteSession.closed_at.is_(None),
                    )
                )
                assert active == 0, "Test tenant has a live session"
                terminals = list(
                    (
                        await db.scalars(
                            select(L4DeskTerminal)
                            .where(
                                L4DeskTerminal.tenant_id == TENANT,
                                L4DeskTerminal.deleted_at.is_(None),
                            )
                            .order_by(L4DeskTerminal.ordinal)
                        )
                    ).all()
                )
                assert len(terminals) >= 2
                free_terminal, paid_terminal = terminals[:2]
                end = await db.scalar(
                    select(func.max(FinBillingCycle.ends_at)).where(
                        FinBillingCycle.tenant_id == TENANT
                    )
                )
                assert end is not None
                as_of = end + timedelta(seconds=1)
                cycle = await FinBillingCycleService.get_or_create_cycle_for_timestamp(
                    db, TENANT, as_of
                )
                assert cycle is not None
                # Local fixture only, invisible outside this rollback transaction.
                cycle.grace_deadline = as_of + timedelta(minutes=10)
                await db.flush()
                charge = await FinTerminalService.process_device_online_monthly_charge(
                    db,
                    TENANT,
                    paid_terminal.terminal_id,
                    operation,
                    as_of,
                    actor="release_rollback_probe",
                    correlation_id=operation,
                )
                assert charge is not None and charge.posted_kopecks == 10000
                repeated = (
                    await FinTerminalService.process_device_online_monthly_charge(
                        db,
                        TENANT,
                        paid_terminal.terminal_id,
                        operation,
                        as_of,
                        actor="release_rollback_probe",
                        correlation_id=operation,
                    )
                )
                assert repeated is not None and repeated.id == charge.id
                assert (await snapshot(db))[1] == before[1] + 1
                grace = await FinEntitlementService.evaluate_session_request(
                    db,
                    TENANT,
                    free_terminal.terminal_id,
                    "video",
                    as_of=cycle.grace_deadline - timedelta(seconds=1),
                )
                blocked = await FinEntitlementService.evaluate_session_request(
                    db,
                    TENANT,
                    free_terminal.terminal_id,
                    "video",
                    as_of=cycle.grace_deadline,
                )
                assert grace["allowed"] and grace["entitlement_state"] == "grace"
                assert not blocked["allowed"]
                assert blocked["error_code"] == "entitlement_blocked"
                assert (
                    await db.scalar(
                        select(func.count())
                        .select_from(L4DeskRemoteSession)
                        .where(
                            L4DeskRemoteSession.tenant_id == TENANT,
                            L4DeskRemoteSession.closed_at.is_(None),
                        )
                    )
                    == 0
                ), "A concurrent live session started"
                session = await L4DeskRepository(db).create_remote_session(
                    tenant_id=TENANT,
                    terminal_id=free_terminal.terminal_id,
                    operation_id=operation,
                    correlation_id=operation,
                    session_type="video",
                    provider_session_id=operation,
                    state="active",
                    active_at=started,
                )
                session.last_cursor = initial_metering_cursor(started)
                await db.flush()
                mock = MockStop(operation)
                stops = await FinStopOutboxService.stop_sessions_for_blocked_tenant(
                    db, TENANT, iot_adapter=mock
                )
                assert len(stops) == 1 and mock.calls == 1
                assert session.state == "closed" and session.source_events_hash
                assert outer.is_active, "Service escaped the outer transaction"
                run = await FinReconciliationService.run_reconciliation(
                    db,
                    FinReconciliationRequest(
                        tenant_id=TENANT,
                        period_start=started - timedelta(seconds=1),
                        period_end=datetime.now(UTC) + timedelta(seconds=1),
                        operation_id=operation,
                        correlation_id=operation,
                        actor="release_rollback_probe",
                        auto_rebuild_projection=False,
                    ),
                )
                assert run.status == "matched", run.details
                assert run.debit_kopecks == run.credit_kopecks == 10000
                evidence.update(
                    monthly_charge_kopecks=charge.posted_kopecks,
                    duplicate_postings=0,
                    grace_window_seconds=600,
                    grace_allowed=True,
                    boundary_refusal="entitlement_blocked",
                    mock_stop_calls=mock.calls,
                    session_state=session.state,
                    source_hash_present=bool(session.source_events_hash),
                    reconciliation=run.status,
                    debit_kopecks=run.debit_kopecks,
                    credit_kopecks=run.credit_kopecks,
                )
                await db.commit()
                assert outer.is_active
        finally:
            await outer.rollback()
        async with AsyncSession(bind=connection) as verify:
            assert await snapshot(verify) == before, "Rollback snapshot changed"
            assert (
                await verify.scalar(
                    select(func.count())
                    .select_from(L4DeskRemoteSession)
                    .where(L4DeskRemoteSession.operation_id == operation)
                )
                == 0
            )
            evidence["rollback_verified"] = True
    await engine.dispose()
    print(json.dumps(evidence))


if __name__ == "__main__":
    asyncio.run(main())
