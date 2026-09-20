from __future__ import annotations

import logging
import uuid
from datetime import UTC, date, datetime

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models_l4desk import FinManualPayment
from app.services.financial_core.accounts import FinAccountService
from app.services.financial_core.cycles import FinBillingCycleService
from app.services.financial_core.exceptions import (
    FinReversalError,
    FinValidationError,
)
from app.services.financial_core.posting import FinPostingService
from app.services.financial_core.reversal import FinReversalService
from app.services.financial_core.schemas import (
    FinPostingEntryRequest,
    FinPostingRequest,
    FinReversalRequest,
)

logger = logging.getLogger(__name__)


class FinManualPaymentService:
    """Service for superuser registration of B2B bank payments and strict storno."""

    @staticmethod
    async def create_manual_payment(
        db: AsyncSession,
        *,
        creator_user_id: int,
        tenant_id: int,
        amount_rubles: int,
        received_on: date,
        document_number: str,
        payer: str,
        purpose: str,
        comment: str | None = None,
        evidence_reference: str | None = None,
        operation_id: str | None = None,
        actor: str = "superuser",
        correlation_id: str = "",
    ) -> FinManualPayment:
        """Create an immutable manual payment record and post double-entry ledger transaction."""
        if not isinstance(amount_rubles, int) or amount_rubles < 1:
            raise FinValidationError(
                "Payment amount must be a positive integer in rubles (>= 1)"
            )

        if not document_number or not document_number.strip():
            raise FinValidationError("Document number is required")
        if not payer or not payer.strip():
            raise FinValidationError("Payer name is required")
        if not purpose or not purpose.strip():
            raise FinValidationError("Purpose is required")

        amount_kopecks = amount_rubles * 100
        effective_op_id = (
            operation_id.strip()
            if operation_id and operation_id.strip()
            else f"manual_pay_{tenant_id}_{uuid.uuid4()}"
        )

        system_accs = await FinAccountService.ensure_system_accounts(db)
        clearing_acc = system_accs["payment_clearing"]
        settlement_acc, _ = await FinAccountService.ensure_tenant_settlement_account(
            db, tenant_id
        )

        posting_req = FinPostingRequest(
            tenant_id=tenant_id,
            operation_id=effective_op_id,
            kind="payment",
            source_project="MenuBuilder",
            source_type="manual_payment",
            source_id=document_number.strip(),
            actor=actor,
            correlation_id=correlation_id or effective_op_id,
            entries=[
                FinPostingEntryRequest(
                    account_id=clearing_acc.id,
                    debit_kopecks=amount_kopecks,
                    credit_kopecks=0,
                ),
                FinPostingEntryRequest(
                    account_id=settlement_acc.id,
                    debit_kopecks=0,
                    credit_kopecks=amount_kopecks,
                ),
            ],
            calculation_snapshot={
                "document_number": document_number.strip(),
                "payer": payer.strip(),
                "purpose": purpose.strip(),
                "comment": comment.strip() if comment else None,
                "evidence_reference": evidence_reference,
                "received_on": received_on.isoformat(),
                "amount_rubles": amount_rubles,
                "amount_kopecks": amount_kopecks,
            },
        )

        tx = await FinPostingService.post_transaction(db, posting_req)

        now_dt = datetime.now(UTC)
        manual_payment = FinManualPayment(
            tenant_id=tenant_id,
            operation_id=effective_op_id,
            amount_kopecks=amount_kopecks,
            received_on=received_on,
            document_number=document_number.strip(),
            purpose=purpose.strip(),
            payer=payer.strip(),
            comment=comment.strip() if comment else None,
            evidence_reference=evidence_reference,
            created_by_user_id=creator_user_id,
            ledger_transaction_id=tx.id,
            correlation_id=correlation_id or effective_op_id,
            created_at=now_dt,
        )
        db.add(manual_payment)
        await db.flush()

        # Atomic cycle anchor fixation on first successful payment
        anchor_dt = datetime.combine(received_on, datetime.min.time(), tzinfo=UTC)
        await FinBillingCycleService.initialize_anchor_from_payment(
            db,
            tenant_id=tenant_id,
            payment_tx_id=tx.id,
            paid_at=anchor_dt,
        )

        logger.info(
            "Registered manual payment id=%s (doc=%s, amount=%s RUB, tx=%s)",
            manual_payment.id,
            manual_payment.document_number,
            amount_rubles,
            tx.id,
        )
        return manual_payment

    @staticmethod
    async def storno_manual_payment(
        db: AsyncSession,
        *,
        manual_payment_id: int,
        reversal_reason: str,
        comment: str | None = None,
        actor: str = "superuser",
        correlation_id: str = "",
        creator_user_id: int | None = None,
    ) -> FinManualPayment:
        """Storno (reverse) an existing posted manual payment with an inverse ledger transaction."""
        if not reversal_reason or not reversal_reason.strip():
            raise FinValidationError("Reversal reason is required")

        # 1. Fetch and lock original payment
        stmt = (
            select(FinManualPayment)
            .where(FinManualPayment.id == manual_payment_id)
            .with_for_update()
        )
        res = await db.execute(stmt)
        original = res.scalar_one_or_none()

        if not original:
            raise FinValidationError(f"Manual payment id={manual_payment_id} not found")

        if original.document_number.startswith("STORNO-"):
            raise FinReversalError("Cannot reverse a reversal payment record")

        # 2. Reverse ledger transaction
        rev_op_id = f"storno_manual_{original.id}_{uuid.uuid4()}"
        rev_req = FinReversalRequest(
            tenant_id=original.tenant_id,
            transaction_id=original.ledger_transaction_id,
            operation_id=rev_op_id,
            actor=actor,
            correlation_id=correlation_id or f"storno-{original.id}",
            reason=reversal_reason.strip(),
            source_project="MenuBuilder",
            source_type="manual_payment_storno",
            source_id=str(original.id),
        )
        reversal_tx = await FinReversalService.reverse_transaction(db, rev_req)

        # 3. Create storno manual payment record
        now_dt = datetime.now(UTC)
        storno_comment = (
            comment.strip()
            if comment and comment.strip()
            else f"Сторно платежа #{original.id} ({original.document_number}): {reversal_reason.strip()}"
        )
        storno_doc_number = f"STORNO-{original.document_number}"

        effective_creator_id = (
            creator_user_id
            if creator_user_id is not None
            else original.created_by_user_id
        )

        storno_payment = FinManualPayment(
            tenant_id=original.tenant_id,
            operation_id=rev_op_id,
            amount_kopecks=original.amount_kopecks,
            received_on=now_dt.date(),
            document_number=storno_doc_number,
            purpose=f"Сторно: {reversal_reason.strip()} ({original.purpose})",
            payer=original.payer,
            comment=storno_comment,
            evidence_reference=original.evidence_reference,
            created_by_user_id=effective_creator_id,
            ledger_transaction_id=reversal_tx.id,
            correlation_id=correlation_id or rev_op_id,
            created_at=now_dt,
        )
        db.add(storno_payment)
        await db.flush()

        logger.info(
            "Storno completed for manual payment id=%s -> storno_id=%s (tx=%s)",
            original.id,
            storno_payment.id,
            reversal_tx.id,
        )
        return storno_payment
