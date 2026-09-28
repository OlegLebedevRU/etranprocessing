from __future__ import annotations

from datetime import UTC, date, datetime, timedelta

import pytest
from sqlalchemy import create_engine, delete, event, insert, select, update
from sqlalchemy.dialects.postgresql import JSONB
from sqlalchemy.exc import IntegrityError
from sqlalchemy.ext.compiler import compiles
from sqlalchemy.schema import CreateColumn

from etranprocessing_db import Base
from etranprocessing_db.l4desk import (
    FinAccount,
    FinArchiveBatch,
    FinBalanceProjection,
    FinBillingCycle,
    FinBillingProfile,
    FinLedgerEntry,
    FinLedgerTransaction,
    FinManualPayment,
    FinNotificationDelivery,
    FinPayment,
    FinReconciliationRun,
    FinTariffVersion,
    FinTerminalMonthlyCharge,
    FinUsageDaily,
    IotConsumerCheckpoint,
    IotEventInbox,
    IotEventQuarantine,
    L4DeskAuditEvent,
    L4DeskMembership,
    L4DeskRegistration,
    L4DeskRemoteSession,
    L4DeskTenantProfile,
    L4DeskTerminal,
)

NOW = datetime(2026, 9, 18, 0, 0, tzinfo=UTC)
HASH = "a" * 64


@compiles(JSONB, "sqlite")
def compile_legacy_jsonb(element, compiler, **kw):
    return "JSON"


@compiles(CreateColumn, "sqlite")
def compile_legacy_jsonb_default(element, compiler, **kw):
    return compiler.visit_create_column(element, **kw).replace("::jsonb", "")


@pytest.fixture
def connection():
    engine = create_engine("sqlite://")

    @event.listens_for(engine, "connect")
    def enable_foreign_keys(dbapi_connection, connection_record):
        dbapi_connection.execute("PRAGMA foreign_keys=ON")

    names = {"orgs", "users", "terminal_types", "terminals"}
    tables = [
        table
        for table in Base.metadata.sorted_tables
        if table.name in names or table.name.startswith(("fin_", "l4desk_", "iot_"))
    ]
    Base.metadata.create_all(engine, tables=tables)
    with engine.connect() as conn, conn.begin():
        for tenant in (1, 2):
            conn.execute(
                insert(Base.metadata.tables["orgs"]).values(
                    org_id=tenant, org_name=f"tenant-{tenant}", name=f"tenant-{tenant}"
                )
            )
        conn.execute(
            insert(Base.metadata.tables["users"]).values(
                id=1, username="test-user", md5_password="test-only-hash"
            )
        )
        conn.execute(
            insert(Base.metadata.tables["terminal_types"]).values(id=0, name="test")
        )
        for terminal in (1, 2):
            conn.execute(
                insert(Base.metadata.tables["terminals"]).values(
                    id=terminal,
                    device_id=terminal,
                    sn=f"SN-{terminal}",
                    org_id=terminal,
                )
            )
            conn.execute(
                insert(L4DeskTerminal).values(
                    terminal_id=terminal,
                    tenant_id=terminal,
                    runtime_terminal_id=terminal,
                    ordinal=1,
                    sn=f"SN-{terminal}",
                    external_terminal_id=f"term-{terminal}",
                    operation_id=f"provision-{terminal}",
                    correlation_id="test",
                )
            )
            conn.execute(insert(FinBillingProfile).values(tenant_id=terminal))
            conn.execute(
                insert(FinBillingCycle).values(
                    id=terminal,
                    tenant_id=terminal,
                    sequence=0,
                    starts_at=NOW,
                    ends_at=NOW + timedelta(days=30),
                    grace_deadline=NOW + timedelta(days=3),
                    timezone="Europe/Moscow",
                )
            )
        conn.execute(
            insert(FinTariffVersion).values(
                id=1,
                version="test-v1",
                effective_from=NOW,
                terminal_month_kopecks=10000,
                hourly_rate_kopecks=100,
                free_daily_seconds=7200,
                actor="test",
                correlation_id="test",
            )
        )
        yield conn
    engine.dispose()


def transaction_values(**changes):
    return {
        "id": 1,
        "tenant_id": 1,
        "operation_id": "op-1",
        "kind": "usage",
        "debit_kopecks": 100,
        "credit_kopecks": 100,
        "source_project": "MenuBuilder",
        "source_type": "daily",
        "source_id": "daily-1",
        "source_events_hash": HASH,
        "actor": "worker:test",
        "correlation_id": "trace-1",
    } | changes


def usage_values(**changes):
    return {
        "id": 1,
        "tenant_id": 1,
        "terminal_id": 1,
        "local_date": date(2026, 9, 18),
        "timezone": "Europe/Moscow",
        "tariff_version_id": 1,
        "source_seconds": 7201,
        "video_seconds": 7200,
        "console_seconds": 1,
        "free_seconds": 7200,
        "billable_seconds": 1,
        "rounded_billable_hours": 1,
        "rate_kopecks": 100,
        "calculated_kopecks": 100,
        "posted_kopecks": 100,
        "discarded_kopecks": 0,
        "source_project": "iot-rpc-rest-app",
        "source_events_hash": HASH,
        "actor": "worker:test",
        "correlation_id": "trace-1",
    } | changes


def monthly_values(**changes):
    return {
        "id": 1,
        "tenant_id": 1,
        "terminal_id": 1,
        "billing_cycle_id": 1,
        "tariff_version_id": 1,
        "is_free": True,
        "first_online_at": NOW,
        "calculated_kopecks": 0,
        "posted_kopecks": 0,
        "discarded_kopecks": 0,
        "source_project": "iot-rpc-rest-app",
        "source_event_id": "evt_first_online",
        "source_events_hash": HASH,
        "actor": "worker:test",
        "correlation_id": "trace-1",
    } | changes


def payment_values(**changes):
    return {
        "id": 1,
        "tenant_id": 1,
        "operation_id": "payment-1",
        "amount_kopecks": 100,
        "actor": "user:1",
        "correlation_id": "trace-1",
    } | changes


def session_values(**changes):
    return {
        "id": 1,
        "tenant_id": 1,
        "terminal_id": 1,
        "operation_id": "session-1",
        "correlation_id": "trace-1",
        "session_type": "console",
    } | changes


def rejects(connection, model, values):
    with pytest.raises(IntegrityError), connection.begin_nested():
        connection.execute(insert(model).values(**values))


@pytest.mark.parametrize(
    "state", ["reserved", "start_requested", "active", "stop_requested"]
)
def test_console_video_share_one_reservation(connection, state):
    connection.execute(
        insert(L4DeskRemoteSession).values(**session_values(state=state, active_at=NOW))
    )
    rejects(
        connection,
        L4DeskRemoteSession,
        session_values(id=2, operation_id="session-2", session_type="video"),
    )
    connection.execute(
        update(L4DeskRemoteSession)
        .where(L4DeskRemoteSession.id == 1)
        .values(state="closed", closed_at=NOW)
    )
    connection.execute(
        insert(L4DeskRemoteSession).values(
            **session_values(id=2, operation_id="session-2", session_type="video")
        )
    )
    rejects(
        connection,
        L4DeskRemoteSession,
        session_values(id=3, state="failed", closed_at=NOW),
    )


@pytest.mark.parametrize(
    "changes",
    [
        {"session_type": "shell"},
        {"state": "active"},
        {"state": "closed"},
        {"tenant_id": 2},
        {"last_cursor": -1},
        {"active_at": NOW, "closed_at": NOW - timedelta(seconds=1)},
    ],
)
def test_invalid_session_metadata(connection, changes):
    rejects(connection, L4DeskRemoteSession, session_values(**changes))


def test_registration_membership_profile_and_audit(connection):
    row = {
        "id": 1,
        "email_normalized": "test@example.invalid",
        "password_hash": "test-only-hash",
        "token_hash": HASH,
        "terms_version": "v1",
        "timezone": "UTC",
        "correlation_id": "test",
        "created_at": NOW,
        "expires_at": NOW + timedelta(hours=1),
    }
    connection.execute(insert(L4DeskRegistration).values(**row))
    rejects(connection, L4DeskRegistration, row | {"id": 2, "token_hash": "b" * 64})
    rejects(
        connection,
        L4DeskRegistration,
        row | {"id": 2, "email_normalized": "second@example.invalid"},
    )
    rejects(
        connection,
        L4DeskRegistration,
        row
        | {
            "id": 2,
            "email_normalized": "second@example.invalid",
            "token_hash": "b" * 64,
            "expires_at": NOW,
        },
    )
    connection.execute(
        insert(L4DeskMembership).values(tenant_id=1, user_id=1, is_owner=True)
    )
    rejects(connection, L4DeskMembership, {"tenant_id": 2, "user_id": 1, "role_id": 3})
    connection.execute(insert(L4DeskTenantProfile).values(tenant_id=1, timezone="UTC"))
    rejects(
        connection,
        L4DeskTenantProfile,
        {"tenant_id": 2, "timezone": "UTC", "pending_timezone": "Europe/Moscow"},
    )
    connection.execute(
        insert(L4DeskAuditEvent).values(
            id=1,
            tenant_id=1,
            actor="user:1",
            event_type="registered",
            subject_type="user",
            subject_id="1",
            correlation_id="test",
            outcome="success",
        )
    )


def test_iot_opaque_ids_and_event_deduplication(connection):
    row = {
        "event_id": "evt_console_started",
        "cursor": 2**40,
        "event_type": "remote_session_active",
        "occurred_at": NOW,
        "sn": "SN-1",
        "terminal_id": "term-external",
        "session_id": "sess-console-a",
        "operation_id": "op-onl-001",
    }
    connection.execute(insert(IotEventInbox).values(**row))
    rejects(connection, IotEventInbox, row)
    connection.execute(
        insert(IotEventInbox).values(
            **(
                row
                | {
                    "event_id": "evt_console_closed",
                    "event_type": "remote_session_closed",
                }
            )
        )
    )
    connection.execute(
        insert(IotConsumerCheckpoint).values(
            consumer_id="menubuilder_iot_event_consumer", last_cursor=2**40
        )
    )
    connection.execute(
        insert(IotEventQuarantine).values(
            event_id="evt_bad",
            cursor=2**40 + 1,
            error_code="invalid",
            error_detail="test",
            raw_event={},
        )
    )
    checkpoint = (
        connection.execute(select(IotConsumerCheckpoint.__table__)).mappings().one()
    )
    assert checkpoint["last_cursor"] == 2**40
    assert checkpoint["feed_name"] == "remote_session_events"


@pytest.mark.parametrize(
    "changes",
    [
        {"video_seconds": -1},
        {"source_seconds": 7200},
        {"billable_seconds": -1},
        {"posted_kopecks": 1},
        {"discarded_kopecks": 100},
        {"discarded_kopecks": -1},
        {"calculated_kopecks": 101},
        {"tenant_id": 2},
        {"rate_kopecks": -1},
        {"posted_at": NOW},
    ],
)
def test_usage_invalid_values(connection, changes):
    rejects(connection, FinUsageDaily, usage_values(**changes))


def test_usage_rounding_and_unique_day(connection):
    connection.execute(
        insert(FinUsageDaily).values(
            **usage_values(calculated_kopecks=199, discarded_kopecks=99)
        )
    )
    rejects(connection, FinUsageDaily, usage_values(id=2))
    connection.execute(
        insert(FinUsageDaily).values(
            **usage_values(
                id=2,
                local_date=date(2026, 9, 19),
                source_seconds=0,
                video_seconds=0,
                console_seconds=0,
                free_seconds=0,
                billable_seconds=0,
                rounded_billable_hours=0,
                calculated_kopecks=0,
                posted_kopecks=0,
            )
        )
    )


def test_monthly_zero_charge_and_idempotency(connection):
    connection.execute(insert(FinTerminalMonthlyCharge).values(**monthly_values()))
    rejects(connection, FinTerminalMonthlyCharge, monthly_values(id=2))
    rejects(connection, FinTerminalMonthlyCharge, monthly_values(id=3, terminal_id=2))
    rejects(
        connection, FinTerminalMonthlyCharge, monthly_values(id=3, billing_cycle_id=2)
    )


@pytest.mark.parametrize(
    "changes",
    [
        {"calculated_kopecks": 100, "posted_kopecks": 100},
        {"posted_kopecks": -100, "calculated_kopecks": -100},
        {"calculated_kopecks": 100, "discarded_kopecks": 100},
        {"posted_at": NOW},
    ],
)
def test_monthly_invalid_values(connection, changes):
    rejects(connection, FinTerminalMonthlyCharge, monthly_values(**changes))


@pytest.mark.parametrize(
    "changes",
    [
        {"debit_kopecks": 200},
        {"debit_kopecks": 99, "credit_kopecks": 99},
        {"debit_kopecks": -100, "credit_kopecks": -100},
        {"status": "posted"},
        {"kind": "reversal"},
        {"kind": "reversal", "corrects_transaction_id": 1},
    ],
)
def test_invalid_ledger_transaction(connection, changes):
    rejects(connection, FinLedgerTransaction, transaction_values(**changes))


def test_balanced_posting_and_reversal_metadata(connection):
    connection.execute(
        insert(FinAccount).values(id=1, tenant_id=1, kind="tenant_settlement")
    )
    connection.execute(insert(FinAccount).values(id=2, kind="usage_revenue"))
    connection.execute(
        insert(FinLedgerTransaction).values(
            **transaction_values(status="posted", posted_at=NOW)
        )
    )
    connection.execute(
        insert(FinLedgerEntry).values(
            id=1,
            transaction_id=1,
            tenant_id=1,
            line_number=1,
            account_id=1,
            debit_kopecks=100,
        )
    )
    connection.execute(
        insert(FinLedgerEntry).values(
            id=2,
            transaction_id=1,
            tenant_id=1,
            line_number=2,
            account_id=2,
            credit_kopecks=100,
        )
    )
    connection.execute(
        insert(FinUsageDaily).values(
            **usage_values(ledger_transaction_id=1, posted_at=NOW)
        )
    )
    connection.execute(
        insert(FinLedgerTransaction).values(
            **transaction_values(
                id=2,
                operation_id="reverse-1",
                source_type="reversal",
                source_id="reversal-1",
                kind="reversal",
                corrects_transaction_id=1,
            )
        )
    )
    rejects(connection, FinLedgerTransaction, transaction_values(id=3))
    rejects(
        connection,
        FinLedgerTransaction,
        transaction_values(id=3, operation_id="new-op"),
    )
    rejects(
        connection,
        FinLedgerTransaction,
        transaction_values(
            id=3, tenant_id=2, kind="reversal", corrects_transaction_id=1
        ),
    )
    connection.execute(
        insert(FinBalanceProjection).values(
            tenant_id=1, account_id=1, balance_kopecks=-100, last_transaction_id=1
        )
    )


@pytest.mark.parametrize(
    "changes",
    [
        {"debit_kopecks": 0, "credit_kopecks": 0},
        {"debit_kopecks": 100, "credit_kopecks": 100},
        {"debit_kopecks": 99},
        {"debit_kopecks": -100},
        {"line_number": 0},
        {"tenant_id": 2},
    ],
)
def test_invalid_ledger_entry(connection, changes):
    connection.execute(
        insert(FinAccount).values(id=1, tenant_id=1, kind="tenant_settlement")
    )
    connection.execute(insert(FinLedgerTransaction).values(**transaction_values()))
    rejects(
        connection,
        FinLedgerEntry,
        {
            "id": 1,
            "transaction_id": 1,
            "tenant_id": 1,
            "line_number": 1,
            "account_id": 1,
            "debit_kopecks": 100,
        }
        | changes,
    )


def test_account_scope_and_unique_global_account(connection):
    connection.execute(insert(FinAccount).values(id=1, kind="usage_revenue"))
    rejects(connection, FinAccount, {"id": 2, "kind": "usage_revenue"})
    rejects(connection, FinAccount, {"id": 2, "kind": "tenant_settlement"})
    rejects(connection, FinAccount, {"id": 2, "kind": "usage_revenue", "tenant_id": 1})
    connection.execute(
        insert(FinAccount).values(id=2, kind="tenant_settlement", tenant_id=1)
    )
    rejects(
        connection, FinAccount, {"id": 3, "kind": "tenant_settlement", "tenant_id": 1}
    )


@pytest.mark.parametrize(
    "changes",
    [
        {"amount_kopecks": 0},
        {"amount_kopecks": -100},
        {"amount_kopecks": 199},
        {"currency": "USD"},
        {"status": "succeeded"},
        {"ledger_transaction_id": 1},
    ],
)
def test_invalid_payment(connection, changes):
    connection.execute(insert(FinLedgerTransaction).values(**transaction_values()))
    rejects(connection, FinPayment, payment_values(**changes))


def test_payment_provider_and_operation_deduplication(connection):
    connection.execute(
        insert(FinPayment).values(**payment_values(provider_payment_id="provider-1"))
    )
    rejects(connection, FinPayment, payment_values(id=2))
    rejects(
        connection,
        FinPayment,
        payment_values(
            id=2, operation_id="payment-2", provider_payment_id="provider-1"
        ),
    )
    connection.execute(
        insert(FinPayment).values(**payment_values(id=2, operation_id="payment-2"))
    )
    connection.execute(
        insert(FinPayment).values(**payment_values(id=3, operation_id="payment-3"))
    )
    connection.execute(
        insert(FinLedgerTransaction).values(
            **transaction_values(kind="payment", status="posted", posted_at=NOW)
        )
    )
    connection.execute(
        update(FinPayment)
        .where(FinPayment.id == 1)
        .values(
            status="succeeded",
            verified_at=NOW,
            succeeded_at=NOW,
            ledger_transaction_id=1,
        )
    )


def test_manual_payment_and_anchor(connection):
    connection.execute(
        insert(FinLedgerTransaction).values(
            **transaction_values(kind="payment", status="posted", posted_at=NOW)
        )
    )
    row = {
        "id": 1,
        "tenant_id": 1,
        "operation_id": "manual-1",
        "amount_kopecks": 100,
        "received_on": NOW.date(),
        "document_number": "test-1",
        "purpose": "test",
        "payer": "Test org",
        "created_by_user_id": 1,
        "ledger_transaction_id": 1,
        "correlation_id": "test",
    }
    connection.execute(insert(FinManualPayment).values(**row))
    rejects(connection, FinManualPayment, row | {"id": 2, "operation_id": "manual-2"})
    rejects(connection, FinManualPayment, row | {"id": 2, "amount_kopecks": 99})
    connection.execute(
        update(FinBillingProfile)
        .where(FinBillingProfile.tenant_id == 1)
        .values(
            anchor_at=NOW,
            anchor_day=31,
            anchor_timezone="Europe/Moscow",
            first_payment_transaction_id=1,
            entitlement="active",
        )
    )
    with pytest.raises(IntegrityError), connection.begin_nested():
        connection.execute(
            update(FinBillingProfile)
            .where(FinBillingProfile.tenant_id == 1)
            .values(anchor_day=32)
        )
    with pytest.raises(IntegrityError), connection.begin_nested():
        connection.execute(
            update(FinBillingProfile)
            .where(FinBillingProfile.tenant_id == 2)
            .values(entitlement="grace")
        )


def test_notification_idempotency_and_retry(connection):
    row = {
        "id": 1,
        "tenant_id": 1,
        "billing_cycle_id": 1,
        "notification_type": "cycle_minus_7",
        "scheduled_at": NOW,
        "correlation_id": "test",
    }
    connection.execute(insert(FinNotificationDelivery).values(**row))
    rejects(connection, FinNotificationDelivery, row | {"id": 2})
    rejects(connection, FinNotificationDelivery, row | {"id": 2, "tenant_id": 2})
    rejects(
        connection,
        FinNotificationDelivery,
        row | {"id": 2, "notification_type": "blocked", "attempts": -1},
    )
    connection.execute(
        update(FinNotificationDelivery)
        .where(FinNotificationDelivery.id == 1)
        .values(status="sent", attempts=1, sent_at=NOW)
    )
    connection.execute(
        insert(FinNotificationDelivery).values(
            **(row | {"id": 2, "notification_type": "blocked"})
        )
    )


def test_reconciliation_preserves_mismatches(connection):
    row = {
        "id": 1,
        "operation_id": "reconcile-1",
        "period_start": NOW,
        "period_end": NOW + timedelta(days=1),
        "actor": "worker:test",
        "correlation_id": "test",
    }
    rejects(connection, FinReconciliationRun, row | {"status": "matched"})
    connection.execute(
        insert(FinReconciliationRun).values(
            **(
                row
                | {
                    "status": "mismatch",
                    "mismatch_count": 1,
                    "debit_kopecks": 100,
                    "credit_kopecks": 200,
                }
            )
        )
    )
    connection.execute(
        insert(FinReconciliationRun).values(
            **(
                row
                | {
                    "id": 2,
                    "operation_id": "reconcile-2",
                    "status": "matched",
                    "mismatch_count": 0,
                    "calculated_kopecks": 199,
                    "posted_kopecks": 100,
                    "discarded_kopecks": 99,
                    "debit_kopecks": 100,
                    "credit_kopecks": 100,
                    "balance_difference_kopecks": 0,
                    "finished_at": NOW,
                }
            )
        )
    )


def test_archive_cursor_guard_and_multi_project_manifest(connection):
    row = {
        "id": "batch-1",
        "source_project": "iot-rpc-rest-app",
        "schema_version": "draft-v1",
        "archive_month": date(2026, 1, 1),
        "source_types": ["session_events"],
        "row_count": 1,
        "through_cursor": 100,
        "storage_reference": "test/batch-1",
        "checksum_sha256": HASH,
        "manifest": {},
        "retain_until": NOW + timedelta(days=1100),
        "actor": "worker:test",
        "correlation_id": "test",
    }
    rejects(connection, FinArchiveBatch, row | {"status": "verified"})
    rejects(
        connection,
        FinArchiveBatch,
        row
        | {
            "status": "verified",
            "verified_at": NOW,
            "purged_at": NOW,
            "consumers_passed_cursor": 99,
        },
    )
    rejects(
        connection,
        FinArchiveBatch,
        row | {"status": "verified", "verified_at": NOW, "purged_at": NOW},
    )
    connection.execute(
        insert(FinArchiveBatch).values(
            **(
                row
                | {
                    "status": "verified",
                    "verified_at": NOW,
                    "purged_at": NOW,
                    "consumers_passed_cursor": 100,
                }
            )
        )
    )
    connection.execute(
        insert(FinArchiveBatch).values(
            **(row | {"source_project": "l4media", "through_cursor": None})
        )
    )


def test_financial_history_survives_runtime_terminal_deletion(connection):
    connection.execute(insert(FinUsageDaily).values(**usage_values()))
    connection.execute(
        Base.metadata.tables["terminals"]
        .delete()
        .where(Base.metadata.tables["terminals"].c.id == 1)
    )
    snapshot = (
        connection.execute(
            select(L4DeskTerminal.__table__).where(L4DeskTerminal.terminal_id == 1)
        )
        .mappings()
        .one()
    )
    assert snapshot["runtime_terminal_id"] is None
    assert snapshot["terminal_id"] == 1
    assert connection.execute(select(FinUsageDaily.terminal_id)).scalar_one() == 1
    with pytest.raises(IntegrityError), connection.begin_nested():
        connection.execute(
            delete(L4DeskTerminal).where(L4DeskTerminal.terminal_id == 1)
        )
