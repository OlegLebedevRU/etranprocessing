from __future__ import annotations

import contextlib
from datetime import UTC, date, datetime
from typing import Any

from sqlalchemy import func, select, update
from sqlalchemy.ext.asyncio import AsyncSession

from app.models_l4desk import (
    FinAccount,
    FinBalanceProjection,
    FinBillingCycle,
    FinBillingProfile,
    FinLedgerEntry,
    FinLedgerTransaction,
    FinManualPayment,
    FinNotificationDelivery,
    FinPayment,
    FinReconciliationRun,
    FinTariffVersion,
    FinTerminalMonthlyCharge,
    FinUsageDaily,
    L4DeskAuditEvent,
    L4DeskMembership,
    L4DeskRegistration,
    L4DeskRemoteSession,
    L4DeskTenantProfile,
    L4DeskTerminal,
)


class L4DeskRepository:
    """Repository for L4Desk commercial entities, tenant management, and audit events."""

    def __init__(self, session: AsyncSession) -> None:
        self.session = session

    # -------------------------------------------------------------------------
    # Registrations (L4D-05-MB)
    # -------------------------------------------------------------------------
    async def create_registration(
        self,
        *,
        email_normalized: str,
        password_hash: str,
        token_hash: str,
        terms_version: str,
        timezone: str,
        correlation_id: str,
        expires_at: datetime,
        source: str | None = None,
    ) -> L4DeskRegistration:
        reg = L4DeskRegistration(
            email_normalized=email_normalized,
            password_hash=password_hash,
            token_hash=token_hash,
            terms_version=terms_version,
            timezone=timezone,
            correlation_id=correlation_id,
            expires_at=expires_at,
            source=source,
        )
        self.session.add(reg)
        await self.session.flush()
        return reg

    async def get_registration_by_token(
        self, token_hash: str
    ) -> L4DeskRegistration | None:
        stmt = select(L4DeskRegistration).where(
            L4DeskRegistration.token_hash == token_hash
        )
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def get_registration_by_email(
        self, email_normalized: str
    ) -> L4DeskRegistration | None:
        stmt = select(L4DeskRegistration).where(
            L4DeskRegistration.email_normalized == email_normalized
        )
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def mark_registration_consumed(
        self,
        registration_id: int,
        user_id: int,
        tenant_id: int,
        consumed_at: datetime | None = None,
    ) -> None:
        if consumed_at is None:
            consumed_at = datetime.now(UTC)
        stmt = (
            update(L4DeskRegistration)
            .where(L4DeskRegistration.id == registration_id)
            .values(
                consumed_at=consumed_at,
                user_id=user_id,
                tenant_id=tenant_id,
            )
        )
        await self.session.execute(stmt)

    async def update_registration_token(
        self,
        registration_id: int,
        *,
        token_hash: str,
        expires_at: datetime,
        password_hash: str | None = None,
        timezone: str | None = None,
        terms_version: str | None = None,
    ) -> None:
        values: dict[str, Any] = {
            "token_hash": token_hash,
            "expires_at": expires_at,
        }
        if password_hash is not None:
            values["password_hash"] = password_hash
        if timezone is not None:
            values["timezone"] = timezone
        if terms_version is not None:
            values["terms_version"] = terms_version
        stmt = (
            update(L4DeskRegistration)
            .where(L4DeskRegistration.id == registration_id)
            .values(**values)
        )
        await self.session.execute(stmt)

    # -------------------------------------------------------------------------
    # Tenant Profiles & Memberships
    # -------------------------------------------------------------------------
    async def get_tenant_profile(self, tenant_id: int) -> L4DeskTenantProfile | None:
        stmt = select(L4DeskTenantProfile).where(
            L4DeskTenantProfile.tenant_id == tenant_id
        )
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def create_tenant_profile(
        self,
        *,
        tenant_id: int,
        timezone: str,
    ) -> L4DeskTenantProfile:
        profile = L4DeskTenantProfile(
            tenant_id=tenant_id,
            timezone=timezone,
        )
        self.session.add(profile)
        await self.session.flush()
        return profile

    async def create_membership(
        self,
        *,
        tenant_id: int,
        user_id: int,
        role_id: int = 5,
        is_owner: bool = False,
    ) -> L4DeskMembership:
        membership = L4DeskMembership(
            tenant_id=tenant_id,
            user_id=user_id,
            role_id=role_id,
            is_owner=is_owner,
        )
        self.session.add(membership)
        await self.session.flush()
        return membership

    async def get_memberships_for_user(self, user_id: int) -> list[L4DeskMembership]:
        stmt = select(L4DeskMembership).where(L4DeskMembership.user_id == user_id)
        res = await self.session.execute(stmt)
        return list(res.scalars().all())

    async def get_memberships_for_tenant(
        self, tenant_id: int
    ) -> list[L4DeskMembership]:
        stmt = select(L4DeskMembership).where(L4DeskMembership.tenant_id == tenant_id)
        res = await self.session.execute(stmt)
        return list(res.scalars().all())

    # -------------------------------------------------------------------------
    # Terminals (L4D-06-MB)
    # -------------------------------------------------------------------------
    async def get_terminal(
        self, terminal_id: int, tenant_id: int | None = None
    ) -> L4DeskTerminal | None:
        stmt = select(L4DeskTerminal).where(L4DeskTerminal.terminal_id == terminal_id)
        if tenant_id is not None:
            stmt = stmt.where(L4DeskTerminal.tenant_id == tenant_id)
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def get_terminal_by_operation_id(
        self, operation_id: str
    ) -> L4DeskTerminal | None:
        stmt = select(L4DeskTerminal).where(L4DeskTerminal.operation_id == operation_id)
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def get_terminal_by_sn(self, sn: str) -> L4DeskTerminal | None:
        stmt = select(L4DeskTerminal).where(
            L4DeskTerminal.sn == sn, L4DeskTerminal.deleted_at.is_(None)
        )
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def get_next_ordinal_for_tenant(self, tenant_id: int) -> int:
        stmt = select(func.coalesce(func.max(L4DeskTerminal.ordinal), 0) + 1).where(
            L4DeskTerminal.tenant_id == tenant_id
        )
        res = await self.session.execute(stmt)
        return int(res.scalar_one())

    async def get_earliest_active_terminal_id(self, tenant_id: int) -> int | None:
        stmt = (
            select(L4DeskTerminal.terminal_id)
            .where(
                L4DeskTerminal.tenant_id == tenant_id,
                L4DeskTerminal.deleted_at.is_(None),
            )
            .order_by(L4DeskTerminal.ordinal.asc(), L4DeskTerminal.created_at.asc())
            .limit(1)
        )
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def list_terminals_by_tenant(
        self, tenant_id: int, include_deleted: bool = False
    ) -> list[L4DeskTerminal]:
        stmt = select(L4DeskTerminal).where(L4DeskTerminal.tenant_id == tenant_id)
        if not include_deleted:
            stmt = stmt.where(L4DeskTerminal.deleted_at.is_(None))
        stmt = stmt.order_by(L4DeskTerminal.ordinal.asc())
        res = await self.session.execute(stmt)
        return list(res.scalars().all())

    async def soft_delete_terminal(
        self, terminal_id: int, tenant_id: int
    ) -> L4DeskTerminal | None:
        term = await self.get_terminal(terminal_id, tenant_id)
        if not term or term.deleted_at is not None:
            return None
        term.deleted_at = datetime.now(UTC)
        await self.session.flush()
        return term

    async def update_terminal_states(
        self,
        terminal_id: int,
        *,
        provisioning_state: str | None = None,
        pin_state: str | None = None,
        device_id: int | None = None,
        certificate_reference: str | None = None,
        last_error: str | None = None,
    ) -> None:
        values: dict[str, Any] = {}
        if provisioning_state is not None:
            values["provisioning_state"] = provisioning_state
        if pin_state is not None:
            values["pin_state"] = pin_state
        if device_id is not None:
            values["device_id"] = device_id
        if certificate_reference is not None:
            values["certificate_reference"] = certificate_reference
        if last_error is not None:
            values["last_error"] = last_error
        if values:
            stmt = (
                update(L4DeskTerminal)
                .where(L4DeskTerminal.terminal_id == terminal_id)
                .values(**values)
            )
            await self.session.execute(stmt)
            await self.session.flush()

    async def create_terminal(
        self,
        *,
        terminal_id: int,
        tenant_id: int,
        ordinal: int,
        sn: str,
        external_terminal_id: str,
        operation_id: str,
        correlation_id: str,
        runtime_terminal_id: int | None = None,
        device_id: int | None = None,
        provisioning_state: str = "pending",
        pin_state: str = "pending",
    ) -> L4DeskTerminal:
        term = L4DeskTerminal(
            terminal_id=terminal_id,
            tenant_id=tenant_id,
            ordinal=ordinal,
            sn=sn,
            external_terminal_id=external_terminal_id,
            operation_id=operation_id,
            correlation_id=correlation_id,
            runtime_terminal_id=runtime_terminal_id,
            device_id=device_id,
            provisioning_state=provisioning_state,
            pin_state=pin_state,
        )
        self.session.add(term)
        await self.session.flush()
        return term

    # -------------------------------------------------------------------------
    # Audit Events
    # -------------------------------------------------------------------------
    async def record_audit_event(
        self,
        *,
        actor: str,
        event_type: str,
        subject_type: str,
        subject_id: str,
        correlation_id: str,
        outcome: str = "success",
        tenant_id: int | None = None,
        operation_id: str | None = None,
        details: dict[str, Any] | None = None,
        occurred_at: datetime | None = None,
    ) -> L4DeskAuditEvent:
        ev = L4DeskAuditEvent(
            actor=actor,
            event_type=event_type,
            subject_type=subject_type,
            subject_id=subject_id,
            correlation_id=correlation_id,
            outcome=outcome,
            tenant_id=tenant_id,
            operation_id=operation_id,
            details=details,
        )
        if occurred_at is not None:
            ev.occurred_at = occurred_at
        self.session.add(ev)
        await self.session.flush()
        return ev

    # -------------------------------------------------------------------------
    # Remote Sessions
    # -------------------------------------------------------------------------
    async def get_session_by_id(self, session_id: int) -> L4DeskRemoteSession | None:
        stmt = select(L4DeskRemoteSession).where(L4DeskRemoteSession.id == session_id)
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def get_active_session_by_terminal_id(
        self, terminal_id: int
    ) -> L4DeskRemoteSession | None:
        from app.models import Terminal

        matched_ids = [terminal_id]
        with contextlib.suppress(Exception):
            stmt_term = select(Terminal.id, Terminal.device_id).where(
                (Terminal.id == terminal_id) | (Terminal.device_id == terminal_id)
            )
            res_term = await self.session.execute(stmt_term)
            if hasattr(res_term, "fetchall"):
                rows: Any = res_term.fetchall()
                if hasattr(rows, "__await__"):
                    rows = await rows  # pyright: ignore[reportGeneralTypeIssues]
                for row in rows:
                    if row[0] is not None and row[0] not in matched_ids:
                        matched_ids.append(row[0])
                    if row[1] is not None and row[1] not in matched_ids:
                        matched_ids.append(row[1])

        stmt = select(L4DeskRemoteSession).where(
            L4DeskRemoteSession.terminal_id.in_(matched_ids),
            L4DeskRemoteSession.state.in_(
                ["reserved", "start_requested", "active", "stop_requested"]
            ),
        )
        res = await self.session.execute(stmt)
        if hasattr(res, "scalar_one_or_none"):
            ret: Any = res.scalar_one_or_none()
            if hasattr(ret, "__await__"):
                ret = await ret  # pyright: ignore[reportGeneralTypeIssues]
            if isinstance(ret, L4DeskRemoteSession):
                return ret
        return None

    async def create_remote_session(
        self,
        *,
        tenant_id: int,
        terminal_id: int,
        operation_id: str,
        correlation_id: str,
        session_type: str,
        requested_by_user_id: int | None = None,
        provider_session_id: str | None = None,
        state: str = "reserved",
        active_at: Any = None,
        closed_at: Any = None,
        reason: str | None = None,
    ) -> L4DeskRemoteSession:
        now_dt = datetime.now(UTC)
        if state == "active" and active_at is None:
            active_at = now_dt
        if state in ("closed", "failed") and closed_at is None:
            closed_at = now_dt

        sess = L4DeskRemoteSession(
            tenant_id=tenant_id,
            terminal_id=terminal_id,
            operation_id=operation_id,
            correlation_id=correlation_id,
            session_type=session_type,
            requested_by_user_id=requested_by_user_id,
            provider_session_id=provider_session_id,
            state=state,
            active_at=active_at,
            closed_at=closed_at,
            reason=reason,
        )
        self.session.add(sess)
        await self.session.flush()
        return sess

    async def ensure_l4desk_terminal(
        self,
        *,
        terminal_id: int,
        tenant_id: int,
        sn: str,
        correlation_id: str,
    ) -> L4DeskTerminal:
        existing = await self.get_terminal(terminal_id, tenant_id)
        if existing is not None:
            return existing

        ord_stmt = select(func.coalesce(func.max(L4DeskTerminal.ordinal), 0) + 1).where(
            L4DeskTerminal.tenant_id == tenant_id
        )
        ord_res = await self.session.execute(ord_stmt)
        next_ord = ord_res.scalar_one()

        term = L4DeskTerminal(
            terminal_id=terminal_id,
            tenant_id=tenant_id,
            ordinal=next_ord,
            sn=sn,
            external_terminal_id=str(terminal_id),
            operation_id=f"legacy-init-{terminal_id}",
            correlation_id=correlation_id,
            runtime_terminal_id=terminal_id,
            device_id=terminal_id,
            provisioning_state="ready",
            pin_state="issued",
        )
        self.session.add(term)
        await self.session.flush()
        return term

    async def get_session_by_operation_id(
        self, tenant_id: int, operation_id: str
    ) -> L4DeskRemoteSession | None:
        stmt = select(L4DeskRemoteSession).where(
            L4DeskRemoteSession.tenant_id == tenant_id,
            L4DeskRemoteSession.operation_id == operation_id,
        )
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def get_session_by_provider_id(
        self, provider_session_id: str
    ) -> L4DeskRemoteSession | None:
        stmt = select(L4DeskRemoteSession).where(
            L4DeskRemoteSession.provider_session_id == provider_session_id
        )
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    # -------------------------------------------------------------------------
    # Financial subledger read helpers
    # -------------------------------------------------------------------------
    async def get_fin_account(
        self, tenant_id: int, kind: str = "tenant_settlement"
    ) -> FinAccount | None:
        stmt = select(FinAccount).where(
            FinAccount.tenant_id == tenant_id,
            FinAccount.kind == kind,
        )
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def get_balance_projection(
        self, tenant_id: int
    ) -> FinBalanceProjection | None:
        stmt = select(FinBalanceProjection).where(
            FinBalanceProjection.tenant_id == tenant_id
        )
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def get_ledger_transaction(
        self, transaction_id: int, tenant_id: int
    ) -> FinLedgerTransaction | None:
        stmt = select(FinLedgerTransaction).where(
            FinLedgerTransaction.id == transaction_id,
            FinLedgerTransaction.tenant_id == tenant_id,
        )
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def list_ledger_transactions(
        self, tenant_id: int, limit: int = 50, offset: int = 0
    ) -> list[FinLedgerTransaction]:
        stmt = (
            select(FinLedgerTransaction)
            .where(FinLedgerTransaction.tenant_id == tenant_id)
            .order_by(FinLedgerTransaction.created_at.desc())
            .offset(offset)
            .limit(limit)
        )
        res = await self.session.execute(stmt)
        return list(res.scalars().all())

    async def list_ledger_entries(self, transaction_id: int) -> list[FinLedgerEntry]:
        stmt = (
            select(FinLedgerEntry)
            .where(FinLedgerEntry.transaction_id == transaction_id)
            .order_by(FinLedgerEntry.line_number)
        )
        res = await self.session.execute(stmt)
        return list(res.scalars().all())

    async def get_reconciliation_run(self, run_id: int) -> FinReconciliationRun | None:
        stmt = select(FinReconciliationRun).where(FinReconciliationRun.id == run_id)
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def list_reconciliation_runs(
        self, tenant_id: int | None = None, limit: int = 50
    ) -> list[FinReconciliationRun]:
        stmt = select(FinReconciliationRun).order_by(
            FinReconciliationRun.started_at.desc()
        )
        if tenant_id is not None:
            stmt = stmt.where(FinReconciliationRun.tenant_id == tenant_id)
        stmt = stmt.limit(limit)
        res = await self.session.execute(stmt)
        return list(res.scalars().all())

    # -------------------------------------------------------------------------
    # Tariff, Cycle, and Metering read helpers
    # -------------------------------------------------------------------------
    async def get_billing_profile(self, tenant_id: int) -> FinBillingProfile | None:
        stmt = select(FinBillingProfile).where(FinBillingProfile.tenant_id == tenant_id)
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def get_billing_cycle(self, cycle_id: int) -> FinBillingCycle | None:
        stmt = select(FinBillingCycle).where(FinBillingCycle.id == cycle_id)
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def list_billing_cycles(
        self, tenant_id: int, limit: int = 50
    ) -> list[FinBillingCycle]:
        stmt = (
            select(FinBillingCycle)
            .where(FinBillingCycle.tenant_id == tenant_id)
            .order_by(FinBillingCycle.sequence.desc())
            .limit(limit)
        )
        res = await self.session.execute(stmt)
        return list(res.scalars().all())

    async def get_usage_daily(
        self, terminal_id: int, local_date: date
    ) -> FinUsageDaily | None:
        stmt = select(FinUsageDaily).where(
            FinUsageDaily.terminal_id == terminal_id,
            FinUsageDaily.local_date == local_date,
        )
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def list_usage_daily(
        self,
        tenant_id: int,
        terminal_id: int | None = None,
        start_date: date | None = None,
        end_date: date | None = None,
        limit: int = 50,
    ) -> list[FinUsageDaily]:
        stmt = select(FinUsageDaily).where(FinUsageDaily.tenant_id == tenant_id)
        if terminal_id is not None:
            stmt = stmt.where(FinUsageDaily.terminal_id == terminal_id)
        if start_date is not None:
            stmt = stmt.where(FinUsageDaily.local_date >= start_date)
        if end_date is not None:
            stmt = stmt.where(FinUsageDaily.local_date <= end_date)
        stmt = stmt.order_by(FinUsageDaily.local_date.desc()).limit(limit)
        res = await self.session.execute(stmt)
        return list(res.scalars().all())

    async def get_monthly_charge(
        self, terminal_id: int, billing_cycle_id: int
    ) -> FinTerminalMonthlyCharge | None:
        stmt = select(FinTerminalMonthlyCharge).where(
            FinTerminalMonthlyCharge.terminal_id == terminal_id,
            FinTerminalMonthlyCharge.billing_cycle_id == billing_cycle_id,
        )
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def list_monthly_charges(
        self, tenant_id: int, billing_cycle_id: int | None = None, limit: int = 50
    ) -> list[FinTerminalMonthlyCharge]:
        stmt = select(FinTerminalMonthlyCharge).where(
            FinTerminalMonthlyCharge.tenant_id == tenant_id
        )
        if billing_cycle_id is not None:
            stmt = stmt.where(
                FinTerminalMonthlyCharge.billing_cycle_id == billing_cycle_id
            )
        stmt = stmt.order_by(FinTerminalMonthlyCharge.first_online_at.desc()).limit(
            limit
        )
        res = await self.session.execute(stmt)
        return list(res.scalars().all())

    async def get_tariff_version(self, version: str) -> FinTariffVersion | None:
        stmt = select(FinTariffVersion).where(FinTariffVersion.version == version)
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def list_tariff_versions(self) -> list[FinTariffVersion]:
        stmt = select(FinTariffVersion).order_by(FinTariffVersion.effective_from.asc())
        res = await self.session.execute(stmt)
        return list(res.scalars().all())

    # -------------------------------------------------------------------------
    # Payments (YooKassa & Manual) read/write helpers
    # -------------------------------------------------------------------------
    async def get_payment(self, payment_id: int) -> FinPayment | None:
        stmt = select(FinPayment).where(FinPayment.id == payment_id)
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def get_payment_by_provider_id(
        self, provider_payment_id: str
    ) -> FinPayment | None:
        stmt = select(FinPayment).where(
            FinPayment.provider_payment_id == provider_payment_id
        )
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def get_payment_by_operation_id(
        self, tenant_id: int, operation_id: str
    ) -> FinPayment | None:
        stmt = select(FinPayment).where(
            FinPayment.tenant_id == tenant_id, FinPayment.operation_id == operation_id
        )
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def list_payments(
        self,
        tenant_id: int | None = None,
        status: str | None = None,
        limit: int = 50,
        offset: int = 0,
    ) -> list[FinPayment]:
        stmt = select(FinPayment)
        if tenant_id is not None:
            stmt = stmt.where(FinPayment.tenant_id == tenant_id)
        if status is not None:
            stmt = stmt.where(FinPayment.status == status)
        stmt = stmt.order_by(FinPayment.created_at.desc()).offset(offset).limit(limit)
        res = await self.session.execute(stmt)
        return list(res.scalars().all())

    async def get_manual_payment(
        self, manual_payment_id: int
    ) -> FinManualPayment | None:
        stmt = select(FinManualPayment).where(FinManualPayment.id == manual_payment_id)
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def list_manual_payments(
        self,
        tenant_id: int | None = None,
        limit: int = 50,
        offset: int = 0,
    ) -> list[FinManualPayment]:
        stmt = select(FinManualPayment)
        if tenant_id is not None:
            stmt = stmt.where(FinManualPayment.tenant_id == tenant_id)
        stmt = (
            stmt.order_by(FinManualPayment.created_at.desc())
            .offset(offset)
            .limit(limit)
        )
        res = await self.session.execute(stmt)
        return list(res.scalars().all())

    # -------------------------------------------------------------------------
    # Active & Stop-requested Remote Sessions (L4D-12-MB)
    # -------------------------------------------------------------------------
    async def get_active_sessions_for_tenant(
        self, tenant_id: int
    ) -> list[L4DeskRemoteSession]:
        stmt = (
            select(L4DeskRemoteSession)
            .where(
                L4DeskRemoteSession.tenant_id == tenant_id,
                L4DeskRemoteSession.state.in_(
                    ["reserved", "start_requested", "active", "stop_requested"]
                ),
            )
            .order_by(L4DeskRemoteSession.id.asc())
        )
        res = await self.session.execute(stmt)
        return list(res.scalars().all())

    async def get_stop_requested_sessions(
        self, limit: int = 50
    ) -> list[L4DeskRemoteSession]:
        stmt = (
            select(L4DeskRemoteSession)
            .where(L4DeskRemoteSession.state == "stop_requested")
            .order_by(L4DeskRemoteSession.id.asc())
            .limit(limit)
        )
        res = await self.session.execute(stmt)
        return list(res.scalars().all())

    # -------------------------------------------------------------------------
    # Notification Deliveries (L4D-12-MB)
    # -------------------------------------------------------------------------
    async def get_notification_delivery(
        self, tenant_id: int, billing_cycle_id: int, notification_type: str
    ) -> FinNotificationDelivery | None:
        stmt = select(FinNotificationDelivery).where(
            FinNotificationDelivery.tenant_id == tenant_id,
            FinNotificationDelivery.billing_cycle_id == billing_cycle_id,
            FinNotificationDelivery.notification_type == notification_type,
        )
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def list_notification_deliveries(
        self,
        tenant_id: int | None = None,
        billing_cycle_id: int | None = None,
        status: str | None = None,
        limit: int = 50,
        offset: int = 0,
    ) -> list[FinNotificationDelivery]:
        stmt = select(FinNotificationDelivery)
        if tenant_id is not None:
            stmt = stmt.where(FinNotificationDelivery.tenant_id == tenant_id)
        if billing_cycle_id is not None:
            stmt = stmt.where(
                FinNotificationDelivery.billing_cycle_id == billing_cycle_id
            )
        if status is not None:
            stmt = stmt.where(FinNotificationDelivery.status == status)
        stmt = (
            stmt.order_by(FinNotificationDelivery.scheduled_at.desc())
            .offset(offset)
            .limit(limit)
        )
        res = await self.session.execute(stmt)
        return list(res.scalars().all())

    async def list_pending_notification_deliveries(
        self, as_of: datetime, limit: int = 50
    ) -> list[FinNotificationDelivery]:
        stmt = (
            select(FinNotificationDelivery)
            .where(
                FinNotificationDelivery.status.in_(["pending", "failed"]),
                FinNotificationDelivery.scheduled_at <= as_of,
                FinNotificationDelivery.attempts < 3,
            )
            .order_by(FinNotificationDelivery.scheduled_at.asc())
            .limit(limit)
        )
        res = await self.session.execute(stmt)
        return list(res.scalars().all())
