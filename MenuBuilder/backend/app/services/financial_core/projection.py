from __future__ import annotations

import logging
from datetime import UTC, datetime

from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models_l4desk import FinBalanceProjection, FinLedgerEntry, FinLedgerTransaction
from app.services.financial_core.accounts import FinAccountService

logger = logging.getLogger(__name__)


class FinProjectionService:
    """Service for fast balance projection reads and full ledger reconstruction."""

    @staticmethod
    async def get_projection(db: AsyncSession, tenant_id: int) -> FinBalanceProjection:
        """Fetch balance projection for a tenant, initializing if absent."""
        stmt = select(FinBalanceProjection).where(
            FinBalanceProjection.tenant_id == tenant_id
        )
        res = await db.execute(stmt)
        proj = res.scalar_one_or_none()

        if proj is None:
            _, proj = await FinAccountService.ensure_tenant_settlement_account(
                db, tenant_id
            )

        return proj

    @staticmethod
    async def get_balance_kopecks(db: AsyncSession, tenant_id: int) -> int:
        """Get the cached balance in kopecks."""
        proj = await FinProjectionService.get_projection(db, tenant_id)
        return proj.balance_kopecks

    @staticmethod
    async def rebuild_projection(
        db: AsyncSession, tenant_id: int, actor: str = "rebuild_projection"
    ) -> tuple[FinBalanceProjection, int, int]:
        """Recalculate true balance strictly from posted ledger entries and update projection.

        Returns (projection, previous_balance_kopecks, recalculated_balance_kopecks).
        """
        # 1. Ensure settlement account exists
        settlement_acc, _ = await FinAccountService.ensure_tenant_settlement_account(
            db, tenant_id
        )

        # 2. Sum credits and debits from posted transactions for this settlement account
        stmt = (
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
                FinLedgerEntry.tenant_id == tenant_id,
                FinLedgerEntry.account_id == settlement_acc.id,
                FinLedgerTransaction.status == "posted",
            )
        )
        res = await db.execute(stmt)
        total_credit, total_debit = res.one()

        true_balance = int(total_credit - total_debit)

        # 3. Find latest posted transaction id
        stmt_max = select(func.max(FinLedgerTransaction.id)).where(
            FinLedgerTransaction.tenant_id == tenant_id,
            FinLedgerTransaction.status == "posted",
        )
        max_tx_id = (await db.execute(stmt_max)).scalar_one_or_none()

        # 4. Lock and update projection
        lock_stmt = (
            select(FinBalanceProjection)
            .where(FinBalanceProjection.tenant_id == tenant_id)
            .with_for_update()
        )
        res_lock = await db.execute(lock_stmt)
        proj = res_lock.scalar_one()

        previous_balance = proj.balance_kopecks
        proj.balance_kopecks = true_balance
        proj.version += 1
        proj.last_transaction_id = max_tx_id
        proj.updated_at = datetime.now(UTC)
        await db.flush()

        logger.info(
            "Rebuilt balance projection for tenant=%s: previous=%s, true=%s, ver=%s (actor=%s)",
            tenant_id,
            previous_balance,
            true_balance,
            proj.version,
            actor,
        )

        return proj, previous_balance, true_balance
