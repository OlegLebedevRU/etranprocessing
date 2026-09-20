from __future__ import annotations

import hashlib
import logging
from datetime import UTC, datetime
from typing import Any

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models_l4desk import (
    FinAccount,
    FinBalanceProjection,
    FinLedgerEntry,
    FinLedgerTransaction,
)
from app.services.financial_core.accounts import FinAccountService
from app.services.financial_core.exceptions import (
    FinAccountNotFoundError,
    FinConcurrencyError,
    FinDuplicatePostingError,
    FinImbalanceError,
    FinImmutableError,
    FinTenantIsolationError,
    FinValidationError,
)
from app.services.financial_core.schemas import FinPostingRequest

logger = logging.getLogger(__name__)

ALLOWED_TRANSACTION_KINDS = {
    "payment",
    "usage",
    "terminal_month",
    "adjustment",
    "reversal",
}


class FinPostingService:
    """Service for posting immutable double-entry ledger transactions and updating balance projections."""

    @staticmethod
    def _validate_types_and_amounts(request: FinPostingRequest) -> tuple[int, int]:
        """Strict validation of integer types, rounding rules, and double-entry balance."""
        if request.kind not in ALLOWED_TRANSACTION_KINDS:
            raise FinValidationError(f"Invalid transaction kind: {request.kind}")

        total_debit = 0
        total_credit = 0

        for idx, entry in enumerate(request.entries, start=1):
            # Strict integer check: float/bool prohibited
            for field_name, val in (
                ("debit_kopecks", entry.debit_kopecks),
                ("credit_kopecks", entry.credit_kopecks),
            ):
                if isinstance(val, bool) or not isinstance(val, int):
                    raise FinValidationError(
                        f"Entry line {idx} {field_name} must be an integer, got {type(val).__name__} (float forbidden)"
                    )
                if val < 0:
                    raise FinValidationError(
                        f"Entry line {idx} {field_name} must be non-negative"
                    )
                # Whole rubles rule: multiple of 100 kopecks
                if val % 100 != 0:
                    raise FinValidationError(
                        f"Entry line {idx} {field_name}={val} is not a multiple of 100 kopecks (whole rubles required)"
                    )

            # Single side rule: exactly one side positive
            if (entry.debit_kopecks > 0 and entry.credit_kopecks > 0) or (
                entry.debit_kopecks == 0 and entry.credit_kopecks == 0
            ):
                raise FinValidationError(
                    f"Entry line {idx} must have exactly one positive side (debit xor credit)"
                )

            total_debit += entry.debit_kopecks
            total_credit += entry.credit_kopecks

        # Invariant: sum(debit) == sum(credit) > 0
        if total_debit != total_credit or total_debit <= 0:
            raise FinImbalanceError(
                f"Double-entry imbalance: debit={total_debit}, credit={total_credit} (must be equal and > 0)"
            )

        return total_debit, total_credit

    @staticmethod
    async def post_transaction(
        db: AsyncSession, request: FinPostingRequest
    ) -> FinLedgerTransaction:
        """Post an immutable transaction with double-entry entries and update balance projection atomically."""
        total_debit, total_credit = FinPostingService._validate_types_and_amounts(
            request
        )

        # 1. Idempotency Check
        stmt_op = select(FinLedgerTransaction).where(
            FinLedgerTransaction.tenant_id == request.tenant_id,
            FinLedgerTransaction.operation_id == request.operation_id,
        )
        existing_op = (await db.execute(stmt_op)).scalar_one_or_none()

        stmt_src = select(FinLedgerTransaction).where(
            FinLedgerTransaction.tenant_id == request.tenant_id,
            FinLedgerTransaction.source_project == request.source_project,
            FinLedgerTransaction.source_type == request.source_type,
            FinLedgerTransaction.source_id == request.source_id,
        )
        existing_src = (await db.execute(stmt_src)).scalar_one_or_none()

        existing = existing_op or existing_src
        if existing is not None:
            # Check for conflict
            is_match = (
                existing.operation_id == request.operation_id
                and existing.source_project == request.source_project
                and existing.source_type == request.source_type
                and existing.source_id == request.source_id
                and existing.debit_kopecks == total_debit
                and existing.credit_kopecks == total_credit
                and existing.kind == request.kind
                and existing.corrects_transaction_id == request.corrects_transaction_id
            )
            if is_match:
                logger.info(
                    "Idempotent replay for transaction id=%s, operation_id=%s",
                    existing.id,
                    request.operation_id,
                )
                return existing
            raise FinDuplicatePostingError(
                f"Conflicting duplicate posting detected for operation_id={request.operation_id} or source={request.source_id}"
            )

        # 2. Correction Constraints Check
        if request.kind in ("adjustment", "reversal"):
            if not request.corrects_transaction_id:
                raise FinValidationError(
                    f"Kind '{request.kind}' requires non-null corrects_transaction_id"
                )
            stmt_corr = select(FinLedgerTransaction).where(
                FinLedgerTransaction.id == request.corrects_transaction_id,
                FinLedgerTransaction.tenant_id == request.tenant_id,
            )
            target_tx = (await db.execute(stmt_corr)).scalar_one_or_none()
            if not target_tx:
                raise FinValidationError(
                    f"Referenced transaction {request.corrects_transaction_id} not found in tenant {request.tenant_id}"
                )
            if target_tx.status != "posted":
                raise FinValidationError(
                    f"Cannot correct unposted transaction {request.corrects_transaction_id}"
                )
        else:
            if request.corrects_transaction_id is not None:
                raise FinValidationError(
                    f"Kind '{request.kind}' must have corrects_transaction_id=None"
                )

        # 3. Account existence & Tenant Isolation Verification
        acc_ids = {e.account_id for e in request.entries}
        stmt_accs = select(FinAccount).where(FinAccount.id.in_(acc_ids))
        loaded_accs = {
            acc.id: acc for acc in (await db.execute(stmt_accs)).scalars().all()
        }

        for acc_id in acc_ids:
            acc = loaded_accs.get(acc_id)
            if not acc:
                raise FinAccountNotFoundError(f"Account {acc_id} does not exist")
            if acc.tenant_id is not None and acc.tenant_id != request.tenant_id:
                raise FinTenantIsolationError(
                    f"Account {acc_id} belongs to tenant {acc.tenant_id}, but transaction is for tenant {request.tenant_id}"
                )

        # 4. Ensure tenant settlement account and balance projection exist
        settlement_acc, _ = await FinAccountService.ensure_tenant_settlement_account(
            db, request.tenant_id
        )

        # 5. Lock and check balance projection
        lock_stmt = (
            select(FinBalanceProjection)
            .where(FinBalanceProjection.tenant_id == request.tenant_id)
            .with_for_update()
        )
        res_proj = await db.execute(lock_stmt)
        locked_proj = res_proj.scalar_one()

        if (
            request.expected_projection_version is not None
            and locked_proj.version != request.expected_projection_version
        ):
            raise FinConcurrencyError(
                f"Optimistic lock violation: expected version {request.expected_projection_version}, current {locked_proj.version}"
            )

        now_dt = datetime.now(UTC)
        source_hash = (
            request.source_events_hash
            or hashlib.sha256(
                f"{request.tenant_id}:{request.operation_id}:{total_debit}".encode()
            ).hexdigest()
        )

        # 6. Create immutable FinLedgerTransaction
        tx = FinLedgerTransaction(
            tenant_id=request.tenant_id,
            operation_id=request.operation_id,
            kind=request.kind,
            status="posted",
            corrects_transaction_id=request.corrects_transaction_id,
            debit_kopecks=total_debit,
            credit_kopecks=total_credit,
            source_project=request.source_project,
            source_type=request.source_type,
            source_id=request.source_id,
            source_event_id=request.source_event_id,
            source_events_hash=source_hash,
            archive_batch_id=request.archive_batch_id,
            calculation_snapshot=request.calculation_snapshot,
            actor=request.actor,
            correlation_id=request.correlation_id,
            created_at=now_dt,
            posted_at=now_dt,
        )
        db.add(tx)
        await db.flush()

        # 7. Create immutable FinLedgerEntry rows
        entries: list[FinLedgerEntry] = []
        tenant_delta = 0
        for idx, entry_req in enumerate(request.entries, start=1):
            entry = FinLedgerEntry(
                transaction_id=tx.id,
                tenant_id=request.tenant_id,
                line_number=idx,
                account_id=entry_req.account_id,
                debit_kopecks=entry_req.debit_kopecks,
                credit_kopecks=entry_req.credit_kopecks,
            )
            db.add(entry)
            entries.append(entry)

            # Accumulate movement on tenant settlement account:
            # Credits increase balance, debits decrease balance
            if entry_req.account_id == settlement_acc.id:
                tenant_delta += entry_req.credit_kopecks - entry_req.debit_kopecks

        await db.flush()

        # 8. Incrementally update FinBalanceProjection in the same DB transaction
        locked_proj.balance_kopecks += tenant_delta
        locked_proj.version += 1
        locked_proj.last_transaction_id = tx.id
        locked_proj.updated_at = now_dt
        await db.flush()

        logger.info(
            "Posted transaction id=%s (kind=%s, amount=%s kopecks, new_balance=%s, ver=%s)",
            tx.id,
            tx.kind,
            total_debit,
            locked_proj.balance_kopecks,
            locked_proj.version,
        )

        return tx

    @staticmethod
    def prevent_mutation(entity: Any) -> None:
        """Guard against modifying or deleting posted ledger records."""
        if isinstance(entity, (FinLedgerTransaction, FinLedgerEntry)) and (
            getattr(entity, "status", None) == "posted"
            or isinstance(entity, FinLedgerEntry)
        ):
            raise FinImmutableError(
                f"Mutation or deletion of posted {type(entity).__name__} is strictly prohibited"
            )
