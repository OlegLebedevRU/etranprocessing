from __future__ import annotations

from datetime import UTC, datetime
from typing import Any

from sqlalchemy import select, update
from sqlalchemy.ext.asyncio import AsyncSession

from app.models_l4desk import (
    FinAccount,
    FinBalanceProjection,
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
    async def get_terminal(self, terminal_id: int) -> L4DeskTerminal | None:
        stmt = select(L4DeskTerminal).where(L4DeskTerminal.terminal_id == terminal_id)
        res = await self.session.execute(stmt)
        return res.scalar_one_or_none()

    async def list_terminals_by_tenant(self, tenant_id: int) -> list[L4DeskTerminal]:
        stmt = select(L4DeskTerminal).where(L4DeskTerminal.tenant_id == tenant_id)
        res = await self.session.execute(stmt)
        return list(res.scalars().all())

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
