from __future__ import annotations

import logging

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models_l4desk import FinAccount, FinBalanceProjection
from app.services.financial_core.exceptions import FinValidationError

logger = logging.getLogger(__name__)

SYSTEM_ACCOUNT_KINDS = ("payment_clearing", "usage_revenue")


class FinAccountService:
    """Service for managing fin_accounts and initial balance projections."""

    @staticmethod
    async def ensure_system_accounts(db: AsyncSession) -> dict[str, FinAccount]:
        """Ensure global clearing and revenue accounts exist (tenant_id IS NULL)."""
        stmt = select(FinAccount).where(FinAccount.tenant_id.is_(None))
        res = await db.execute(stmt)
        existing = {acc.kind: acc for acc in res.scalars().all()}

        created = False
        for kind in SYSTEM_ACCOUNT_KINDS:
            if kind not in existing:
                account = FinAccount(
                    tenant_id=None,
                    kind=kind,
                    currency="RUB",
                )
                db.add(account)
                existing[kind] = account
                created = True

        if created:
            await db.flush()
            logger.info("Initialized missing global financial system accounts")

        return existing

    @staticmethod
    async def get_system_account(db: AsyncSession, kind: str) -> FinAccount | None:
        """Fetch global system account by kind."""
        if kind not in SYSTEM_ACCOUNT_KINDS:
            raise FinValidationError(f"Invalid system account kind: {kind}")
        stmt = select(FinAccount).where(
            FinAccount.tenant_id.is_(None), FinAccount.kind == kind
        )
        res = await db.execute(stmt)
        return res.scalar_one_or_none()

    @staticmethod
    async def ensure_tenant_settlement_account(
        db: AsyncSession, tenant_id: int
    ) -> tuple[FinAccount, FinBalanceProjection]:
        """Ensure tenant settlement account and corresponding balance projection exist."""
        if (
            not isinstance(tenant_id, int)
            or isinstance(tenant_id, bool)
            or tenant_id <= 0
        ):
            raise FinValidationError(f"Invalid tenant_id: {tenant_id}")

        stmt = select(FinAccount).where(
            FinAccount.tenant_id == tenant_id,
            FinAccount.kind == "tenant_settlement",
        )
        res = await db.execute(stmt)
        account = res.scalar_one_or_none()

        if account is None:
            account = FinAccount(
                tenant_id=tenant_id,
                kind="tenant_settlement",
                currency="RUB",
            )
            db.add(account)
            await db.flush()
            logger.info(
                "Created tenant settlement account id=%s for tenant=%s",
                account.id,
                tenant_id,
            )

        # Check balance projection
        proj_stmt = select(FinBalanceProjection).where(
            FinBalanceProjection.tenant_id == tenant_id
        )
        proj_res = await db.execute(proj_stmt)
        projection = proj_res.scalar_one_or_none()

        if projection is None:
            projection = FinBalanceProjection(
                tenant_id=tenant_id,
                account_id=account.id,
                balance_kopecks=0,
                version=0,
            )
            db.add(projection)
            await db.flush()
            logger.info(
                "Initialized balance projection for tenant=%s (account_id=%s)",
                tenant_id,
                account.id,
            )

        return account, projection

    @staticmethod
    async def get_tenant_settlement_account(
        db: AsyncSession, tenant_id: int
    ) -> FinAccount | None:
        """Fetch tenant settlement account if it exists."""
        stmt = select(FinAccount).where(
            FinAccount.tenant_id == tenant_id,
            FinAccount.kind == "tenant_settlement",
        )
        res = await db.execute(stmt)
        return res.scalar_one_or_none()

    @staticmethod
    async def get_account_by_id(db: AsyncSession, account_id: int) -> FinAccount | None:
        """Fetch fin_account by primary key."""
        stmt = select(FinAccount).where(FinAccount.id == account_id)
        res = await db.execute(stmt)
        return res.scalar_one_or_none()
