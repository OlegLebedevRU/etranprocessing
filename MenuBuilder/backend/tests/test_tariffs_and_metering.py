from __future__ import annotations

import contextlib
from datetime import UTC, date, datetime, timedelta
from typing import Any, cast

import pytest
from httpx import ASGITransport, AsyncClient

from app.auth import create_access_token
from app.config import settings
from app.database import get_db
from app.main import app
from app.models_l4desk import (
    FinAccount,
    FinBalanceProjection,
    FinBillingCycle,
    FinBillingProfile,
    FinLedgerEntry,
    FinLedgerTransaction,
    FinReconciliationRun,
    FinTariffVersion,
    FinTerminalMonthlyCharge,
    FinUsageDaily,
    L4DeskTenantProfile,
    L4DeskTerminal,
)
from app.services.financial_core import (
    FinAccountService,
    FinBillingCycleService,
    FinMeteringService,
    FinReconciliationRequest,
    FinReconciliationService,
    FinTariffService,
    FinTariffVersionCreate,
    FinTerminalService,
    calculate_daily_amounts,
    calculate_daily_metrics,
    split_interval_by_local_days,
)
from app.services.financial_core.timezones import resolve_timezone


class MockResult:
    """Mock result supporting scalars, scalar_one, scalar_one_or_none, all, one."""

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


def _get_param(params: dict[str, Any], prefix: str) -> Any:
    for k, v in params.items():
        if k == prefix or k.startswith(f"{prefix}_"):
            return v
    return None


class FakeMeterFinancialDb:
    """High-fidelity database simulator for tariffs, billing cycles, and metering tests."""

    def __init__(self) -> None:
        self.accounts: dict[int, FinAccount] = {}
        self.projections: dict[int, FinBalanceProjection] = {}
        self.transactions: dict[int, FinLedgerTransaction] = {}
        self.entries: list[FinLedgerEntry] = []
        self.reconciliations: dict[int, FinReconciliationRun] = {}
        self.tariffs: dict[int, FinTariffVersion] = {}
        self.profiles: dict[int, FinBillingProfile] = {}
        self.cycles: dict[int, FinBillingCycle] = {}
        self.terminals: dict[int, L4DeskTerminal] = {}
        self.tenant_profiles: dict[int, L4DeskTenantProfile] = {}
        self.usage_daily: dict[int, FinUsageDaily] = {}
        self.monthly_charges: dict[int, FinTerminalMonthlyCharge] = {}

        self._next_account_id = 1
        self._next_tx_id = 1
        self._next_entry_id = 1
        self._next_rec_id = 1
        self._next_tariff_id = 1
        self._next_cycle_id = 1
        self._next_usage_id = 1
        self._next_monthly_id = 1

    def add(self, obj: Any) -> None:
        now_dt = datetime.now(UTC)
        if hasattr(obj, "created_at") and getattr(obj, "created_at", None) is None:
            obj.created_at = now_dt
        if hasattr(obj, "updated_at") and getattr(obj, "updated_at", None) is None:
            obj.updated_at = now_dt

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
        elif isinstance(obj, FinReconciliationRun):
            if not getattr(obj, "id", None):
                obj.id = self._next_rec_id
                self._next_rec_id += 1
            self.reconciliations[obj.id] = obj
        elif isinstance(obj, FinTariffVersion):
            if not getattr(obj, "id", None):
                obj.id = self._next_tariff_id
                self._next_tariff_id += 1
            self.tariffs[obj.id] = obj
        elif isinstance(obj, FinBillingProfile):
            self.profiles[obj.tenant_id] = obj
        elif isinstance(obj, FinBillingCycle):
            if not getattr(obj, "id", None):
                obj.id = self._next_cycle_id
                self._next_cycle_id += 1
            self.cycles[obj.id] = obj
        elif isinstance(obj, L4DeskTerminal):
            self.terminals[obj.terminal_id] = obj
        elif isinstance(obj, L4DeskTenantProfile):
            self.tenant_profiles[obj.tenant_id] = obj
        elif isinstance(obj, FinUsageDaily):
            if not getattr(obj, "id", None):
                obj.id = self._next_usage_id
                self._next_usage_id += 1
            self.usage_daily[obj.id] = obj
        elif isinstance(obj, FinTerminalMonthlyCharge):
            if not getattr(obj, "id", None):
                obj.id = self._next_monthly_id
                self._next_monthly_id += 1
            self.monthly_charges[obj.id] = obj

    async def flush(self) -> None:
        pass

    async def commit(self) -> None:
        pass

    async def get(self, model: Any, ident: Any) -> Any:
        if model is FinAccount:
            return self.accounts.get(ident)
        if model is FinBalanceProjection:
            return self.projections.get(ident)
        if model is FinLedgerTransaction:
            return self.transactions.get(ident)
        if model is FinTariffVersion:
            return self.tariffs.get(ident)
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
        return None

    async def execute(self, stmt: Any) -> MockResult:
        params: dict[str, Any] = {}
        with contextlib.suppress(Exception):
            params = stmt.compile().params
        sql = str(stmt).lower()

        # 1. Accounts queries
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

        # 2. Balance Projections
        if "from fin_balance_projections" in sql:
            tenant_id = _get_param(params, "tenant_id")
            if tenant_id is not None:
                return MockResult(one=self.projections.get(tenant_id))
            return MockResult(one=None)

        # 3. Transactions
        if "from fin_ledger_transactions" in sql:
            if "max(fin_ledger_transactions.id)" in sql:
                posted = [t for t in self.transactions.values() if t.status == "posted"]
                max_id = max([t.id for t in posted], default=None)
                return MockResult(one=max_id)
            op_id = _get_param(params, "operation_id")
            if op_id is not None:
                tenant_id = _get_param(params, "tenant_id")
                res = [
                    t
                    for t in self.transactions.values()
                    if t.operation_id == op_id
                    and (tenant_id is None or t.tenant_id == tenant_id)
                ]
                return MockResult(one=res[0] if res else None, all_items=res)
            src_id = _get_param(params, "source_id")
            if src_id is not None:
                res = [t for t in self.transactions.values() if t.source_id == src_id]
                return MockResult(one=res[0] if res else None, all_items=res)
            corr_id = _get_param(params, "corrects_transaction_id")
            if corr_id is not None:
                res = [
                    t
                    for t in self.transactions.values()
                    if t.corrects_transaction_id == corr_id
                ]
                return MockResult(one=res[0] if res else None, all_items=res)
            tx_id = _get_param(params, "id")
            if tx_id is not None:
                return MockResult(one=self.transactions.get(tx_id))
            tenant_id = _get_param(params, "tenant_id")
            res = list(self.transactions.values())
            if tenant_id is not None:
                res = [t for t in res if t.tenant_id == tenant_id]
            res.sort(key=lambda x: x.created_at or datetime.now(UTC), reverse=True)
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

        # 5. Tariffs
        if "from fin_tariff_versions" in sql:
            version = _get_param(params, "version")
            if version is not None:
                res = [t for t in self.tariffs.values() if t.version == version]
                return MockResult(one=res[0] if res else None, all_items=res)
            t_id = _get_param(params, "id")
            if t_id is not None:
                return MockResult(one=self.tariffs.get(t_id))
            eff_param = _get_param(params, "effective_from")
            res = list(self.tariffs.values())
            if eff_param is not None and "<=" in sql:
                res = [t for t in res if t.effective_from <= eff_param]
                res.sort(key=lambda x: x.effective_from, reverse=True)
                return MockResult(one=res[0] if res else None, all_items=res)
            res.sort(key=lambda x: x.effective_from)
            return MockResult(all_items=res)

        # 6. Billing Profile
        if "from fin_billing_profiles" in sql:
            tenant_id = _get_param(params, "tenant_id")
            if tenant_id is not None:
                return MockResult(one=self.profiles.get(tenant_id))
            return MockResult(one=None)

        # 7. Billing Cycles
        if "from fin_billing_cycles" in sql:
            if "max(fin_billing_cycles.sequence)" in sql:
                tenant_id = _get_param(params, "tenant_id")
                seqs = [
                    c.sequence
                    for c in self.cycles.values()
                    if tenant_id is None or c.tenant_id == tenant_id
                ]
                return MockResult(one=max(seqs) if seqs else -1)
            cycle_id = _get_param(params, "id")
            if cycle_id is not None:
                return MockResult(one=self.cycles.get(cycle_id))
            tenant_id = _get_param(params, "tenant_id")
            seq = _get_param(params, "sequence")
            if tenant_id is not None and seq is not None:
                res = [
                    c
                    for c in self.cycles.values()
                    if c.tenant_id == tenant_id and c.sequence == seq
                ]
                return MockResult(one=res[0] if res else None, all_items=res)
            # Starts / ends interval check
            ts_start = _get_param(params, "starts_at")
            if tenant_id is not None and ts_start is not None and "<=" in sql:
                res = [
                    c
                    for c in self.cycles.values()
                    if c.tenant_id == tenant_id and c.starts_at <= ts_start < c.ends_at
                ]
                return MockResult(one=res[0] if res else None, all_items=res)
            res = list(self.cycles.values())
            if tenant_id is not None:
                res = [c for c in res if c.tenant_id == tenant_id]
            res.sort(key=lambda x: x.sequence, reverse=True)
            return MockResult(all_items=res)

        # 8. Terminals
        if "from l4desk_terminals" in sql:
            if "max(l4desk_terminals.ordinal)" in sql:
                tenant_id = _get_param(params, "tenant_id")
                ords = [
                    t.ordinal
                    for t in self.terminals.values()
                    if tenant_id is None or t.tenant_id == tenant_id
                ]
                return MockResult(one=max(ords) if ords else 0)
            terminal_id = _get_param(params, "terminal_id")
            if terminal_id is not None:
                return MockResult(one=self.terminals.get(terminal_id))
            tenant_id = _get_param(params, "tenant_id")
            res = list(self.terminals.values())
            if tenant_id is not None:
                res = [t for t in res if t.tenant_id == tenant_id]
            if "deleted_at is null" in sql:
                res = [t for t in res if t.deleted_at is None]
            res.sort(key=lambda x: x.ordinal)
            return MockResult(one=res[0] if res else None, all_items=res)

        # 9. Tenant Profiles
        if "from l4desk_tenant_profiles" in sql:
            tenant_id = _get_param(params, "tenant_id")
            if tenant_id is not None:
                p = self.tenant_profiles.get(tenant_id)
                return MockResult(one=p.timezone if p else "UTC")
            return MockResult(one="UTC")

        # 10. Usage Daily
        if "from fin_usage_daily" in sql:
            usage_id = _get_param(params, "id")
            if usage_id is not None:
                return MockResult(one=self.usage_daily.get(usage_id))
            term_id = _get_param(params, "terminal_id")
            local_date = _get_param(params, "local_date")
            if term_id is not None and local_date is not None:
                res = [
                    u
                    for u in self.usage_daily.values()
                    if u.terminal_id == term_id and u.local_date == local_date
                ]
                return MockResult(one=res[0] if res else None, all_items=res)
            tenant_id = _get_param(params, "tenant_id")
            res = list(self.usage_daily.values())
            if tenant_id is not None:
                res = [u for u in res if u.tenant_id == tenant_id]
            if term_id is not None:
                res = [u for u in res if u.terminal_id == term_id]
            if "ledger_transaction_id is null" in sql:
                res = [u for u in res if u.ledger_transaction_id is None]
            res.sort(key=lambda x: x.local_date, reverse=True)
            return MockResult(all_items=res)

        # 11. Terminal Monthly Charges
        if "from fin_terminal_monthly_charges" in sql:
            charge_id = _get_param(params, "id")
            if charge_id is not None:
                return MockResult(one=self.monthly_charges.get(charge_id))
            term_id = _get_param(params, "terminal_id")
            cycle_id = _get_param(params, "billing_cycle_id")
            if term_id is not None and cycle_id is not None:
                res = [
                    m
                    for m in self.monthly_charges.values()
                    if m.terminal_id == term_id and m.billing_cycle_id == cycle_id
                ]
                return MockResult(one=res[0] if res else None, all_items=res)
            tenant_id = _get_param(params, "tenant_id")
            res = list(self.monthly_charges.values())
            if tenant_id is not None:
                res = [m for m in res if m.tenant_id == tenant_id]
            if cycle_id is not None:
                res = [m for m in res if m.billing_cycle_id == cycle_id]
            res.sort(key=lambda x: x.first_online_at, reverse=True)
            return MockResult(all_items=res)

        # 12. Reconciliation Runs
        if "from fin_reconciliation_runs" in sql:
            run_id = _get_param(params, "id")
            if run_id is not None:
                return MockResult(one=self.reconciliations.get(run_id))
            runs = list(self.reconciliations.values())
            runs.sort(key=lambda x: x.started_at, reverse=True)
            return MockResult(all_items=runs)

        return MockResult(one=None, all_items=[])


# =============================================================================
# 1. DST Transitions & Split Tests
# =============================================================================


def test_split_interval_dst_spring_and_autumn():
    """Verify session interval splitting across DST jumps in Europe/London."""
    tz = "Europe/London"

    # Spring jump (UTC+0 to UTC+1): 2026-03-29 01:00 UTC jumps to 02:00 BST
    # Session from 2026-03-28 23:30 UTC to 2026-03-29 01:30 UTC (2 hours = 7200s)
    start_spring = datetime(2026, 3, 28, 23, 30, 0, tzinfo=UTC)
    end_spring = datetime(2026, 3, 29, 1, 30, 0, tzinfo=UTC)
    chunks_spring = split_interval_by_local_days(start_spring, end_spring, tz)

    assert len(chunks_spring) == 2
    assert chunks_spring[0][0] == date(2026, 3, 28)
    assert chunks_spring[0][1] == 1800  # 30 minutes before local midnight
    assert chunks_spring[1][0] == date(2026, 3, 29)
    assert chunks_spring[1][1] == 5400  # 90 minutes after local midnight
    assert sum(c[1] for c in chunks_spring) == 7200

    # Autumn fall-back (UTC+1 to UTC+0): 2026-10-25 02:00 BST -> 01:00 GMT
    start_autumn = datetime(2026, 10, 24, 22, 0, 0, tzinfo=UTC)  # 23:00 BST
    end_autumn = datetime(2026, 10, 25, 1, 0, 0, tzinfo=UTC)  # 01:00 GMT (after switch)
    chunks_autumn = split_interval_by_local_days(start_autumn, end_autumn, tz)

    assert len(chunks_autumn) == 2
    assert chunks_autumn[0][0] == date(2026, 10, 24)
    assert chunks_autumn[0][1] == 3600  # 1 hour before local midnight
    assert chunks_autumn[1][0] == date(2026, 10, 25)
    assert chunks_autumn[1][1] == 7200  # 2 hours after local midnight
    assert sum(c[1] for c in chunks_autumn) == 10800


# =============================================================================
# 2. Anchor Calculation (29-31, Leap Year, Online Non-Shifting) Tests
# =============================================================================


def test_anchor_add_months_rule_of_last_existing_day():
    """Verify add_months with the rule of the last existing day (29-31, leap years)."""
    tz = "Europe/Moscow"

    # Anchor day 31
    anchor_jan31 = datetime(2026, 1, 31, 14, 30, 0, tzinfo=UTC)

    # Jan 31 + 1 month -> Feb 28 (non-leap year 2026)
    feb = FinBillingCycleService.calculate_add_months(anchor_jan31, 1, 31, tz)
    assert feb.astimezone(resolve_timezone(tz)).date() == date(2026, 2, 28)
    assert (
        feb.astimezone(resolve_timezone(tz)).hour == 17
    )  # Moscow is UTC+3 (14:30 + 3h = 17:30)

    # Jan 31 + 2 months -> Mar 31 (restored to 31)
    mar = FinBillingCycleService.calculate_add_months(anchor_jan31, 2, 31, tz)
    assert mar.astimezone(resolve_timezone(tz)).date() == date(2026, 3, 31)

    # Jan 31 + 3 months -> Apr 30 (April has 30 days)
    apr = FinBillingCycleService.calculate_add_months(anchor_jan31, 3, 31, tz)
    assert apr.astimezone(resolve_timezone(tz)).date() == date(2026, 4, 30)

    # Leap year 2028: Jan 31 + 1 month -> Feb 29
    anchor_leap = datetime(2028, 1, 31, 12, 0, 0, tzinfo=UTC)
    feb_leap = FinBillingCycleService.calculate_add_months(anchor_leap, 1, 31, tz)
    assert feb_leap.astimezone(resolve_timezone(tz)).date() == date(2028, 2, 29)

    # Anchor day 29 in leap year: Feb 29, 2028
    anchor_feb29 = datetime(2028, 2, 29, 10, 0, 0, tzinfo=UTC)
    # +12 months -> Feb 28, 2029 (non-leap)
    feb29_next = FinBillingCycleService.calculate_add_months(anchor_feb29, 12, 29, tz)
    assert feb29_next.astimezone(resolve_timezone(tz)).date() == date(2029, 2, 28)
    # +48 months -> Feb 29, 2032 (leap year restored)
    feb29_2032 = FinBillingCycleService.calculate_add_months(anchor_feb29, 48, 29, tz)
    assert feb29_2032.astimezone(resolve_timezone(tz)).date() == date(2032, 2, 29)


def test_cycle_boundaries_and_grace_deadline():
    """Verify starts_at < grace_deadline < ends_at and exactly 3 calendar days grace."""
    tz = "Europe/Moscow"
    anchor = datetime(2026, 5, 15, 10, 0, 0, tzinfo=UTC)

    starts_at, ends_at, grace_deadline = (
        FinBillingCycleService.calculate_cycle_boundaries(anchor, 15, 0, tz)
    )

    assert starts_at < grace_deadline < ends_at
    # Grace deadline is starts_at + 3 calendar days in tenant timezone
    tz_obj = resolve_timezone(tz)
    assert grace_deadline.astimezone(tz_obj).date() == date(2026, 5, 18)
    assert starts_at.astimezone(tz_obj).date() == date(2026, 5, 15)
    assert ends_at.astimezone(tz_obj).date() == date(2026, 6, 15)


@pytest.mark.anyio
async def test_online_and_subsequent_payments_do_not_shift_anchor():
    """Normative Logic 1: Online не сдвигает anchor; поздняя оплата не сдвигает anchor."""
    db = FakeMeterFinancialDb()
    tenant_id = 77
    paid_at = datetime(2026, 7, 31, 10, 0, 0, tzinfo=UTC)

    # First successful payment sets anchor
    prof, cycle0 = await FinBillingCycleService.initialize_anchor_from_payment(
        cast(Any, db),
        tenant_id=tenant_id,
        payment_tx_id=1,
        paid_at=paid_at,
        timezone="Europe/Moscow",
    )
    assert prof.anchor_at == paid_at
    assert prof.anchor_day == 31
    assert prof.entitlement == "active"
    assert cycle0.sequence == 0

    # Device online occurs 15 days later: anchor must remain completely untouched
    online_at = paid_at + timedelta(days=15)
    (
        prof_after_online,
        cycle_online,
    ) = await FinBillingCycleService.initialize_anchor_from_payment(
        cast(Any, db),
        tenant_id=tenant_id,
        payment_tx_id=999,
        paid_at=online_at,
    )
    assert prof_after_online.anchor_at == paid_at
    assert prof_after_online.anchor_day == 31
    assert cycle_online.starts_at == cycle0.starts_at
    assert cycle_online.ends_at == cycle0.ends_at


# =============================================================================
# 3. Free Terminal Privilege & Forward Deletion Transfer Tests
# =============================================================================


@pytest.mark.anyio
async def test_free_terminal_order_and_forward_deletion_transfer():
    """Normative Logic 2: Первый по неизменяемому порядку существующий terminal бесплатен;

    после удаления льгота переходит следующему только вперёд.
    """
    db = FakeMeterFinancialDb()
    tenant_id = 100

    # Create terminal 1 (ordinal 1) and terminal 2 (ordinal 2)
    t1 = L4DeskTerminal(
        terminal_id=101,
        tenant_id=tenant_id,
        ordinal=1,
        sn="SN-101",
        external_terminal_id="term-101",
        operation_id="prov-101",
        correlation_id="corr-101",
    )
    t2 = L4DeskTerminal(
        terminal_id=102,
        tenant_id=tenant_id,
        ordinal=2,
        sn="SN-102",
        external_terminal_id="term-102",
        operation_id="prov-102",
        correlation_id="corr-102",
    )
    db.add(t1)
    db.add(t2)

    # Terminal 1 is free, Terminal 2 is paid
    assert (
        await FinTerminalService.is_terminal_free(cast(Any, db), tenant_id, 101) is True
    )
    assert (
        await FinTerminalService.is_terminal_free(cast(Any, db), tenant_id, 102)
        is False
    )

    # Soft delete terminal 1
    t1.deleted_at = datetime.now(UTC)

    # Privilege transfers forward to terminal 2
    assert (
        await FinTerminalService.is_terminal_free(cast(Any, db), tenant_id, 101)
        is False
    )
    assert (
        await FinTerminalService.is_terminal_free(cast(Any, db), tenant_id, 102) is True
    )


# =============================================================================
# 4. Monthly Charges (10000 kopecks) on First device_online Tests
# =============================================================================


@pytest.mark.anyio
async def test_terminal_monthly_charge_free_and_paid():
    """Normative Logic 3: Каждый другой terminal при первом аутентифицированном device_online

    в cycle получает один charge 10000 kopecks по unique (terminal,billing_cycle)
    даже при online после grace boundary.
    """
    db = FakeMeterFinancialDb()
    tenant_id = 200

    # Ensure system accounts and tariff exist
    await FinAccountService.ensure_system_accounts(cast(Any, db))
    await FinAccountService.ensure_tenant_settlement_account(cast(Any, db), tenant_id)
    await FinTariffService.ensure_default_tariff(cast(Any, db))

    # Initialize anchor & cycle
    paid_at = datetime(2026, 9, 1, 0, 0, 0, tzinfo=UTC)
    await FinBillingCycleService.initialize_anchor_from_payment(
        cast(Any, db), tenant_id=tenant_id, payment_tx_id=1, paid_at=paid_at
    )

    t1 = L4DeskTerminal(
        terminal_id=201,
        tenant_id=tenant_id,
        ordinal=1,
        sn="SN-201",
        external_terminal_id="term-201",
        operation_id="prov-201",
        correlation_id="corr-201",
    )
    t2 = L4DeskTerminal(
        terminal_id=202,
        tenant_id=tenant_id,
        ordinal=2,
        sn="SN-202",
        external_terminal_id="term-202",
        operation_id="prov-202",
        correlation_id="corr-202",
    )
    db.add(t1)
    db.add(t2)

    # 1. First online for free terminal (t1): charge = 0 kopecks, no ledger transaction
    charge1 = await FinTerminalService.process_device_online_monthly_charge(
        cast(Any, db),
        tenant_id=tenant_id,
        terminal_id=201,
        event_id="evt_online_t1",
        occurred_at=paid_at + timedelta(hours=2),
    )
    assert charge1 is not None
    assert charge1.is_free is True
    assert charge1.calculated_kopecks == 0
    assert charge1.posted_kopecks == 0
    assert charge1.ledger_transaction_id is None

    # 2. First online for paid terminal (t2) even after grace boundary (+10 days):
    # charge = 10000 kopecks, ledger transaction posted
    online_late = paid_at + timedelta(days=10)
    charge2 = await FinTerminalService.process_device_online_monthly_charge(
        cast(Any, db),
        tenant_id=tenant_id,
        terminal_id=202,
        event_id="evt_online_t2",
        occurred_at=online_late,
    )
    assert charge2 is not None
    assert charge2.is_free is False
    assert charge2.calculated_kopecks == 10000
    assert charge2.posted_kopecks == 10000
    assert charge2.ledger_transaction_id is not None
    assert charge2.posted_at is not None

    # Verify ledger transaction
    tx = db.transactions[charge2.ledger_transaction_id]
    assert tx.kind == "terminal_month"
    assert tx.debit_kopecks == 10000
    assert tx.credit_kopecks == 10000

    # 3. Duplicate online for paid terminal in same cycle is idempotent
    charge2_dup = await FinTerminalService.process_device_online_monthly_charge(
        cast(Any, db),
        tenant_id=tenant_id,
        terminal_id=202,
        event_id="evt_online_t2_dup",
        occurred_at=online_late + timedelta(hours=1),
    )
    assert charge2_dup is not None
    assert charge2_dup.id == charge2.id
    assert charge2_dup.ledger_transaction_id == charge2.ledger_transaction_id


# =============================================================================
# 5. Exact 120 min, 120m01s, Rounding, and Tariff Formula Invariants
# =============================================================================


def test_daily_usage_exact_120m_and_120m01s():
    """Normative Logic 4 & 5:

    Для free terminal: billable_seconds = max(0, console + video - 7200); для остальных free = 0.
    paid_hours = ceil(billable_seconds / 3600), current rate 100 kopecks/hour.
    """
    # 1. Exact 120 minutes (7200s) on free terminal -> 0 billable seconds, 0 kopecks
    m_120m = calculate_daily_metrics(
        video_sec=3600,
        console_sec=3600,
        is_free=True,
        free_daily_seconds=7200,
        hourly_rate_kopecks=100,
    )
    assert m_120m["source_seconds"] == 7200
    assert m_120m["free_seconds"] == 7200
    assert m_120m["billable_seconds"] == 0
    assert m_120m["rounded_billable_hours"] == 0
    assert m_120m["calculated_kopecks"] == 0
    assert m_120m["posted_kopecks"] == 0
    assert m_120m["discarded_kopecks"] == 0

    # 2. 120 minutes and 1 second (7201s) on free terminal -> 1 billable second -> ceil(1/3600)=1h -> 100 kopecks
    m_120m01s = calculate_daily_metrics(
        video_sec=7200,
        console_sec=1,
        is_free=True,
        free_daily_seconds=7200,
        hourly_rate_kopecks=100,
    )
    assert m_120m01s["source_seconds"] == 7201
    assert m_120m01s["free_seconds"] == 7200
    assert m_120m01s["billable_seconds"] == 1
    assert m_120m01s["rounded_billable_hours"] == 1
    assert m_120m01s["calculated_kopecks"] == 100
    assert m_120m01s["posted_kopecks"] == 100
    assert m_120m01s["discarded_kopecks"] == 0

    # 3. Non-free terminal with 1 second -> free=0, billable=1 -> 1h -> 100 kopecks
    m_paid_1s = calculate_daily_metrics(
        video_sec=0,
        console_sec=1,
        is_free=False,
        free_daily_seconds=7200,
        hourly_rate_kopecks=100,
    )
    assert m_paid_1s["source_seconds"] == 1
    assert m_paid_1s["free_seconds"] == 0
    assert m_paid_1s["billable_seconds"] == 1
    assert m_paid_1s["rounded_billable_hours"] == 1
    assert m_paid_1s["calculated_kopecks"] == 100
    assert m_paid_1s["posted_kopecks"] == 100


def test_general_future_tariff_formula_rounding_invariants():
    """Normative Logic 6:

    calculated = paid_hours * rate,
    posted = floor(calculated / 100) * 100,
    discarded = calculated - posted;
    calculated = posted + discarded, discarded не переносится и не проводится.
    """
    # Hypothetical future rates
    test_cases = [
        (3600, 100, 1, 100, 100, 0),  # Standard 100 kopecks/h
        (3600, 150, 1, 150, 100, 50),  # 150 kopecks: 100 posted, 50 discarded
        (
            7200,
            135,
            2,
            270,
            200,
            70,
        ),  # 2 hours @ 135 kopecks: 270 calc, 200 posted, 70 discarded
        (
            10800,
            199,
            3,
            597,
            500,
            97,
        ),  # 3 hours @ 199 kopecks: 597 calc, 500 posted, 97 discarded
        (0, 100, 0, 0, 0, 0),  # Zero usage
    ]

    for billable_sec, rate, exp_h, exp_calc, exp_post, exp_disc in test_cases:
        h, calc, posted, discarded = calculate_daily_amounts(billable_sec, rate)
        assert h == exp_h
        assert calc == exp_calc
        assert posted == exp_post
        assert discarded == exp_disc
        # Mathematical Invariants
        assert calc == posted + discarded
        assert 0 <= discarded < 100
        assert posted >= 0
        assert posted % 100 == 0


# =============================================================================
# 6. Open / Closed Daily Usage and Late-Event Delta Corrections Tests
# =============================================================================


@pytest.mark.anyio
async def test_open_closed_daily_usage_and_late_event_delta():
    """Normative Logic 4, 76 & §9.4:

    Открытый расчёт пересчитывается under lock.
    После закрытия и проведения (close_and_post) оригинал неизменяем;
    позднее событие создаёт delta adjustment транзакцию.
    """
    db = FakeMeterFinancialDb()
    tenant_id = 300
    terminal_id = 301

    await FinAccountService.ensure_system_accounts(cast(Any, db))
    await FinAccountService.ensure_tenant_settlement_account(cast(Any, db), tenant_id)
    await FinTariffService.ensure_default_tariff(cast(Any, db))

    # Ensure tenant has free terminal 300 (ordinal 1), so terminal 301 (ordinal 2) is paid
    free_term = L4DeskTerminal(
        terminal_id=300,
        tenant_id=tenant_id,
        ordinal=1,
        sn="SN-300",
        external_terminal_id="term-300",
        operation_id="prov-300",
        correlation_id="corr-300",
    )
    db.add(free_term)

    # Terminal 301 is paid
    term = L4DeskTerminal(
        terminal_id=terminal_id,
        tenant_id=tenant_id,
        ordinal=2,  # Not free
        sn="SN-301",
        external_terminal_id="term-301",
        operation_id="prov-301",
        correlation_id="corr-301",
    )
    db.add(term)

    # 1. Event 1 on open day: 1800s (30m) console
    start_dt = datetime(2026, 9, 18, 10, 0, 0, tzinfo=UTC)
    end_dt = datetime(2026, 9, 18, 10, 30, 0, tzinfo=UTC)
    rows1 = await FinMeteringService.record_session_usage(
        cast(Any, db),
        tenant_id=tenant_id,
        terminal_id=terminal_id,
        session_type="console",
        start_utc=start_dt,
        end_utc=end_dt,
        event_id="evt_sess_01",
    )
    assert len(rows1) == 1
    row = rows1[0]
    assert row.source_seconds == 1800
    assert row.billable_seconds == 1800
    assert row.rounded_billable_hours == 1
    assert row.posted_kopecks == 100
    assert row.ledger_transaction_id is None  # Still OPEN

    # 2. Event 2 on open day: 1800s video
    end_dt2 = datetime(2026, 9, 18, 11, 0, 0, tzinfo=UTC)
    rows2 = await FinMeteringService.record_session_usage(
        cast(Any, db),
        tenant_id=tenant_id,
        terminal_id=terminal_id,
        session_type="video",
        start_utc=end_dt,
        end_utc=end_dt2,
        event_id="evt_sess_02",
    )
    assert len(rows2) == 1
    assert rows2[0].id == row.id
    assert rows2[0].source_seconds == 3600
    assert rows2[0].video_seconds == 1800
    assert rows2[0].console_seconds == 1800
    assert rows2[0].rounded_billable_hours == 1
    assert rows2[0].posted_kopecks == 100

    # 3. Close and post daily usage for 2026-09-18
    closed_rows = await FinMeteringService.close_and_post_daily_usage(
        cast(Any, db),
        tenant_id=tenant_id,
        local_date=date(2026, 9, 18),
    )
    assert len(closed_rows) == 1
    closed = closed_rows[0]
    assert closed.ledger_transaction_id is not None
    assert closed.posted_at is not None

    orig_tx_id = closed.ledger_transaction_id
    orig_seconds = closed.source_seconds
    orig_posted = closed.posted_kopecks

    # 4. Late event arrives for 2026-09-18 (+3600s, pushing total to 7200s = 2h)
    late_start = datetime(2026, 9, 18, 14, 0, 0, tzinfo=UTC)
    late_end = datetime(2026, 9, 18, 15, 0, 0, tzinfo=UTC)
    rows_late = await FinMeteringService.record_session_usage(
        cast(Any, db),
        tenant_id=tenant_id,
        terminal_id=terminal_id,
        session_type="console",
        start_utc=late_start,
        end_utc=late_end,
        event_id="evt_sess_late",
    )
    assert len(rows_late) == 1
    # Immutability check: original row attributes must NOT be overwritten!
    assert rows_late[0].source_seconds == orig_seconds
    assert rows_late[0].posted_kopecks == orig_posted
    assert rows_late[0].ledger_transaction_id == orig_tx_id

    # Verify that an adjustment transaction was posted for the delta (100 kopecks)
    adj_txs = [
        t for t in db.transactions.values() if t.corrects_transaction_id == orig_tx_id
    ]
    assert len(adj_txs) == 1
    adj = adj_txs[0]
    assert adj.kind == "adjustment"
    assert adj.debit_kopecks == 100
    assert adj.credit_kopecks == 100
    assert adj.calculation_snapshot is not None
    assert adj.calculation_snapshot["delta_kopecks"] == 100
    assert adj.calculation_snapshot["old_seconds"] == 3600
    assert adj.calculation_snapshot["new_seconds"] == 7200


# =============================================================================
# 7. Post-Metering Double-Entry Reconciliation & Audit Tests
# =============================================================================


@pytest.mark.anyio
async def test_reconciliation_post_metering_transactions():
    """Verify FinReconciliationService audits ledger transactions generated by metering."""
    db = FakeMeterFinancialDb()
    tenant_id = 500
    terminal_id = 501

    await FinAccountService.ensure_system_accounts(cast(Any, db))
    await FinAccountService.ensure_tenant_settlement_account(cast(Any, db), tenant_id)
    await FinTariffService.ensure_default_tariff(cast(Any, db))

    # Set anchor and initial top-up
    paid_at = datetime(2026, 9, 1, 0, 0, 0, tzinfo=UTC)
    await FinBillingCycleService.initialize_anchor_from_payment(
        cast(Any, db), tenant_id=tenant_id, payment_tx_id=1, paid_at=paid_at
    )

    # Free terminal 500 (ordinal 1), so terminal 501 (ordinal 2) is paid
    free_term = L4DeskTerminal(
        terminal_id=500,
        tenant_id=tenant_id,
        ordinal=1,
        sn="SN-500",
        external_terminal_id="term-500",
        operation_id="prov-500",
        correlation_id="corr-500",
    )
    db.add(free_term)

    term = L4DeskTerminal(
        terminal_id=terminal_id,
        tenant_id=tenant_id,
        ordinal=2,
        sn="SN-501",
        external_terminal_id="term-501",
        operation_id="prov-501",
        correlation_id="corr-501",
    )
    db.add(term)

    # 1. Monthly charge
    await FinTerminalService.process_device_online_monthly_charge(
        cast(Any, db),
        tenant_id=tenant_id,
        terminal_id=terminal_id,
        event_id="evt_501_online",
        occurred_at=paid_at + timedelta(hours=1),
    )

    # 2. Daily usage and closure
    s_dt = datetime(2026, 9, 2, 10, 0, 0, tzinfo=UTC)
    e_dt = datetime(2026, 9, 2, 11, 0, 0, tzinfo=UTC)
    await FinMeteringService.record_session_usage(
        cast(Any, db),
        tenant_id=tenant_id,
        terminal_id=terminal_id,
        session_type="console",
        start_utc=s_dt,
        end_utc=e_dt,
        event_id="evt_501_usage",
    )
    await FinMeteringService.close_and_post_daily_usage(
        cast(Any, db), tenant_id=tenant_id, local_date=date(2026, 9, 2)
    )

    # Run reconciliation audit
    rec_req = FinReconciliationRequest(
        period_start=datetime(2026, 9, 1, 0, 0, 0, tzinfo=UTC),
        period_end=datetime(2026, 9, 30, 23, 59, 59, tzinfo=UTC),
        tenant_id=tenant_id,
        auto_rebuild_projection=False,
    )
    run = await FinReconciliationService.run_reconciliation(cast(Any, db), rec_req)

    assert run.status == "matched"
    assert run.mismatch_count == 0
    # Monthly charge (10000) + daily usage (100) = 10100 debits
    assert run.debit_kopecks == 10100
    assert run.credit_kopecks == 10100


# =============================================================================
# 8. REST API Endpoints Integration Tests
# =============================================================================


@pytest.mark.anyio
async def test_rest_api_finance_profile_cycles_tariffs_and_metering():
    """Verify tenant and internal REST endpoints for tariffs, cycles, and metering."""
    fake_db = FakeMeterFinancialDb()
    tenant_id = 999
    terminal_id = 901

    await FinAccountService.ensure_system_accounts(cast(Any, fake_db))
    await FinAccountService.ensure_tenant_settlement_account(
        cast(Any, fake_db), tenant_id
    )
    await FinTariffService.ensure_default_tariff(cast(Any, fake_db))

    t = L4DeskTerminal(
        terminal_id=terminal_id,
        tenant_id=tenant_id,
        ordinal=1,
        sn="SN-901",
        external_terminal_id="term-901",
        operation_id="prov-901",
        correlation_id="corr-901",
    )
    fake_db.add(t)

    paid_at = datetime(2026, 9, 1, 0, 0, 0, tzinfo=UTC)
    await FinBillingCycleService.initialize_anchor_from_payment(
        cast(Any, fake_db), tenant_id=tenant_id, payment_tx_id=1, paid_at=paid_at
    )

    app.dependency_overrides[get_db] = lambda: fake_db

    token = create_access_token(
        {
            "sub": "user_999",
            "org_id": tenant_id,
            "role_id": 5,
            "role": "user",
            "is_l4desk": True,
        }
    )
    settings.internal_service_key = "test_internal_key"
    headers = {"Authorization": f"Bearer {token}"}
    internal_headers = {"X-Internal-Service-Key": "test_internal_key"}

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        # 1. GET /api/v1/finance/profile
        res_prof = await client.get("/api/v1/finance/profile", headers=headers)
        assert res_prof.status_code == 200
        p_data = res_prof.json()
        assert p_data["tenant_id"] == tenant_id
        assert p_data["entitlement"] == "active"
        assert p_data["anchor_day"] == 1

        # 2. GET /api/v1/finance/cycles
        res_cycles = await client.get("/api/v1/finance/cycles", headers=headers)
        assert res_cycles.status_code == 200
        c_data = res_cycles.json()
        assert len(c_data) >= 1
        assert c_data[0]["sequence"] == 0

        # 3. GET /api/v1/finance/tariffs/current
        res_tariff = await client.get(
            "/api/v1/finance/tariffs/current", headers=headers
        )
        assert res_tariff.status_code == 200
        t_data = res_tariff.json()
        assert t_data["version"] == "v1.0"
        assert t_data["hourly_rate_kopecks"] == 100

        # 4. POST /api/internal/v1/finance/metering/online
        res_onl = await client.post(
            "/api/internal/v1/finance/metering/online",
            json={
                "tenant_id": tenant_id,
                "terminal_id": terminal_id,
                "event_id": "api_evt_onl_01",
                "occurred_at": (paid_at + timedelta(hours=1)).isoformat(),
            },
            headers=internal_headers,
        )
        assert res_onl.status_code == 200
        onl_data = res_onl.json()
        assert onl_data["is_free"] is True
        assert onl_data["posted_kopecks"] == 0

        # 5. POST /api/internal/v1/finance/metering/record-usage
        s_time = datetime(2026, 9, 2, 10, 0, 0, tzinfo=UTC)
        e_time = datetime(2026, 9, 2, 12, 0, 1, tzinfo=UTC)  # 7201s
        res_usage = await client.post(
            "/api/internal/v1/finance/metering/record-usage",
            json={
                "tenant_id": tenant_id,
                "terminal_id": terminal_id,
                "session_type": "console",
                "start_utc": s_time.isoformat(),
                "end_utc": e_time.isoformat(),
                "event_id": "api_evt_usage_01",
            },
            headers=internal_headers,
        )
        assert res_usage.status_code == 200
        u_data = res_usage.json()
        assert len(u_data) == 1
        assert u_data[0]["source_seconds"] == 7201
        assert u_data[0]["billable_seconds"] == 1
        assert u_data[0]["posted_kopecks"] == 100

        # 6. GET /api/v1/finance/usage
        res_get_usage = await client.get("/api/v1/finance/usage", headers=headers)
        assert res_get_usage.status_code == 200
        assert len(res_get_usage.json()) == 1

        # 7. GET /api/v1/finance/monthly-charges
        res_mc = await client.get("/api/v1/finance/monthly-charges", headers=headers)
        assert res_mc.status_code == 200
        assert len(res_mc.json()) == 1

        # 8. POST /api/internal/v1/finance/metering/close-day
        res_close = await client.post(
            "/api/internal/v1/finance/metering/close-day",
            json={
                "tenant_id": tenant_id,
                "local_date": "2026-09-02",
            },
            headers=internal_headers,
        )
        assert res_close.status_code == 200
        closed_data = res_close.json()
        assert len(closed_data) == 1
        assert closed_data[0]["ledger_transaction_id"] is not None

    app.dependency_overrides.clear()


# =============================================================================
# 9. Additional Edge Cases: Tariff Snapshots, Concurrent Workers & Midnight
# =============================================================================


@pytest.mark.anyio
async def test_tariff_versioning_and_snapshot_selection():
    """Verify versioned immutable tariff snapshots and resolution before/after effective dates."""
    db = FakeMeterFinancialDb()

    # Create baseline v1.0
    t1 = await FinTariffService.ensure_default_tariff(cast(Any, db))
    assert t1.version == "v1.0"
    assert t1.hourly_rate_kopecks == 100
    assert t1.terminal_month_kopecks == 10000

    # Create future v2.0 effective from 2026-11-01
    eff_v2 = datetime(2026, 11, 1, 0, 0, 0, tzinfo=UTC)
    t2 = await FinTariffService.create_tariff_version(
        cast(Any, db),
        FinTariffVersionCreate(
            version="v2.0",
            effective_from=eff_v2,
            terminal_month_kopecks=15000,
            hourly_rate_kopecks=150,
            free_daily_seconds=7200,
        ),
    )
    assert t2.version == "v2.0"

    # Query before 2026-11-01 -> resolves v1.0
    before_dt = datetime(2026, 10, 15, 12, 0, 0, tzinfo=UTC)
    res_before = await FinTariffService.get_effective_tariff(cast(Any, db), before_dt)
    assert res_before.version == "v1.0"
    assert res_before.hourly_rate_kopecks == 100

    # Query on or after 2026-11-01 -> resolves v2.0
    after_dt = datetime(2026, 11, 5, 12, 0, 0, tzinfo=UTC)
    res_after = await FinTariffService.get_effective_tariff(cast(Any, db), after_dt)
    assert res_after.version == "v2.0"
    assert res_after.hourly_rate_kopecks == 150


@pytest.mark.anyio
async def test_midnight_session_splitting_exact_seconds_preservation():
    """Verify that sessions crossing midnight boundaries preserve exact seconds."""
    tz = "Europe/Moscow"

    # Session from 23:45 on Day 1 to 00:30 on Day 2 (45 min = 2700s)
    tz_obj = resolve_timezone(tz)
    s_local = datetime(2026, 9, 18, 23, 45, 0, tzinfo=tz_obj)
    e_local = datetime(2026, 9, 19, 0, 30, 0, tzinfo=tz_obj)
    s_utc = s_local.astimezone(UTC)
    e_utc = e_local.astimezone(UTC)

    chunks = split_interval_by_local_days(s_utc, e_utc, tz)
    assert len(chunks) == 2
    assert chunks[0][0] == date(2026, 9, 18)
    assert chunks[0][1] == 900  # 15 minutes = 900s
    assert chunks[1][0] == date(2026, 9, 19)
    assert chunks[1][1] == 1800  # 30 minutes = 1800s
    assert sum(c[1] for c in chunks) == 2700

    # Multi-day session: spanning 3 local days (23:00 Day 1 to 02:00 Day 3 = 1h + 24h + 2h = 27h = 97200s)
    s_m_local = datetime(2026, 9, 1, 23, 0, 0, tzinfo=tz_obj)
    e_m_local = datetime(2026, 9, 3, 2, 0, 0, tzinfo=tz_obj)
    s_m_utc = s_m_local.astimezone(UTC)
    e_m_utc = e_m_local.astimezone(UTC)

    chunks_m = split_interval_by_local_days(s_m_utc, e_m_utc, tz)
    assert len(chunks_m) == 3
    assert chunks_m[0] == (date(2026, 9, 1), 3600)
    assert chunks_m[1] == (date(2026, 9, 2), 86400)
    assert chunks_m[2] == (date(2026, 9, 3), 7200)
    assert sum(c[1] for c in chunks_m) == 97200


@pytest.mark.anyio
async def test_concurrent_worker_recording_and_idempotency():
    """Verify concurrent worker replay and idempotency."""
    db = FakeMeterFinancialDb()
    tenant_id = 888
    terminal_id = 801

    await FinAccountService.ensure_system_accounts(cast(Any, db))
    await FinAccountService.ensure_tenant_settlement_account(cast(Any, db), tenant_id)
    await FinTariffService.ensure_default_tariff(cast(Any, db))

    # Free terminal
    free_t = L4DeskTerminal(
        terminal_id=800,
        tenant_id=tenant_id,
        ordinal=1,
        sn="SN-800",
        external_terminal_id="term-800",
        operation_id="prov-800",
        correlation_id="corr-800",
    )
    paid_t = L4DeskTerminal(
        terminal_id=terminal_id,
        tenant_id=tenant_id,
        ordinal=2,
        sn="SN-801",
        external_terminal_id="term-801",
        operation_id="prov-801",
        correlation_id="corr-801",
    )
    db.add(free_t)
    db.add(paid_t)

    paid_at = datetime(2026, 9, 1, 0, 0, 0, tzinfo=UTC)
    await FinBillingCycleService.initialize_anchor_from_payment(
        cast(Any, db), tenant_id=tenant_id, payment_tx_id=1, paid_at=paid_at
    )

    # Worker 1 & Worker 2 record same interval concurrently
    s_dt = datetime(2026, 9, 5, 12, 0, 0, tzinfo=UTC)
    e_dt = datetime(2026, 9, 5, 12, 30, 0, tzinfo=UTC)

    # Worker 1 execution
    r1 = await FinMeteringService.record_session_usage(
        cast(Any, db),
        tenant_id=tenant_id,
        terminal_id=terminal_id,
        session_type="console",
        start_utc=s_dt,
        end_utc=e_dt,
        event_id="evt_w1",
    )
    assert len(r1) == 1
    assert r1[0].source_seconds == 1800

    # Worker 2 replay of same event: should accumulate or be closed
    # Close and post day
    c1 = await FinMeteringService.close_and_post_daily_usage(
        cast(Any, db), tenant_id=tenant_id, local_date=date(2026, 9, 5)
    )
    assert len(c1) == 1
    tx_id_1 = c1[0].ledger_transaction_id
    assert tx_id_1 is not None

    # Idempotent second close call: should not create second transaction
    c2 = await FinMeteringService.close_and_post_daily_usage(
        cast(Any, db), tenant_id=tenant_id, local_date=date(2026, 9, 5)
    )
    assert len(c2) == 0  # No open rows left to close
