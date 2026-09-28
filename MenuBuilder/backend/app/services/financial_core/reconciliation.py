from __future__ import annotations

import logging
import uuid
from datetime import UTC, datetime
from typing import Any

from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models_l4desk import (
    FinAccount,
    FinLedgerEntry,
    FinLedgerTransaction,
    FinManualPayment,
    FinPayment,
    FinReconciliationRun,
    FinTerminalMonthlyCharge,
    FinUsageDaily,
    L4DeskRemoteSession,
)
from app.services.financial_core.accounts import FinAccountService
from app.services.financial_core.exceptions import FinValidationError
from app.services.financial_core.projection import FinProjectionService
from app.services.financial_core.schemas import FinReconciliationRequest

logger = logging.getLogger(__name__)


class FinReconciliationService:
    """Service for executing reconciliation runs and detecting corruption or invariants violations."""

    @staticmethod
    async def run_reconciliation(
        db: AsyncSession, request: FinReconciliationRequest
    ) -> FinReconciliationRun:
        """Run comprehensive financial subledger reconciliation over a given period."""
        if request.period_end <= request.period_start:
            raise FinValidationError("period_end must be greater than period_start")

        started_at = datetime.now(UTC)
        op_id = request.operation_id or f"rec_{uuid.uuid4().hex[:16]}"
        correlation_id = request.correlation_id or f"corr_{uuid.uuid4().hex[:16]}"

        mismatch_count = 0
        balance_difference_kopecks = 0
        details: dict[str, Any] = {
            "imbalanced_transactions": [],
            "reversal_violations": [],
            "tenant_isolation_violations": [],
            "projection_mismatches": [],
            "duplicate_postings": [],
            "calculated_discarded_mismatches": [],
            "rounding_violations": [],
            "source_hash_violations": [],
            "coverage_violations": [],
            "unposted_charges_or_payments": [],
        }

        # 1. Fetch transactions in the period
        stmt_tx = select(FinLedgerTransaction).where(
            FinLedgerTransaction.created_at >= request.period_start,
            FinLedgerTransaction.created_at < request.period_end,
        )
        if request.tenant_id is not None:
            stmt_tx = stmt_tx.where(FinLedgerTransaction.tenant_id == request.tenant_id)
        stmt_tx = stmt_tx.order_by(FinLedgerTransaction.created_at)

        tx_list = (await db.execute(stmt_tx)).scalars().all()

        total_debit = 0
        total_credit = 0
        total_posted = 0
        total_discarded = 0
        tenants_in_scope: set[int] = set()

        if request.tenant_id is not None:
            tenants_in_scope.add(request.tenant_id)

        # 2. Check each transaction and its entries
        for tx in tx_list:
            tenants_in_scope.add(tx.tenant_id)

            if tx.status == "posted":
                total_debit += tx.debit_kopecks
                total_credit += tx.credit_kopecks
                total_posted += tx.debit_kopecks

                # Invariant: posted % 100 == 0
                if tx.debit_kopecks % 100 != 0 or tx.credit_kopecks % 100 != 0:
                    mismatch_count += 1
                    details["rounding_violations"].append(
                        {
                            "type": "transaction",
                            "transaction_id": tx.id,
                            "debit_kopecks": tx.debit_kopecks,
                            "credit_kopecks": tx.credit_kopecks,
                        }
                    )

                # Invariant: source hash presence
                if not tx.source_events_hash or not str(tx.source_events_hash).strip():
                    mismatch_count += 1
                    details["source_hash_violations"].append(
                        {
                            "type": "transaction",
                            "transaction_id": tx.id,
                            "reason": "Missing source_events_hash",
                        }
                    )

                # Accumulate discarded kopecks and check calculation snapshot
                if tx.calculation_snapshot and isinstance(
                    tx.calculation_snapshot, dict
                ):
                    disc = tx.calculation_snapshot.get("discarded_kopecks", 0)
                    if isinstance(disc, int) and disc >= 0:
                        total_discarded += disc
                    calc = tx.calculation_snapshot.get("calculated_kopecks")
                    if isinstance(calc, int) and isinstance(disc, int):
                        snap_posted = tx.calculation_snapshot.get(
                            "posted_kopecks", tx.debit_kopecks
                        )
                        if isinstance(snap_posted, int) and (
                            calc != snap_posted + disc or disc < 0 or disc >= 100
                        ):
                            mismatch_count += 1
                            details["calculated_discarded_mismatches"].append(
                                {
                                    "transaction_id": tx.id,
                                    "calculated": calc,
                                    "posted": snap_posted,
                                    "discarded": disc,
                                }
                            )

            # Invariant: Transaction header balance (debit == credit)
            if tx.debit_kopecks != tx.credit_kopecks or tx.debit_kopecks <= 0:
                mismatch_count += 1
                details["imbalanced_transactions"].append(
                    {
                        "transaction_id": tx.id,
                        "debit_kopecks": tx.debit_kopecks,
                        "credit_kopecks": tx.credit_kopecks,
                    }
                )

            # Fetch entries
            stmt_e = (
                select(FinLedgerEntry)
                .where(FinLedgerEntry.transaction_id == tx.id)
                .order_by(FinLedgerEntry.line_number)
            )
            entries = (await db.execute(stmt_e)).scalars().all()

            sum_e_debit = sum(e.debit_kopecks for e in entries)
            sum_e_credit = sum(e.credit_kopecks for e in entries)

            if sum_e_debit != tx.debit_kopecks or sum_e_credit != tx.credit_kopecks:
                mismatch_count += 1
                details["imbalanced_transactions"].append(
                    {
                        "transaction_id": tx.id,
                        "tx_debit": tx.debit_kopecks,
                        "entries_debit": sum_e_debit,
                        "tx_credit": tx.credit_kopecks,
                        "entries_credit": sum_e_credit,
                    }
                )

            # Reversal invariant: corrects_transaction_id must reference valid posted transaction
            if tx.kind in ("adjustment", "reversal"):
                if not tx.corrects_transaction_id:
                    mismatch_count += 1
                    details["reversal_violations"].append(
                        {
                            "transaction_id": tx.id,
                            "reason": "Missing corrects_transaction_id",
                        }
                    )
                else:
                    ref_tx = await db.get(
                        FinLedgerTransaction, tx.corrects_transaction_id
                    )
                    if (
                        not ref_tx
                        or ref_tx.tenant_id != tx.tenant_id
                        or ref_tx.status != "posted"
                    ):
                        mismatch_count += 1
                        details["reversal_violations"].append(
                            {
                                "transaction_id": tx.id,
                                "corrects_transaction_id": tx.corrects_transaction_id,
                                "reason": "Invalid or mismatched corrected transaction",
                            }
                        )

            # Tenant isolation: every entry must have tenant_id == tx.tenant_id and account belongs to tenant or global
            for e in entries:
                if e.tenant_id != tx.tenant_id:
                    mismatch_count += 1
                    details["tenant_isolation_violations"].append(
                        {
                            "transaction_id": tx.id,
                            "entry_id": e.id,
                            "expected_tenant": tx.tenant_id,
                            "actual_tenant": e.tenant_id,
                        }
                    )
                acc = await db.get(FinAccount, e.account_id)
                if acc and acc.tenant_id is not None and acc.tenant_id != tx.tenant_id:
                    mismatch_count += 1
                    details["tenant_isolation_violations"].append(
                        {
                            "transaction_id": tx.id,
                            "entry_id": e.id,
                            "account_id": e.account_id,
                            "account_tenant": acc.tenant_id,
                            "tx_tenant": tx.tenant_id,
                        }
                    )

        # 3. Check Daily Usage invariants (calculated = posted + discarded, posted % 100 == 0, source_events_hash)
        stmt_usage = select(FinUsageDaily).where(
            FinUsageDaily.local_date >= request.period_start.date(),
            FinUsageDaily.local_date <= request.period_end.date(),
        )
        if request.tenant_id is not None:
            stmt_usage = stmt_usage.where(FinUsageDaily.tenant_id == request.tenant_id)
        usage_rows = (await db.execute(stmt_usage)).scalars().all()

        for u in usage_rows:
            tenants_in_scope.add(u.tenant_id)
            if u.calculated_kopecks != (u.posted_kopecks + u.discarded_kopecks):
                mismatch_count += 1
                details["calculated_discarded_mismatches"].append(
                    {
                        "usage_id": u.id,
                        "tenant_id": u.tenant_id,
                        "terminal_id": u.terminal_id,
                        "local_date": str(u.local_date),
                        "calculated_kopecks": u.calculated_kopecks,
                        "posted_kopecks": u.posted_kopecks,
                        "discarded_kopecks": u.discarded_kopecks,
                    }
                )
            if u.discarded_kopecks < 0 or u.discarded_kopecks >= 100:
                mismatch_count += 1
                details["calculated_discarded_mismatches"].append(
                    {
                        "usage_id": u.id,
                        "reason": "Discarded kopecks out of bounds [0, 99]",
                        "discarded_kopecks": u.discarded_kopecks,
                    }
                )
            if u.posted_kopecks % 100 != 0:
                mismatch_count += 1
                details["rounding_violations"].append(
                    {
                        "type": "usage_daily",
                        "usage_id": u.id,
                        "posted_kopecks": u.posted_kopecks,
                    }
                )
            if not u.source_events_hash or not str(u.source_events_hash).strip():
                mismatch_count += 1
                details["source_hash_violations"].append(
                    {
                        "type": "usage_daily",
                        "usage_id": u.id,
                        "reason": "Missing source_events_hash",
                    }
                )
            if u.ledger_transaction_id is not None:
                ref_tx = await db.get(FinLedgerTransaction, u.ledger_transaction_id)
                if not ref_tx or ref_tx.status != "posted":
                    mismatch_count += 1
                    details["coverage_violations"].append(
                        {
                            "type": "usage_daily",
                            "usage_id": u.id,
                            "ledger_transaction_id": u.ledger_transaction_id,
                            "reason": "Referenced ledger transaction not found or unposted",
                        }
                    )

        # 4. Check Closed Sessions source_events_hash coverage
        stmt_sess = select(L4DeskRemoteSession).where(
            L4DeskRemoteSession.closed_at >= request.period_start,
            L4DeskRemoteSession.closed_at < request.period_end,
        )
        if request.tenant_id is not None:
            stmt_sess = stmt_sess.where(
                L4DeskRemoteSession.tenant_id == request.tenant_id
            )
        sessions = (await db.execute(stmt_sess)).scalars().all()

        for s in sessions:
            if s.active_at is not None and (
                not s.source_events_hash or not str(s.source_events_hash).strip()
            ):
                mismatch_count += 1
                details["source_hash_violations"].append(
                    {
                        "type": "remote_session",
                        "session_id": s.id,
                        "operation_id": s.operation_id,
                        "reason": "Closed active session missing source_events_hash",
                    }
                )

        # 5. Check Monthly Charges and Payment postings uniqueness & coverage
        stmt_charges = select(FinTerminalMonthlyCharge).where(
            FinTerminalMonthlyCharge.created_at >= request.period_start,
            FinTerminalMonthlyCharge.created_at < request.period_end,
        )
        if request.tenant_id is not None:
            stmt_charges = stmt_charges.where(
                FinTerminalMonthlyCharge.tenant_id == request.tenant_id
            )
        charges = (await db.execute(stmt_charges)).scalars().all()

        seen_charges: set[tuple[int, int]] = set()
        for ch in charges:
            tenants_in_scope.add(ch.tenant_id)
            charge_key = (ch.terminal_id, ch.billing_cycle_id)
            if charge_key in seen_charges:
                mismatch_count += 1
                details["duplicate_postings"].append(
                    {
                        "type": "monthly_charge",
                        "charge_id": ch.id,
                        "terminal_id": ch.terminal_id,
                        "billing_cycle_id": ch.billing_cycle_id,
                        "reason": "Duplicate monthly charge for same terminal and cycle",
                    }
                )
            seen_charges.add(charge_key)

            if ch.posted_kopecks % 100 != 0:
                mismatch_count += 1
                details["rounding_violations"].append(
                    {
                        "type": "monthly_charge",
                        "charge_id": ch.id,
                        "posted_kopecks": ch.posted_kopecks,
                    }
                )

            if not ch.is_free and ch.posted_kopecks > 0:
                if ch.ledger_transaction_id is None:
                    mismatch_count += 1
                    details["unposted_charges_or_payments"].append(
                        {
                            "type": "monthly_charge",
                            "charge_id": ch.id,
                            "reason": "Paid monthly charge missing ledger_transaction_id",
                        }
                    )
                else:
                    ref_tx = await db.get(
                        FinLedgerTransaction, ch.ledger_transaction_id
                    )
                    if not ref_tx or ref_tx.status != "posted":
                        mismatch_count += 1
                        details["unposted_charges_or_payments"].append(
                            {
                                "type": "monthly_charge",
                                "charge_id": ch.id,
                                "ledger_transaction_id": ch.ledger_transaction_id,
                                "reason": "Referenced transaction not found or unposted",
                            }
                        )

        # Check YooKassa Payments
        stmt_pay = select(FinPayment).where(
            FinPayment.created_at >= request.period_start,
            FinPayment.created_at < request.period_end,
        )
        if request.tenant_id is not None:
            stmt_pay = stmt_pay.where(FinPayment.tenant_id == request.tenant_id)
        payments = (await db.execute(stmt_pay)).scalars().all()

        seen_pay_txs: set[int] = set()
        for pay in payments:
            tenants_in_scope.add(pay.tenant_id)
            if pay.amount_kopecks <= 0 or pay.amount_kopecks % 100 != 0:
                mismatch_count += 1
                details["rounding_violations"].append(
                    {
                        "type": "payment",
                        "payment_id": pay.id,
                        "amount_kopecks": pay.amount_kopecks,
                    }
                )
            if pay.status == "succeeded":
                if pay.ledger_transaction_id is None:
                    mismatch_count += 1
                    details["unposted_charges_or_payments"].append(
                        {
                            "type": "payment",
                            "payment_id": pay.id,
                            "reason": "Succeeded payment missing ledger_transaction_id",
                        }
                    )
                else:
                    if pay.ledger_transaction_id in seen_pay_txs:
                        mismatch_count += 1
                        details["duplicate_postings"].append(
                            {
                                "type": "payment",
                                "payment_id": pay.id,
                                "ledger_transaction_id": pay.ledger_transaction_id,
                                "reason": "Duplicate posting for payment",
                            }
                        )
                    seen_pay_txs.add(pay.ledger_transaction_id)
                    ref_tx = await db.get(
                        FinLedgerTransaction, pay.ledger_transaction_id
                    )
                    if not ref_tx or ref_tx.status != "posted":
                        mismatch_count += 1
                        details["unposted_charges_or_payments"].append(
                            {
                                "type": "payment",
                                "payment_id": pay.id,
                                "ledger_transaction_id": pay.ledger_transaction_id,
                                "reason": "Referenced payment transaction not found or unposted",
                            }
                        )

        # Check Manual Payments
        stmt_mp = select(FinManualPayment).where(
            FinManualPayment.created_at >= request.period_start,
            FinManualPayment.created_at < request.period_end,
        )
        if request.tenant_id is not None:
            stmt_mp = stmt_mp.where(FinManualPayment.tenant_id == request.tenant_id)
        manual_payments = (await db.execute(stmt_mp)).scalars().all()

        seen_mp_txs: set[int] = set()
        for mp in manual_payments:
            tenants_in_scope.add(mp.tenant_id)
            if mp.amount_kopecks <= 0 or mp.amount_kopecks % 100 != 0:
                mismatch_count += 1
                details["rounding_violations"].append(
                    {
                        "type": "manual_payment",
                        "manual_payment_id": mp.id,
                        "amount_kopecks": mp.amount_kopecks,
                    }
                )
            if mp.ledger_transaction_id is None:
                mismatch_count += 1
                details["unposted_charges_or_payments"].append(
                    {
                        "type": "manual_payment",
                        "manual_payment_id": mp.id,
                        "reason": "Manual payment missing ledger_transaction_id",
                    }
                )
            else:
                if mp.ledger_transaction_id in seen_mp_txs:
                    mismatch_count += 1
                    details["duplicate_postings"].append(
                        {
                            "type": "manual_payment",
                            "manual_payment_id": mp.id,
                            "ledger_transaction_id": mp.ledger_transaction_id,
                            "reason": "Duplicate posting for manual payment",
                        }
                    )
                seen_mp_txs.add(mp.ledger_transaction_id)
                ref_tx = await db.get(FinLedgerTransaction, mp.ledger_transaction_id)
                if not ref_tx or ref_tx.status != "posted":
                    mismatch_count += 1
                    details["unposted_charges_or_payments"].append(
                        {
                            "type": "manual_payment",
                            "manual_payment_id": mp.id,
                            "ledger_transaction_id": mp.ledger_transaction_id,
                            "reason": "Referenced transaction not found or unposted",
                        }
                    )

        # 3. Check Balance Projection consistency and corruption detection
        for t_id in tenants_in_scope:
            settlement_acc = await FinAccountService.get_tenant_settlement_account(
                db, t_id
            )
            if not settlement_acc:
                continue

            # Calculate true ledger balance up to period_end
            stmt_ledger = (
                select(
                    func.coalesce(func.sum(FinLedgerEntry.credit_kopecks), 0),
                    func.coalesce(func.sum(FinLedgerEntry.debit_kopecks), 0),
                )
                .join(
                    FinLedgerTransaction,
                    (FinLedgerEntry.transaction_id == FinLedgerTransaction.id)
                    & (FinLedgerEntry.tenant_id == FinLedgerTransaction.tenant_id),
                )
                .where(
                    FinLedgerEntry.tenant_id == t_id,
                    FinLedgerEntry.account_id == settlement_acc.id,
                    FinLedgerTransaction.status == "posted",
                    FinLedgerTransaction.created_at < request.period_end,
                )
            )
            res_ledger = await db.execute(stmt_ledger)
            t_credit, t_debit = res_ledger.one()
            true_balance = int(t_credit - t_debit)

            # Compare with projection
            proj = await FinProjectionService.get_projection(db, t_id)
            if proj.balance_kopecks != true_balance:
                diff = abs(proj.balance_kopecks - true_balance)
                balance_difference_kopecks += diff
                mismatch_count += 1
                details["projection_mismatches"].append(
                    {
                        "tenant_id": t_id,
                        "projected_kopecks": proj.balance_kopecks,
                        "true_ledger_kopecks": true_balance,
                        "difference_kopecks": diff,
                    }
                )
                if request.auto_rebuild_projection:
                    await FinProjectionService.rebuild_projection(
                        db, t_id, actor=request.actor
                    )
                    details.setdefault("rebuilt_projections", []).append(t_id)

        # 4. Resolve overall status and ensure DB constraints
        total_calculated = total_posted + total_discarded
        is_matched = (
            mismatch_count == 0
            and balance_difference_kopecks == 0
            and total_debit == total_credit
        )
        status = "matched" if is_matched else "mismatch"

        finished_at = datetime.now(UTC)

        run = FinReconciliationRun(
            tenant_id=request.tenant_id,
            operation_id=op_id,
            period_start=request.period_start,
            period_end=request.period_end,
            status=status,
            calculated_kopecks=total_calculated,
            posted_kopecks=total_posted,
            discarded_kopecks=total_discarded,
            debit_kopecks=total_debit,
            credit_kopecks=total_credit,
            balance_difference_kopecks=balance_difference_kopecks,
            mismatch_count=mismatch_count,
            details=details,
            actor=request.actor,
            correlation_id=correlation_id,
            started_at=started_at,
            finished_at=finished_at,
        )
        db.add(run)
        await db.flush()

        logger.info(
            "Fin reconciliation completed: run_id=%s, op=%s, status=%s, mismatches=%s, diff=%s",
            run.id,
            run.operation_id,
            run.status,
            run.mismatch_count,
            run.balance_difference_kopecks,
        )

        return run
