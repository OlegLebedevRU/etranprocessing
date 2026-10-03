"""Run against a disposable PostgreSQL database, never a production database."""

import asyncio
import os
from datetime import UTC, datetime

import pytest
from etranprocessing_db import Base
from sqlalchemy import select
from sqlalchemy.ext.asyncio import async_sessionmaker, create_async_engine

from app.config import settings
from app.models import Org, Terminal, TerminalType
from app.models_l4desk import (
    FinLedgerTransaction,
    FinPayment,
    FinUsageDaily,
    L4DeskTenantProfile,
    L4DeskTerminal,
)
from app.services.subscription_payments import (
    SubscriptionItem,
    SubscriptionOrderRequest,
    create_order,
    quote,
    sync_order,
)
from app.services.subscriptions import check_creation_limit
from app.services.usage import UsageService
from app.services.yookassa import MockYooKassaClient, set_yookassa_client_override

pytestmark = pytest.mark.anyio


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.fixture
async def pg(monkeypatch):
    url = os.environ.get("SUBSCRIPTION_TEST_DATABASE_URL")
    if not url:
        pytest.skip("Requires a disposable PostgreSQL test database")
    from sqlalchemy.engine import make_url

    if make_url(url).database != "subscription_test":
        pytest.fail(
            "Destructive fixtures require the dedicated subscription_test database"
        )
    engine = create_async_engine(url)
    # The supplied DB must be dedicated to these tests. They never use DATABASE_URL.
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.drop_all)
        await conn.run_sync(Base.metadata.create_all)
    factory = async_sessionmaker(engine, expire_on_commit=False)
    async with factory() as db:
        db.add(
            Org(
                org_id=9911,
                org_name="Subscription fixture",
                name="Subscription fixture",
                timezone="UTC",
            )
        )
        await db.flush()
        db.add(L4DeskTenantProfile(tenant_id=9911, timezone="UTC"))
        db.add(TerminalType(id=0, name="Fixture"))
        await db.flush()
        for i in (1, 2, 3):
            db.add(
                Terminal(
                    id=i, device_id=i, sn=f"fixture-{i}", org_id=9911, is_active=True
                )
            )
        await db.flush()
        for i in (1, 2, 3):
            db.add(
                L4DeskTerminal(
                    terminal_id=i,
                    runtime_terminal_id=i,
                    tenant_id=9911,
                    ordinal=i,
                    sn=f"fixture-{i}",
                    external_terminal_id=f"term-{i}",
                    operation_id=f"fixture-{i}",
                    correlation_id="fixture",
                )
            )
        await db.commit()
    monkeypatch.setattr(settings, "yookassa_enabled", True)
    monkeypatch.setattr(settings, "yookassa_return_url_base", "https://localhost")
    client = MockYooKassaClient()
    set_yookassa_client_override(client)
    yield factory, client
    set_yookassa_client_override(None)
    await engine.dispose()


async def test_duration_replay_never_posts_money(pg):
    factory, _ = pg
    async with factory() as db:
        kwargs = {
            "tenant_id": 9911,
            "terminal_id": 1,
            "session_type": "video",
            "start_utc": datetime(2026, 10, 2, 23, 59, 30, tzinfo=UTC),
            "end_utc": datetime(2026, 10, 3, 0, 0, 30, tzinfo=UTC),
            "event_id": "duration-replay",
        }
        await UsageService.record_session_usage(db, **kwargs)
        await db.commit()
        await UsageService.record_session_usage(db, **kwargs)
        await db.commit()
        rows = list(await db.scalars(select(FinUsageDaily)))
        assert sum(r.source_seconds for r in rows) == 60
        assert len(rows) == 2
        assert all(
            r.posted_kopecks == 0 and r.ledger_transaction_id is None for r in rows
        )
        assert not list(await db.scalars(select(FinLedgerTransaction)))


async def test_success_replayed_by_two_consumers_extends_once(pg):
    factory, client = pg
    items = [SubscriptionItem(terminal_id=2, months=1)]
    async with factory() as db:
        result = await quote(db, 9911, items)
        order = await create_order(
            db,
            9911,
            SubscriptionOrderRequest(
                items=items,
                operation_id="one-subscription-order",
                quote_hash=result["quote_hash"],
            ),
            "fixture",
            "fixture@example.test",
        )
        payment = await db.get(FinPayment, order["id"])
        assert payment is not None and payment.provider_payment_id
        pid = payment.provider_payment_id
    client.set_payment_status(pid, "succeeded")
    client.payments[pid]["captured_at"] = "2026-10-03T12:00:00Z"

    async def consumer():
        async with factory() as db:
            await sync_order(db, order["id"], 9911)

    await asyncio.gather(consumer(), consumer())
    async with factory() as db:
        terminal = await db.get(L4DeskTerminal, 2)
        assert terminal is not None
        assert terminal.paid_until == datetime(2026, 11, 3, 12, tzinfo=UTC)
        assert not list(await db.scalars(select(FinLedgerTransaction)))


async def test_limit_is_enforced_on_actual_database(pg):
    factory, _ = pg
    from fastapi import HTTPException

    async with factory() as db:
        with pytest.raises(HTTPException) as caught:
            await check_creation_limit(db, 9911)
        assert caught.value.status_code == 409


async def make_order(factory, *, months=1, key="subscription-integration-order"):
    async with factory() as db:
        items = [SubscriptionItem(terminal_id=2, months=months)]
        quoted = await quote(db, 9911, items)
        request = SubscriptionOrderRequest(
            items=items, operation_id=key, quote_hash=quoted["quote_hash"]
        )
        return await create_order(
            db, 9911, request, "fixture", "fixture@example.test"
        ), request


@pytest.mark.parametrize("tamper", ["amount", "tenant", "purpose", "captured_at"])
async def test_unverified_payment_cannot_grant_access(pg, tamper):
    from fastapi import HTTPException

    factory, client = pg
    order, _ = await make_order(factory)
    async with factory() as db:
        pid = (await db.get(FinPayment, order["id"])).provider_payment_id
    client.set_payment_status(pid, "succeeded")
    data = client.payments[pid]
    data["captured_at"] = "2026-10-03T12:00:00Z"
    if tamper == "amount":
        data["amount"]["value"] = "not-an-amount"
    elif tamper == "captured_at":
        data["captured_at"] = "not-a-date"
    else:
        data["metadata"]["tenant_id" if tamper == "tenant" else "purpose"] = "wrong"
    async with factory() as db:
        with pytest.raises(HTTPException) as caught:
            await sync_order(db, order["id"], 9911)
        assert caught.value.status_code == 409
        await db.rollback()
        assert (await db.get(L4DeskTerminal, 2)).paid_until is None
        assert not list(await db.scalars(select(FinLedgerTransaction)))


async def test_timeout_reuses_order_and_preserves_receipt_snapshot(pg, monkeypatch):
    from app.services.yookassa import YooKassaNetworkError

    factory, client = pg
    client.simulate_timeout = True
    async with factory() as db:
        items = [SubscriptionItem(terminal_id=2, months=3)]
        quoted = await quote(db, 9911, items)
        request = SubscriptionOrderRequest(
            items=items,
            operation_id="uncertain-create-order",
            quote_hash=quoted["quote_hash"],
        )
        with pytest.raises(YooKassaNetworkError):
            await create_order(db, 9911, request, "fixture", "fixture@example.test")
    client.simulate_timeout = False
    original_vat = settings.yookassa_vat_code
    monkeypatch.setattr(settings, "yookassa_vat_code", 2)
    async with factory() as db:
        first = await create_order(db, 9911, request, "fixture", "fixture@example.test")
        second = await create_order(
            db, 9911, request, "fixture", "fixture@example.test"
        )
        assert first["id"] == second["id"]
        assert len(list(await db.scalars(select(FinPayment)))) == 1
        pid = (await db.get(FinPayment, first["id"])).provider_payment_id
        assert client.payments[pid]["receipt"]["items"][0]["vat_code"] == original_vat


async def test_gate_disabled_still_reconciles_existing_money(pg, monkeypatch):
    from fastapi import HTTPException

    from app.services.subscriptions import list_subscriptions

    factory, client = pg
    order, request = await make_order(factory)
    async with factory() as db:
        pid = (await db.get(FinPayment, order["id"])).provider_payment_id
    client.set_payment_status(pid, "succeeded")
    client.payments[pid]["captured_at"] = "2026-10-03T12:00:00Z"
    monkeypatch.setattr(settings, "yookassa_enabled", False)
    async with factory() as db:
        await sync_order(db, order["id"], 9911)
        assert (await db.get(L4DeskTerminal, 2)).paid_until is not None
        states = await list_subscriptions(db, 9911)
        assert states[0].state == "free" and states[0].allowed
        assert states[1].state == "payments_disabled" and not states[1].allowed
        with pytest.raises(HTTPException) as caught:
            await create_order(db, 9911, request, "fixture", "fixture@example.test")
        assert caught.value.status_code == 409


async def test_free_transfer_and_pending_delete_guard(pg):
    from fastapi import HTTPException

    from app.services.subscription_payments import check_pending_orders
    from app.services.subscriptions import list_subscriptions

    factory, _ = pg
    await make_order(factory)
    async with factory() as db:
        with pytest.raises(HTTPException) as caught:
            await check_pending_orders(db, 9911)
        assert caught.value.status_code == 409
        await db.rollback()
        first = await db.get(L4DeskTerminal, 1)
        first.deleted_at = datetime.now(UTC)
        await db.commit()
        states = await list_subscriptions(db, 9911)
        assert states[0].terminal_id == 2 and states[0].state == "free"


async def test_admin_correction_has_audit_and_does_not_activate_terminal(pg):
    from app.routers.subscriptions import SubscriptionCorrection, correct_subscription
    from app.services.subscriptions import list_subscriptions

    factory, _ = pg
    async with factory() as db:
        runtime = await db.get(Terminal, 2)
        runtime.is_active = False
        await db.commit()
        correction = SubscriptionCorrection(
            paid_until=datetime(2026, 10, 24, tzinfo=UTC),
            reason="Компенсация подтверждённого простоя",
            operation_id="correction-integration",
        )
        await correct_subscription(
            2, correction, {"is_superuser": True, "username": "admin"}, db
        )
        await correct_subscription(
            2, correction, {"is_superuser": True, "username": "admin"}, db
        )
        states = await list_subscriptions(db, 9911)
        assert states[1].state == "admin_disabled" and not states[1].allowed
        from app.models_l4desk import L4DeskAuditEvent

        rows = list(
            await db.scalars(
                select(L4DeskAuditEvent).where(
                    L4DeskAuditEvent.event_type == "subscription.corrected"
                )
            )
        )
        assert len(rows) == 1
        profile = await db.get(L4DeskTenantProfile, 9911)
        profile.timezone = "Europe/Berlin"
        await db.commit()
        assert (await list_subscriptions(db, 9911))[1].grace_until == datetime(
            2026, 10, 27, tzinfo=UTC
        )


async def test_delete_service_respects_pending_order_then_transfers_free(pg):
    from fastapi import HTTPException

    from app.services.subscriptions import list_subscriptions
    from app.services.terminal_onboarding_service import TerminalOnboardingService

    factory, client = pg
    order, _ = await make_order(factory)
    async with factory() as db:
        service = TerminalOnboardingService(db)
        with pytest.raises(HTTPException) as caught:
            await service.delete_terminal(
                user={"org_id": 9911, "role_id": 5}, terminal_id=1
            )
        assert caught.value.status_code == 409
        await db.rollback()
        pid = (await db.get(FinPayment, order["id"])).provider_payment_id
    client.set_payment_status(pid, "canceled")
    async with factory() as db:
        await sync_order(db, order["id"], 9911)
        result = await TerminalOnboardingService(db).delete_terminal(
            user={"org_id": 9911, "role_id": 5}, terminal_id=1
        )
        assert result["earliest_free_terminal_id"] == 2
        assert (await list_subscriptions(db, 9911))[0].state == "free"
        assert (await list_subscriptions(db, 9911, include_deleted=True))[
            0
        ].state == "deleted"


async def test_subscription_http_contract_with_actual_database(pg, monkeypatch):
    from httpx import ASGITransport, AsyncClient

    from app.auth import get_current_user
    from app.database import get_db
    from app.main import app

    factory, _ = pg

    async def database():
        async with factory() as db:
            yield db

    previous = app.dependency_overrides.copy()
    app.dependency_overrides[get_db] = database
    app.dependency_overrides[get_current_user] = lambda: {
        "org_id": 9911,
        "role_id": 5,
        "username": "fixture@example.test",
    }
    try:
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            summary = await client.get("/api/subscriptions")
            assert summary.status_code == 200 and len(summary.json()["items"]) == 3
            assert (
                await client.get("/api/subscriptions?org_id=9999")
            ).status_code == 403
            assert (
                await client.post(
                    "/api/subscriptions/quote",
                    json={"items": [{"terminal_id": 1, "months": 1}]},
                )
            ).status_code == 409
            assert (
                await client.post(
                    "/api/subscriptions/quote",
                    json={"items": [{"terminal_id": 2, "months": True}]},
                )
            ).status_code == 422
            quoted = (
                await client.post(
                    "/api/subscriptions/quote",
                    json={"items": [{"terminal_id": 2, "months": 3}]},
                )
            ).json()
            monkeypatch.setattr(settings, "yookassa_enabled", False)
            response = await client.post(
                "/api/subscriptions/orders",
                json={
                    "items": [{"terminal_id": 2, "months": 3}],
                    "operation_id": "http-subscription-order",
                    "quote_hash": quoted["quote_hash"],
                },
            )
            assert (
                response.status_code == 409
                and response.json()["detail"]["code"] == "payments_disabled"
            )
            assert (
                await client.post("/api/v1/finance/yookassa/webhook", json={})
            ).json() == {"status": "ignored"}
    finally:
        app.dependency_overrides.clear()
        app.dependency_overrides.update(previous)


async def test_reminder_schedule_is_idempotent_and_retry_is_bounded(pg, monkeypatch):
    from datetime import timedelta
    from unittest.mock import AsyncMock

    from app.models import User
    from app.models_l4desk import L4DeskAuditEvent, L4DeskMembership
    from app.services.subscription_notifications import notify_tenant

    factory, _ = pg
    email = AsyncMock()
    async with factory() as db:
        user = User(
            username="fixture@example.test",
            md5_password="test-only",
            org_id=9911,
            role_id=5,
        )
        db.add(user)
        await db.flush()
        db.add(
            L4DeskMembership(tenant_id=9911, user_id=user.id, role_id=5, is_owner=True)
        )
        terminal = await db.get(L4DeskTerminal, 2)
        terminal.paid_until = datetime.now(UTC) + timedelta(hours=12)
        await db.commit()
        await notify_tenant(db, 9911, email)
        await notify_tenant(db, 9911, email)
        email.send_email.assert_awaited_once()
        events = list(
            await db.scalars(
                select(L4DeskAuditEvent).where(
                    L4DeskAuditEvent.event_type == "subscription.reminder"
                )
            )
        )
        assert len(events) == 1 and events[0].outcome == "sent"
        monkeypatch.setattr(settings, "l4desk_notification_max_retries", 2)
        terminal.paid_until += timedelta(seconds=1)
        await db.commit()
        email.send_email.side_effect = RuntimeError("Test email outage")
        for _ in range(4):
            await notify_tenant(db, 9911, email)
        assert email.send_email.await_count == 3  # One success, two bounded retries.
        events = list(
            await db.scalars(
                select(L4DeskAuditEvent).where(
                    L4DeskAuditEvent.event_type == "subscription.reminder"
                )
            )
        )
        assert {event.outcome for event in events} == {"sent", "failed"}


async def test_only_addressed_blocked_terminal_session_stops(pg):
    from datetime import timedelta
    from unittest.mock import AsyncMock

    from app.models_l4desk import L4DeskRemoteSession
    from app.services.remote_session_metering import initial_metering_cursor
    from app.services.remote_session_stop import RemoteSessionStopService

    factory, _ = pg
    active_at = datetime.now(UTC) - timedelta(minutes=2)
    async with factory() as db:
        for terminal_id in (1, 2):
            db.add(
                L4DeskRemoteSession(
                    tenant_id=9911,
                    terminal_id=terminal_id,
                    operation_id=f"scope-stop-{terminal_id}",
                    correlation_id="scope-stop",
                    session_type="console",
                    state="active",
                    provider_session_id=f"provider-{terminal_id}",
                    active_at=active_at,
                    last_cursor=initial_metering_cursor(active_at),
                )
            )
        await db.commit()
        adapter = AsyncMock()
        await RemoteSessionStopService.stop_sessions_for_blocked_tenant(
            db, 9911, terminal_id=2, iot_adapter=adapter
        )
        rows = list(
            await db.scalars(
                select(L4DeskRemoteSession).order_by(L4DeskRemoteSession.terminal_id)
            )
        )
        assert rows[0].state == "active" and rows[1].state == "closed"
        adapter.stop_remote_session.assert_awaited_once()
        assert not list(await db.scalars(select(FinLedgerTransaction)))


async def test_additive_migration_keeps_existing_terminal_state(pg):
    import importlib.util
    from pathlib import Path

    from alembic.migration import MigrationContext
    from alembic.operations import Operations
    from sqlalchemy import text

    factory, _ = pg
    path = (
        Path(__file__).resolve().parents[3]
        / "ProcessingBackend/backend/alembic/versions/029_terminal_subscription.py"
    )
    if not path.exists():
        pytest.fail("Migration source must be mounted in the test environment")
    spec = importlib.util.spec_from_file_location("subscription_migration", path)
    migration = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(migration)
    async with factory() as db:
        await db.execute(text("ALTER TABLE l4desk_terminals DROP COLUMN paid_until"))
        connection = await db.connection()

        def apply(sync_connection):
            with Operations.context(MigrationContext.configure(sync_connection)):
                migration.upgrade()

        await connection.run_sync(apply)
        await db.commit()
        records = list(await db.scalars(select(L4DeskTerminal)))
        assert len(records) == 3 and all(r.paid_until is None for r in records)
        assert all(r.is_active for r in await db.scalars(select(Terminal)))


async def test_different_concurrent_orders_add_both_periods(pg):
    factory, client = pg
    first, _ = await make_order(factory, key="parallel-first-order")
    second, _ = await make_order(factory, key="parallel-second-order")
    async with factory() as db:
        for order in (first, second):
            payment = await db.get(FinPayment, order["id"])
            client.set_payment_status(payment.provider_payment_id, "succeeded")
            client.payments[payment.provider_payment_id]["captured_at"] = (
                "2026-10-03T12:00:00Z"
            )

    async def apply(order):
        async with factory() as db:
            await sync_order(db, order["id"], 9911)

    await asyncio.gather(apply(first), apply(second))
    async with factory() as db:
        assert (await db.get(L4DeskTerminal, 2)).paid_until == datetime(
            2026, 12, 3, 12, tzinfo=UTC
        )


async def test_legacy_payment_history_excludes_subscription_orders(pg):
    from app.services.financial_core.hub_service import HubService

    factory, client = pg
    order, _ = await make_order(factory, key="subscription-history-isolation")
    async with factory() as db:
        payment = await db.get(FinPayment, order["id"])
        client.set_payment_status(payment.provider_payment_id, "succeeded")
        client.payments[payment.provider_payment_id]["captured_at"] = (
            "2026-10-03T12:00:00Z"
        )
        await sync_order(db, order["id"], 9911)
        db.add(
            FinPayment(
                tenant_id=9911,
                operation_id="old-payment",
                actor="fixture",
                provider_payment_id="old-provider-payment",
                verified_at=datetime.now(UTC),
                succeeded_at=datetime.now(UTC),
                provider="yookassa",
                amount_kopecks=10000,
                status="succeeded",
                correlation_id="old-history",
            )
        )
        await db.commit()
        history = await HubService.list_payments(
            db, tenant_id=9911, payment_source="yookassa"
        )
        assert history.total == 1 and history.items[0].status == "succeeded"
        assert "old-payment" in history.items[0].details
        errors = await HubService.list_payments(
            db, tenant_id=9911, payment_source="yookassa", only_errors=True
        )
        assert (
            errors.total == 1
        )  # Only the old succeeded payment lacks its old posting.


async def test_old_financial_stop_request_cannot_stop_free_terminal(pg):
    from unittest.mock import AsyncMock

    from app.models_l4desk import L4DeskRemoteSession
    from app.services.remote_session_stop import RemoteSessionStopService

    factory, _ = pg
    async with factory() as db:
        session = L4DeskRemoteSession(
            tenant_id=9911,
            terminal_id=1,
            operation_id="historical-stop-request",
            correlation_id="historical-stop",
            session_type="console",
            state="stop_requested",
            reason="entitlement_blocked",
            provider_session_id="historical-provider",
            active_at=datetime.now(UTC),
        )
        db.add(session)
        await db.commit()
        adapter = AsyncMock()
        assert (
            await RemoteSessionStopService.process_stop_outbox(
                db, tenant_id=9911, iot_adapter=adapter
            )
            == []
        )
        assert session.state == "active" and session.reason is None
        adapter.stop_remote_session.assert_not_awaited()


async def test_posted_history_is_immutable_but_late_duration_is_reported(pg):
    from datetime import timedelta

    from app.routers.subscriptions import usage

    factory, _ = pg
    start = datetime.now(UTC).replace(hour=10, minute=0, second=0, microsecond=0)
    async with factory() as db:
        await UsageService.record_session_usage(
            db,
            tenant_id=9911,
            terminal_id=1,
            session_type="video",
            start_utc=start,
            end_utc=start + timedelta(seconds=60),
            event_id="before-switch",
        )
        row = await db.scalar(select(FinUsageDaily))
        history = FinLedgerTransaction(
            tenant_id=9911,
            operation_id="history-usage",
            kind="usage",
            status="posted",
            debit_kopecks=100,
            credit_kopecks=100,
            source_project="fixture",
            source_type="history",
            source_id="history",
            source_events_hash="history",
            actor="fixture",
            correlation_id="history",
            posted_at=start,
        )
        db.add(history)
        await db.flush()
        row.ledger_transaction_id = history.id
        row.posted_at = start
        row.rate_kopecks = 100
        row.calculated_kopecks = 100
        row.posted_kopecks = 100
        await db.commit()
        await UsageService.record_session_usage(
            db,
            tenant_id=9911,
            terminal_id=1,
            session_type="console",
            start_utc=start,
            end_utc=start + timedelta(seconds=30),
            event_id="late-duration",
        )
        await db.commit()
        assert row.source_seconds == 60 and row.posted_kopecks == 100
        result = await usage(
            start=start.date(),
            end=start.date(),
            terminal_id=1,
            user={"org_id": 9911},
            db=db,
        )
        assert result[0]["video_seconds"] == 60 and result[0]["console_seconds"] == 30
        assert len(list(await db.scalars(select(FinLedgerTransaction)))) == 1
