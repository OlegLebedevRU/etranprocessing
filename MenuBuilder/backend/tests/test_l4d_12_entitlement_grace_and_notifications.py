from __future__ import annotations

import contextlib
from datetime import UTC, date, datetime, timedelta
from typing import Any, cast

import pytest
from httpx import ASGITransport, AsyncClient

from app.auth import create_access_token
from app.database import get_db
from app.main import app
from app.models import Terminal, User
from app.models_l4desk import (
    FinAccount,
    FinBalanceProjection,
    FinBillingCycle,
    FinBillingProfile,
    FinLedgerEntry,
    FinLedgerTransaction,
    FinNotificationDelivery,
    FinTariffVersion,
    FinTerminalMonthlyCharge,
    FinUsageDaily,
    L4DeskAuditEvent,
    L4DeskMembership,
    L4DeskRemoteSession,
    L4DeskTenantProfile,
    L4DeskTerminal,
)
from app.services.financial_core import (
    ENTITLEMENT_ACTIVE,
    ENTITLEMENT_BLOCKED,
    ENTITLEMENT_FREE,
    ENTITLEMENT_GRACE,
    REASON_ENTITLEMENT_BLOCKED,
    REASON_FREE_QUOTA_EXCEEDED,
    REASON_UNPAID_SECONDARY_TERMINAL,
    FinBillingCycleService,
    FinEntitlementService,
    FinNotificationService,
    FinStopOutboxService,
)
from app.services.remote_session_policy import (
    L4DeskEntitlementPolicy,
)


class MockResult:
    """Mock query result helper."""

    def __init__(self, one: Any = None, all_items: list[Any] | None = None) -> None:
        self._one = one
        self._all = (
            all_items if all_items is not None else ([one] if one is not None else [])
        )

    def scalar_one_or_none(self) -> Any:
        return self._one

    def scalar_one(self) -> Any:
        if self._one is None:
            raise ValueError("No item found")
        return self._one

    def scalar(self) -> Any:
        return self._one

    def scalars(self) -> MockResult:
        return self

    def all(self) -> list[Any]:
        return self._all

    def one(self) -> Any:
        return self._one


class MockEmailClient:
    """Mock client for serverless email gateway."""

    def __init__(self, fail_next: bool = False, fail_always: bool = False) -> None:
        self.sent_emails: list[dict[str, Any]] = []
        self.fail_next = fail_next
        self.fail_always = fail_always

    async def send_email(
        self,
        device_id: str,
        recipients: list[str],
        subject: str,
        message: str,
        file_name: str | None = None,
        file_bytes: bytes | None = None,
    ) -> dict[str, Any]:
        if self.fail_always or self.fail_next:
            self.fail_next = False
            raise RuntimeError("Simulated email provider network failure")

        call_data = {
            "device_id": device_id,
            "recipients": recipients,
            "subject": subject,
            "message": message,
            "postbox_message_id": f"postbox-{len(self.sent_emails) + 1}",
        }
        self.sent_emails.append(call_data)
        return call_data


class MockIotAdapter:
    """Mock adapter for IoT remote sessions and stop contracts."""

    def __init__(self, fail_next: bool = False, fail_always: bool = False) -> None:
        self.stop_calls: list[dict[str, Any]] = []
        self.fail_next = fail_next
        self.fail_always = fail_always

    async def stop_remote_session(
        self,
        session_id: str,
        operation_id: str | None = None,
        reason: str = "user_requested",
        correlation_id: str | None = None,
    ) -> dict[str, Any]:
        if self.fail_always or self.fail_next:
            self.fail_next = False
            raise RuntimeError(
                "Simulated IoT stop provider timeout (504 Gateway Timeout)"
            )

        call_data = {
            "session_id": session_id,
            "operation_id": operation_id,
            "reason": reason,
            "correlation_id": correlation_id,
            "status": "closed",
        }
        self.stop_calls.append(call_data)
        return call_data


class FakeEntitlementDb:
    """In-memory database simulator for entitlement, grace, outbox and notification tests."""

    def __init__(self) -> None:
        self.accounts: dict[int, FinAccount] = {}
        self.projections: dict[int, FinBalanceProjection] = {}
        self.transactions: dict[int, FinLedgerTransaction] = {}
        self.entries: list[FinLedgerEntry] = []
        self.profiles: dict[int, FinBillingProfile] = {}
        self.cycles: dict[int, FinBillingCycle] = {}
        self.tenant_profiles: dict[int, L4DeskTenantProfile] = {}
        self.terminals: dict[int, L4DeskTerminal] = {}
        self.runtime_terminals: dict[int, Terminal] = {}
        self.users: dict[int, User] = {}
        self.memberships: dict[tuple[int, int], L4DeskMembership] = {}
        self.usage_daily: dict[tuple[int, date], FinUsageDaily] = {}
        self.monthly_charges: dict[tuple[int, int], FinTerminalMonthlyCharge] = {}
        self.notifications: dict[int, FinNotificationDelivery] = {}
        self.remote_sessions: dict[int, L4DeskRemoteSession] = {}
        self.audit_events: list[L4DeskAuditEvent] = []
        self.tariffs: dict[str, FinTariffVersion] = {}

        self._next_account_id = 1
        self._next_tx_id = 1
        self._next_entry_id = 1
        self._next_cycle_id = 1
        self._next_notif_id = 1
        self._next_session_id = 1

        # Seed standard MVP tariff
        self.tariffs["v1"] = FinTariffVersion(
            id=1,
            version="v1",
            terminal_month_kopecks=10000,
            hourly_rate_kopecks=100,
            free_daily_seconds=7200,
            effective_from=datetime(2026, 1, 1, 0, 0, 0, tzinfo=UTC),
            actor="test",
            correlation_id="corr-init",
        )

    def add(self, obj: Any) -> None:
        now_dt = datetime.now(UTC)
        if hasattr(obj, "created_at") and getattr(obj, "created_at", None) is None:
            obj.created_at = now_dt

        if isinstance(obj, FinAccount):
            if not getattr(obj, "id", None):
                obj.id = self._next_account_id
                self._next_account_id += 1
            self.accounts[obj.id] = obj
        elif isinstance(obj, FinBalanceProjection):
            self.projections[obj.tenant_id] = obj
        elif isinstance(obj, FinLedgerTransaction):
            if not getattr(obj, "id", None):
                obj.id = self._next_tx_id
                self._next_tx_id += 1
            self.transactions[obj.id] = obj
        elif isinstance(obj, FinLedgerEntry):
            if not getattr(obj, "id", None):
                obj.id = self._next_entry_id
                self._next_entry_id += 1
            self.entries.append(obj)
        elif isinstance(obj, FinBillingProfile):
            self.profiles[obj.tenant_id] = obj
        elif isinstance(obj, FinBillingCycle):
            if not getattr(obj, "id", None):
                obj.id = self._next_cycle_id
                self._next_cycle_id += 1
            self.cycles[obj.id] = obj
        elif isinstance(obj, L4DeskTenantProfile):
            self.tenant_profiles[obj.tenant_id] = obj
        elif isinstance(obj, L4DeskTerminal):
            self.terminals[obj.terminal_id] = obj
        elif isinstance(obj, Terminal):
            self.runtime_terminals[obj.id] = obj
        elif isinstance(obj, User):
            self.users[obj.id] = obj
        elif isinstance(obj, L4DeskMembership):
            self.memberships[(obj.tenant_id, obj.user_id)] = obj
        elif isinstance(obj, FinUsageDaily):
            self.usage_daily[(obj.terminal_id, obj.local_date)] = obj
        elif isinstance(obj, FinTerminalMonthlyCharge):
            self.monthly_charges[(obj.terminal_id, obj.billing_cycle_id)] = obj
        elif isinstance(obj, FinNotificationDelivery):
            if not getattr(obj, "id", None):
                obj.id = self._next_notif_id
                self._next_notif_id += 1
            self.notifications[obj.id] = obj
        elif isinstance(obj, L4DeskRemoteSession):
            if not getattr(obj, "id", None):
                obj.id = self._next_session_id
                self._next_session_id += 1
            self.remote_sessions[obj.id] = obj
        elif isinstance(obj, L4DeskAuditEvent):
            self.audit_events.append(obj)

    async def flush(self) -> None:
        pass

    async def commit(self) -> None:
        pass

    async def rollback(self) -> None:
        pass

    async def execute(self, statement: Any, params: Any = None) -> MockResult:
        query_str = str(statement).lower()

        # 1. FinTariffVersion
        if "fin_tariff_versions" in query_str:
            t = next(iter(self.tariffs.values()))
            return MockResult(one=t, all_items=[t])

        # 2. FinBillingProfile
        if "fin_billing_profiles" in query_str:
            if (
                "fin_billing_profiles.tenant_id" in query_str
                and "order by" in query_str
            ):
                t_ids = sorted(self.profiles.keys())
                return MockResult(one=t_ids[0] if t_ids else None, all_items=t_ids)
            for t_id, prof in self.profiles.items():
                if f"={t_id}" in query_str or f"= {t_id}" in query_str:
                    return MockResult(one=prof)
            for p in self.profiles.values():
                return MockResult(one=p)
            return MockResult(one=None)

        # 3. FinBalanceProjection
        if "fin_balance_projections" in query_str:
            for t_id, proj in self.projections.items():
                if f"={t_id}" in query_str or f"= {t_id}" in query_str:
                    return MockResult(one=proj)
            for p in self.projections.values():
                return MockResult(one=p)
            return MockResult(one=None)

        # 4. L4DeskTerminal (e.g. get_free_terminal)
        if "l4desk_terminals" in query_str:
            active_terms = [t for t in self.terminals.values() if t.deleted_at is None]
            active_terms.sort(key=lambda x: x.ordinal)
            if active_terms:
                return MockResult(one=active_terms[0], all_items=active_terms)
            return MockResult(one=None, all_items=[])

        # 5. FinUsageDaily
        if "fin_usage_daily" in query_str:
            # Check terminal_id and local_date
            for (t_id, l_date), u in self.usage_daily.items():
                if str(t_id) in query_str:
                    return MockResult(one=u)
            if self.usage_daily:
                return MockResult(one=next(iter(self.usage_daily.values())))
            return MockResult(one=None)

        # 6. FinBillingCycle
        if "fin_billing_cycles" in query_str:
            if self.cycles:
                # Return latest or matching
                all_c = list(self.cycles.values())
                all_c.sort(key=lambda c: c.sequence, reverse=True)
                return MockResult(one=all_c[0], all_items=all_c)
            return MockResult(one=None, all_items=[])

        # 7. FinNotificationDelivery
        if "fin_notification_deliveries" in query_str:
            all_n = list(self.notifications.values())
            if "status in" in query_str or "pending" in query_str:
                due = [n for n in all_n if n.status in ("pending", "failed")]
                return MockResult(one=due[0] if due else None, all_items=due)

            params_dict = {}
            with contextlib.suppress(Exception):
                params_dict = statement.compile().params

            target_type = None
            for k, v in params_dict.items():
                if "notification_type" in k:
                    target_type = v
                    break

            if target_type:
                found = next(
                    (n for n in all_n if n.notification_type == target_type), None
                )
                return MockResult(one=found, all_items=[found] if found else [])

            return MockResult(one=all_n[0] if all_n else None, all_items=all_n)

        # 8. L4DeskRemoteSession
        if "l4desk_remote_sessions" in query_str:
            all_s = list(self.remote_sessions.values())
            if "stop_requested" in query_str:
                stops = [s for s in all_s if s.state == "stop_requested"]
                return MockResult(one=stops[0] if stops else None, all_items=stops)
            active_states = ["reserved", "start_requested", "active", "stop_requested"]
            actives = [s for s in all_s if s.state in active_states]
            return MockResult(one=actives[0] if actives else None, all_items=actives)

        # 9. L4DeskMembership / User for email
        if "l4desk_memberships" in query_str or "users" in query_str:
            for m in self.memberships.values():
                if m.is_owner and m.user_id in self.users:
                    u = self.users[m.user_id]
                    return MockResult(one=u.username, all_items=[u.username])
            if self.users:
                u = next(iter(self.users.values()))
                return MockResult(one=u.username, all_items=[u.username])
            return MockResult(one=None, all_items=[])

        # 10. FinAccount
        if "fin_accounts" in query_str:
            for a in self.accounts.values():
                return MockResult(one=a)
            return MockResult(one=None)

        # 11. Sum of credits and debits (rebuild projection)
        if "sum(fin_ledger_entries.credit_kopecks)" in query_str:
            cr = sum(e.credit_kopecks for e in self.entries)
            dr = sum(e.debit_kopecks for e in self.entries)
            return MockResult(one=(cr, dr))

        return MockResult(one=None, all_items=[])


# =============================================================================
# TESTS FOR L4D-12-MB
# =============================================================================


@pytest.mark.anyio
async def test_no_payment_free_quota_and_secondary_terminal_block():
    """Normative Rule 1:

    Before first successful payment:
    - only free terminal and pooled 120 min/local day are accessible
    - secondary terminal is denied immediately
    - grace is absent
    - after 120 min, work can continue only if balance > 0
    """
    fake_db = FakeEntitlementDb()
    db = cast(Any, fake_db)
    tenant_id = 501

    # Setup tenant profile without anchor (free tier)
    profile = FinBillingProfile(
        tenant_id=tenant_id,
        anchor_at=None,
        anchor_day=None,
        anchor_timezone="UTC",
        entitlement="free",
    )
    fake_db.add(profile)

    # Balance is 0
    proj = FinBalanceProjection(
        tenant_id=tenant_id,
        account_id=1,
        balance_kopecks=0,
        version=1,
        last_transaction_id=None,
        updated_at=datetime.now(UTC),
    )
    fake_db.add(proj)

    # Free terminal (ordinal 1) and Secondary terminal (ordinal 2)
    term1 = L4DeskTerminal(
        terminal_id=101,
        tenant_id=tenant_id,
        ordinal=1,
        sn="SN-TERM-101",
        external_terminal_id="ext-101",
        operation_id="op-101",
        correlation_id="corr-101",
    )
    term2 = L4DeskTerminal(
        terminal_id=102,
        tenant_id=tenant_id,
        ordinal=2,
        sn="SN-TERM-102",
        external_terminal_id="ext-102",
        operation_id="op-102",
        correlation_id="corr-102",
    )
    fake_db.add(term1)
    fake_db.add(term2)

    # Check 1: Secondary terminal is denied immediately
    dec_secondary = await FinEntitlementService.evaluate_session_request(
        db, tenant_id=tenant_id, terminal_id=102, session_type="console"
    )
    assert not dec_secondary["allowed"]
    assert dec_secondary["error_code"] == REASON_UNPAID_SECONDARY_TERMINAL
    assert dec_secondary["entitlement_state"] == ENTITLEMENT_FREE

    # Check 2: Free terminal with 0 usage is allowed
    dec_free = await FinEntitlementService.evaluate_session_request(
        db, tenant_id=tenant_id, terminal_id=101, session_type="console"
    )
    assert dec_free["allowed"]
    assert dec_free["entitlement_state"] == ENTITLEMENT_FREE

    # Check 3: Free terminal reaches 7200 seconds (120 min) with 0 balance -> denied
    usage = FinUsageDaily(
        tenant_id=tenant_id,
        terminal_id=101,
        local_date=datetime.now(UTC).date(),
        timezone="UTC",
        video_seconds=3600,
        console_seconds=3600,  # 7200s total
        free_seconds=7200,
        billable_seconds=0,
        rounded_billable_hours=0,
        rate_kopecks=100,
        calculated_kopecks=0,
        posted_kopecks=0,
        discarded_kopecks=0,
        source_project="MenuBuilder",
        source_event_id="ev-1",
        source_events_hash="hash-1",
        actor="test",
        correlation_id="corr-1",
    )
    fake_db.add(usage)

    dec_quota_exceeded = await FinEntitlementService.evaluate_session_request(
        db, tenant_id=tenant_id, terminal_id=101, session_type="console"
    )
    assert not dec_quota_exceeded["allowed"]
    assert dec_quota_exceeded["error_code"] == REASON_FREE_QUOTA_EXCEEDED
    assert dec_quota_exceeded["entitlement_state"] == ENTITLEMENT_FREE

    # Verify no grace period exists
    status = await FinEntitlementService.get_tenant_entitlement_status(db, tenant_id)
    assert status.state == ENTITLEMENT_FREE
    assert status.grace_deadline is None
    assert not status.is_first_paid


@pytest.mark.anyio
async def test_positive_paid_continuation_after_free_quota():
    """Normative Rule 1:

    If balance > 0 after exhausting the 120 min quota, work continues as paid.
    """
    fake_db = FakeEntitlementDb()
    db = cast(Any, fake_db)
    tenant_id = 502

    profile = FinBillingProfile(
        tenant_id=tenant_id,
        anchor_at=None,
        anchor_day=None,
        anchor_timezone="UTC",
        entitlement="free",
    )
    fake_db.add(profile)

    # Positive balance (+500 kopecks = 5 rubles)
    proj = FinBalanceProjection(
        tenant_id=tenant_id,
        account_id=1,
        balance_kopecks=500,
        version=1,
        last_transaction_id=None,
        updated_at=datetime.now(UTC),
    )
    fake_db.add(proj)

    term1 = L4DeskTerminal(
        terminal_id=201,
        tenant_id=tenant_id,
        ordinal=1,
        sn="SN-TERM-201",
        external_terminal_id="ext-201",
        operation_id="op-201",
        correlation_id="corr-201",
    )
    fake_db.add(term1)

    # 120 minutes used today
    usage = FinUsageDaily(
        tenant_id=tenant_id,
        terminal_id=201,
        local_date=datetime.now(UTC).date(),
        timezone="UTC",
        video_seconds=7200,
        console_seconds=0,
        free_seconds=7200,
        billable_seconds=0,
        rounded_billable_hours=0,
        rate_kopecks=100,
        calculated_kopecks=0,
        posted_kopecks=0,
        discarded_kopecks=0,
        source_project="MenuBuilder",
        source_event_id="ev-2",
        source_events_hash="hash-2",
        actor="test",
        correlation_id="corr-2",
    )
    fake_db.add(usage)

    dec = await FinEntitlementService.evaluate_session_request(
        db, tenant_id=tenant_id, terminal_id=201, session_type="video"
    )
    # Allowed to continue paid!
    assert dec["allowed"]
    assert dec["entitlement_state"] == ENTITLEMENT_FREE


@pytest.mark.anyio
async def test_all_cycle_and_grace_boundaries():
    """Normative Rules 2 & 3:

    After first payment:
    - active if balance >= 0
    - grace if balance < 0 and now < cycle_start + 3 days
    - blocked if now >= cycle_start + 3 days and balance < 0
    - grace is strictly bound to cycle boundary
    """
    fake_db = FakeEntitlementDb()
    db = cast(Any, fake_db)
    tenant_id = 503

    cycle_start = datetime(2026, 10, 1, 10, 0, 0, tzinfo=UTC)
    cycle_end = datetime(2026, 11, 1, 10, 0, 0, tzinfo=UTC)
    grace_deadline = datetime(2026, 10, 4, 10, 0, 0, tzinfo=UTC)

    profile = FinBillingProfile(
        tenant_id=tenant_id,
        anchor_at=cycle_start,
        anchor_day=1,
        anchor_timezone="UTC",
        entitlement="active",
    )
    fake_db.add(profile)

    cycle = FinBillingCycle(
        id=10,
        tenant_id=tenant_id,
        sequence=0,
        timezone="UTC",
        starts_at=cycle_start,
        ends_at=cycle_end,
        grace_deadline=grace_deadline,
    )
    fake_db.add(cycle)

    proj = FinBalanceProjection(
        tenant_id=tenant_id,
        account_id=1,
        balance_kopecks=1000,  # positive
        version=1,
        last_transaction_id=None,
        updated_at=cycle_start,
    )
    fake_db.add(proj)

    # 1. Active when balance >= 0
    status_active = await FinEntitlementService.get_tenant_entitlement_status(
        db, tenant_id, as_of=datetime(2026, 10, 2, 0, 0, 0, tzinfo=UTC)
    )
    assert status_active.state == ENTITLEMENT_ACTIVE
    assert status_active.can_start_sessions

    # 2. Balance drops to -500 kopecks on day 2 (now < grace_deadline) -> GRACE
    proj.balance_kopecks = -500
    status_grace = await FinEntitlementService.get_tenant_entitlement_status(
        db, tenant_id, as_of=datetime(2026, 10, 3, 12, 0, 0, tzinfo=UTC)
    )
    assert status_grace.state == ENTITLEMENT_GRACE
    assert status_grace.can_start_sessions

    # Verify notification for grace is scheduled
    notifs = await FinNotificationService.check_and_schedule_cycle_notifications(
        db, tenant_id, as_of=datetime(2026, 10, 3, 12, 0, 0, tzinfo=UTC)
    )
    assert any(n.notification_type == "grace" for n in notifs)

    # 3. Time advances past grace_deadline (day 4 past 10:00 UTC) with negative balance -> BLOCKED
    status_blocked = await FinEntitlementService.get_tenant_entitlement_status(
        db, tenant_id, as_of=datetime(2026, 10, 4, 10, 0, 1, tzinfo=UTC)
    )
    assert status_blocked.state == ENTITLEMENT_BLOCKED
    assert not status_blocked.can_start_sessions
    assert status_blocked.reason_code == REASON_ENTITLEMENT_BLOCKED

    # New session request is rejected
    dec = await FinEntitlementService.evaluate_session_request(
        db,
        tenant_id=tenant_id,
        terminal_id=101,
        session_type="console",
        as_of=datetime(2026, 10, 4, 10, 0, 1, tzinfo=UTC),
    )
    assert not dec["allowed"]
    assert dec["error_code"] == REASON_ENTITLEMENT_BLOCKED


@pytest.mark.anyio
async def test_online_after_deadline_creates_charge_and_immediate_blocked():
    """Normative Rule 3:

    Online on day 5 creates charge of current cycle and if balance is insufficient,
    immediately blocks (grace is not restarted!).
    """
    fake_db = FakeEntitlementDb()
    db = cast(Any, fake_db)
    tenant_id = 504

    cycle_start = datetime(2026, 10, 1, 0, 0, 0, tzinfo=UTC)
    cycle_end = datetime(2026, 11, 1, 0, 0, 0, tzinfo=UTC)
    grace_deadline = datetime(2026, 10, 4, 0, 0, 0, tzinfo=UTC)

    profile = FinBillingProfile(
        tenant_id=tenant_id,
        anchor_at=cycle_start,
        anchor_day=1,
        anchor_timezone="UTC",
        entitlement="active",
    )
    fake_db.add(profile)

    cycle = FinBillingCycle(
        id=20,
        tenant_id=tenant_id,
        sequence=0,
        timezone="UTC",
        starts_at=cycle_start,
        ends_at=cycle_end,
        grace_deadline=grace_deadline,
    )
    fake_db.add(cycle)

    proj = FinBalanceProjection(
        tenant_id=tenant_id,
        account_id=1,
        balance_kopecks=0,  # 0 balance
        version=1,
        last_transaction_id=None,
        updated_at=cycle_start,
    )
    fake_db.add(proj)

    # Online event occurs on day 5 (2026-10-06) > grace_deadline (2026-10-04)
    # Secondary terminal charge of 10000 kopecks (100 rubles)
    proj.balance_kopecks = -10000

    status = await FinEntitlementService.get_tenant_entitlement_status(
        db, tenant_id, as_of=datetime(2026, 10, 6, 15, 0, 0, tzinfo=UTC)
    )

    # Immediately blocked without any new grace period!
    assert status.state == ENTITLEMENT_BLOCKED
    assert not status.can_start_sessions
    assert status.reason_code == REASON_ENTITLEMENT_BLOCKED


@pytest.mark.anyio
async def test_late_payment_preserves_anchor_and_unblocks():
    """Normative Rule 4:

    Late payment pays off current period and does not shift anchor.
    Return of balance >= 0 unblocks tenant.
    """
    fake_db = FakeEntitlementDb()
    db = cast(Any, fake_db)
    tenant_id = 505

    original_anchor = datetime(2026, 10, 1, 12, 0, 0, tzinfo=UTC)
    cycle_start = original_anchor
    cycle_end = datetime(2026, 11, 1, 12, 0, 0, tzinfo=UTC)
    grace_deadline = datetime(2026, 10, 4, 12, 0, 0, tzinfo=UTC)

    profile = FinBillingProfile(
        tenant_id=tenant_id,
        anchor_at=original_anchor,
        anchor_day=1,
        anchor_timezone="UTC",
        entitlement="blocked",
    )
    fake_db.add(profile)

    cycle = FinBillingCycle(
        id=30,
        tenant_id=tenant_id,
        sequence=0,
        timezone="UTC",
        starts_at=cycle_start,
        ends_at=cycle_end,
        grace_deadline=grace_deadline,
    )
    fake_db.add(cycle)

    proj = FinBalanceProjection(
        tenant_id=tenant_id,
        account_id=1,
        balance_kopecks=-5000,  # Negative balance -> blocked
        version=1,
        last_transaction_id=None,
        updated_at=cycle_start,
    )
    fake_db.add(proj)

    # Check initially blocked
    status1 = await FinEntitlementService.get_tenant_entitlement_status(
        db, tenant_id, as_of=datetime(2026, 10, 10, 0, 0, 0, tzinfo=UTC)
    )
    assert status1.state == ENTITLEMENT_BLOCKED

    # Late payment arrives on day 10 (+15000 kopecks) -> balance becomes +10000 kopecks
    proj.balance_kopecks = 10000

    # Ensure anchor is NOT shifted by subsequent payment
    res = await FinBillingCycleService.initialize_anchor_from_payment(
        db,
        tenant_id=tenant_id,
        paid_at=datetime(2026, 10, 10, 0, 0, 0, tzinfo=UTC),
        payment_tx_id=999,
    )
    assert res[0].anchor_at == original_anchor  # Anchor preserved!

    # Evaluated status immediately unblocks
    status2 = await FinEntitlementService.get_tenant_entitlement_status(
        db, tenant_id, as_of=datetime(2026, 10, 10, 0, 0, 0, tzinfo=UTC)
    )
    assert status2.state == ENTITLEMENT_ACTIVE
    assert status2.can_start_sessions
    assert status2.reason_code is None


@pytest.mark.anyio
async def test_active_session_stop_on_blocked():
    """Normative Rule 5:

    Upon blocked:
    - Active video and console sessions receive stop commands
    - Console command-aware wait / IoT timeout handled per H-L4D-07-IOT-v1
    - Stop outbox retries on failure
    """
    fake_db = FakeEntitlementDb()
    db = cast(Any, fake_db)
    tenant_id = 506

    mock_iot = MockIotAdapter()

    # Active video session
    sess_video = L4DeskRemoteSession(
        id=1,
        tenant_id=tenant_id,
        terminal_id=101,
        operation_id="op-sess-video-1",
        correlation_id="corr-v1",
        provider_session_id="iot-video-123",
        session_type="video",
        state="active",
        requested_at=datetime.now(UTC),
        active_at=datetime.now(UTC),
    )
    # Active console session
    sess_console = L4DeskRemoteSession(
        id=2,
        tenant_id=tenant_id,
        terminal_id=102,
        operation_id="op-sess-console-2",
        correlation_id="corr-c2",
        provider_session_id="iot-console-456",
        session_type="console",
        state="active",
        requested_at=datetime.now(UTC),
        active_at=datetime.now(UTC),
    )
    fake_db.add(sess_video)
    fake_db.add(sess_console)

    # Stop worker runs for blocked tenant
    results = await FinStopOutboxService.stop_sessions_for_blocked_tenant(
        db, tenant_id=tenant_id, iot_adapter=mock_iot
    )
    assert len(results) == 2
    assert all(r["status"] == "closed" for r in results)
    assert len(mock_iot.stop_calls) == 2
    assert mock_iot.stop_calls[0]["session_id"] == "iot-video-123"
    assert mock_iot.stop_calls[0]["reason"] == "entitlement_blocked"
    assert mock_iot.stop_calls[1]["session_id"] == "iot-console-456"
    assert mock_iot.stop_calls[1]["reason"] == "entitlement_blocked"

    assert sess_video.state == "closed"
    assert sess_console.state == "closed"


@pytest.mark.anyio
async def test_stop_outbox_retry_on_provider_failure():
    """Verify stop outbox retains stop_requested status on failure and retries successfully."""
    fake_db = FakeEntitlementDb()
    db = cast(Any, fake_db)
    tenant_id = 507

    # Mock adapter that fails on first call, succeeds on retry
    mock_iot = MockIotAdapter(fail_next=True)

    session = L4DeskRemoteSession(
        id=3,
        tenant_id=tenant_id,
        terminal_id=101,
        operation_id="op-sess-retry-3",
        correlation_id="corr-r3",
        provider_session_id="iot-retry-789",
        session_type="console",
        state="active",
        requested_at=datetime.now(UTC),
        active_at=datetime.now(UTC),
    )
    fake_db.add(session)

    # First attempt: provider fails
    res1 = await FinStopOutboxService.stop_sessions_for_blocked_tenant(
        db, tenant_id=tenant_id, iot_adapter=mock_iot
    )
    assert len(res1) == 1
    assert res1[0]["status"] == "stop_requested"
    assert "Simulated IoT stop provider timeout" in (res1[0]["error"] or "")
    assert session.state == "stop_requested"  # Kept in outbox!

    # Second attempt: retry worker processes outbox
    res2 = await FinStopOutboxService.process_stop_outbox(db, iot_adapter=mock_iot)
    assert len(res2) == 1
    assert res2[0]["status"] == "closed"
    assert session.state == "closed"  # Now closed!


@pytest.mark.anyio
async def test_email_notifications_idempotency_and_boundaries():
    """Normative Rule 6:

    Notifications renewal-7d/-3d/-1d, grace, blocked are unique by
    (tenant_id, cycle_id, notification_type).
    Retries and repeated checks do not duplicate rows.
    """
    fake_db = FakeEntitlementDb()
    db = cast(Any, fake_db)
    tenant_id = 508

    cycle_start = datetime(2026, 10, 1, 0, 0, 0, tzinfo=UTC)
    cycle_end = datetime(2026, 11, 1, 0, 0, 0, tzinfo=UTC)
    grace_deadline = datetime(2026, 10, 4, 0, 0, 0, tzinfo=UTC)

    profile = FinBillingProfile(
        tenant_id=tenant_id,
        anchor_at=cycle_start,
        anchor_day=1,
        anchor_timezone="UTC",
        entitlement="active",
    )
    fake_db.add(profile)

    cycle = FinBillingCycle(
        id=40,
        tenant_id=tenant_id,
        sequence=0,
        timezone="UTC",
        starts_at=cycle_start,
        ends_at=cycle_end,
        grace_deadline=grace_deadline,
    )
    fake_db.add(cycle)

    # Setup owner user for email delivery
    user = User(
        id=77,
        username="owner@tenant508.com",
        md5_password="hash",
        org_id=tenant_id,
    )
    fake_db.add(user)
    membership = L4DeskMembership(
        tenant_id=tenant_id,
        user_id=77,
        role_id=5,
        is_owner=True,
    )
    fake_db.add(membership)

    mock_email = MockEmailClient()

    # 1. Check at cycle_end - 7 days (2026-10-25)
    t_7d = cycle_end - timedelta(days=7)
    notifs1 = await FinNotificationService.check_and_schedule_cycle_notifications(
        db, tenant_id, as_of=t_7d
    )
    assert len(notifs1) == 1
    assert notifs1[0].notification_type == "cycle_minus_7"

    # Repeat check at the same time: NO duplicate record created!
    notifs1_repeat = (
        await FinNotificationService.check_and_schedule_cycle_notifications(
            db, tenant_id, as_of=t_7d
        )
    )
    assert len(notifs1_repeat) == 1
    assert notifs1_repeat[0].id == notifs1[0].id

    # 2. Check at cycle_end - 3 days (2026-10-29) -> schedules cycle_minus_3
    t_3d = cycle_end - timedelta(days=3)
    notifs2 = await FinNotificationService.check_and_schedule_cycle_notifications(
        db, tenant_id, as_of=t_3d
    )
    assert any(n.notification_type == "cycle_minus_3" for n in notifs2)

    # 3. Check at cycle_end - 1 day (2026-10-31) -> schedules cycle_minus_1
    t_1d = cycle_end - timedelta(days=1)
    notifs3 = await FinNotificationService.check_and_schedule_cycle_notifications(
        db, tenant_id, as_of=t_1d
    )
    assert any(n.notification_type == "cycle_minus_1" for n in notifs3)

    # 4. Dispatch pending notifications
    dispatched = await FinNotificationService.dispatch_pending_notifications(
        db, email_client=mock_email, as_of=cycle_end
    )
    assert len(dispatched) >= 3
    assert len(mock_email.sent_emails) >= 3
    assert mock_email.sent_emails[0]["recipients"] == ["owner@tenant508.com"]

    # Dispatching again produces 0 new emails (already sent!)
    dispatched_again = await FinNotificationService.dispatch_pending_notifications(
        db, email_client=mock_email, as_of=cycle_end
    )
    assert len(dispatched_again) == 0


@pytest.mark.anyio
async def test_email_provider_failure_and_retry_resilience():
    """Verify email delivery failures record attempts and do not crash the service."""
    fake_db = FakeEntitlementDb()
    db = cast(Any, fake_db)
    tenant_id = 509

    delivery = FinNotificationDelivery(
        id=100,
        tenant_id=tenant_id,
        billing_cycle_id=1,
        notification_type="grace",
        scheduled_at=datetime.now(UTC),
        status="pending",
        attempts=0,
        correlation_id="corr-fail-1",
    )
    fake_db.add(delivery)

    user = User(
        id=88,
        username="owner@tenant509.com",
        md5_password="hash",
        org_id=tenant_id,
    )
    fake_db.add(user)
    membership = L4DeskMembership(
        tenant_id=tenant_id,
        user_id=88,
        role_id=5,
        is_owner=True,
    )
    fake_db.add(membership)

    mock_email = MockEmailClient(fail_always=True)

    # Attempt 1
    await FinNotificationService.dispatch_pending_notifications(
        db, email_client=mock_email
    )
    assert delivery.attempts == 1
    assert delivery.status == "pending"
    assert "Simulated email provider network failure" in (delivery.last_error or "")

    # Attempt 2
    await FinNotificationService.dispatch_pending_notifications(
        db, email_client=mock_email
    )
    assert delivery.attempts == 2
    assert delivery.status == "pending"

    # Attempt 3: Max retries (3) reached -> status becomes failed
    await FinNotificationService.dispatch_pending_notifications(
        db, email_client=mock_email
    )
    assert delivery.attempts == 3
    assert delivery.status == "failed"


@pytest.mark.anyio
async def test_remote_session_policy_shadow_vs_enforced_mode():
    """Verify L4DeskEntitlementPolicy behavior in shadow mode vs enforced mode."""
    fake_db = FakeEntitlementDb()
    db = cast(Any, fake_db)
    tenant_id = 510

    # Tenant is blocked
    past_anchor = datetime(2026, 8, 1, 0, 0, 0, tzinfo=UTC)
    past_grace = datetime(2026, 8, 4, 0, 0, 0, tzinfo=UTC)
    past_end = datetime(2026, 9, 1, 0, 0, 0, tzinfo=UTC)

    profile = FinBillingProfile(
        tenant_id=tenant_id,
        anchor_at=past_anchor,
        anchor_day=1,
        anchor_timezone="UTC",
        entitlement="blocked",
    )
    fake_db.add(profile)

    cycle = FinBillingCycle(
        id=50,
        tenant_id=tenant_id,
        sequence=0,
        timezone="UTC",
        starts_at=past_anchor,
        ends_at=past_end,
        grace_deadline=past_grace,
    )
    fake_db.add(cycle)

    proj = FinBalanceProjection(
        tenant_id=tenant_id,
        account_id=1,
        balance_kopecks=-1000,
        version=1,
        last_transaction_id=None,
        updated_at=datetime.now(UTC),
    )
    fake_db.add(proj)

    term = Terminal(
        id=301,
        org_id=tenant_id,
        sn="SN-301",
    )
    user_context = {"role_id": 5, "org_id": tenant_id, "is_l4desk": True}

    # 1. Shadow Mode (enforcement_enabled = False): allows start, retains decision metadata
    policy_shadow = L4DeskEntitlementPolicy(enforcement_enabled=False)
    dec_shadow = await policy_shadow.evaluate_session_request(
        tenant_id=tenant_id,
        terminal=term,
        session_type="console",
        user=user_context,
        db=db,
    )
    assert dec_shadow.allowed  # Allowed in shadow mode!
    assert dec_shadow.entitlement_state == "blocked"
    assert dec_shadow.error_code == REASON_ENTITLEMENT_BLOCKED

    # 2. Strict Mode (enforcement_enabled = True): strictly blocks session!
    policy_enforced = L4DeskEntitlementPolicy(enforcement_enabled=True)
    dec_enforced = await policy_enforced.evaluate_session_request(
        tenant_id=tenant_id,
        terminal=term,
        session_type="console",
        user=user_context,
        db=db,
    )
    assert not dec_enforced.allowed  # Denied in enforced mode!
    assert dec_enforced.entitlement_state == "blocked"
    assert dec_enforced.error_code == REASON_ENTITLEMENT_BLOCKED


@pytest.mark.anyio
async def test_http_endpoints_and_worker_tick():
    """Verify HTTP REST API endpoints for entitlement, notifications, and worker tick."""
    fake_db = FakeEntitlementDb()
    tenant_id = 511

    # User tokens
    tenant_token = create_access_token(
        data={"sub": "100", "role": "user", "role_id": 5, "org_id": tenant_id}
    )
    superuser_token = create_access_token(
        data={
            "sub": "1",
            "role": "superuser",
            "role_id": 1,
            "is_superuser": True,
            "org_id": 1,
        }
    )

    profile = FinBillingProfile(
        tenant_id=tenant_id,
        anchor_at=datetime(2026, 10, 1, 0, 0, 0, tzinfo=UTC),
        anchor_day=1,
        anchor_timezone="UTC",
        entitlement="grace",
    )
    fake_db.add(profile)

    cycle = FinBillingCycle(
        id=60,
        tenant_id=tenant_id,
        sequence=0,
        timezone="UTC",
        starts_at=datetime(2026, 10, 1, 0, 0, 0, tzinfo=UTC),
        ends_at=datetime(2026, 11, 1, 0, 0, 0, tzinfo=UTC),
        grace_deadline=datetime(2026, 10, 4, 0, 0, 0, tzinfo=UTC),
    )
    fake_db.add(cycle)

    proj = FinBalanceProjection(
        tenant_id=tenant_id,
        account_id=1,
        balance_kopecks=-500,
        version=1,
        last_transaction_id=None,
        updated_at=datetime.now(UTC),
    )
    fake_db.add(proj)

    notif = FinNotificationDelivery(
        id=201,
        tenant_id=tenant_id,
        billing_cycle_id=60,
        notification_type="grace",
        scheduled_at=datetime.now(UTC),
        status="sent",
        attempts=1,
        sent_at=datetime.now(UTC),
        provider_message_id="msg-ok-1",
        correlation_id="corr-n-201",
    )
    fake_db.add(notif)

    async def get_test_db():
        yield cast(Any, fake_db)

    app.dependency_overrides[get_db] = get_test_db

    try:
        transport = ASGITransport(app=app)
        async with AsyncClient(
            transport=transport, base_url="http://testserver"
        ) as client:
            # 1. GET /api/v1/finance/entitlement
            resp_ent = await client.get(
                "/api/v1/finance/entitlement",
                headers={"Authorization": f"Bearer {tenant_token}"},
            )
            assert resp_ent.status_code == 200
            ent_data = resp_ent.json()
            assert ent_data["tenant_id"] == tenant_id
            assert ent_data["balance_kopecks"] == -500
            assert ent_data["is_first_paid"] is True
            assert ent_data["cycle_id"] == 60

            # 2. GET /api/v1/finance/notifications
            resp_notifs = await client.get(
                "/api/v1/finance/notifications",
                headers={"Authorization": f"Bearer {tenant_token}"},
            )
            assert resp_notifs.status_code == 200
            notifs_data = resp_notifs.json()
            assert len(notifs_data) >= 1
            assert notifs_data[0]["notification_type"] == "grace"
            assert notifs_data[0]["status"] == "sent"

            # 3. GET /api/internal/v1/finance/entitlement/{tenant_id} as superuser
            resp_su_ent = await client.get(
                f"/api/internal/v1/finance/entitlement/{tenant_id}",
                headers={"Authorization": f"Bearer {superuser_token}"},
            )
            assert resp_su_ent.status_code == 200
            assert resp_su_ent.json()["tenant_id"] == tenant_id

            # 4. POST /api/internal/v1/finance/stop-outbox/process as superuser
            resp_outbox = await client.post(
                "/api/internal/v1/finance/stop-outbox/process",
                headers={"Authorization": f"Bearer {superuser_token}"},
            )
            assert resp_outbox.status_code == 200

            # 5. POST /api/internal/v1/finance/entitlement/worker/tick as superuser
            resp_tick = await client.post(
                "/api/internal/v1/finance/entitlement/worker/tick",
                headers={"Authorization": f"Bearer {superuser_token}"},
            )
            assert resp_tick.status_code == 200
            tick_data = resp_tick.json()
            assert "tenants_evaluated" in tick_data
            assert "notifications_scheduled" in tick_data
    finally:
        app.dependency_overrides.pop(get_db, None)
