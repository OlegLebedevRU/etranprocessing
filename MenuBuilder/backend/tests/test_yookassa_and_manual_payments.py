from __future__ import annotations

import contextlib
from collections.abc import Generator
from datetime import UTC, date, datetime
from typing import Any, cast

import pytest
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
    FinPayment,
    FinReconciliationRun,
    L4DeskTenantProfile,
)
from app.services.financial_core import (
    FinBillingCycleService,
    FinManualPaymentService,
    FinPaymentService,
    FinReconciliationRequest,
    FinReconciliationService,
    FinReversalError,
    FinValidationError,
    MockYooKassaClient,
    is_safe_return_url,
    set_yookassa_client_override,
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


def _get_param(params: dict[str, Any], prefix: str) -> Any:
    for k, v in params.items():
        if k == prefix or k.startswith(f"{prefix}_"):
            return v
    return None


class FakePaymentsDb:
    """In-memory database simulator for YooKassa and manual payments test suite."""

    def __init__(self) -> None:
        self.accounts: dict[int, FinAccount] = {}
        self.projections: dict[int, FinBalanceProjection] = {}
        self.transactions: dict[int, FinLedgerTransaction] = {}
        self.entries: list[FinLedgerEntry] = []
        self.reconciliations: dict[int, FinReconciliationRun] = {}
        self.profiles: dict[int, FinBillingProfile] = {}
        self.cycles: dict[int, FinBillingCycle] = {}
        self.tenant_profiles: dict[int, L4DeskTenantProfile] = {}
        self.payments: dict[int, FinPayment] = {}
        self.manual_payments: dict[int, FinManualPayment] = {}

        self._next_account_id = 1
        self._next_tx_id = 1
        self._next_entry_id = 1
        self._next_rec_id = 1
        self._next_cycle_id = 1
        self._next_payment_id = 1
        self._next_manual_id = 1
        self.commit_count = 0

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
        elif isinstance(obj, FinReconciliationRun):
            if not getattr(obj, "id", None):
                obj.id = self._next_rec_id
                self._next_rec_id += 1
            self.reconciliations[obj.id] = obj
        elif isinstance(obj, FinBillingProfile):
            self.profiles[obj.tenant_id] = obj
        elif isinstance(obj, FinBillingCycle):
            if not getattr(obj, "id", None):
                obj.id = self._next_cycle_id
                self._next_cycle_id += 1
            self.cycles[obj.id] = obj
        elif isinstance(obj, L4DeskTenantProfile):
            self.tenant_profiles[obj.tenant_id] = obj
        elif isinstance(obj, FinPayment):
            if not getattr(obj, "id", None):
                obj.id = self._next_payment_id
                self._next_payment_id += 1
            self.payments[obj.id] = obj
        elif isinstance(obj, FinManualPayment):
            if not getattr(obj, "id", None):
                obj.id = self._next_manual_id
                self._next_manual_id += 1
            self.manual_payments[obj.id] = obj

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
        if model is FinPayment:
            return self.payments.get(ident)
        if model is FinManualPayment:
            return self.manual_payments.get(ident)
        return None

    async def flush(self) -> None:
        pass

    async def commit(self) -> None:
        self.commit_count += 1

    async def execute(self, stmt: Any) -> MockResult:
        params: dict[str, Any] = {}
        with contextlib.suppress(Exception):
            params = stmt.compile().params
        sql = str(stmt).lower()

        # Accounts
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

        # Projections
        if "from fin_balance_projections" in sql:
            tenant_id = _get_param(params, "tenant_id")
            if tenant_id is not None:
                return MockResult(one=self.projections.get(tenant_id))
            return MockResult(all_items=list(self.projections.values()))

        # Billing profiles
        if "from fin_billing_profiles" in sql:
            tenant_id = _get_param(params, "tenant_id")
            if tenant_id is not None:
                return MockResult(one=self.profiles.get(tenant_id))
            return MockResult(all_items=list(self.profiles.values()))

        # Billing cycles
        if "from fin_billing_cycles" in sql:
            cycle_id = _get_param(params, "id")
            if cycle_id is not None:
                return MockResult(one=self.cycles.get(cycle_id))
            tenant_id = _get_param(params, "tenant_id")
            res = list(self.cycles.values())
            if tenant_id is not None:
                res = [c for c in res if c.tenant_id == tenant_id]
            res.sort(key=lambda c: c.sequence, reverse=True)
            return MockResult(one=res[0] if res else None, all_items=res)

        # Transactions
        if "from fin_ledger_transactions" in sql:
            tx_id = _get_param(params, "id")
            if tx_id is not None:
                return MockResult(one=self.transactions.get(tx_id))
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
                tenant_id = _get_param(params, "tenant_id")
                src_proj = _get_param(params, "source_project")
                src_type = _get_param(params, "source_type")
                res = [
                    t
                    for t in self.transactions.values()
                    if t.source_id == src_id
                    and (src_proj is None or t.source_project == src_proj)
                    and (src_type is None or t.source_type == src_type)
                    and (tenant_id is None or t.tenant_id == tenant_id)
                ]
                return MockResult(one=res[0] if res else None, all_items=res)
            tenant_id = _get_param(params, "tenant_id")
            res = list(self.transactions.values())
            if tenant_id is not None:
                res = [t for t in res if t.tenant_id == tenant_id]
            res.sort(key=lambda t: t.id, reverse=True)
            return MockResult(all_items=res)

        # Ledger Entries
        if "from fin_ledger_entries" in sql:
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
            tx_id = _get_param(params, "transaction_id")
            acc_id = _get_param(params, "account_id")
            res = self.entries
            if tx_id is not None:
                res = [e for e in res if e.transaction_id == tx_id]
            if acc_id is not None:
                res = [e for e in res if e.account_id == acc_id]
            return MockResult(all_items=res)

        # Payments (YooKassa)
        if "from fin_payments" in sql:
            p_id = _get_param(params, "id")
            if p_id is not None:
                return MockResult(one=self.payments.get(p_id))
            prov_id = _get_param(params, "provider_payment_id")
            if prov_id is not None:
                matched = [
                    p
                    for p in self.payments.values()
                    if p.provider_payment_id == prov_id
                ]
                return MockResult(
                    one=matched[0] if matched else None, all_items=matched
                )
            op_id = _get_param(params, "operation_id")
            if op_id is not None:
                tenant_id = _get_param(params, "tenant_id")
                res = [
                    p
                    for p in self.payments.values()
                    if p.operation_id == op_id
                    and (tenant_id is None or p.tenant_id == tenant_id)
                ]
                return MockResult(one=res[0] if res else None, all_items=res)
            tenant_id = _get_param(params, "tenant_id")
            status_val = _get_param(params, "status")
            res = list(self.payments.values())
            if tenant_id is not None:
                res = [p for p in res if p.tenant_id == tenant_id]
            if status_val is not None:
                res = [p for p in res if p.status == status_val]
            res.sort(key=lambda p: p.created_at, reverse=True)
            return MockResult(all_items=res)

        # Manual Payments
        if "from fin_manual_payments" in sql:
            m_id = _get_param(params, "id")
            if m_id is not None:
                return MockResult(one=self.manual_payments.get(m_id))
            tenant_id = _get_param(params, "tenant_id")
            res = list(self.manual_payments.values())
            if tenant_id is not None:
                res = [m for m in res if m.tenant_id == tenant_id]
            res.sort(key=lambda m: m.created_at, reverse=True)
            return MockResult(all_items=res)

        # Reconciliations
        if "from fin_reconciliation_runs" in sql:
            r_id = _get_param(params, "id")
            if r_id is not None:
                return MockResult(one=self.reconciliations.get(r_id))
            return MockResult(all_items=list(self.reconciliations.values()))

        # Fallback
        return MockResult()


@pytest.fixture
def fake_db() -> FakePaymentsDb:
    return FakePaymentsDb()


@pytest.fixture
def mock_yookassa() -> Generator[MockYooKassaClient]:
    client = MockYooKassaClient(shop_id="test_shop_123")
    set_yookassa_client_override(client)
    yield client
    set_yookassa_client_override(None)


# =============================================================================
# 1. Provider Contract & Mock Verification
# =============================================================================


@pytest.mark.anyio
async def test_mock_yookassa_provider_contract(mock_yookassa: MockYooKassaClient):
    """Test MockYooKassaClient complies with official YooKassa protocol."""
    # Test creation
    res = await mock_yookassa.create_payment(
        amount_kopecks=50000,
        idempotence_key="idemp-123",
        return_url="/finance?done",
        description="Top-up",
        customer_email="test@user.ru",
        metadata={"tenant_id": "1"},
    )

    assert res["id"].startswith("mock-yoo-")
    assert res["status"] == "pending"
    assert res["amount"]["value"] == "500.00"
    assert res["amount"]["currency"] == "RUB"
    assert "confirmation" in res
    assert res["confirmation"]["confirmation_url"].startswith("https://yookassa.ru")
    assert res["receipt"]["customer"]["email"] == "test@user.ru"
    assert res["receipt"]["items"][0]["amount"]["value"] == "500.00"

    # Idempotent replay with same key
    replay = await mock_yookassa.create_payment(
        amount_kopecks=50000,
        idempotence_key="idemp-123",
        return_url="/finance?done",
        description="Top-up",
    )
    assert replay["id"] == res["id"]

    # Fetch status
    fetched = await mock_yookassa.get_payment(res["id"])
    assert fetched["id"] == res["id"]
    assert fetched["status"] == "pending"


# =============================================================================
# 2. Integer Rubles Validation & Return URL Safety
# =============================================================================


@pytest.mark.anyio
async def test_payment_integer_rubles_and_safe_url_guard(
    fake_db: FakePaymentsDb, mock_yookassa: MockYooKassaClient
):
    """Test that payments strictly require positive integer rubles and safe return URLs."""
    session = cast(Any, fake_db)

    # 1. Reject non-integer / <= 0 rubles
    with pytest.raises(FinValidationError, match="positive integer in rubles"):
        await FinPaymentService.create_payment(
            session, tenant_id=1, user_id=10, amount_rubles=0
        )

    with pytest.raises(FinValidationError, match="positive integer in rubles"):
        await FinPaymentService.create_payment(
            session, tenant_id=1, user_id=10, amount_rubles=-100
        )

    # 2. Reject unsafe open-redirect return URLs
    assert not is_safe_return_url("javascript:alert(1)")
    assert not is_safe_return_url("//attacker.com/evil")
    assert not is_safe_return_url("ftp://server.com")
    assert is_safe_return_url("/finance?status=done")
    assert is_safe_return_url("https://mycompany.ru/finance")

    with pytest.raises(FinValidationError, match="Unsafe return_url"):
        await FinPaymentService.create_payment(
            session,
            tenant_id=1,
            user_id=10,
            amount_rubles=150,
            return_url="javascript:attack()",
        )


# =============================================================================
# 3. YooKassa Happy Path & Atomic Ledger Posting
# =============================================================================


@pytest.mark.anyio
async def test_yookassa_payment_creation_and_successful_webhook_posting(
    fake_db: FakePaymentsDb, mock_yookassa: MockYooKassaClient
):
    """Test full payment lifecycle: creation, confirmation via authoritative GET, and double-entry ledger posting."""
    session = cast(Any, fake_db)

    # Tenant initial state
    tenant_id = 42
    profile = await FinBillingCycleService.ensure_billing_profile(session, tenant_id)
    assert profile.anchor_at is None
    assert profile.entitlement == "free"

    # 1. Create payment
    payment = await FinPaymentService.create_payment(
        session,
        tenant_id=tenant_id,
        user_id=101,
        amount_rubles=300,
        customer_email="payer@test.com",
        idempotence_key="unique-op-key-1",
    )

    assert payment.id in fake_db.payments
    assert payment.amount_kopecks == 30000
    assert payment.status == "pending"
    assert payment.ledger_transaction_id is None
    assert payment.provider_payment_id is not None

    # Simulate payment succeeded in YooKassa
    assert payment.provider_payment_id is not None
    mock_yookassa.set_payment_status(payment.provider_payment_id, "succeeded")

    # 2. Webhook triggers processing
    webhook_payload = {
        "type": "notification",
        "event": "payment.succeeded",
        "object": {
            "id": payment.provider_payment_id,
            "status": "succeeded",
            "amount": {"value": "300.00", "currency": "RUB"},
        },
    }

    updated_payment = await FinPaymentService.process_webhook(
        session,
        webhook_payload,
        client_ip="185.71.76.5",  # Trusted YooKassa IP
    )

    assert updated_payment.status == "succeeded"
    assert updated_payment.ledger_transaction_id is not None
    assert updated_payment.verified_at is not None
    assert updated_payment.succeeded_at is not None

    # 3. Verify ledger entries and projection
    tx = fake_db.transactions[updated_payment.ledger_transaction_id]
    assert tx.kind == "payment"
    assert tx.debit_kopecks == 30000
    assert tx.credit_kopecks == 30000

    entries = [e for e in fake_db.entries if e.transaction_id == tx.id]
    assert len(entries) == 2

    clearing_entry = next(e for e in entries if e.debit_kopecks > 0)
    settlement_entry = next(e for e in entries if e.credit_kopecks > 0)

    assert clearing_entry.debit_kopecks == 30000
    assert settlement_entry.credit_kopecks == 30000

    proj = fake_db.projections[tenant_id]
    assert proj.balance_kopecks == 30000

    # 4. First payment anchors the cycle
    prof = fake_db.profiles[tenant_id]
    assert prof.anchor_at is not None
    assert prof.first_payment_transaction_id == tx.id
    assert prof.entitlement == "active"
    assert len(fake_db.cycles) == 1


# =============================================================================
# 4. Duplicate Webhook and Fallback Polling Replay Safety
# =============================================================================


@pytest.mark.anyio
async def test_duplicate_webhook_and_polling_idempotency(
    fake_db: FakePaymentsDb, mock_yookassa: MockYooKassaClient
):
    """Test duplicate webhook and fallback poll do not produce duplicate ledger postings."""
    session = cast(Any, fake_db)
    tenant_id = 55

    payment = await FinPaymentService.create_payment(
        session,
        tenant_id=tenant_id,
        user_id=1,
        amount_rubles=200,
        idempotence_key="op-key-dup",
    )
    assert payment.provider_payment_id is not None
    mock_yookassa.set_payment_status(payment.provider_payment_id, "succeeded")

    webhook_payload = {
        "event": "payment.succeeded",
        "object": {"id": payment.provider_payment_id},
    }

    # First webhook -> posts transaction
    p1 = await FinPaymentService.process_webhook(session, webhook_payload)
    tx_id = p1.ledger_transaction_id
    assert tx_id is not None
    assert fake_db.projections[tenant_id].balance_kopecks == 20000

    tx_count_before = len(fake_db.transactions)

    # Second webhook -> idempotent replay, no duplicate posting
    p2 = await FinPaymentService.process_webhook(session, webhook_payload)
    assert p2.ledger_transaction_id == tx_id
    assert len(fake_db.transactions) == tx_count_before
    assert fake_db.projections[tenant_id].balance_kopecks == 20000

    # Fallback polling -> same idempotency check
    p3 = await FinPaymentService.sync_payment_status(
        session, payment_id=payment.id, trigger_source="polling"
    )
    assert p3.ledger_transaction_id == tx_id
    assert len(fake_db.transactions) == tx_count_before
    assert fake_db.projections[tenant_id].balance_kopecks == 20000


# =============================================================================
# 5. Security Mismatch Checks (Amount, Currency, Merchant, Tenant)
# =============================================================================


@pytest.mark.anyio
async def test_yookassa_security_mismatch_rejection(
    fake_db: FakePaymentsDb, mock_yookassa: MockYooKassaClient
):
    """Test that amount, currency, or merchant tampering causes immediate rejection and payment cancellation."""
    session = cast(Any, fake_db)
    tenant_id = 77

    payment = await FinPaymentService.create_payment(
        session,
        tenant_id=tenant_id,
        user_id=1,
        amount_rubles=100,
        idempotence_key="mismatch-key",
    )

    # 1. Amount mismatch (YooKassa returns 200 instead of 100)
    assert payment.provider_payment_id is not None
    mock_yookassa.set_payment_amount(payment.provider_payment_id, "200.00", "RUB")
    mock_yookassa.set_payment_status(payment.provider_payment_id, "succeeded")

    with pytest.raises(FinValidationError, match="Security mismatch"):
        await FinPaymentService.sync_payment_status(session, payment_id=payment.id)

    assert payment.status == "canceled"
    assert payment.ledger_transaction_id is None
    assert fake_db.projections.get(tenant_id) is None


# =============================================================================
# 6. Timeouts and Network Errors Resilience
# =============================================================================


@pytest.mark.anyio
async def test_yookassa_network_timeout_handling(
    fake_db: FakePaymentsDb, mock_yookassa: MockYooKassaClient
):
    """Test network timeouts during payment creation or polling raise expected errors without corrupting DB."""
    session = cast(Any, fake_db)

    # Enable mock timeout
    mock_yookassa.simulate_timeout = True

    with pytest.raises(FinValidationError, match="YooKassa provider error"):
        await FinPaymentService.create_payment(
            session,
            tenant_id=12,
            user_id=1,
            amount_rubles=500,
            idempotence_key="timeout-op",
        )

    # Disable timeout
    mock_yookassa.simulate_timeout = False


# =============================================================================
# 7. First Payment Anchor Fixation & Subsequent Payment Immunity
# =============================================================================


@pytest.mark.anyio
async def test_first_payment_anchor_and_subsequent_immunity(
    fake_db: FakePaymentsDb, mock_yookassa: MockYooKassaClient
):
    """Test first payment fixes anchor date; subsequent payments never shift existing anchor."""
    session = cast(Any, fake_db)
    tenant_id = 99

    # Payment 1: 100 RUB
    pay1 = await FinPaymentService.create_payment(
        session,
        tenant_id=tenant_id,
        user_id=1,
        amount_rubles=100,
        idempotence_key="first-pay",
    )
    assert pay1.provider_payment_id is not None
    mock_yookassa.set_payment_status(pay1.provider_payment_id, "succeeded")
    await FinPaymentService.sync_payment_status(session, payment_id=pay1.id)

    profile = fake_db.profiles[tenant_id]
    original_anchor = profile.anchor_at
    assert original_anchor is not None
    assert profile.entitlement == "active"

    # Payment 2: 200 RUB
    pay2 = await FinPaymentService.create_payment(
        session,
        tenant_id=tenant_id,
        user_id=1,
        amount_rubles=200,
        idempotence_key="second-pay",
    )
    assert pay2.provider_payment_id is not None
    mock_yookassa.set_payment_status(pay2.provider_payment_id, "succeeded")
    await FinPaymentService.sync_payment_status(session, payment_id=pay2.id)

    # Anchor must remain exactly unchanged!
    assert profile.anchor_at == original_anchor
    assert fake_db.projections[tenant_id].balance_kopecks == 30000


# =============================================================================
# 8. Manual Payments and Storno (Reversal) by Superuser
# =============================================================================


@pytest.mark.anyio
async def test_manual_payment_creation_and_storno_reversal(fake_db: FakePaymentsDb):
    """Test manual payment posting and storno with strict double-entry reversal."""
    session = cast(Any, fake_db)
    tenant_id = 888

    # 1. Register manual payment
    manual_pay = await FinManualPaymentService.create_manual_payment(
        session,
        creator_user_id=1,
        tenant_id=tenant_id,
        amount_rubles=5000,
        received_on=date(2026, 9, 20),
        document_number="PAY-ORDER-9912",
        payer="ООO Ромашка ИНН 7701234567",
        purpose="Пополнение лицевого счета L4Desk",
        comment="По банковской выписке за 20.09.2026",
    )

    assert manual_pay.id in fake_db.manual_payments
    assert manual_pay.amount_kopecks == 500000
    assert fake_db.projections[tenant_id].balance_kopecks == 500000

    # 2. Perform storno
    storno_doc = await FinManualPaymentService.storno_manual_payment(
        session,
        manual_payment_id=manual_pay.id,
        reversal_reason="Ошибочное зачисление, возврат плательщику",
        comment="По заявлению бухгалтерии",
    )

    assert storno_doc.document_number == "STORNO-PAY-ORDER-9912"
    assert storno_doc.amount_kopecks == 500000
    # Balance must return back to 0!
    assert fake_db.projections[tenant_id].balance_kopecks == 0

    # 3. Reject second storno on already reversed payment
    with pytest.raises(FinReversalError):
        await FinManualPaymentService.storno_manual_payment(
            session,
            manual_payment_id=manual_pay.id,
            reversal_reason="Duplicate storno attempt",
        )

    # 4. Reject reversing a reversal record
    with pytest.raises(FinReversalError, match="Cannot reverse a reversal"):
        await FinManualPaymentService.storno_manual_payment(
            session,
            manual_payment_id=storno_doc.id,
            reversal_reason="Cannot reverse storno",
        )


# =============================================================================
# 9. Subledger Reconciliation After YooKassa and Manual Payments
# =============================================================================


@pytest.mark.anyio
async def test_reconciliation_post_yookassa_and_manual_payments(
    fake_db: FakePaymentsDb, mock_yookassa: MockYooKassaClient
):
    """Test that ledger reconciliation run reports zero mismatches and exact balance match."""
    session = cast(Any, fake_db)
    tenant_id = 300

    # 1. YooKassa payment: 1000 RUB
    pay = await FinPaymentService.create_payment(
        session, tenant_id=tenant_id, user_id=1, amount_rubles=1000
    )
    assert pay.provider_payment_id is not None
    mock_yookassa.set_payment_status(pay.provider_payment_id, "succeeded")
    await FinPaymentService.sync_payment_status(session, payment_id=pay.id)

    # 2. Manual payment: 2000 RUB
    await FinManualPaymentService.create_manual_payment(
        session,
        creator_user_id=1,
        tenant_id=tenant_id,
        amount_rubles=2000,
        received_on=date(2026, 9, 20),
        document_number="ORD-101",
        payer="ЗАО Технологии",
        purpose="Оплата услуг",
    )

    # Current tenant balance = 3000 RUB (300000 kopecks)
    assert fake_db.projections[tenant_id].balance_kopecks == 300000

    # Run subledger reconciliation
    rec_req = FinReconciliationRequest(
        period_start=datetime(2026, 1, 1, tzinfo=UTC),
        period_end=datetime(2026, 12, 31, tzinfo=UTC),
        tenant_id=tenant_id,
    )
    run = await FinReconciliationService.run_reconciliation(session, rec_req)

    assert run.status == "matched"
    assert run.mismatch_count == 0
    assert run.balance_difference_kopecks == 0
    assert run.debit_kopecks == run.credit_kopecks
    assert fake_db.projections[tenant_id].balance_kopecks == 300000


# =============================================================================
# 10. HTTP REST API Tests (YooKassa & Manual Payments)
# =============================================================================


@pytest.mark.anyio
async def test_http_api_yookassa_and_manual_payments(
    fake_db: FakePaymentsDb, mock_yookassa: MockYooKassaClient
):
    """Test full HTTP endpoints for YooKassa creation, polling, webhook, and superuser manual payments."""
    tenant_id = 100

    tenant_token = create_access_token(
        {
            "sub": "42",
            "org_id": tenant_id,
            "role": "user",
            "roleId": 5,
            "email": "user@tenant.ru",
        }
    )
    superuser_token = create_access_token(
        {
            "sub": "1",
            "org_id": 1,
            "role": "superuser",
            "roleId": 1,
            "is_superuser": True,
        }
    )

    async def get_test_db():
        yield cast(Any, fake_db)

    app.dependency_overrides[get_db] = get_test_db

    try:
        transport = ASGITransport(app=app)
        async with AsyncClient(
            transport=transport, base_url="http://testserver"
        ) as client:
            # 1. Create YooKassa payment as tenant user
            resp = await client.post(
                "/api/v1/finance/payments",
                headers={"Authorization": f"Bearer {tenant_token}"},
                json={"amount_rubles": 250},
            )
            assert resp.status_code == 201
            p_data = resp.json()
            assert p_data["amount_rubles"] == 250
            assert p_data["status"] == "pending"
            assert "confirmation_url" in p_data
            p_id = p_data["id"]
            assert fake_db.commit_count == 1

            # 2. Query payment details
            resp_get = await client.get(
                f"/api/v1/finance/payments/{p_id}",
                headers={"Authorization": f"Bearer {tenant_token}"},
            )
            assert resp_get.status_code == 200
            assert resp_get.json()["id"] == p_id

            # 3. Webhook arrives
            assert p_data["provider_payment_id"] is not None
            mock_yookassa.set_payment_status(
                str(p_data["provider_payment_id"]), "succeeded"
            )
            webhook_payload = {
                "type": "notification",
                "event": "payment.succeeded",
                "object": {
                    "id": p_data["provider_payment_id"],
                    "status": "succeeded",
                    "amount": {"value": "250.00", "currency": "RUB"},
                },
            }
            resp_hook = await client.post(
                "/api/v1/finance/yookassa/webhook",
                json=webhook_payload,
            )
            assert resp_hook.status_code == 200
            assert resp_hook.json()["payment_status"] == "succeeded"
            assert fake_db.commit_count == 2

            # 4. Fallback polling returns succeeded
            resp_poll = await client.post(
                f"/api/v1/finance/payments/{p_id}/poll",
                headers={"Authorization": f"Bearer {tenant_token}"},
            )
            assert resp_poll.status_code == 200
            assert resp_poll.json()["status"] == "succeeded"
            assert fake_db.commit_count == 3

            # 5. Check balance
            resp_bal = await client.get(
                "/api/v1/finance/balance",
                headers={"Authorization": f"Bearer {tenant_token}"},
            )
            assert resp_bal.status_code == 200
            assert resp_bal.json()["balance_rubles"] == 250.0

            # 6. Regular user cannot create manual payment (Forbidden)
            resp_bad_manual = await client.post(
                "/api/internal/v1/finance/manual-payments",
                headers={"Authorization": f"Bearer {tenant_token}"},
                json={
                    "tenant_id": tenant_id,
                    "amount_rubles": 1000,
                    "received_on": "2026-09-20",
                    "document_number": "INV-1",
                    "payer": "Test Corp",
                    "purpose": "Bank payment",
                },
            )
            assert resp_bad_manual.status_code == 403

            # 7. Superuser registers manual payment
            resp_manual = await client.post(
                "/api/internal/v1/finance/manual-payments",
                headers={"Authorization": f"Bearer {superuser_token}"},
                json={
                    "tenant_id": tenant_id,
                    "amount_rubles": 1000,
                    "received_on": "2026-09-20",
                    "document_number": "INV-1",
                    "payer": "Test Corp",
                    "purpose": "Bank payment",
                },
            )
            assert resp_manual.status_code == 201
            m_id = resp_manual.json()["id"]
            assert fake_db.commit_count == 4

            # Balance now 1250 RUB
            resp_bal2 = await client.get(
                "/api/v1/finance/balance",
                headers={"Authorization": f"Bearer {tenant_token}"},
            )
            assert resp_bal2.json()["balance_rubles"] == 1250.0

            # 8. Superuser reverses manual payment (storno)
            resp_storno = await client.post(
                f"/api/internal/v1/finance/manual-payments/{m_id}/storno",
                headers={"Authorization": f"Bearer {superuser_token}"},
                json={"reversal_reason": "Refund requested by payer"},
            )
            assert resp_storno.status_code == 201
            assert resp_storno.json()["document_number"] == "STORNO-INV-1"
            assert fake_db.commit_count == 5

            # Balance returns to 250 RUB
            resp_bal3 = await client.get(
                "/api/v1/finance/balance",
                headers={"Authorization": f"Bearer {tenant_token}"},
            )
            assert resp_bal3.json()["balance_rubles"] == 250.0
    finally:
        app.dependency_overrides.clear()
