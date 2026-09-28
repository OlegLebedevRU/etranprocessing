from __future__ import annotations

import contextlib
from datetime import UTC, datetime, timedelta
from typing import Any, cast

import pydantic
import pytest
from httpx import ASGITransport, AsyncClient

from app.auth import create_access_token
from app.config import settings
from app.database import get_db
from app.main import app
from app.models_l4desk import (
    FinAccount,
    FinBalanceProjection,
    FinLedgerEntry,
    FinLedgerTransaction,
    FinReconciliationRun,
)
from app.services.financial_core import (
    FinAccountService,
    FinConcurrencyError,
    FinDuplicatePostingError,
    FinImbalanceError,
    FinImmutableError,
    FinPostingEntryRequest,
    FinPostingRequest,
    FinPostingService,
    FinProjectionService,
    FinReconciliationRequest,
    FinReconciliationService,
    FinReversalError,
    FinReversalRequest,
    FinReversalService,
    FinTenantIsolationError,
    FinValidationError,
)


class MockResult:
    """Mock result returning scalars, scalar_one, scalar_one_or_none, all, one."""

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


class FakeFinancialDb:
    """High-fidelity in-memory database simulator for financial core subledger tests."""

    def __init__(self) -> None:
        self.accounts: dict[int, FinAccount] = {}
        self.projections: dict[int, FinBalanceProjection] = {}
        self.transactions: dict[int, FinLedgerTransaction] = {}
        self.entries: list[FinLedgerEntry] = []
        self.reconciliations: dict[int, FinReconciliationRun] = {}

        self._next_account_id = 1
        self._next_tx_id = 1
        self._next_entry_id = 1
        self._next_rec_id = 1

    def add(self, obj: Any) -> None:
        now_dt = datetime.now(UTC)
        if hasattr(obj, "created_at") and getattr(obj, "created_at", None) is None:
            obj.created_at = now_dt
        if hasattr(obj, "updated_at") and getattr(obj, "updated_at", None) is None:
            obj.updated_at = now_dt

        if isinstance(obj, FinAccount):
            if not obj.id:
                obj.id = self._next_account_id
                self._next_account_id += 1
            self.accounts[obj.id] = obj
        elif isinstance(obj, FinBalanceProjection):
            self.projections[obj.tenant_id] = obj
        elif isinstance(obj, FinLedgerTransaction):
            if not obj.id:
                obj.id = self._next_tx_id
                self._next_tx_id += 1
            self.transactions[obj.id] = obj
        elif isinstance(obj, FinLedgerEntry):
            if not obj.id:
                obj.id = self._next_entry_id
                self._next_entry_id += 1
            self.entries.append(obj)
        elif isinstance(obj, FinReconciliationRun):
            if not obj.id:
                obj.id = self._next_rec_id
                self._next_rec_id += 1
            self.reconciliations[obj.id] = obj

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
        if model is FinReconciliationRun:
            return self.reconciliations.get(ident)
        return None

    async def execute(self, stmt: Any) -> MockResult:
        params: dict[str, Any] = {}
        with contextlib.suppress(Exception):
            params = stmt.compile().params
        sql = str(stmt).lower()

        # 1. Accounts queries
        if "from fin_accounts" in sql:
            # Query global accounts (tenant_id IS NULL)
            if "tenant_id is null" in sql:
                kind = _get_param(params, "kind")
                res = [
                    a
                    for a in self.accounts.values()
                    if a.tenant_id is None and (kind is None or a.kind == kind)
                ]
                return MockResult(one=res[0] if res else None, all_items=res)

            # Query by id IN (...)
            if "fin_accounts.id in" in sql:
                return MockResult(all_items=list(self.accounts.values()))

            # Query by primary key id
            acc_id = _get_param(params, "id")
            if acc_id is not None:
                acc = self.accounts.get(acc_id)
                return MockResult(one=acc)

            # Query by tenant_id and kind
            tenant_id = _get_param(params, "tenant_id")
            kind = _get_param(params, "kind")
            res = list(self.accounts.values())
            if tenant_id is not None:
                res = [a for a in res if a.tenant_id == tenant_id]
            if kind is not None:
                res = [a for a in res if a.kind == kind]
            return MockResult(one=res[0] if res else None, all_items=res)

        # 2. Balance Projection queries
        if "from fin_balance_projections" in sql:
            tenant_id = _get_param(params, "tenant_id")
            if tenant_id is not None:
                return MockResult(one=self.projections.get(tenant_id))
            return MockResult(one=None)

        # 3. Ledger Transactions queries
        if "from fin_ledger_transactions" in sql:
            # func.max(id) query
            if "max(fin_ledger_transactions.id)" in sql:
                posted = [t for t in self.transactions.values() if t.status == "posted"]
                max_id = max([t.id for t in posted], default=None)
                return MockResult(one=max_id)

            # Filter by corrects_transaction_id
            corr_id = _get_param(params, "corrects_transaction_id")
            if corr_id is not None:
                kind = _get_param(params, "kind")
                res = [
                    t
                    for t in self.transactions.values()
                    if t.corrects_transaction_id == corr_id
                    and (kind is None or t.kind == kind)
                ]
                return MockResult(one=res[0] if res else None, all_items=res)

            # Filter by operation_id
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

            # Filter by source_id
            src_id = _get_param(params, "source_id")
            if src_id is not None:
                res = [t for t in self.transactions.values() if t.source_id == src_id]
                return MockResult(one=res[0] if res else None, all_items=res)

            # Filter by primary key id
            tx_id = _get_param(params, "id")
            if tx_id is not None:
                res = [t for t in self.transactions.values() if t.id == tx_id]
                return MockResult(one=res[0] if res else None, all_items=res)

            # List transactions for tenant
            tenant_id = _get_param(params, "tenant_id")
            res = list(self.transactions.values())
            if tenant_id is not None:
                res = [t for t in res if t.tenant_id == tenant_id]
            res.sort(key=lambda x: x.created_at or datetime.now(UTC), reverse=True)
            return MockResult(all_items=res)

        # 4. Ledger Entries queries
        if "from fin_ledger_entries" in sql:
            # Aggregation query (credits, debits)
            if "coalesce(sum(fin_ledger_entries.credit_kopecks)" in sql:
                tenant_id = _get_param(params, "tenant_id")
                total_credit = 0
                total_debit = 0
                for e in self.entries:
                    tx = self.transactions.get(e.transaction_id)
                    if (
                        tx
                        and tx.status == "posted"
                        and (tenant_id is None or e.tenant_id == tenant_id)
                    ):
                        settlement_acc = self.accounts.get(e.account_id)
                        if (
                            settlement_acc
                            and settlement_acc.kind == "tenant_settlement"
                        ):
                            total_credit += e.credit_kopecks
                            total_debit += e.debit_kopecks
                return MockResult(one=(total_credit, total_debit))

            # Filter by transaction_id
            tx_id = _get_param(params, "transaction_id")
            if tx_id is not None:
                res = [e for e in self.entries if e.transaction_id == tx_id]
                res.sort(key=lambda x: x.line_number)
                return MockResult(all_items=res)

            return MockResult(all_items=self.entries)

        # 5. Reconciliation Runs queries
        if "from fin_reconciliation_runs" in sql:
            run_id = _get_param(params, "id")
            if run_id is not None:
                return MockResult(one=self.reconciliations.get(run_id))
            runs = list(self.reconciliations.values())
            runs.sort(key=lambda x: x.started_at, reverse=True)
            return MockResult(all_items=runs)

        return MockResult(one=None, all_items=[])


# =============================================================================
# Unit Tests for Financial Services
# =============================================================================


@pytest.mark.anyio
async def test_ensure_system_accounts_and_tenant_settlement():
    """Verify initialization of global system accounts and tenant settlement account."""
    db = FakeFinancialDb()

    # 1. Initialize system accounts
    sys_accs = await FinAccountService.ensure_system_accounts(cast(Any, db))
    assert "payment_clearing" in sys_accs
    assert "usage_revenue" in sys_accs
    assert sys_accs["payment_clearing"].tenant_id is None
    assert sys_accs["payment_clearing"].currency == "RUB"
    assert sys_accs["usage_revenue"].tenant_id is None
    assert sys_accs["usage_revenue"].currency == "RUB"

    # Calling again should be idempotent
    sys_accs_2 = await FinAccountService.ensure_system_accounts(cast(Any, db))
    assert sys_accs_2["payment_clearing"].id == sys_accs["payment_clearing"].id

    # 2. Initialize tenant settlement account & projection
    tenant_acc, proj = await FinAccountService.ensure_tenant_settlement_account(
        cast(Any, db), tenant_id=42
    )
    assert tenant_acc.tenant_id == 42
    assert tenant_acc.kind == "tenant_settlement"
    assert tenant_acc.currency == "RUB"
    assert proj.tenant_id == 42
    assert proj.account_id == tenant_acc.id
    assert proj.balance_kopecks == 0
    assert proj.version == 0


@pytest.mark.anyio
async def test_post_payment_transaction_double_entry_and_projection():
    """Verify double-entry posting for customer top-up (Dr payment_clearing, Cr tenant_settlement)."""
    db = FakeFinancialDb()
    sys_accs = await FinAccountService.ensure_system_accounts(cast(Any, db))
    tenant_acc, _ = await FinAccountService.ensure_tenant_settlement_account(
        cast(Any, db), tenant_id=10
    )

    req = FinPostingRequest(
        tenant_id=10,
        operation_id="pay_001",
        kind="payment",
        source_project="MenuBuilder",
        source_type="yookassa_payment",
        source_id="yk_12345",
        actor="user_10",
        correlation_id="corr_001",
        entries=[
            FinPostingEntryRequest(
                account_id=sys_accs["payment_clearing"].id,
                debit_kopecks=1000_00,  # 1000 RUB
                credit_kopecks=0,
            ),
            FinPostingEntryRequest(
                account_id=tenant_acc.id,
                debit_kopecks=0,
                credit_kopecks=1000_00,  # 1000 RUB
            ),
        ],
    )

    tx = await FinPostingService.post_transaction(cast(Any, db), req)

    assert tx.id == 1
    assert tx.status == "posted"
    assert tx.debit_kopecks == 1000_00
    assert tx.credit_kopecks == 1000_00
    assert tx.posted_at is not None

    # Check projection updated
    proj = await FinProjectionService.get_projection(cast(Any, db), tenant_id=10)
    assert proj.balance_kopecks == 1000_00
    assert proj.version == 1
    assert proj.last_transaction_id == tx.id


@pytest.mark.anyio
async def test_post_usage_charge_decrements_tenant_balance():
    """Verify double-entry posting for consumption (Dr tenant_settlement, Cr usage_revenue)."""
    db = FakeFinancialDb()
    sys_accs = await FinAccountService.ensure_system_accounts(cast(Any, db))
    tenant_acc, proj = await FinAccountService.ensure_tenant_settlement_account(
        cast(Any, db), tenant_id=10
    )
    # Give tenant initial 1000 RUB
    proj.balance_kopecks = 1000_00
    proj.version = 1

    req = FinPostingRequest(
        tenant_id=10,
        operation_id="usage_001",
        kind="usage",
        source_project="MenuBuilder",
        source_type="daily_usage",
        source_id="daily_2026-09-20",
        actor="usage_worker",
        correlation_id="corr_002",
        entries=[
            FinPostingEntryRequest(
                account_id=tenant_acc.id,
                debit_kopecks=250_00,  # 250 RUB debit
                credit_kopecks=0,
            ),
            FinPostingEntryRequest(
                account_id=sys_accs["usage_revenue"].id,
                debit_kopecks=0,
                credit_kopecks=250_00,  # 250 RUB credit
            ),
        ],
        calculation_snapshot={"discarded_kopecks": 45, "calculated_kopecks": 250_45},
    )

    tx = await FinPostingService.post_transaction(cast(Any, db), req)
    assert tx.status == "posted"

    # Tenant balance decremented: 1000_00 - 250_00 = 750_00
    proj_updated = await FinProjectionService.get_projection(
        cast(Any, db), tenant_id=10
    )
    assert proj_updated.balance_kopecks == 750_00
    assert proj_updated.version == 2
    assert proj_updated.last_transaction_id == tx.id


@pytest.mark.anyio
async def test_float_rejection():
    """Verify that floats are strictly rejected in all financial postings."""
    db = FakeFinancialDb()
    sys_accs = await FinAccountService.ensure_system_accounts(cast(Any, db))
    tenant_acc, _ = await FinAccountService.ensure_tenant_settlement_account(
        cast(Any, db), tenant_id=10
    )

    # 1. Pydantic level rejection of fractional float
    with pytest.raises(pydantic.ValidationError):
        FinPostingEntryRequest(
            account_id=sys_accs["payment_clearing"].id,
            debit_kopecks=500.50,  # type: ignore
            credit_kopecks=0,
        )

    # 2. Service level strict type validation (rejects any non-int type)
    entry1 = FinPostingEntryRequest(
        account_id=sys_accs["payment_clearing"].id,
        debit_kopecks=500_00,
        credit_kopecks=0,
    )
    object.__setattr__(entry1, "debit_kopecks", 500.50)
    entry2 = FinPostingEntryRequest(
        account_id=tenant_acc.id,
        debit_kopecks=0,
        credit_kopecks=500_00,
    )
    req = FinPostingRequest(
        tenant_id=10,
        operation_id="op_float",
        kind="payment",
        source_project="MenuBuilder",
        source_type="test",
        source_id="test_float",
        actor="test",
        correlation_id="corr",
        entries=[entry1, entry2],
    )
    with pytest.raises(FinValidationError, match="float forbidden"):
        FinPostingService._validate_types_and_amounts(req)


@pytest.mark.anyio
async def test_kopecks_whole_rubles_rounding_rule():
    """Verify that non-multiples of 100 kopecks are rejected with FinValidationError."""
    req = FinPostingRequest(
        tenant_id=10,
        operation_id="op_rubles",
        kind="payment",
        source_project="MenuBuilder",
        source_type="test",
        source_id="test_round",
        actor="test",
        correlation_id="corr",
        entries=[
            FinPostingEntryRequest(
                account_id=1,
                debit_kopecks=500_50,  # Not multiple of 100
                credit_kopecks=0,
            ),
            FinPostingEntryRequest(
                account_id=2,
                debit_kopecks=0,
                credit_kopecks=500_50,
            ),
        ],
    )
    with pytest.raises(FinValidationError, match="multiple of 100 kopecks"):
        FinPostingService._validate_types_and_amounts(req)


@pytest.mark.anyio
async def test_double_entry_imbalance_rejection():
    """Verify that debit != credit or zero amount raises FinImbalanceError."""
    # 1. debit != credit
    req_imbalance = FinPostingRequest(
        tenant_id=10,
        operation_id="op_imb",
        kind="payment",
        source_project="MenuBuilder",
        source_type="test",
        source_id="test_imb",
        actor="test",
        correlation_id="corr",
        entries=[
            FinPostingEntryRequest(
                account_id=1,
                debit_kopecks=500_00,
                credit_kopecks=0,
            ),
            FinPostingEntryRequest(
                account_id=2,
                debit_kopecks=0,
                credit_kopecks=400_00,  # 400 != 500
            ),
        ],
    )
    with pytest.raises(FinImbalanceError, match="Double-entry imbalance"):
        FinPostingService._validate_types_and_amounts(req_imbalance)

    # 2. debit = credit = 0
    req_zero = FinPostingRequest(
        tenant_id=10,
        operation_id="op_zero",
        kind="payment",
        source_project="MenuBuilder",
        source_type="test",
        source_id="test_zero",
        actor="test",
        correlation_id="corr",
        entries=[
            FinPostingEntryRequest(
                account_id=1,
                debit_kopecks=0,
                credit_kopecks=0,
            ),
            FinPostingEntryRequest(
                account_id=2,
                debit_kopecks=0,
                credit_kopecks=0,
            ),
        ],
    )
    with pytest.raises(FinValidationError):
        FinPostingService._validate_types_and_amounts(req_zero)


@pytest.mark.anyio
async def test_single_side_entry_rule():
    """Verify that each entry must be strictly one-sided (debit > 0 xor credit > 0)."""
    req_both = FinPostingRequest(
        tenant_id=10,
        operation_id="op_both",
        kind="payment",
        source_project="MenuBuilder",
        source_type="test",
        source_id="test_both",
        actor="test",
        correlation_id="corr",
        entries=[
            FinPostingEntryRequest(
                account_id=1,
                debit_kopecks=200_00,
                credit_kopecks=200_00,  # Both sides positive!
            ),
            FinPostingEntryRequest(
                account_id=2,
                debit_kopecks=200_00,
                credit_kopecks=200_00,
            ),
        ],
    )
    with pytest.raises(FinValidationError, match="exactly one positive side"):
        FinPostingService._validate_types_and_amounts(req_both)


@pytest.mark.anyio
async def test_idempotent_posting_replay():
    """Verify that submitting identical operation_id returns the existing transaction."""
    db = FakeFinancialDb()
    sys_accs = await FinAccountService.ensure_system_accounts(cast(Any, db))
    tenant_acc, _ = await FinAccountService.ensure_tenant_settlement_account(
        cast(Any, db), tenant_id=10
    )

    req = FinPostingRequest(
        tenant_id=10,
        operation_id="idempotent_test_op",
        kind="payment",
        source_project="MenuBuilder",
        source_type="yookassa",
        source_id="yk_999",
        actor="test",
        correlation_id="corr_idempotent",
        entries=[
            FinPostingEntryRequest(
                account_id=sys_accs["payment_clearing"].id,
                debit_kopecks=500_00,
                credit_kopecks=0,
            ),
            FinPostingEntryRequest(
                account_id=tenant_acc.id,
                debit_kopecks=0,
                credit_kopecks=500_00,
            ),
        ],
    )

    tx1 = await FinPostingService.post_transaction(cast(Any, db), req)
    tx2 = await FinPostingService.post_transaction(cast(Any, db), req)

    assert tx1.id == tx2.id
    # Projection version should remain 1 (not double-incremented)
    proj = await FinProjectionService.get_projection(cast(Any, db), tenant_id=10)
    assert proj.version == 1
    assert proj.balance_kopecks == 500_00


@pytest.mark.anyio
async def test_duplicate_posting_conflict_detection():
    """Verify that submitting same operation_id with conflicting amounts raises FinDuplicatePostingError."""
    db = FakeFinancialDb()
    sys_accs = await FinAccountService.ensure_system_accounts(cast(Any, db))
    tenant_acc, _ = await FinAccountService.ensure_tenant_settlement_account(
        cast(Any, db), tenant_id=10
    )

    req1 = FinPostingRequest(
        tenant_id=10,
        operation_id="conflict_test_op",
        kind="payment",
        source_project="MenuBuilder",
        source_type="yookassa",
        source_id="yk_conflict",
        actor="test",
        correlation_id="corr_1",
        entries=[
            FinPostingEntryRequest(
                account_id=sys_accs["payment_clearing"].id,
                debit_kopecks=500_00,
                credit_kopecks=0,
            ),
            FinPostingEntryRequest(
                account_id=tenant_acc.id,
                debit_kopecks=0,
                credit_kopecks=500_00,
            ),
        ],
    )
    await FinPostingService.post_transaction(cast(Any, db), req1)

    # Conflicting amount with same operation_id
    req2 = FinPostingRequest(
        tenant_id=10,
        operation_id="conflict_test_op",
        kind="payment",
        source_project="MenuBuilder",
        source_type="yookassa",
        source_id="yk_conflict",
        actor="test",
        correlation_id="corr_2",
        entries=[
            FinPostingEntryRequest(
                account_id=sys_accs["payment_clearing"].id,
                debit_kopecks=700_00,  # Conflict: 700 != 500
                credit_kopecks=0,
            ),
            FinPostingEntryRequest(
                account_id=tenant_acc.id,
                debit_kopecks=0,
                credit_kopecks=700_00,
            ),
        ],
    )
    with pytest.raises(FinDuplicatePostingError):
        await FinPostingService.post_transaction(cast(Any, db), req2)


@pytest.mark.anyio
async def test_tenant_isolation_violation_rejection():
    """Verify that posting entries referencing another tenant's account raises FinTenantIsolationError."""
    db = FakeFinancialDb()
    tenant_1_acc, _ = await FinAccountService.ensure_tenant_settlement_account(
        cast(Any, db), tenant_id=1
    )
    tenant_2_acc, _ = await FinAccountService.ensure_tenant_settlement_account(
        cast(Any, db), tenant_id=2
    )

    # Transaction for tenant 1 references tenant 2's account
    req = FinPostingRequest(
        tenant_id=1,
        operation_id="iso_fail",
        kind="payment",
        source_project="MenuBuilder",
        source_type="test",
        source_id="test_iso",
        actor="test",
        correlation_id="corr_iso",
        entries=[
            FinPostingEntryRequest(
                account_id=tenant_1_acc.id,
                debit_kopecks=500_00,
                credit_kopecks=0,
            ),
            FinPostingEntryRequest(
                account_id=tenant_2_acc.id,  # Belongs to tenant 2!
                debit_kopecks=0,
                credit_kopecks=500_00,
            ),
        ],
    )
    with pytest.raises(FinTenantIsolationError):
        await FinPostingService.post_transaction(cast(Any, db), req)


@pytest.mark.anyio
async def test_reversal_happy_path_and_invariants():
    """Verify reversal of posted transaction: inverse entries, original link, balance restoration."""
    db = FakeFinancialDb()
    sys_accs = await FinAccountService.ensure_system_accounts(cast(Any, db))
    tenant_acc, _ = await FinAccountService.ensure_tenant_settlement_account(
        cast(Any, db), tenant_id=10
    )

    # 1. Post original payment (1000 RUB)
    pay_req = FinPostingRequest(
        tenant_id=10,
        operation_id="orig_payment_1",
        kind="payment",
        source_project="MenuBuilder",
        source_type="payment",
        source_id="pay_orig",
        actor="user",
        correlation_id="corr_orig",
        entries=[
            FinPostingEntryRequest(
                account_id=sys_accs["payment_clearing"].id,
                debit_kopecks=1000_00,
                credit_kopecks=0,
            ),
            FinPostingEntryRequest(
                account_id=tenant_acc.id,
                debit_kopecks=0,
                credit_kopecks=1000_00,
            ),
        ],
    )
    orig_tx = await FinPostingService.post_transaction(cast(Any, db), pay_req)
    proj = await FinProjectionService.get_projection(cast(Any, db), tenant_id=10)
    assert proj.balance_kopecks == 1000_00
    assert proj.version == 1

    # 2. Reverse payment
    rev_req = FinReversalRequest(
        tenant_id=10,
        transaction_id=orig_tx.id,
        operation_id="rev_payment_1",
        actor="admin",
        correlation_id="corr_rev",
        reason="Chargeback requested",
    )
    rev_tx = await FinReversalService.reverse_transaction(cast(Any, db), rev_req)

    assert rev_tx.kind == "reversal"
    assert rev_tx.corrects_transaction_id == orig_tx.id
    assert rev_tx.status == "posted"

    # Inverted entries checked: Dr tenant_settlement 1000_00, Cr payment_clearing 1000_00
    entries = [e for e in db.entries if e.transaction_id == rev_tx.id]
    assert len(entries) == 2
    clearing_entry = next(
        e for e in entries if e.account_id == sys_accs["payment_clearing"].id
    )
    settlement_entry = next(e for e in entries if e.account_id == tenant_acc.id)
    assert clearing_entry.credit_kopecks == 1000_00
    assert clearing_entry.debit_kopecks == 0
    assert settlement_entry.debit_kopecks == 1000_00
    assert settlement_entry.credit_kopecks == 0

    # Balance back to 0, version = 2
    proj_after_rev = await FinProjectionService.get_projection(
        cast(Any, db), tenant_id=10
    )
    assert proj_after_rev.balance_kopecks == 0
    assert proj_after_rev.version == 2


@pytest.mark.anyio
async def test_reversal_edge_cases_and_prohibitions():
    """Verify that double reversal and reversal of reversal are strictly prohibited."""
    db = FakeFinancialDb()
    sys_accs = await FinAccountService.ensure_system_accounts(cast(Any, db))
    tenant_acc, _ = await FinAccountService.ensure_tenant_settlement_account(
        cast(Any, db), tenant_id=10
    )

    # Post payment
    orig_tx = await FinPostingService.post_transaction(
        cast(Any, db),
        FinPostingRequest(
            tenant_id=10,
            operation_id="orig_tx_2",
            kind="payment",
            source_project="MenuBuilder",
            source_type="payment",
            source_id="p_2",
            actor="user",
            correlation_id="c_2",
            entries=[
                FinPostingEntryRequest(
                    account_id=sys_accs["payment_clearing"].id,
                    debit_kopecks=100_00,
                    credit_kopecks=0,
                ),
                FinPostingEntryRequest(
                    account_id=tenant_acc.id,
                    debit_kopecks=0,
                    credit_kopecks=100_00,
                ),
            ],
        ),
    )

    # First reversal succeeds
    rev_tx = await FinReversalService.reverse_transaction(
        cast(Any, db),
        FinReversalRequest(
            tenant_id=10,
            transaction_id=orig_tx.id,
            operation_id="rev_1",
            actor="admin",
            correlation_id="c_r1",
            reason="reason 1",
        ),
    )

    # Second reversal of same transaction must fail
    with pytest.raises(FinReversalError, match="has already been reversed"):
        await FinReversalService.reverse_transaction(
            cast(Any, db),
            FinReversalRequest(
                tenant_id=10,
                transaction_id=orig_tx.id,
                operation_id="rev_2",
                actor="admin",
                correlation_id="c_r2",
                reason="reason 2",
            ),
        )

    # Reversing a reversal must fail
    with pytest.raises(FinReversalError, match="Cannot reverse a reversal"):
        await FinReversalService.reverse_transaction(
            cast(Any, db),
            FinReversalRequest(
                tenant_id=10,
                transaction_id=rev_tx.id,
                operation_id="rev_of_rev",
                actor="admin",
                correlation_id="c_ror",
                reason="reason 3",
            ),
        )


@pytest.mark.anyio
async def test_projection_rebuild_from_ledger():
    """Verify projection rebuild restores true balance when projection was corrupted or out of sync."""
    db = FakeFinancialDb()
    sys_accs = await FinAccountService.ensure_system_accounts(cast(Any, db))
    tenant_acc, proj = await FinAccountService.ensure_tenant_settlement_account(
        cast(Any, db), tenant_id=10
    )

    # Post payment: +500 RUB
    await FinPostingService.post_transaction(
        cast(Any, db),
        FinPostingRequest(
            tenant_id=10,
            operation_id="p1",
            kind="payment",
            source_project="MenuBuilder",
            source_type="payment",
            source_id="p1",
            actor="u",
            correlation_id="c",
            entries=[
                FinPostingEntryRequest(
                    account_id=sys_accs["payment_clearing"].id,
                    debit_kopecks=500_00,
                    credit_kopecks=0,
                ),
                FinPostingEntryRequest(
                    account_id=tenant_acc.id,
                    debit_kopecks=0,
                    credit_kopecks=500_00,
                ),
            ],
        ),
    )
    # Post usage: -150 RUB
    await FinPostingService.post_transaction(
        cast(Any, db),
        FinPostingRequest(
            tenant_id=10,
            operation_id="u1",
            kind="usage",
            source_project="MenuBuilder",
            source_type="usage",
            source_id="u1",
            actor="u",
            correlation_id="c",
            entries=[
                FinPostingEntryRequest(
                    account_id=tenant_acc.id,
                    debit_kopecks=150_00,
                    credit_kopecks=0,
                ),
                FinPostingEntryRequest(
                    account_id=sys_accs["usage_revenue"].id,
                    debit_kopecks=0,
                    credit_kopecks=150_00,
                ),
            ],
        ),
    )

    assert proj.balance_kopecks == 350_00

    # Artificially corrupt the projection
    proj.balance_kopecks = 9999_00

    # Rebuild projection
    rebuilt_proj, prev, true_bal = await FinProjectionService.rebuild_projection(
        cast(Any, db), tenant_id=10, actor="reconciliation_agent"
    )

    assert prev == 9999_00
    assert true_bal == 350_00
    assert rebuilt_proj.balance_kopecks == 350_00
    assert rebuilt_proj.version == 3


@pytest.mark.anyio
async def test_optimistic_concurrency_version_protection():
    """Verify that expected_projection_version protects against concurrent postings."""
    db = FakeFinancialDb()
    sys_accs = await FinAccountService.ensure_system_accounts(cast(Any, db))
    tenant_acc, _ = await FinAccountService.ensure_tenant_settlement_account(
        cast(Any, db), tenant_id=10
    )

    # Post with expected version 0 -> succeeds, version becomes 1
    req1 = FinPostingRequest(
        tenant_id=10,
        operation_id="ver_0",
        kind="payment",
        source_project="MenuBuilder",
        source_type="test",
        source_id="v0",
        actor="u",
        correlation_id="c",
        expected_projection_version=0,
        entries=[
            FinPostingEntryRequest(
                account_id=sys_accs["payment_clearing"].id,
                debit_kopecks=100_00,
                credit_kopecks=0,
            ),
            FinPostingEntryRequest(
                account_id=tenant_acc.id,
                debit_kopecks=0,
                credit_kopecks=100_00,
            ),
        ],
    )
    await FinPostingService.post_transaction(cast(Any, db), req1)

    # Post with outdated expected version 0 -> must fail with FinConcurrencyError
    req2 = FinPostingRequest(
        tenant_id=10,
        operation_id="ver_0_stale",
        kind="payment",
        source_project="MenuBuilder",
        source_type="test",
        source_id="v0_stale",
        actor="u",
        correlation_id="c",
        expected_projection_version=0,  # Stale version! Current is 1
        entries=[
            FinPostingEntryRequest(
                account_id=sys_accs["payment_clearing"].id,
                debit_kopecks=100_00,
                credit_kopecks=0,
            ),
            FinPostingEntryRequest(
                account_id=tenant_acc.id,
                debit_kopecks=0,
                credit_kopecks=100_00,
            ),
        ],
    )
    with pytest.raises(FinConcurrencyError, match="Optimistic lock violation"):
        await FinPostingService.post_transaction(cast(Any, db), req2)


@pytest.mark.anyio
async def test_reconciliation_matched_happy_path():
    """Verify reconciliation run succeeds with status='matched' and constraint compliance."""
    db = FakeFinancialDb()
    sys_accs = await FinAccountService.ensure_system_accounts(cast(Any, db))
    tenant_acc, _ = await FinAccountService.ensure_tenant_settlement_account(
        cast(Any, db), tenant_id=10
    )

    # Post valid transaction
    await FinPostingService.post_transaction(
        cast(Any, db),
        FinPostingRequest(
            tenant_id=10,
            operation_id="rec_tx_1",
            kind="payment",
            source_project="MenuBuilder",
            source_type="payment",
            source_id="rec_p1",
            actor="u",
            correlation_id="c",
            entries=[
                FinPostingEntryRequest(
                    account_id=sys_accs["payment_clearing"].id,
                    debit_kopecks=300_00,
                    credit_kopecks=0,
                ),
                FinPostingEntryRequest(
                    account_id=tenant_acc.id,
                    debit_kopecks=0,
                    credit_kopecks=300_00,
                ),
            ],
            calculation_snapshot={"discarded_kopecks": 0, "calculated_kopecks": 300_00},
        ),
    )

    now = datetime.now(UTC)
    req = FinReconciliationRequest(
        period_start=now - timedelta(hours=1),
        period_end=now + timedelta(hours=1),
        tenant_id=10,
    )

    run = await FinReconciliationService.run_reconciliation(cast(Any, db), req)

    assert run.status == "matched"
    assert run.mismatch_count == 0
    assert run.balance_difference_kopecks == 0
    assert run.debit_kopecks == 300_00
    assert run.credit_kopecks == 300_00
    assert run.posted_kopecks == 300_00
    assert run.discarded_kopecks == 0
    assert run.calculated_kopecks == 300_00
    assert run.finished_at is not None


@pytest.mark.anyio
async def test_immutability_guard():
    """Verify that FinPostingService.prevent_mutation raises FinImmutableError for posted rows."""
    posted_tx = FinLedgerTransaction(status="posted")
    with pytest.raises(FinImmutableError, match="strictly prohibited"):
        FinPostingService.prevent_mutation(posted_tx)

    entry = FinLedgerEntry()
    with pytest.raises(FinImmutableError, match="strictly prohibited"):
        FinPostingService.prevent_mutation(entry)


# =============================================================================
# HTTP API Integration Tests
# =============================================================================


@pytest.fixture
def fake_db_fixture():
    db = FakeFinancialDb()
    return db


@pytest.mark.anyio
async def test_http_api_finance_balance_and_transactions(fake_db_fixture):
    """Test GET /api/v1/finance/balance and GET /api/v1/finance/transactions."""
    app.dependency_overrides[get_db] = lambda: fake_db_fixture

    # Pre-populate
    _tenant_acc, proj = await FinAccountService.ensure_tenant_settlement_account(
        cast(Any, fake_db_fixture), tenant_id=55
    )
    proj.balance_kopecks = 750_00
    proj.version = 1

    token = create_access_token(
        {
            "sub": "test_owner",
            "org_id": 55,
            "role": "owner",
            "role_id": 5,
        }
    )

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # 1. GET balance
        resp = await client.get(
            "/api/v1/finance/balance",
            headers={"Authorization": f"Bearer {token}"},
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data["tenant_id"] == 55
        assert data["balance_kopecks"] == 750_00
        assert data["balance_rubles"] == 750.0
        assert data["version"] == 1

        # 2. GET transactions
        resp_tx = await client.get(
            "/api/v1/finance/transactions",
            headers={"Authorization": f"Bearer {token}"},
        )
        assert resp_tx.status_code == 200
        assert isinstance(resp_tx.json(), list)


@pytest.mark.anyio
async def test_http_api_internal_post_and_reversal(fake_db_fixture):
    """Test POST /api/internal/v1/finance/post and POST /api/internal/v1/finance/reversal."""
    app.dependency_overrides[get_db] = lambda: fake_db_fixture
    settings.internal_service_key = "test_internal_key"

    sys_accs = await FinAccountService.ensure_system_accounts(
        cast(Any, fake_db_fixture)
    )
    tenant_acc, _ = await FinAccountService.ensure_tenant_settlement_account(
        cast(Any, fake_db_fixture), tenant_id=55
    )

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # 1. Post transaction
        post_payload = {
            "tenant_id": 55,
            "operation_id": "api_post_1",
            "kind": "payment",
            "source_project": "MenuBuilder",
            "source_type": "yookassa",
            "source_id": "yk_api_1",
            "actor": "internal_system",
            "correlation_id": "corr_api_1",
            "entries": [
                {
                    "account_id": sys_accs["payment_clearing"].id,
                    "debit_kopecks": 500_00,
                    "credit_kopecks": 0,
                },
                {
                    "account_id": tenant_acc.id,
                    "debit_kopecks": 0,
                    "credit_kopecks": 500_00,
                },
            ],
        }

        resp = await client.post(
            "/api/internal/v1/finance/post",
            json=post_payload,
            headers={"X-Internal-Service-Key": "test_internal_key"},
        )
        assert resp.status_code == 201
        tx_data = resp.json()
        assert tx_data["id"] == 1
        assert tx_data["status"] == "posted"
        assert tx_data["debit_kopecks"] == 500_00

        # 2. Reverse transaction
        rev_payload = {
            "tenant_id": 55,
            "transaction_id": 1,
            "operation_id": "api_rev_1",
            "actor": "admin",
            "correlation_id": "corr_rev_api_1",
            "reason": "Test reversal",
        }
        resp_rev = await client.post(
            "/api/internal/v1/finance/reversal",
            json=rev_payload,
            headers={"X-Internal-Service-Key": "test_internal_key"},
        )
        assert resp_rev.status_code == 201
        rev_data = resp_rev.json()
        assert rev_data["kind"] == "reversal"
        assert rev_data["corrects_transaction_id"] == 1


@pytest.mark.anyio
async def test_reconciliation_corruption_detection_and_auto_rebuild():
    """Verify reconciliation detects projection corruption and can auto-rebuild it."""
    db = FakeFinancialDb()
    sys_accs = await FinAccountService.ensure_system_accounts(cast(Any, db))
    tenant_acc, proj = await FinAccountService.ensure_tenant_settlement_account(
        cast(Any, db), tenant_id=10
    )

    # Post payment (500 RUB)
    await FinPostingService.post_transaction(
        cast(Any, db),
        FinPostingRequest(
            tenant_id=10,
            operation_id="rec_corr_tx",
            kind="payment",
            source_project="MenuBuilder",
            source_type="payment",
            source_id="p_corr",
            actor="u",
            correlation_id="c",
            entries=[
                FinPostingEntryRequest(
                    account_id=sys_accs["payment_clearing"].id,
                    debit_kopecks=500_00,
                    credit_kopecks=0,
                ),
                FinPostingEntryRequest(
                    account_id=tenant_acc.id,
                    debit_kopecks=0,
                    credit_kopecks=500_00,
                ),
            ],
        ),
    )

    # Corrupt projection
    proj.balance_kopecks = 200_00  # Corrupted: 200 != 500

    now = datetime.now(UTC)
    req = FinReconciliationRequest(
        period_start=now - timedelta(hours=1),
        period_end=now + timedelta(hours=1),
        tenant_id=10,
        auto_rebuild_projection=True,
    )

    run = await FinReconciliationService.run_reconciliation(cast(Any, db), req)

    assert run.status == "mismatch"
    assert run.mismatch_count == 1
    assert run.balance_difference_kopecks == 300_00
    assert run.details is not None
    assert len(run.details["projection_mismatches"]) == 1
    assert 10 in run.details.get("rebuilt_projections", [])

    # Verify projection was restored to true ledger balance (500_00)
    assert proj.balance_kopecks == 500_00


@pytest.mark.anyio
async def test_http_api_internal_reconciliation_and_rebuild(fake_db_fixture):
    """Test POST /api/internal/v1/finance/reconciliation and /rebuild-projection/{tenant_id}."""
    app.dependency_overrides[get_db] = lambda: fake_db_fixture
    settings.internal_service_key = "test_internal_key"

    _tenant_acc, proj = await FinAccountService.ensure_tenant_settlement_account(
        cast(Any, fake_db_fixture), tenant_id=77
    )
    proj.balance_kopecks = 100_00

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # 1. Rebuild projection endpoint
        resp_rebuild = await client.post(
            "/api/internal/v1/finance/rebuild-projection/77",
            headers={"X-Internal-Service-Key": "test_internal_key"},
        )
        assert resp_rebuild.status_code == 200
        rebuild_data = resp_rebuild.json()
        assert rebuild_data["tenant_id"] == 77
        assert (
            rebuild_data["balance_kopecks"] == 0
        )  # No transactions, so true balance is 0

        # 2. Trigger reconciliation endpoint
        now = datetime.now(UTC)
        rec_payload = {
            "period_start": (now - timedelta(days=1)).isoformat(),
            "period_end": (now + timedelta(days=1)).isoformat(),
            "tenant_id": 77,
        }
        resp_rec = await client.post(
            "/api/internal/v1/finance/reconciliation",
            json=rec_payload,
            headers={"X-Internal-Service-Key": "test_internal_key"},
        )
        assert resp_rec.status_code == 200
        rec_data = resp_rec.json()
        assert rec_data["status"] == "matched"
        assert rec_data["mismatch_count"] == 0

        # 3. List reconciliation runs
        resp_runs = await client.get(
            "/api/internal/v1/finance/reconciliation/runs",
            headers={"X-Internal-Service-Key": "test_internal_key"},
        )
        assert resp_runs.status_code == 200
        runs_list = resp_runs.json()
        assert len(runs_list) >= 1
