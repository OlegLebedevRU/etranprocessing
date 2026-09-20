from __future__ import annotations

import logging

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models_l4desk import FinLedgerEntry, FinLedgerTransaction
from app.services.financial_core.exceptions import FinReversalError
from app.services.financial_core.posting import FinPostingService
from app.services.financial_core.schemas import (
    FinPostingEntryRequest,
    FinPostingRequest,
    FinReversalRequest,
)

logger = logging.getLogger(__name__)


class FinReversalService:
    """Service for reversing posted ledger transactions with inverse entries and strict validation."""

    @staticmethod
    async def reverse_transaction(
        db: AsyncSession, request: FinReversalRequest
    ) -> FinLedgerTransaction:
        """Create and post a reversal transaction that exactly inverts the original entries."""
        # 1. Fetch original transaction
        stmt_orig = select(FinLedgerTransaction).where(
            FinLedgerTransaction.id == request.transaction_id,
            FinLedgerTransaction.tenant_id == request.tenant_id,
        )
        orig_res = await db.execute(stmt_orig)
        original = orig_res.scalar_one_or_none()

        if not original:
            raise FinReversalError(
                f"Original transaction id={request.transaction_id} not found in tenant {request.tenant_id}"
            )

        if original.status != "posted":
            raise FinReversalError(
                f"Cannot reverse transaction id={request.transaction_id} with status '{original.status}'"
            )

        if original.kind == "reversal":
            raise FinReversalError(
                f"Cannot reverse a reversal transaction id={request.transaction_id}"
            )

        # 2. Check if already reversed
        stmt_existing = select(FinLedgerTransaction).where(
            FinLedgerTransaction.tenant_id == request.tenant_id,
            FinLedgerTransaction.corrects_transaction_id == original.id,
            FinLedgerTransaction.kind == "reversal",
        )
        existing_rev = (await db.execute(stmt_existing)).scalar_one_or_none()
        if existing_rev:
            raise FinReversalError(
                f"Transaction id={original.id} has already been reversed by transaction id={existing_rev.id}"
            )

        # 3. Fetch original entries
        stmt_entries = (
            select(FinLedgerEntry)
            .where(FinLedgerEntry.transaction_id == original.id)
            .order_by(FinLedgerEntry.line_number)
        )
        orig_entries = (await db.execute(stmt_entries)).scalars().all()
        if not orig_entries:
            raise FinReversalError(
                f"Original transaction id={original.id} has no ledger entries"
            )

        # 4. Invert entries: new debit = old credit, new credit = old debit
        inverted_entries = [
            FinPostingEntryRequest(
                account_id=entry.account_id,
                debit_kopecks=entry.credit_kopecks,
                credit_kopecks=entry.debit_kopecks,
            )
            for entry in orig_entries
        ]

        # 5. Formulate posting request
        snapshot = {
            "reversal_reason": request.reason,
            "original_transaction_id": original.id,
            "original_operation_id": original.operation_id,
            "original_kind": original.kind,
            "original_debit_kopecks": original.debit_kopecks,
            "original_credit_kopecks": original.credit_kopecks,
        }

        posting_req = FinPostingRequest(
            tenant_id=request.tenant_id,
            operation_id=request.operation_id,
            kind="reversal",
            corrects_transaction_id=original.id,
            source_project=request.source_project or "MenuBuilder",
            source_type=request.source_type or "reversal",
            source_id=request.source_id or f"rev_{original.id}",
            actor=request.actor,
            correlation_id=request.correlation_id,
            entries=inverted_entries,
            calculation_snapshot=snapshot,
            expected_projection_version=request.expected_projection_version,
        )

        reversal_tx = await FinPostingService.post_transaction(db, posting_req)

        logger.info(
            "Reversed transaction id=%s via new reversal transaction id=%s for tenant=%s",
            original.id,
            reversal_tx.id,
            request.tenant_id,
        )

        return reversal_tx
