"""Move one unpaid E2E owner to a previously verified vacant organization ID.

The source organization and its deleted terminal remain for audit. Run without
E2E_APPLY=1 for a read-only preflight; rerunning after an uncertain commit is safe.
"""

from __future__ import annotations

import asyncio
import os
from datetime import UTC, datetime

from sqlalchemy import func, select, update

from app.database import async_session
from app.models import Org, OrgBillingSettings, OrgStatus, User, UserSession
from app.models_l4desk import (
    FinBalanceProjection,
    FinLedgerTransaction,
    FinPayment,
    FinUsageDaily,
    L4DeskAuditEvent,
    L4DeskMembership,
    L4DeskRegistration,
    L4DeskRemoteSession,
    L4DeskTenantProfile,
    L4DeskTerminal,
)


async def _count(session, model, *conditions) -> int:
    result = await session.scalar(
        select(func.count()).select_from(model).where(*conditions)
    )
    return int(result or 0)


async def main() -> None:
    email = os.environ["E2E_REGISTRATION_EMAIL"].strip().lower()
    source_id = int(os.environ["E2E_SOURCE_TENANT_ID"])
    target_id = int(os.environ["E2E_TARGET_TENANT_ID"])
    apply = os.environ.get("E2E_APPLY") == "1"
    if source_id <= 0 or target_id <= 0 or source_id == target_id:
        raise ValueError("Distinct positive source and target tenant IDs are required")

    async with async_session() as session, session.begin():
        source = await session.get(Org, source_id)
        target = await session.get(Org, target_id)
        user = (
            await session.execute(
                select(User).where(User.username == email).with_for_update()
            )
        ).scalar_one_or_none()
        registration = (
            await session.execute(
                select(L4DeskRegistration)
                .where(L4DeskRegistration.email_normalized == email)
                .with_for_update()
            )
        ).scalar_one_or_none()
        if source is None or user is None or registration is None:
            raise RuntimeError("Source tenant, owner or registration is missing")
        if source.email != email or not source.is_email_verified:
            raise RuntimeError("Source tenant email verification differs")
        if user.role_id != 5 or user.role != "l4desk_owner" or not user.is_active:
            raise RuntimeError("Owner role or active state differs")
        if registration.user_id != user.id or registration.consumed_at is None:
            raise RuntimeError("Registration is not confirmed for this owner")

        old_membership = await session.get(L4DeskMembership, (source_id, user.id))
        new_membership = await session.get(L4DeskMembership, (target_id, user.id))
        if target is not None:
            if (
                user.org_id == target_id
                and registration.tenant_id == target_id
                and new_membership is not None
                and new_membership.is_owner
                and old_membership is None
            ):
                print(f"ALREADY_MOVED tenant_id={target_id} user_id={user.id}")
                return
            raise RuntimeError("Target ID exists without a completed reassignment")
        if user.org_id != source_id or registration.tenant_id != source_id:
            raise RuntimeError("Owner or registration no longer belongs to source")
        if old_membership is None or not old_membership.is_owner:
            raise RuntimeError("Source owner membership is missing")
        if new_membership is not None:
            raise RuntimeError("Target membership already exists")

        active_terminals = await _count(
            session,
            L4DeskTerminal,
            L4DeskTerminal.tenant_id == source_id,
            L4DeskTerminal.deleted_at.is_(None),
        )
        ledger = await _count(
            session, FinLedgerTransaction, FinLedgerTransaction.tenant_id == source_id
        )
        payments = await _count(session, FinPayment, FinPayment.tenant_id == source_id)
        usage = await _count(
            session, FinUsageDaily, FinUsageDaily.tenant_id == source_id
        )
        other_users = await _count(
            session, User, User.org_id == source_id, User.id != user.id
        )
        active_sessions = await _count(
            session,
            L4DeskRemoteSession,
            L4DeskRemoteSession.tenant_id == source_id,
            L4DeskRemoteSession.state.in_(
                ("reserved", "start_requested", "active", "stop_requested")
            ),
        )
        if any(
            (active_terminals, ledger, payments, usage, other_users, active_sessions)
        ):
            raise RuntimeError("Source tenant has active or financial records")
        projection = await session.get(FinBalanceProjection, source_id)
        if projection is not None and projection.balance_kopecks != 0:
            raise RuntimeError("Source tenant balance is not zero")
        if (
            await _count(
                session, L4DeskMembership, L4DeskMembership.tenant_id == source_id
            )
            != 1
        ):
            raise RuntimeError("Source tenant has another membership")

        billing = await session.get(OrgBillingSettings, source_id)
        status = (
            await session.execute(
                select(OrgStatus).where(OrgStatus.org_id == source_id)
            )
        ).scalar_one_or_none()
        tenant_profile = await session.get(L4DeskTenantProfile, source_id)
        if billing is None or status is None or tenant_profile is None:
            raise RuntimeError("Source tenant settings are incomplete")
        if billing.monthly_price_minor != 0 or billing.billing_mode != "prepaid":
            raise RuntimeError("Source tenant does not have the free package")
        if await _count(
            session, L4DeskMembership, L4DeskMembership.tenant_id == target_id
        ):
            raise RuntimeError("Target ID already has a membership")

        print(
            f"PREFLIGHT source={source_id} target={target_id} user_id={user.id} "
            "active_terminals=0 ledger=0 payments=0 usage=0"
        )
        if not apply:
            return

        session.add(
            Org(
                org_id=target_id,
                org_name=source.org_name,
                name=source.name,
                status=source.status,
                is_active=source.is_active,
                timezone=source.timezone,
                email=source.email,
                phone=source.phone,
                notify_by_email=source.notify_by_email,
                send_reports=source.send_reports,
                is_email_verified=source.is_email_verified,
                email_verified_at=source.email_verified_at,
            )
        )
        await session.flush()
        session.add(
            OrgBillingSettings(
                org_id=target_id,
                monthly_price_minor=billing.monthly_price_minor,
                currency=billing.currency,
                billing_mode=billing.billing_mode,
                min_billing_periods=billing.min_billing_periods,
                allowed_billing_periods=billing.allowed_billing_periods,
                default_selection_mode=billing.default_selection_mode,
                cert_billing_mode=billing.cert_billing_mode,
                cert_price_minor=billing.cert_price_minor,
                tenant_pin_creation_enabled=billing.tenant_pin_creation_enabled,
                cert_charge_primary_issue=billing.cert_charge_primary_issue,
                cert_charge_reissue=billing.cert_charge_reissue,
            )
        )
        session.add(OrgStatus(org_id=target_id, status=status.status))
        session.add(
            L4DeskTenantProfile(tenant_id=target_id, timezone=tenant_profile.timezone)
        )
        await session.flush()

        await session.delete(old_membership)
        await session.flush()
        session.add(
            L4DeskMembership(
                tenant_id=target_id, user_id=user.id, role_id=5, is_owner=True
            )
        )
        user.org_id = target_id
        if user.last_org_id == source_id:
            user.last_org_id = target_id
        registration.tenant_id = target_id
        await session.execute(
            update(UserSession)
            .where(UserSession.user_id == user.id, UserSession.is_revoked.is_(False))
            .values(is_revoked=True)
        )
        session.add(
            L4DeskAuditEvent(
                actor="e2e_operator",
                event_type="tenant.reassigned",
                subject_type="user",
                subject_id=str(user.id),
                tenant_id=target_id,
                correlation_id=registration.correlation_id,
                outcome="success",
                details={
                    "source_tenant_id": source_id,
                    "target_tenant_id": target_id,
                    "reason": "iot_org_id_collision_in_isolated_e2e",
                    "occurred_at": datetime.now(UTC).isoformat(),
                },
            )
        )
    print(f"MOVED tenant_id={target_id} user_id={user.id}")


if __name__ == "__main__":
    asyncio.run(main())
