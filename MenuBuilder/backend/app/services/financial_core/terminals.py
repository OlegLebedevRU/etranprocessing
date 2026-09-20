from __future__ import annotations

import hashlib
import logging
from datetime import datetime

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models_l4desk import FinTerminalMonthlyCharge, L4DeskTerminal
from app.services.financial_core.accounts import FinAccountService
from app.services.financial_core.cycles import FinBillingCycleService
from app.services.financial_core.posting import FinPostingService
from app.services.financial_core.schemas import (
    FinPostingEntryRequest,
    FinPostingRequest,
)
from app.services.financial_core.tariffs import FinTariffService

logger = logging.getLogger(__name__)


class FinTerminalService:
    """Service for free terminal privilege resolution and terminal monthly charges."""

    @staticmethod
    async def get_free_terminal(
        db: AsyncSession, tenant_id: int
    ) -> L4DeskTerminal | None:
        """Resolve the current free terminal for tenant.

        Normative Logic:
        2. Первый по неизменяемому порядку существующий terminal бесплатен;
           после удаления льгота переходит следующему только вперёд.
        """
        stmt = (
            select(L4DeskTerminal)
            .where(
                L4DeskTerminal.tenant_id == tenant_id,
                L4DeskTerminal.deleted_at.is_(None),
            )
            .order_by(L4DeskTerminal.ordinal.asc())
            .limit(1)
        )
        res = await db.execute(stmt)
        return res.scalar_one_or_none()

    @staticmethod
    async def is_terminal_free(
        db: AsyncSession, tenant_id: int, terminal_id: int
    ) -> bool:
        """Check if terminal currently holds the free terminal privilege."""
        free_term = await FinTerminalService.get_free_terminal(db, tenant_id)
        if free_term is None:
            return False
        return free_term.terminal_id == terminal_id

    @staticmethod
    async def process_device_online_monthly_charge(
        db: AsyncSession,
        tenant_id: int,
        terminal_id: int,
        event_id: str,
        occurred_at: datetime,
        actor: str = "metering_worker",
        correlation_id: str = "",
    ) -> FinTerminalMonthlyCharge | None:
        """Process monthly charge upon authenticated device_online in billing cycle.

        Normative Logic:
        3. Каждый другой terminal при первом аутентифицированном device_online в cycle
           получает один charge 10000 kopecks по unique (terminal,billing_cycle)
           даже при online после grace boundary.
        """
        cycle = await FinBillingCycleService.get_or_create_cycle_for_timestamp(
            db, tenant_id, occurred_at
        )
        if cycle is None:
            # Tenant has no active billing cycle (free tier without anchor)
            return None

        # Check unique constraint (terminal_id, billing_cycle_id)
        stmt_existing = select(FinTerminalMonthlyCharge).where(
            FinTerminalMonthlyCharge.terminal_id == terminal_id,
            FinTerminalMonthlyCharge.billing_cycle_id == cycle.id,
        )
        res_existing = await db.execute(stmt_existing)
        existing = res_existing.scalar_one_or_none()
        if existing is not None:
            logger.debug(
                "Monthly charge already exists for terminal=%s in cycle=%s",
                terminal_id,
                cycle.id,
            )
            return existing

        is_free = await FinTerminalService.is_terminal_free(db, tenant_id, terminal_id)
        tariff = await FinTariffService.get_effective_tariff(db, cycle.starts_at)
        source_hash = hashlib.sha256(
            f"{terminal_id}:{cycle.id}:{event_id}".encode()
        ).hexdigest()
        corr_id = correlation_id or f"monthly-{terminal_id}-{cycle.id}"

        if is_free:
            # Free terminal receives a zero-charge record with snapshot
            charge = FinTerminalMonthlyCharge(
                tenant_id=tenant_id,
                terminal_id=terminal_id,
                billing_cycle_id=cycle.id,
                tariff_version_id=tariff.id,
                is_free=True,
                first_online_at=occurred_at,
                calculated_kopecks=0,
                posted_kopecks=0,
                discarded_kopecks=0,
                source_project="MenuBuilder",
                source_event_id=event_id,
                source_events_hash=source_hash,
                archive_batch_id=None,
                ledger_transaction_id=None,
                actor=actor,
                correlation_id=corr_id,
                posted_at=None,
            )
            db.add(charge)
            await db.flush()
            logger.info(
                "Created zero monthly charge for free terminal=%s in cycle=%s",
                terminal_id,
                cycle.id,
            )
            return charge

        # Paid terminal receives 10000 kopecks charge
        rate = tariff.terminal_month_kopecks
        calculated_kopecks = rate
        posted_kopecks = (calculated_kopecks // 100) * 100
        discarded_kopecks = calculated_kopecks - posted_kopecks

        settlement_acc, _ = await FinAccountService.ensure_tenant_settlement_account(
            db, tenant_id
        )
        revenue_acc = await FinAccountService.get_system_account(db, "usage_revenue")
        if revenue_acc is None:
            sys_accs = await FinAccountService.ensure_system_accounts(db)
            revenue_acc = sys_accs["usage_revenue"]

        post_req = FinPostingRequest(
            tenant_id=tenant_id,
            operation_id=f"op-monthly-{terminal_id}-{cycle.id}",
            kind="terminal_month",
            source_project="MenuBuilder",
            source_type="monthly_charge",
            source_id=f"monthly-{terminal_id}-{cycle.id}",
            source_event_id=event_id,
            source_events_hash=source_hash,
            actor=actor,
            correlation_id=corr_id,
            entries=[
                FinPostingEntryRequest(
                    account_id=settlement_acc.id,
                    debit_kopecks=posted_kopecks,
                    credit_kopecks=0,
                ),
                FinPostingEntryRequest(
                    account_id=revenue_acc.id,
                    debit_kopecks=0,
                    credit_kopecks=posted_kopecks,
                ),
            ],
        )
        tx = await FinPostingService.post_transaction(db, post_req)

        charge = FinTerminalMonthlyCharge(
            tenant_id=tenant_id,
            terminal_id=terminal_id,
            billing_cycle_id=cycle.id,
            tariff_version_id=tariff.id,
            is_free=False,
            first_online_at=occurred_at,
            calculated_kopecks=calculated_kopecks,
            posted_kopecks=posted_kopecks,
            discarded_kopecks=discarded_kopecks,
            source_project="MenuBuilder",
            source_event_id=event_id,
            source_events_hash=tx.source_events_hash,
            archive_batch_id=None,
            ledger_transaction_id=tx.id,
            actor=actor,
            correlation_id=corr_id,
            posted_at=tx.posted_at,
        )
        db.add(charge)
        await db.flush()
        logger.info(
            "Charged 10000 kopecks for terminal=%s in cycle=%s (tx_id=%s)",
            terminal_id,
            cycle.id,
            tx.id,
        )
        return charge
