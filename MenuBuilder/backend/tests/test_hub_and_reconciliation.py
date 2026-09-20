from __future__ import annotations

import contextlib
from datetime import UTC, datetime, timedelta
from typing import Any, cast

import pytest
from etranprocessing_db.models.org import Org
from httpx import ASGITransport, AsyncClient

from app.auth import create_access_token
from app.database import get_db
from app.main import app
from app.models_l4desk import (
    FinAccount,
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
    L4DeskAuditEvent,
    L4DeskRegistration,
    L4DeskRemoteSession,
    L4DeskTerminal,
)
from app.services.financial_core import (
    FinAccountService,
    FinReconciliationRequest,
    FinReconciliationService,
)
from app.services.financial_core.hub_schemas import (
    HubManualPaymentCreateRequest,
    HubManualPaymentStornoRequest,
)
from app.services.financial_core.hub_service import HubService


def _get_param(params: dict[str, Any], prefix: str) -> Any:
    for k, v in params.items():
        if k == prefix or k.startswith(f"{prefix}_"):
            return v
    return None


class MockResult:
    def __init__(self, one: Any = None, all_items: list[Any] | None = None) -> None:
        self._one = one
        self._all = (
            all_items if all_items is not None else ([one] if one is not None else [])
        )

    def scalars(self) -> MockResult:
        return self

    def scalar(self) -> Any:
        return self._one

    def scalar_one_or_none(self) -> Any:
        return self._one

    def scalar_one(self) -> Any:
        if self._one is not None:
            return self._one
        if self._all:
            return self._all[0]
        return None

    def first(self) -> Any:
        return self._all[0] if self._all else None

    def one(self) -> Any:
        if self._one is not None:
            return self._one
        if self._all:
            return self._all[0]
        return None

    def all(self) -> list[Any]:
        return list(self._all)


class FakeHubDb:
    """High-fidelity in-memory database simulator for Hub and Reconciliation tests."""

    def __init__(self) -> None:
        self.accounts: dict[int, FinAccount] = {}
        self.projections: dict[int, FinBalanceProjection] = {}
        self.transactions: dict[int, FinLedgerTransaction] = {}
        self.entries: list[FinLedgerEntry] = []
        self.tariffs: dict[int, FinTariffVersion] = {}
        self.profiles: dict[int, FinBillingProfile] = {}
        self.cycles: dict[int, FinBillingCycle] = {}
        self.terminals: dict[int, L4DeskTerminal] = {}
        self.usage_daily: dict[int, FinUsageDaily] = {}
        self.monthly_charges: dict[int, FinTerminalMonthlyCharge] = {}
        self.payments: dict[int, FinPayment] = {}
        self.manual_payments: dict[int, FinManualPayment] = {}
        self.notifications: dict[int, FinNotificationDelivery] = {}
        self.audit_events: dict[int, L4DeskAuditEvent] = {}
        self.registrations: dict[int, L4DeskRegistration] = {}
        self.reconciliations: dict[int, FinReconciliationRun] = {}
        self.orgs: dict[int, Org] = {}
        self.sessions: dict[int, L4DeskRemoteSession] = {}

        self._next_id = 100

    def add(self, obj: Any) -> None:
        now_dt = datetime.now(UTC)
        if hasattr(obj, "created_at") and getattr(obj, "created_at", None) is None:
            obj.created_at = now_dt

        if isinstance(obj, FinAccount):
            if not getattr(obj, "id", None):
                obj.id = self._next_id
                self._next_id += 1
            self.accounts[obj.id] = obj
        elif isinstance(obj, FinBalanceProjection):
            self.projections[obj.tenant_id] = obj
        elif isinstance(obj, FinLedgerTransaction):
            if not getattr(obj, "id", None):
                obj.id = self._next_id
                self._next_id += 1
            self.transactions[obj.id] = obj
        elif isinstance(obj, FinLedgerEntry):
            if not getattr(obj, "id", None):
                obj.id = self._next_id
                self._next_id += 1
            self.entries.append(obj)
        elif isinstance(obj, FinTariffVersion):
            if not getattr(obj, "id", None):
                obj.id = self._next_id
                self._next_id += 1
            self.tariffs[obj.id] = obj
        elif isinstance(obj, FinBillingProfile):
            self.profiles[obj.tenant_id] = obj
        elif isinstance(obj, FinBillingCycle):
            if not getattr(obj, "id", None):
                obj.id = self._next_id
                self._next_id += 1
            self.cycles[obj.id] = obj
        elif isinstance(obj, L4DeskTerminal):
            if not getattr(obj, "terminal_id", None):
                obj.terminal_id = self._next_id
                self._next_id += 1
            self.terminals[obj.terminal_id] = obj
        elif isinstance(obj, FinUsageDaily):
            if not getattr(obj, "id", None):
                obj.id = self._next_id
                self._next_id += 1
            self.usage_daily[obj.id] = obj
        elif isinstance(obj, FinTerminalMonthlyCharge):
            if not getattr(obj, "id", None):
                obj.id = self._next_id
                self._next_id += 1
            self.monthly_charges[obj.id] = obj
        elif isinstance(obj, FinPayment):
            if not getattr(obj, "id", None):
                obj.id = self._next_id
                self._next_id += 1
            self.payments[obj.id] = obj
        elif isinstance(obj, FinManualPayment):
            if not getattr(obj, "id", None):
                obj.id = self._next_id
                self._next_id += 1
            self.manual_payments[obj.id] = obj
        elif isinstance(obj, FinNotificationDelivery):
            if not getattr(obj, "id", None):
                obj.id = self._next_id
                self._next_id += 1
            self.notifications[obj.id] = obj
        elif isinstance(obj, L4DeskAuditEvent):
            if not getattr(obj, "id", None):
                obj.id = self._next_id
                self._next_id += 1
            self.audit_events[obj.id] = obj
        elif isinstance(obj, L4DeskRegistration):
            if not getattr(obj, "id", None):
                obj.id = self._next_id
                self._next_id += 1
            self.registrations[obj.id] = obj
        elif isinstance(obj, FinReconciliationRun):
            if not getattr(obj, "id", None):
                obj.id = self._next_id
                self._next_id += 1
            self.reconciliations[obj.id] = obj
        elif isinstance(obj, Org):
            self.orgs[obj.org_id] = obj
        elif isinstance(obj, L4DeskRemoteSession):
            if not getattr(obj, "id", None):
                obj.id = self._next_id
                self._next_id += 1
            self.sessions[obj.id] = obj

    def add_all(self, objs: list[Any]) -> None:
        for o in objs:
            self.add(o)

    async def get(self, model: Any, ident: Any) -> Any:
        if model is FinAccount:
            return self.accounts.get(ident)
        if model is FinBalanceProjection:
            return self.projections.get(ident)
        if model is FinLedgerTransaction:
            return self.transactions.get(ident)
        if model is FinBillingProfile:
            return self.profiles.get(ident)
        if model is FinBillingCycle:
            return self.cycles.get(ident)
        if model is L4DeskTerminal:
            return self.terminals.get(ident)
        if model is FinUsageDaily:
            return self.usage_daily.get(ident)
        if model is FinTerminalMonthlyCharge:
            return self.monthly_charges.get(ident)
        if model is FinPayment:
            return self.payments.get(ident)
        if model is FinManualPayment:
            return self.manual_payments.get(ident)
        if model is L4DeskRegistration:
            return self.registrations.get(ident)
        if model is L4DeskRemoteSession:
            return self.sessions.get(ident)
        if model is L4DeskAuditEvent:
            return self.audit_events.get(ident)
        if model is Org:
            return self.orgs.get(ident)
        return None

    async def flush(self) -> None:
        pass

    async def commit(self) -> None:
        pass

    async def execute(self, stmt: Any) -> MockResult:
        params: dict[str, Any] = {}
        with contextlib.suppress(Exception):
            params = stmt.compile().params
        sql = str(stmt).lower()

        # 1. Accounts
        if "from fin_accounts" in sql:
            if "tenant_id is null" in sql:
                kind = _get_param(params, "kind")
                res = [
                    a
                    for a in self.accounts.values()
                    if a.tenant_id is None and (kind is None or a.kind == kind)
                ]
                return MockResult(one=res[0] if res else None, all_items=res)
            if "fin_accounts.id in" in sql:
                return MockResult(all_items=list(self.accounts.values()))
            acc_id = _get_param(params, "id")
            if acc_id is not None:
                return MockResult(one=self.accounts.get(acc_id))
            tenant_id = _get_param(params, "tenant_id")
            kind = _get_param(params, "kind")
            res = list(self.accounts.values())
            if tenant_id is not None:
                res = [a for a in res if a.tenant_id == tenant_id]
            if kind is not None:
                res = [a for a in res if a.kind == kind]
            return MockResult(one=res[0] if res else None, all_items=res)

        # 2. Projections
        if "from fin_balance_projections" in sql:
            if "coalesce(sum(fin_balance_projections.balance_kopecks)" in sql:
                tot = sum(p.balance_kopecks for p in self.projections.values())
                return MockResult(one=tot)
            tenant_id = _get_param(params, "tenant_id")
            if tenant_id is not None:
                return MockResult(one=self.projections.get(tenant_id))
            if "in" in sql:
                return MockResult(
                    all_items=[
                        (p.tenant_id, p.balance_kopecks)
                        for p in self.projections.values()
                    ]
                )
            return MockResult(all_items=list(self.projections.values()))

        # 3. Transactions
        if "from fin_ledger_transactions" in sql:
            tx_id = _get_param(params, "id")
            if tx_id is not None:
                return MockResult(one=self.transactions.get(tx_id))
            corr = _get_param(params, "correlation_id")
            if corr is not None:
                res = [
                    t for t in self.transactions.values() if t.correlation_id == corr
                ]
                return MockResult(one=res[0] if res else None, all_items=res)
            tenant_id = _get_param(params, "tenant_id")
            res = list(self.transactions.values())
            if tenant_id is not None:
                res = [t for t in res if t.tenant_id == tenant_id]
            res.sort(key=lambda t: t.created_at, reverse=True)
            return MockResult(all_items=res)

        # 4. Entries
        if "from fin_ledger_entries" in sql:
            if "coalesce(sum(fin_ledger_entries.credit_kopecks)" in sql:
                tenant_id = _get_param(params, "tenant_id")
                tot_c = 0
                tot_d = 0
                for e in self.entries:
                    tx = self.transactions.get(e.transaction_id)
                    if (
                        tx
                        and tx.status == "posted"
                        and (tenant_id is None or e.tenant_id == tenant_id)
                    ):
                        acc = self.accounts.get(e.account_id)
                        if acc and acc.kind == "tenant_settlement":
                            tot_c += e.credit_kopecks
                            tot_d += e.debit_kopecks
                return MockResult(one=(tot_c, tot_d))
            tx_id = _get_param(params, "transaction_id")
            if tx_id is not None:
                res = [e for e in self.entries if e.transaction_id == tx_id]
                res.sort(key=lambda x: x.line_number)
                return MockResult(all_items=res)
            return MockResult(all_items=self.entries)

        # 5. Usage Daily
        if "from fin_usage_daily" in sql:
            if "count" in sql:
                return MockResult(one=len(self.usage_daily))
            u_id = _get_param(params, "id")
            if u_id is not None:
                return MockResult(one=self.usage_daily.get(u_id))
            term_id = _get_param(params, "terminal_id")
            tenant_id = _get_param(params, "tenant_id")
            res = list(self.usage_daily.values())
            if term_id is not None:
                res = [u for u in res if u.terminal_id == term_id]
            if tenant_id is not None:
                res = [u for u in res if u.tenant_id == tenant_id]
            return MockResult(one=res[0] if res else None, all_items=res)

        # 6. Monthly charges
        if "from fin_terminal_monthly_charges" in sql:
            res = list(self.monthly_charges.values())
            tenant_id = _get_param(params, "tenant_id")
            if tenant_id is not None:
                res = [c for c in res if c.tenant_id == tenant_id]
            return MockResult(one=res[0] if res else None, all_items=res)

        # 7. Payments
        if "from fin_payments" in sql:
            p_id = _get_param(params, "id")
            if p_id is not None:
                return MockResult(one=self.payments.get(p_id))
            tenant_id = _get_param(params, "tenant_id")
            res = list(self.payments.values())
            if tenant_id is not None:
                res = [p for p in res if p.tenant_id == tenant_id]
            return MockResult(one=res[0] if res else None, all_items=res)

        # 8. Manual payments
        if "from fin_manual_payments" in sql:
            mp_id = _get_param(params, "id")
            if mp_id is not None:
                return MockResult(one=self.manual_payments.get(mp_id))
            tenant_id = _get_param(params, "tenant_id")
            res = list(self.manual_payments.values())
            if tenant_id is not None:
                res = [p for p in res if p.tenant_id == tenant_id]
            return MockResult(one=res[0] if res else None, all_items=res)

        # 9. Remote sessions
        if "from l4desk_remote_sessions" in sql:
            if "count" in sql:
                return MockResult(one=len(self.sessions))
            corr = _get_param(params, "correlation_id")
            if corr is not None:
                res = [s for s in self.sessions.values() if s.correlation_id == corr]
                return MockResult(one=res[0] if res else None, all_items=res)
            tenant_id = _get_param(params, "tenant_id")
            res = list(self.sessions.values())
            if tenant_id is not None:
                res = [s for s in res if s.tenant_id == tenant_id]
            return MockResult(one=res[0] if res else None, all_items=res)

        # 10. Terminals
        if "from l4desk_terminals" in sql:
            if "count" in sql:
                return MockResult(one=len(self.terminals))
            if "select l4desk_terminals.terminal_id, l4desk_terminals.sn" in sql:
                return MockResult(
                    all_items=[(t.terminal_id, t.sn) for t in self.terminals.values()]
                )
            t_id = _get_param(params, "terminal_id")
            if t_id is not None:
                return MockResult(one=self.terminals.get(t_id))
            corr = _get_param(params, "correlation_id")
            if corr is not None:
                res = [t for t in self.terminals.values() if t.correlation_id == corr]
                return MockResult(one=res[0] if res else None, all_items=res)
            return MockResult(all_items=list(self.terminals.values()))

        # 11. Registrations
        if "from l4desk_registrations" in sql:
            if "count" in sql:
                return MockResult(one=len(self.registrations))
            corr = _get_param(params, "correlation_id")
            if corr is not None:
                res = [
                    r for r in self.registrations.values() if r.correlation_id == corr
                ]
                return MockResult(one=res[0] if res else None, all_items=res)
            tenant_id = _get_param(params, "tenant_id")
            res = list(self.registrations.values())
            if tenant_id is not None:
                res = [r for r in res if r.tenant_id == tenant_id]
            return MockResult(one=res[0] if res else None, all_items=res)

        # 12. Orgs
        if "from orgs" in sql:
            return MockResult(
                all_items=[(o.org_id, o.org_name) for o in self.orgs.values()]
            )

        # 13. Notifications
        if "from fin_notification_deliveries" in sql:
            if "count" in sql:
                return MockResult(one=len(self.notifications))
            return MockResult(all_items=list(self.notifications.values()))

        # 14. Audit Events
        if "from l4desk_audit_events" in sql:
            if "count" in sql:
                return MockResult(one=len(self.audit_events))
            return MockResult(all_items=list(self.audit_events.values()))

        # 15. Billing Profiles
        if "from fin_billing_profiles" in sql:
            if "count" in sql:
                return MockResult(one=len(self.profiles))
            return MockResult(all_items=list(self.profiles.values()))

        # 16. Billing Cycles
        if "from fin_billing_cycles" in sql:
            return MockResult(
                all_items=[
                    (c.tenant_id, c.ends_at, c.grace_deadline)
                    for c in self.cycles.values()
                ]
            )

        return MockResult(one=None, all_items=[])


@pytest.fixture
def fake_db() -> FakeHubDb:
    return FakeHubDb()


@pytest.mark.anyio
async def test_reconciliation_all_invariants(fake_db: FakeHubDb):
    """Verify reconciliation detects:
    - debit=credit
    - projection rebuild
    - calculated=posted+discarded
    - posted%100=0
    - source hash/event coverage
    - unique monthly charge / payment posting
    """
    now = datetime.now(UTC)
    period_start = now - timedelta(days=2)
    period_end = now + timedelta(days=2)
    tenant_id = 100

    # Initialize system and tenant accounts
    sys_accs = await FinAccountService.ensure_system_accounts(cast(Any, fake_db))
    tenant_acc, proj = await FinAccountService.ensure_tenant_settlement_account(
        cast(Any, fake_db), tenant_id
    )

    # 1. Add valid transaction
    tx_valid = FinLedgerTransaction(
        tenant_id=tenant_id,
        operation_id="tx_v1",
        kind="payment",
        status="posted",
        debit_kopecks=10000,
        credit_kopecks=10000,
        source_project="MenuBuilder",
        source_type="payment",
        source_id="p1",
        source_events_hash="hash_valid_tx",
        actor="admin",
        correlation_id="corr_v1",
        created_at=now,
    )
    fake_db.add(tx_valid)

    e1 = FinLedgerEntry(
        tenant_id=tenant_id,
        transaction_id=tx_valid.id,
        account_id=sys_accs["payment_clearing"].id,
        line_number=1,
        debit_kopecks=10000,
        credit_kopecks=0,
    )
    e2 = FinLedgerEntry(
        tenant_id=tenant_id,
        transaction_id=tx_valid.id,
        account_id=tenant_acc.id,
        line_number=2,
        debit_kopecks=0,
        credit_kopecks=10000,
    )
    fake_db.add_all([e1, e2])

    # Update projection to match true ledger balance
    proj.balance_kopecks = 10000

    # 2. Add usage daily with calculated != posted + discarded
    u_mismatch = FinUsageDaily(
        tenant_id=tenant_id,
        terminal_id=101,
        local_date=now.date(),
        timezone="UTC",
        tariff_version_id=1,
        source_seconds=3600,
        video_seconds=1800,
        console_seconds=1800,
        free_seconds=0,
        billable_seconds=3600,
        rounded_billable_hours=1,
        rate_kopecks=100,
        calculated_kopecks=150,  # Mismatch: 150 != 100 + 0
        posted_kopecks=100,
        discarded_kopecks=0,
        source_project="iot-rpc-rest-app",
        source_events_hash="hash_u1",
        actor="metering",
        correlation_id="corr_u1",
        created_at=now,
    )
    fake_db.add(u_mismatch)

    # 3. Add usage daily with rounding violation (posted % 100 != 0)
    u_rounding = FinUsageDaily(
        tenant_id=tenant_id,
        terminal_id=102,
        local_date=now.date(),
        timezone="UTC",
        tariff_version_id=1,
        source_seconds=1800,
        video_seconds=1800,
        console_seconds=0,
        free_seconds=0,
        billable_seconds=1800,
        rounded_billable_hours=1,
        rate_kopecks=100,
        calculated_kopecks=155,
        posted_kopecks=155,  # Rounding violation: 155 % 100 != 0
        discarded_kopecks=0,
        source_project="iot-rpc-rest-app",
        source_events_hash="hash_u2",
        actor="metering",
        correlation_id="corr_u2",
        created_at=now,
    )
    fake_db.add(u_rounding)

    # 4. Add usage daily with missing source_events_hash
    u_nohash = FinUsageDaily(
        tenant_id=tenant_id,
        terminal_id=103,
        local_date=now.date(),
        timezone="UTC",
        tariff_version_id=1,
        source_seconds=3600,
        video_seconds=0,
        console_seconds=3600,
        free_seconds=0,
        billable_seconds=3600,
        rounded_billable_hours=1,
        rate_kopecks=100,
        calculated_kopecks=100,
        posted_kopecks=100,
        discarded_kopecks=0,
        source_project="iot-rpc-rest-app",
        source_events_hash="",  # Missing hash
        actor="metering",
        correlation_id="corr_u3",
        created_at=now,
    )
    fake_db.add(u_nohash)

    # 5. Add closed session missing source_events_hash
    sess_nohash = L4DeskRemoteSession(
        tenant_id=tenant_id,
        terminal_id=101,
        operation_id="sess_nohash",
        correlation_id="corr_s1",
        session_type="video",
        state="closed",
        requested_at=now - timedelta(hours=1),
        active_at=now - timedelta(hours=1),
        closed_at=now - timedelta(minutes=10),
        source_events_hash="",  # Missing hash on closed session
    )
    fake_db.add(sess_nohash)

    # 6. Add duplicate monthly charges for same terminal and cycle
    tariff = FinTariffVersion(
        version="v1.0",
        effective_from=now - timedelta(days=10),
        terminal_month_kopecks=10000,
        hourly_rate_kopecks=100,
        free_daily_seconds=7200,
        actor="admin",
        correlation_id="t_v1",
    )
    fake_db.add(tariff)

    cycle = FinBillingCycle(
        tenant_id=tenant_id,
        sequence=1,
        starts_at=now - timedelta(days=1),
        ends_at=now + timedelta(days=29),
        grace_deadline=now + timedelta(days=32),
        timezone="UTC",
    )
    fake_db.add(cycle)

    ch1 = FinTerminalMonthlyCharge(
        tenant_id=tenant_id,
        terminal_id=200,
        billing_cycle_id=cycle.id,
        tariff_version_id=tariff.id,
        is_free=False,
        first_online_at=now,
        calculated_kopecks=10000,
        posted_kopecks=10000,
        discarded_kopecks=0,
        source_project="iot",
        source_event_id="e1",
        source_events_hash="h1",
        actor="sys",
        correlation_id="c_ch1",
        ledger_transaction_id=tx_valid.id,
        created_at=now,
    )
    ch2 = FinTerminalMonthlyCharge(
        tenant_id=tenant_id,
        terminal_id=200,
        billing_cycle_id=cycle.id,  # Duplicate! Same terminal and cycle
        tariff_version_id=tariff.id,
        is_free=False,
        first_online_at=now,
        calculated_kopecks=10000,
        posted_kopecks=10000,
        discarded_kopecks=0,
        source_project="iot",
        source_event_id="e2",
        source_events_hash="h2",
        actor="sys",
        correlation_id="c_ch2",
        ledger_transaction_id=tx_valid.id,
        created_at=now,
    )
    fake_db.add_all([ch1, ch2])

    # Run reconciliation
    rec_req = FinReconciliationRequest(
        period_start=period_start,
        period_end=period_end,
        tenant_id=tenant_id,
    )
    run = await FinReconciliationService.run_reconciliation(cast(Any, fake_db), rec_req)

    assert run.status == "mismatch"
    assert run.mismatch_count is not None and run.mismatch_count > 0
    details = run.details or {}

    assert len(details.get("calculated_discarded_mismatches", [])) >= 1
    assert len(details.get("rounding_violations", [])) >= 1
    assert len(details.get("source_hash_violations", [])) >= 2  # usage + session
    assert len(details.get("duplicate_postings", [])) >= 1  # duplicate monthly charge


@pytest.mark.anyio
async def test_correlation_drilldown_nodes_and_mismatches(fake_db: FakeHubDb):
    """Verify correlation drill-down builds chain and marks absent facts as mismatches without hallucinations."""
    now = datetime.now(UTC)
    tenant_id = 77
    terminal_id = 701

    # Add terminal
    term = L4DeskTerminal(
        terminal_id=terminal_id,
        tenant_id=tenant_id,
        ordinal=1,
        sn="SN-TEST-77",
        external_terminal_id="ext-77",
        operation_id="op-prov-77",
        correlation_id="corr-drill-77",
        provisioning_state="ready",
        pin_state="consumed",
        certificate_reference="cert_ref_77",
        created_at=now,
        first_online_at=now,
        last_online_at=now,
    )
    fake_db.add(term)

    # 1. Query drilldown by correlation_id where registration, session, usage, ledger DO NOT exist
    res = await HubService.get_correlation_drilldown(
        cast(Any, fake_db), correlation_id="corr-drill-77"
    )

    assert res.overall_status == "mismatch"
    assert "REGISTRATION_NOT_FOUND" in res.mismatch_codes

    nodes = res.nodes
    # Terminal should be present
    assert nodes["terminal"].present is True
    assert nodes["terminal"].mismatch is False
    assert nodes["terminal"].fact is not None
    assert nodes["terminal"].fact["sn"] == "SN-TEST-77"

    # PIN/Provisioning should be present and valid
    assert nodes["pin_provisioning"].present is True
    assert nodes["pin_provisioning"].mismatch is False

    # Registration must be absent and marked mismatch
    assert nodes["registration"].present is False
    assert nodes["registration"].mismatch is True
    assert nodes["registration"].mismatch_code == "REGISTRATION_NOT_FOUND"
    assert nodes["registration"].fact is None  # Never hallucinated!

    # 2. Add complete fact chain: Registration, Session, Usage, Ledger
    reg = L4DeskRegistration(
        tenant_id=tenant_id,
        email_normalized="owner@tenant77.test",
        password_hash="pwd",
        token_hash="tok77",
        terms_version="v1",
        timezone="UTC",
        source="web",
        expires_at=now + timedelta(days=1),
        consumed_at=now,
        correlation_id="corr-drill-77",
        created_at=now,
    )
    fake_db.add(reg)

    session = L4DeskRemoteSession(
        tenant_id=tenant_id,
        terminal_id=terminal_id,
        operation_id="sess_op_77",
        correlation_id="corr-drill-77",
        session_type="console",
        state="closed",
        requested_at=now,
        active_at=now,
        closed_at=now + timedelta(minutes=30),
        source_events_hash="hash_sess_77",
    )
    fake_db.add(session)

    tx = FinLedgerTransaction(
        tenant_id=tenant_id,
        operation_id="tx_77",
        kind="usage",
        status="posted",
        debit_kopecks=100,
        credit_kopecks=100,
        source_project="MenuBuilder",
        source_type="daily_usage",
        source_id="1",
        source_events_hash="hash_tx_77",
        actor="metering",
        correlation_id="corr-drill-77",
        created_at=now,
    )
    fake_db.add(tx)

    usage = FinUsageDaily(
        tenant_id=tenant_id,
        terminal_id=terminal_id,
        local_date=now.date(),
        timezone="UTC",
        tariff_version_id=1,
        source_seconds=1800,
        video_seconds=0,
        console_seconds=1800,
        free_seconds=0,
        billable_seconds=1800,
        rounded_billable_hours=1,
        rate_kopecks=100,
        calculated_kopecks=100,
        posted_kopecks=100,
        discarded_kopecks=0,
        source_project="iot",
        source_events_hash="hash_u_77",
        ledger_transaction_id=tx.id,
        actor="metering",
        correlation_id="corr-drill-77",
        created_at=now,
    )
    fake_db.add(usage)

    # Re-run drilldown
    res_complete = await HubService.get_correlation_drilldown(
        cast(Any, fake_db), correlation_id="corr-drill-77", session_id=session.id
    )

    assert res_complete.overall_status == "matched"
    assert len(res_complete.mismatch_codes) == 0
    assert res_complete.nodes["registration"].present is True
    assert res_complete.nodes["registration"].mismatch is False
    assert res_complete.nodes["online_session"].present is True
    assert res_complete.nodes["online_session"].mismatch is False
    assert res_complete.nodes["usage"].present is True
    assert res_complete.nodes["usage"].mismatch is False
    assert res_complete.nodes["ledger_payment"].present is True
    assert res_complete.nodes["ledger_payment"].mismatch is False


@pytest.mark.anyio
async def test_manual_payment_and_storno_with_code11(fake_db: FakeHubDb):
    """Verify manual payment and storno strictly require confirmation code '11' and create audit events."""
    now = datetime.now(UTC)
    tenant_id = 88

    await FinAccountService.ensure_system_accounts(cast(Any, fake_db))
    await FinAccountService.ensure_tenant_settlement_account(
        cast(Any, fake_db), tenant_id
    )

    # 1. Invalid confirmation code
    bad_req = HubManualPaymentCreateRequest(
        tenant_id=tenant_id,
        amount_rubles=5000,
        received_on=now.date(),
        document_number="ORDER-8801",
        purpose="Оплата лицензии",
        payer="ООO Вектор",
        confirmation_code="99",  # Invalid!
    )
    with pytest.raises(Exception, match="Confirmation code '11' is required"):
        await HubService.create_manual_payment_with_code11(
            cast(Any, fake_db), bad_req, actor="superadmin", user_id=1
        )

    # 2. Valid confirmation code 11
    valid_req = HubManualPaymentCreateRequest(
        tenant_id=tenant_id,
        amount_rubles=5000,
        received_on=now.date(),
        document_number="ORDER-8801",
        purpose="Оплата лицензии",
        payer="ООO Вектор",
        confirmation_code="11",  # Valid!
        correlation_id="corr-pay-8801",
    )
    res_pmt = await HubService.create_manual_payment_with_code11(
        cast(Any, fake_db), valid_req, actor="superadmin", user_id=1
    )
    assert res_pmt.id > 0
    assert res_pmt.amount_kopecks == 500000
    assert res_pmt.document_number == "ORDER-8801"

    # Check audit log
    audit = next(iter(fake_db.audit_events.values()))
    assert audit is not None
    assert audit.event_type == "hub_manual_payment_created"
    assert audit.details is not None and audit.details["confirmation_code"] == "11"

    # 3. Storno with invalid code
    bad_storno = HubManualPaymentStornoRequest(
        reversal_reason="Ошибочный платёж",
        confirmation_code="wrong",
    )
    with pytest.raises(Exception, match="Confirmation code '11' is required"):
        await HubService.storno_manual_payment_with_code11(
            cast(Any, fake_db), res_pmt.id, bad_storno, actor="superadmin"
        )

    # 4. Storno with code 11
    valid_storno = HubManualPaymentStornoRequest(
        reversal_reason="Ошибочный платёж клиента",
        confirmation_code="11",
        correlation_id="corr-storno-8801",
    )
    res_storno = await HubService.storno_manual_payment_with_code11(
        cast(Any, fake_db), res_pmt.id, valid_storno, actor="superadmin"
    )
    assert res_storno.document_number == "STORNO-ORDER-8801"

    # Check audit log for storno
    events = list(fake_db.audit_events.values())
    audit_storno = events[1] if len(events) > 1 else None
    assert audit_storno is not None
    assert audit_storno.event_type == "hub_manual_payment_storno"
    assert (
        audit_storno.details is not None
        and audit_storno.details["confirmation_code"] == "11"
    )


@pytest.mark.anyio
async def test_hub_http_api_rbac_and_views(fake_db: FakeHubDb):
    """Verify HTTP API endpoints for all Hub tabs, RBAC isolation, and filtering."""
    app.dependency_overrides[get_db] = lambda: fake_db
    now = datetime.now(UTC)

    # Seed some data
    org = Org(org_id=200, org_name="Test Company 200")
    fake_db.add(org)

    reg = L4DeskRegistration(
        id=1,
        tenant_id=200,
        email_normalized="user200@test.com",
        password_hash="pwd",
        token_hash="tok200",
        terms_version="v1",
        timezone="UTC",
        source="l4desk",
        expires_at=now + timedelta(days=1),
        consumed_at=now,
        correlation_id="corr-reg-200",
        created_at=now,
    )
    fake_db.add(reg)

    term = L4DeskTerminal(
        terminal_id=201,
        tenant_id=200,
        ordinal=1,
        sn="SN-201",
        provisioning_state="ready",
        pin_state="consumed",
        created_at=now,
        correlation_id="corr-term-201",
    )
    fake_db.add(term)

    sess = L4DeskRemoteSession(
        id=1,
        tenant_id=200,
        terminal_id=201,
        operation_id="op_sess_200",
        correlation_id="corr-sess-200",
        session_type="video",
        state="closed",
        requested_at=now,
        active_at=now,
        closed_at=now + timedelta(minutes=15),
        source_events_hash="hash200",
    )
    fake_db.add(sess)

    notif = FinNotificationDelivery(
        tenant_id=200,
        billing_cycle_id=1,
        notification_type="cycle_minus_3",
        scheduled_at=now,
        status="sent",
        attempts=1,
        correlation_id="corr-notif-200",
    )
    fake_db.add(notif)

    user_token = create_access_token(
        {"sub": "user_200", "org_id": 200, "role_id": 5, "role": "user"}
    )
    superuser_token = create_access_token(
        {
            "sub": "super_admin",
            "org_id": 1,
            "role_id": 1,
            "role": "superuser",
            "is_superuser": True,
        }
    )

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        # 1. Anonymous forbidden
        res_anon = await client.get("/api/v1/admin/hub/registrations")
        assert res_anon.status_code in (401, 403)

        # 2. Regular user (role_id = 5) forbidden
        res_user = await client.get(
            "/api/v1/admin/hub/registrations",
            headers={"Authorization": f"Bearer {user_token}"},
        )
        assert res_user.status_code == 403

        # 3. Superuser allowed
        headers_su = {"Authorization": f"Bearer {superuser_token}"}

        # Registrations tab
        res_reg = await client.get(
            "/api/v1/admin/hub/registrations", headers=headers_su
        )
        assert res_reg.status_code == 200
        data_reg = res_reg.json()
        assert data_reg["total"] == 1
        assert data_reg["items"][0]["email_normalized"] == "user200@test.com"
        assert data_reg["items"][0]["tenant_name"] == "Test Company 200"

        # Terminals tab
        res_term = await client.get("/api/v1/admin/hub/terminals", headers=headers_su)
        assert res_term.status_code == 200
        data_term = res_term.json()
        assert data_term["total"] == 1
        assert data_term["items"][0]["sn"] == "SN-201"
        assert data_term["items"][0]["is_free"] is True

        # Sessions tab
        res_sess = await client.get("/api/v1/admin/hub/sessions", headers=headers_su)
        assert res_sess.status_code == 200
        data_sess = res_sess.json()
        assert data_sess["total"] == 1
        assert data_sess["items"][0]["session_type"] == "video"

        # Notifications tab
        res_notif = await client.get(
            "/api/v1/admin/hub/notifications", headers=headers_su
        )
        assert res_notif.status_code == 200
        data_notif = res_notif.json()
        assert data_notif["total"] == 1
        assert data_notif["items"][0]["notification_type"] == "cycle_minus_3"

        # Correlation drilldown via HTTP
        res_dd = await client.get(
            "/api/v1/admin/hub/correlation-drilldown?correlation_id=corr-term-201",
            headers=headers_su,
        )
        assert res_dd.status_code == 200
        data_dd = res_dd.json()
        assert "nodes" in data_dd
        assert data_dd["nodes"]["terminal"]["present"] is True
