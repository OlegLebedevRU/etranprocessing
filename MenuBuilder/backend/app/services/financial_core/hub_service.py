from __future__ import annotations

import contextlib
import logging
import uuid
from datetime import UTC, date, datetime
from typing import Any

from etranprocessing_db.models.org import Org
from sqlalchemy import func, or_, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models_l4desk import (
    FinBalanceProjection,
    FinBillingCycle,
    FinBillingProfile,
    FinLedgerTransaction,
    FinManualPayment,
    FinNotificationDelivery,
    FinPayment,
    FinUsageDaily,
    L4DeskAuditEvent,
    L4DeskRegistration,
    L4DeskRemoteSession,
    L4DeskTerminal,
)
from app.services.financial_core.exceptions import FinValidationError
from app.services.financial_core.hub_schemas import (
    CorrelationDrilldownResponse,
    CorrelationFactNode,
    HubAuditEventItem,
    HubAuditEventsResponse,
    HubFinanceOverviewResponse,
    HubManualPaymentCreateRequest,
    HubManualPaymentStornoRequest,
    HubNotificationItem,
    HubNotificationsResponse,
    HubPaymentItem,
    HubPaymentsResponse,
    HubRegistrationItem,
    HubRegistrationsResponse,
    HubSessionItem,
    HubSessionsResponse,
    HubTenantFinanceItem,
    HubTerminalItem,
    HubTerminalsResponse,
    HubUsageItem,
    HubUsageResponse,
)
from app.services.financial_core.manual_payments import FinManualPaymentService
from app.services.financial_core.schemas import (
    FinManualPaymentRead,
)

logger = logging.getLogger(__name__)


class HubService:
    """Superuser hub query and correlation drill-down service."""

    @staticmethod
    async def _get_org_names(db: AsyncSession, tenant_ids: set[int]) -> dict[int, str]:
        """Safely fetch organization names by tenant IDs."""
        if not tenant_ids:
            return {}
        try:
            stmt = select(Org.org_id, Org.org_name).where(Org.org_id.in_(tenant_ids))
            res = await db.execute(stmt)
            return {row[0]: row[1] for row in res.all()}
        except Exception as exc:  # noqa: BLE001
            logger.warning("Failed to fetch org names: %s", exc)
            return {}

    @staticmethod
    async def list_registrations(
        db: AsyncSession,
        tenant_id: int | None = None,
        email: str | None = None,
        status: str | None = None,
        period_start: datetime | None = None,
        period_end: datetime | None = None,
        correlation_id: str | None = None,
        only_errors: bool = False,
        page: int = 1,
        page_size: int = 50,
    ) -> HubRegistrationsResponse:
        now = datetime.now(UTC)
        query = select(L4DeskRegistration)

        if tenant_id is not None:
            query = query.where(L4DeskRegistration.tenant_id == tenant_id)
        if email:
            query = query.where(
                L4DeskRegistration.email_normalized.ilike(f"%{email.lower().strip()}%")
            )
        if correlation_id:
            query = query.where(
                L4DeskRegistration.correlation_id == correlation_id.strip()
            )
        if period_start:
            query = query.where(L4DeskRegistration.created_at >= period_start)
        if period_end:
            query = query.where(L4DeskRegistration.created_at <= period_end)

        if status == "consumed":
            query = query.where(L4DeskRegistration.consumed_at.is_not(None))
        elif status == "expired":
            query = query.where(
                L4DeskRegistration.consumed_at.is_(None),
                L4DeskRegistration.expires_at < now,
            )
        elif status == "pending":
            query = query.where(
                L4DeskRegistration.consumed_at.is_(None),
                L4DeskRegistration.expires_at >= now,
            )

        if only_errors:
            # Errors: expired without being consumed
            query = query.where(
                L4DeskRegistration.consumed_at.is_(None),
                L4DeskRegistration.expires_at < now,
            )

        count_stmt = select(func.count()).select_from(query.subquery())
        total_res = await db.execute(count_stmt)
        total = total_res.scalar() or 0

        query = query.order_by(L4DeskRegistration.created_at.desc())
        offset = (page - 1) * page_size
        query = query.offset(offset).limit(page_size)

        rows = (await db.execute(query)).scalars().all()
        tenant_ids = {r.tenant_id for r in rows if r.tenant_id}
        org_names = await HubService._get_org_names(db, tenant_ids)

        # Check first payment for tenants
        first_paid_tenants: set[int] = set()
        if tenant_ids:
            with contextlib.suppress(Exception):
                prof_stmt = select(FinBillingProfile.tenant_id).where(
                    FinBillingProfile.tenant_id.in_(tenant_ids),
                    FinBillingProfile.first_payment_transaction_id.is_not(None),
                )
                prof_res = (await db.execute(prof_stmt)).scalars().all()
                first_paid_tenants = set(prof_res)

        items: list[HubRegistrationItem] = []
        for r in rows:
            if r.consumed_at is not None:
                calc_status = "consumed"
            elif r.expires_at < now:
                calc_status = "expired"
            else:
                calc_status = "pending"

            t_name = org_names.get(r.tenant_id) if r.tenant_id is not None else None
            is_first_paid = (
                (r.tenant_id in first_paid_tenants)
                if r.tenant_id is not None
                else False
            )

            items.append(
                HubRegistrationItem(
                    id=r.id,
                    tenant_id=r.tenant_id,
                    tenant_name=t_name,
                    email_normalized=r.email_normalized,
                    status=calc_status,
                    terms_version=r.terms_version,
                    timezone=r.timezone,
                    source=r.source,
                    created_at=r.created_at,
                    expires_at=r.expires_at,
                    consumed_at=r.consumed_at,
                    correlation_id=r.correlation_id,
                    is_first_paid=is_first_paid,
                )
            )

        return HubRegistrationsResponse(
            items=items,
            total=total,
            page=page,
            page_size=page_size,
        )

    @staticmethod
    async def list_terminals(
        db: AsyncSession,
        tenant_id: int | None = None,
        terminal_id: int | None = None,
        sn: str | None = None,
        provisioning_state: str | None = None,
        pin_state: str | None = None,
        is_free: bool | None = None,
        period_start: datetime | None = None,
        period_end: datetime | None = None,
        only_errors: bool = False,
        page: int = 1,
        page_size: int = 50,
    ) -> HubTerminalsResponse:
        query = select(L4DeskTerminal)

        if tenant_id is not None:
            query = query.where(L4DeskTerminal.tenant_id == tenant_id)
        if terminal_id is not None:
            query = query.where(L4DeskTerminal.terminal_id == terminal_id)
        if sn:
            query = query.where(L4DeskTerminal.sn.ilike(f"%{sn.strip()}%"))
        if provisioning_state:
            query = query.where(L4DeskTerminal.provisioning_state == provisioning_state)
        if pin_state:
            query = query.where(L4DeskTerminal.pin_state == pin_state)
        if is_free is not None:
            if is_free:
                query = query.where(L4DeskTerminal.ordinal == 1)
            else:
                query = query.where(L4DeskTerminal.ordinal > 1)
        if period_start:
            query = query.where(L4DeskTerminal.created_at >= period_start)
        if period_end:
            query = query.where(L4DeskTerminal.created_at <= period_end)

        if only_errors:
            query = query.where(
                or_(
                    L4DeskTerminal.provisioning_state == "failed",
                    L4DeskTerminal.pin_state == "failed",
                    L4DeskTerminal.last_error.is_not(None),
                )
            )

        count_stmt = select(func.count()).select_from(query.subquery())
        total_res = await db.execute(count_stmt)
        total = total_res.scalar() or 0

        query = query.order_by(L4DeskTerminal.created_at.desc())
        offset = (page - 1) * page_size
        query = query.offset(offset).limit(page_size)

        rows = (await db.execute(query)).scalars().all()
        tenant_ids = {r.tenant_id for r in rows}
        org_names = await HubService._get_org_names(db, tenant_ids)

        now = datetime.now(UTC)
        items: list[HubTerminalItem] = []
        for r in rows:
            is_online = False
            if r.last_online_at is not None:
                # Online if checked in within last 300 seconds
                diff = (now - r.last_online_at).total_seconds()
                is_online = 0 <= diff <= 300

            items.append(
                HubTerminalItem(
                    terminal_id=r.terminal_id,
                    tenant_id=r.tenant_id,
                    tenant_name=org_names.get(r.tenant_id),
                    sn=r.sn,
                    ordinal=r.ordinal,
                    external_terminal_id=r.external_terminal_id,
                    provisioning_state=r.provisioning_state,
                    pin_state=r.pin_state,
                    certificate_reference=r.certificate_reference,
                    first_online_at=r.first_online_at,
                    last_online_at=r.last_online_at,
                    is_online=is_online,
                    is_free=(r.ordinal == 1),
                    last_error=r.last_error,
                    correlation_id=r.correlation_id,
                    created_at=r.created_at,
                )
            )

        return HubTerminalsResponse(
            items=items,
            total=total,
            page=page,
            page_size=page_size,
        )

    @staticmethod
    async def list_sessions(
        db: AsyncSession,
        tenant_id: int | None = None,
        terminal_id: int | None = None,
        session_type: str | None = None,
        state: str | None = None,
        session_id: int | None = None,
        correlation_id: str | None = None,
        period_start: datetime | None = None,
        period_end: datetime | None = None,
        only_errors: bool = False,
        page: int = 1,
        page_size: int = 50,
    ) -> HubSessionsResponse:
        query = select(L4DeskRemoteSession)

        if tenant_id is not None:
            query = query.where(L4DeskRemoteSession.tenant_id == tenant_id)
        if terminal_id is not None:
            query = query.where(L4DeskRemoteSession.terminal_id == terminal_id)
        if session_type:
            query = query.where(L4DeskRemoteSession.session_type == session_type)
        if state:
            query = query.where(L4DeskRemoteSession.state == state)
        if session_id is not None:
            query = query.where(L4DeskRemoteSession.id == session_id)
        if correlation_id:
            query = query.where(
                L4DeskRemoteSession.correlation_id == correlation_id.strip()
            )
        if period_start:
            query = query.where(L4DeskRemoteSession.requested_at >= period_start)
        if period_end:
            query = query.where(L4DeskRemoteSession.requested_at <= period_end)

        if only_errors:
            query = query.where(
                or_(
                    L4DeskRemoteSession.state == "failed",
                    L4DeskRemoteSession.reason.is_not(None),
                )
            )

        count_stmt = select(func.count()).select_from(query.subquery())
        total_res = await db.execute(count_stmt)
        total = total_res.scalar() or 0

        query = query.order_by(L4DeskRemoteSession.requested_at.desc())
        offset = (page - 1) * page_size
        query = query.offset(offset).limit(page_size)

        rows = (await db.execute(query)).scalars().all()

        # Terminal SNs
        terminal_ids = {r.terminal_id for r in rows}
        term_map: dict[int, str] = {}
        if terminal_ids:
            t_stmt = select(L4DeskTerminal.terminal_id, L4DeskTerminal.sn).where(
                L4DeskTerminal.terminal_id.in_(terminal_ids)
            )
            t_res = await db.execute(t_stmt)
            term_map = {row[0]: row[1] for row in t_res.all()}

        now = datetime.now(UTC)
        items: list[HubSessionItem] = []
        for r in rows:
            dur = 0
            if r.active_at is not None:
                end_t = r.closed_at or now
                dur = max(0, int((end_t - r.active_at).total_seconds()))

            items.append(
                HubSessionItem(
                    id=r.id,
                    tenant_id=r.tenant_id,
                    terminal_id=r.terminal_id,
                    terminal_sn=term_map.get(r.terminal_id),
                    operation_id=r.operation_id,
                    correlation_id=r.correlation_id,
                    session_type=r.session_type,
                    state=r.state,
                    requested_at=r.requested_at,
                    active_at=r.active_at,
                    closed_at=r.closed_at,
                    duration_seconds=dur,
                    reason=r.reason,
                    source_events_hash=r.source_events_hash,
                )
            )

        return HubSessionsResponse(
            items=items,
            total=total,
            page=page,
            page_size=page_size,
        )

    @staticmethod
    async def list_usage(
        db: AsyncSession,
        tenant_id: int | None = None,
        terminal_id: int | None = None,
        start_date: date | None = None,
        end_date: date | None = None,
        free_paid: str | None = None,
        only_unreconciled: bool = False,
        page: int = 1,
        page_size: int = 50,
    ) -> HubUsageResponse:
        query = select(FinUsageDaily)

        if tenant_id is not None:
            query = query.where(FinUsageDaily.tenant_id == tenant_id)
        if terminal_id is not None:
            query = query.where(FinUsageDaily.terminal_id == terminal_id)
        if start_date:
            query = query.where(FinUsageDaily.local_date >= start_date)
        if end_date:
            query = query.where(FinUsageDaily.local_date <= end_date)

        if free_paid == "free":
            query = query.where(FinUsageDaily.billable_seconds == 0)
        elif free_paid == "paid":
            query = query.where(FinUsageDaily.billable_seconds > 0)

        if only_unreconciled:
            # Unposted or mismatch: billable seconds > 0 but ledger transaction not linked
            query = query.where(
                FinUsageDaily.billable_seconds > 0,
                FinUsageDaily.ledger_transaction_id.is_(None),
            )

        count_stmt = select(func.count()).select_from(query.subquery())
        total_res = await db.execute(count_stmt)
        total = total_res.scalar() or 0

        query = query.order_by(FinUsageDaily.local_date.desc(), FinUsageDaily.id.desc())
        offset = (page - 1) * page_size
        query = query.offset(offset).limit(page_size)

        rows = (await db.execute(query)).scalars().all()

        terminal_ids = {r.terminal_id for r in rows}
        term_map: dict[int, str] = {}
        if terminal_ids:
            t_stmt = select(L4DeskTerminal.terminal_id, L4DeskTerminal.sn).where(
                L4DeskTerminal.terminal_id.in_(terminal_ids)
            )
            t_res = await db.execute(t_stmt)
            term_map = {row[0]: row[1] for row in t_res.all()}

        items: list[HubUsageItem] = []
        for r in rows:
            # Reconciled invariant: calculated == posted + discarded
            is_reconciled = (
                r.calculated_kopecks == (r.posted_kopecks + r.discarded_kopecks)
                and (r.posted_kopecks % 100 == 0)
                and (r.ledger_transaction_id is not None or r.billable_seconds == 0)
            )
            items.append(
                HubUsageItem(
                    id=r.id,
                    tenant_id=r.tenant_id,
                    terminal_id=r.terminal_id,
                    terminal_sn=term_map.get(r.terminal_id),
                    local_date=r.local_date,
                    source_seconds=r.source_seconds,
                    video_seconds=r.video_seconds,
                    console_seconds=r.console_seconds,
                    free_seconds=r.free_seconds,
                    billable_seconds=r.billable_seconds,
                    calculated_kopecks=r.calculated_kopecks,
                    posted_kopecks=r.posted_kopecks,
                    discarded_kopecks=r.discarded_kopecks,
                    is_reconciled=is_reconciled,
                    ledger_transaction_id=r.ledger_transaction_id,
                    source_events_hash=r.source_events_hash,
                    correlation_id=r.correlation_id,
                    posted_at=r.posted_at,
                )
            )

        return HubUsageResponse(
            items=items,
            total=total,
            page=page,
            page_size=page_size,
        )

    @staticmethod
    async def get_finance_overview(
        db: AsyncSession,
        tenant_id: int | None = None,
        active_grace_blocked: str | None = None,
        only_errors: bool = False,
        page: int = 1,
        page_size: int = 50,
    ) -> HubFinanceOverviewResponse:
        query = select(FinBillingProfile)

        if tenant_id is not None:
            query = query.where(FinBillingProfile.tenant_id == tenant_id)
        if active_grace_blocked in ("active", "grace", "blocked"):
            query = query.where(FinBillingProfile.entitlement == active_grace_blocked)

        # Global counters
        all_profiles_res = await db.execute(select(FinBillingProfile))
        all_profiles = all_profiles_res.scalars().all()
        total_tenants = len(all_profiles)
        active_count = sum(1 for p in all_profiles if p.entitlement == "active")
        grace_count = sum(1 for p in all_profiles if p.entitlement == "grace")
        blocked_count = sum(1 for p in all_profiles if p.entitlement == "blocked")

        all_proj_res = await db.execute(
            select(func.coalesce(func.sum(FinBalanceProjection.balance_kopecks), 0))
        )
        total_balance_kopecks = all_proj_res.scalar() or 0
        total_balance_rubles = round(total_balance_kopecks / 100.0, 2)

        count_stmt = select(func.count()).select_from(query.subquery())
        total_res = await db.execute(count_stmt)
        total = total_res.scalar() or 0

        query = query.order_by(FinBillingProfile.tenant_id.asc())
        offset = (page - 1) * page_size
        query = query.offset(offset).limit(page_size)
        profiles = (await db.execute(query)).scalars().all()

        tenant_ids = {p.tenant_id for p in profiles}
        org_names = await HubService._get_org_names(db, tenant_ids)

        # Fetch Projections
        proj_map: dict[int, int] = {}
        if tenant_ids:
            p_stmt = select(
                FinBalanceProjection.tenant_id, FinBalanceProjection.balance_kopecks
            ).where(FinBalanceProjection.tenant_id.in_(tenant_ids))
            p_res = await db.execute(p_stmt)
            proj_map = {row[0]: row[1] for row in p_res.all()}

        # Fetch active billing cycles
        cycle_map: dict[int, tuple[datetime, datetime]] = {}
        if tenant_ids:
            c_stmt = (
                select(
                    FinBillingCycle.tenant_id,
                    FinBillingCycle.ends_at,
                    FinBillingCycle.grace_deadline,
                )
                .where(FinBillingCycle.tenant_id.in_(tenant_ids))
                .order_by(FinBillingCycle.sequence.desc())
            )
            c_res = await db.execute(c_stmt)
            for row in c_res.all():
                if row[0] not in cycle_map:
                    cycle_map[row[0]] = (row[1], row[2])

        # Terminal count
        term_count_map: dict[int, int] = {}
        if tenant_ids:
            t_stmt = (
                select(L4DeskTerminal.tenant_id, func.count(L4DeskTerminal.terminal_id))
                .where(L4DeskTerminal.tenant_id.in_(tenant_ids))
                .group_by(L4DeskTerminal.tenant_id)
            )
            t_res = await db.execute(t_stmt)
            term_count_map = {row[0]: row[1] for row in t_res.all()}

        # Failed notifications
        failed_notif_tenants: set[int] = set()
        if tenant_ids:
            fn_stmt = select(FinNotificationDelivery.tenant_id).where(
                FinNotificationDelivery.tenant_id.in_(tenant_ids),
                FinNotificationDelivery.status == "failed",
            )
            fn_res = (await db.execute(fn_stmt)).scalars().all()
            failed_notif_tenants = set(fn_res)

        items: list[HubTenantFinanceItem] = []
        for p in profiles:
            bal_kop = proj_map.get(p.tenant_id, 0)
            c_ends, g_dead = cycle_map.get(p.tenant_id, (None, None))
            has_failed_notifs = p.tenant_id in failed_notif_tenants

            if only_errors and p.entitlement == "active" and not has_failed_notifs:
                continue

            items.append(
                HubTenantFinanceItem(
                    tenant_id=p.tenant_id,
                    tenant_name=org_names.get(p.tenant_id),
                    balance_kopecks=bal_kop,
                    balance_rubles=round(bal_kop / 100.0, 2),
                    entitlement=p.entitlement,
                    anchor_day=p.anchor_day,
                    current_cycle_ends_at=c_ends,
                    grace_deadline=g_dead,
                    terminal_count=term_count_map.get(p.tenant_id, 0),
                    has_failed_notifications=has_failed_notifs,
                )
            )

        return HubFinanceOverviewResponse(
            total_tenants=total_tenants,
            active_tenants=active_count,
            grace_tenants=grace_count,
            blocked_tenants=blocked_count,
            total_balance_rubles=total_balance_rubles,
            tenants=items,
            total=total,
            page=page,
            page_size=page_size,
        )

    @staticmethod
    async def list_payments(
        db: AsyncSession,
        tenant_id: int | None = None,
        payment_source: str | None = None,
        status: str | None = None,
        provider_payment_id: str | None = None,
        period_start: datetime | None = None,
        period_end: datetime | None = None,
        only_errors: bool = False,
        page: int = 1,
        page_size: int = 50,
    ) -> HubPaymentsResponse:
        items: list[HubPaymentItem] = []
        tenant_ids: set[int] = set()

        # 1. YooKassa Payments
        if payment_source in (None, "yookassa"):
            q_pay = select(FinPayment)
            if tenant_id is not None:
                q_pay = q_pay.where(FinPayment.tenant_id == tenant_id)
            if status:
                q_pay = q_pay.where(FinPayment.status == status)
            if provider_payment_id:
                q_pay = q_pay.where(
                    FinPayment.provider_payment_id.ilike(
                        f"%{provider_payment_id.strip()}%"
                    )
                )
            if period_start:
                q_pay = q_pay.where(FinPayment.created_at >= period_start)
            if period_end:
                q_pay = q_pay.where(FinPayment.created_at <= period_end)
            if only_errors:
                q_pay = q_pay.where(
                    or_(
                        FinPayment.status.in_(["canceled", "failed"]),
                        (FinPayment.status == "succeeded")
                        & (FinPayment.ledger_transaction_id.is_(None)),
                    )
                )

            pay_res = (await db.execute(q_pay)).scalars().all()
            for p in pay_res:
                tenant_ids.add(p.tenant_id)
                items.append(
                    HubPaymentItem(
                        id=p.id,
                        tenant_id=p.tenant_id,
                        source="yookassa",
                        amount_rubles=round(p.amount_kopecks / 100.0, 2),
                        amount_kopecks=p.amount_kopecks,
                        status=p.status,
                        reference=p.provider_payment_id or f"pay_{p.id}",
                        details=f"Provider: {p.provider}, Operation: {p.operation_id}",
                        ledger_transaction_id=p.ledger_transaction_id,
                        created_at=p.created_at,
                        correlation_id=p.correlation_id,
                    )
                )

        # 2. Manual Payments
        if payment_source in (None, "manual", "storno"):
            q_mp = select(FinManualPayment)
            if tenant_id is not None:
                q_mp = q_mp.where(FinManualPayment.tenant_id == tenant_id)
            if provider_payment_id:
                q_mp = q_mp.where(
                    FinManualPayment.document_number.ilike(
                        f"%{provider_payment_id.strip()}%"
                    )
                )
            if period_start:
                q_mp = q_mp.where(FinManualPayment.created_at >= period_start)
            if period_end:
                q_mp = q_mp.where(FinManualPayment.created_at <= period_end)

            mp_res = (await db.execute(q_mp)).scalars().all()
            for mp in mp_res:
                tenant_ids.add(mp.tenant_id)
                items.append(
                    HubPaymentItem(
                        id=mp.id,
                        tenant_id=mp.tenant_id,
                        source="manual",
                        amount_rubles=round(mp.amount_kopecks / 100.0, 2),
                        amount_kopecks=mp.amount_kopecks,
                        status="posted",
                        reference=mp.document_number,
                        payer=mp.payer,
                        purpose=mp.purpose,
                        comment=mp.comment,
                        ledger_transaction_id=mp.ledger_transaction_id,
                        created_at=mp.created_at,
                        correlation_id=mp.correlation_id,
                    )
                )

        # Sort combined items by created_at desc
        items.sort(key=lambda x: x.created_at, reverse=True)
        total = len(items)

        # Pagination in memory
        start_idx = (page - 1) * page_size
        end_idx = start_idx + page_size
        paginated_items = items[start_idx:end_idx]

        org_names = await HubService._get_org_names(db, tenant_ids)
        for it in paginated_items:
            it.tenant_name = org_names.get(it.tenant_id)

        return HubPaymentsResponse(
            items=paginated_items,
            total=total,
            page=page,
            page_size=page_size,
        )

    @staticmethod
    async def list_notifications(
        db: AsyncSession,
        tenant_id: int | None = None,
        notification_type: str | None = None,
        status: str | None = None,
        only_errors: bool = False,
        page: int = 1,
        page_size: int = 50,
    ) -> HubNotificationsResponse:
        query = select(FinNotificationDelivery)

        if tenant_id is not None:
            query = query.where(FinNotificationDelivery.tenant_id == tenant_id)
        if notification_type:
            query = query.where(
                FinNotificationDelivery.notification_type == notification_type
            )
        if status:
            query = query.where(FinNotificationDelivery.status == status)
        if only_errors:
            query = query.where(FinNotificationDelivery.status == "failed")

        count_stmt = select(func.count()).select_from(query.subquery())
        total_res = await db.execute(count_stmt)
        total = total_res.scalar() or 0

        query = query.order_by(
            FinNotificationDelivery.scheduled_at.desc(),
            FinNotificationDelivery.id.desc(),
        )
        offset = (page - 1) * page_size
        query = query.offset(offset).limit(page_size)

        rows = (await db.execute(query)).scalars().all()
        tenant_ids = {r.tenant_id for r in rows}
        org_names = await HubService._get_org_names(db, tenant_ids)

        items: list[HubNotificationItem] = []
        for r in rows:
            items.append(
                HubNotificationItem(
                    id=r.id,
                    tenant_id=r.tenant_id,
                    tenant_name=org_names.get(r.tenant_id),
                    billing_cycle_id=r.billing_cycle_id,
                    notification_type=r.notification_type,
                    scheduled_at=r.scheduled_at,
                    status=r.status,
                    attempts=r.attempts,
                    sent_at=r.sent_at,
                    last_error=r.last_error,
                    correlation_id=r.correlation_id,
                )
            )

        return HubNotificationsResponse(
            items=items,
            total=total,
            page=page,
            page_size=page_size,
        )

    @staticmethod
    async def list_audit_events(
        db: AsyncSession,
        tenant_id: int | None = None,
        event_type: str | None = None,
        subject_type: str | None = None,
        outcome: str | None = None,
        period_start: datetime | None = None,
        period_end: datetime | None = None,
        only_errors: bool = False,
        page: int = 1,
        page_size: int = 50,
    ) -> HubAuditEventsResponse:
        query = select(L4DeskAuditEvent)

        if tenant_id is not None:
            query = query.where(L4DeskAuditEvent.tenant_id == tenant_id)
        if event_type:
            query = query.where(
                L4DeskAuditEvent.event_type.ilike(f"%{event_type.strip()}%")
            )
        if subject_type:
            query = query.where(L4DeskAuditEvent.subject_type == subject_type)
        if outcome:
            query = query.where(L4DeskAuditEvent.outcome == outcome)
        if period_start:
            query = query.where(L4DeskAuditEvent.occurred_at >= period_start)
        if period_end:
            query = query.where(L4DeskAuditEvent.occurred_at <= period_end)
        if only_errors:
            query = query.where(L4DeskAuditEvent.outcome != "success")

        count_stmt = select(func.count()).select_from(query.subquery())
        total_res = await db.execute(count_stmt)
        total = total_res.scalar() or 0

        query = query.order_by(
            L4DeskAuditEvent.occurred_at.desc(), L4DeskAuditEvent.id.desc()
        )
        offset = (page - 1) * page_size
        query = query.offset(offset).limit(page_size)

        rows = (await db.execute(query)).scalars().all()
        items: list[HubAuditEventItem] = []
        for r in rows:
            items.append(
                HubAuditEventItem(
                    id=r.id,
                    tenant_id=r.tenant_id,
                    actor=r.actor,
                    event_type=r.event_type,
                    subject_type=r.subject_type,
                    subject_id=r.subject_id,
                    outcome=r.outcome,
                    details=r.details,
                    correlation_id=r.correlation_id,
                    occurred_at=r.occurred_at,
                )
            )

        return HubAuditEventsResponse(
            items=items,
            total=total,
            page=page,
            page_size=page_size,
        )

    @staticmethod
    async def get_correlation_drilldown(
        db: AsyncSession,
        correlation_id: str | None = None,
        tenant_id: int | None = None,
        terminal_id: int | None = None,
        session_id: int | None = None,
        payment_id: int | None = None,
        registration_id: int | None = None,
    ) -> CorrelationDrilldownResponse:
        """
        Build full correlation audit chain:
        registration -> terminal -> pin_provisioning -> online_session -> usage -> ledger_payment.
        Strict rule: missing fact is flagged as mismatch and NEVER mocked/hallucinated.
        """
        query_params = {
            "correlation_id": correlation_id,
            "tenant_id": tenant_id,
            "terminal_id": terminal_id,
            "session_id": session_id,
            "payment_id": payment_id,
            "registration_id": registration_id,
        }

        mismatches: list[str] = []

        # 1. Resolve Session Fact
        session_row: L4DeskRemoteSession | None = None
        if session_id is not None:
            session_row = await db.get(L4DeskRemoteSession, session_id)
        elif correlation_id:
            s_stmt = select(L4DeskRemoteSession).where(
                L4DeskRemoteSession.correlation_id == correlation_id
            )
            s_res = await db.execute(s_stmt)
            session_row = s_res.scalars().first()

        # Update context from session if found
        if session_row:
            if tenant_id is None:
                tenant_id = session_row.tenant_id
            if terminal_id is None:
                terminal_id = session_row.terminal_id

        # 2. Resolve Terminal Fact
        terminal_row: L4DeskTerminal | None = None
        if terminal_id is not None:
            terminal_row = await db.get(L4DeskTerminal, terminal_id)
        elif correlation_id:
            t_stmt = select(L4DeskTerminal).where(
                L4DeskTerminal.correlation_id == correlation_id
            )
            t_res = await db.execute(t_stmt)
            terminal_row = t_res.scalars().first()

        if terminal_row and tenant_id is None:
            tenant_id = terminal_row.tenant_id

        # 3. Resolve Registration Fact
        reg_row: L4DeskRegistration | None = None
        if registration_id is not None:
            reg_row = await db.get(L4DeskRegistration, registration_id)
        elif correlation_id:
            r_stmt = select(L4DeskRegistration).where(
                L4DeskRegistration.correlation_id == correlation_id
            )
            r_res = await db.execute(r_stmt)
            reg_row = r_res.scalars().first()
        elif tenant_id is not None:
            # Check if tenant has a registration record
            r_stmt = (
                select(L4DeskRegistration)
                .where(L4DeskRegistration.tenant_id == tenant_id)
                .order_by(L4DeskRegistration.created_at.asc())
            )
            r_res = await db.execute(r_stmt)
            reg_row = r_res.scalars().first()

        # 4. Resolve Usage Fact
        usage_row: FinUsageDaily | None = None
        if terminal_id is not None:
            if session_row and session_row.requested_at:
                s_date = session_row.requested_at.date()
                u_stmt = select(FinUsageDaily).where(
                    FinUsageDaily.terminal_id == terminal_id,
                    FinUsageDaily.local_date == s_date,
                )
                u_res = await db.execute(u_stmt)
                usage_row = u_res.scalars().first()
            if not usage_row:
                # Latest usage for terminal
                u_stmt = (
                    select(FinUsageDaily)
                    .where(FinUsageDaily.terminal_id == terminal_id)
                    .order_by(FinUsageDaily.local_date.desc())
                )
                u_res = await db.execute(u_stmt)
                usage_row = u_res.scalars().first()

        # 5. Resolve Ledger / Payment Fact
        ledger_tx: FinLedgerTransaction | None = None
        payment_row: FinPayment | FinManualPayment | None = None

        if payment_id is not None:
            payment_row = await db.get(FinPayment, payment_id)
            if not payment_row:
                payment_row = await db.get(FinManualPayment, payment_id)
            if payment_row and payment_row.ledger_transaction_id:
                ledger_tx = await db.get(
                    FinLedgerTransaction, payment_row.ledger_transaction_id
                )
        elif usage_row and usage_row.ledger_transaction_id:
            ledger_tx = await db.get(
                FinLedgerTransaction, usage_row.ledger_transaction_id
            )
        elif correlation_id:
            tx_stmt = select(FinLedgerTransaction).where(
                FinLedgerTransaction.correlation_id == correlation_id
            )
            tx_res = await db.execute(tx_stmt)
            ledger_tx = tx_res.scalars().first()

        if not payment_row and tenant_id is not None:
            # Check latest payment for tenant
            p_stmt = (
                select(FinPayment)
                .where(FinPayment.tenant_id == tenant_id)
                .order_by(FinPayment.created_at.desc())
            )
            payment_row = (await db.execute(p_stmt)).scalars().first()
            if not payment_row:
                mp_stmt = (
                    select(FinManualPayment)
                    .where(FinManualPayment.tenant_id == tenant_id)
                    .order_by(FinManualPayment.created_at.desc())
                )
                payment_row = (await db.execute(mp_stmt)).scalars().first()

        # Assemble Nodes
        nodes: dict[str, CorrelationFactNode] = {}

        # 1. Registration Node
        if reg_row:
            reg_mismatch = (
                reg_row.consumed_at is None and reg_row.expires_at < datetime.now(UTC)
            )
            if reg_mismatch:
                mismatches.append("REGISTRATION_EXPIRED_UNCONSUMED")
            nodes["registration"] = CorrelationFactNode(
                node_type="registration",
                label="Регистрация",
                present=True,
                mismatch=reg_mismatch,
                mismatch_code="REGISTRATION_EXPIRED_UNCONSUMED"
                if reg_mismatch
                else None,
                details=f"Email: {reg_row.email_normalized}, Tenant: {reg_row.tenant_id}, Status: {'consumed' if reg_row.consumed_at else 'pending'}",
                fact={
                    "id": reg_row.id,
                    "email": reg_row.email_normalized,
                    "tenant_id": reg_row.tenant_id,
                    "created_at": reg_row.created_at.isoformat()
                    if reg_row.created_at
                    else None,
                    "consumed_at": reg_row.consumed_at.isoformat()
                    if reg_row.consumed_at
                    else None,
                    "correlation_id": reg_row.correlation_id,
                },
            )
        else:
            mismatches.append("REGISTRATION_NOT_FOUND")
            nodes["registration"] = CorrelationFactNode(
                node_type="registration",
                label="Регистрация",
                present=False,
                mismatch=True,
                mismatch_code="REGISTRATION_NOT_FOUND",
                details="Факт регистрации не найден для указанного контекста",
                fact=None,
            )

        # 2. Terminal Node
        if terminal_row:
            term_mismatch = terminal_row.deleted_at is not None
            if term_mismatch:
                mismatches.append("TERMINAL_DELETED")
            nodes["terminal"] = CorrelationFactNode(
                node_type="terminal",
                label="Терминал",
                present=True,
                mismatch=term_mismatch,
                mismatch_code="TERMINAL_DELETED" if term_mismatch else None,
                details=f"SN: {terminal_row.sn}, Ordinal: {terminal_row.ordinal}, State: {terminal_row.provisioning_state}",
                fact={
                    "terminal_id": terminal_row.terminal_id,
                    "tenant_id": terminal_row.tenant_id,
                    "sn": terminal_row.sn,
                    "ordinal": terminal_row.ordinal,
                    "is_free": terminal_row.ordinal == 1,
                    "provisioning_state": terminal_row.provisioning_state,
                    "pin_state": terminal_row.pin_state,
                    "last_online_at": terminal_row.last_online_at.isoformat()
                    if terminal_row.last_online_at
                    else None,
                },
            )
        else:
            mismatches.append("TERMINAL_NOT_FOUND")
            nodes["terminal"] = CorrelationFactNode(
                node_type="terminal",
                label="Терминал",
                present=False,
                mismatch=True,
                mismatch_code="TERMINAL_NOT_FOUND",
                details="Терминал не найден в базе данных",
                fact=None,
            )

        # 3. PIN / Provisioning Node
        if terminal_row:
            pin_failed = (
                terminal_row.provisioning_state == "failed"
                or terminal_row.pin_state == "failed"
                or terminal_row.last_error is not None
            )
            pin_code = "PIN_PROVISIONING_FAILED" if pin_failed else None
            if pin_failed:
                mismatches.append(pin_code or "PIN_PROVISIONING_FAILED")
            nodes["pin_provisioning"] = CorrelationFactNode(
                node_type="pin_provisioning",
                label="PIN и сертификат",
                present=True,
                mismatch=pin_failed,
                mismatch_code=pin_code,
                details=f"PIN state: {terminal_row.pin_state}, Prov: {terminal_row.provisioning_state}, Cert ref: {terminal_row.certificate_reference or 'None'}",
                fact={
                    "pin_state": terminal_row.pin_state,
                    "provisioning_state": terminal_row.provisioning_state,
                    "certificate_reference": terminal_row.certificate_reference,
                    "last_error": terminal_row.last_error,
                },
            )
        else:
            mismatches.append("PIN_PROVISIONING_MISSING")
            nodes["pin_provisioning"] = CorrelationFactNode(
                node_type="pin_provisioning",
                label="PIN и сертификат",
                present=False,
                mismatch=True,
                mismatch_code="PIN_PROVISIONING_MISSING",
                details="Информация о подготовке PIN отсутствует",
                fact=None,
            )

        # 4. Online / Session Node
        if session_row:
            sess_mismatch = (
                session_row.state == "failed" or not session_row.source_events_hash
            )
            sess_code = (
                "SESSION_FAILED"
                if session_row.state == "failed"
                else (
                    "SESSION_HASH_MISSING"
                    if not session_row.source_events_hash
                    else None
                )
            )
            if sess_mismatch and sess_code:
                mismatches.append(sess_code)
            nodes["online_session"] = CorrelationFactNode(
                node_type="online_session",
                label="Сессия / Online",
                present=True,
                mismatch=sess_mismatch,
                mismatch_code=sess_code,
                details=f"Type: {session_row.session_type}, State: {session_row.state}, Op: {session_row.operation_id}",
                fact={
                    "session_id": session_row.id,
                    "session_type": session_row.session_type,
                    "state": session_row.state,
                    "requested_at": session_row.requested_at.isoformat()
                    if session_row.requested_at
                    else None,
                    "active_at": session_row.active_at.isoformat()
                    if session_row.active_at
                    else None,
                    "closed_at": session_row.closed_at.isoformat()
                    if session_row.closed_at
                    else None,
                    "source_events_hash": session_row.source_events_hash,
                },
            )
        else:
            is_sess_needed = session_id is not None
            nodes["online_session"] = CorrelationFactNode(
                node_type="online_session",
                label="Сессия / Online",
                present=False,
                mismatch=is_sess_needed,
                mismatch_code="SESSION_NOT_FOUND" if is_sess_needed else None,
                details="Удалённая сессия отсутствует в запрошенном контексте",
                fact=None,
            )
            if is_sess_needed:
                mismatches.append("SESSION_NOT_FOUND")

        # 5. Usage Node
        if usage_row:
            u_mismatch = (
                usage_row.calculated_kopecks
                != (usage_row.posted_kopecks + usage_row.discarded_kopecks)
                or (usage_row.posted_kopecks % 100 != 0)
                or (
                    usage_row.billable_seconds > 0
                    and usage_row.ledger_transaction_id is None
                )
            )
            u_code = "USAGE_UNRECONCILED" if u_mismatch else None
            if u_mismatch and u_code:
                mismatches.append(u_code)
            nodes["usage"] = CorrelationFactNode(
                node_type="usage",
                label="Потребление (Usage)",
                present=True,
                mismatch=u_mismatch,
                mismatch_code=u_code,
                details=f"Date: {usage_row.local_date}, Billable: {usage_row.billable_seconds}s, Calc: {usage_row.calculated_kopecks}k, Posted: {usage_row.posted_kopecks}k",
                fact={
                    "id": usage_row.id,
                    "date": str(usage_row.local_date),
                    "billable_seconds": usage_row.billable_seconds,
                    "calculated_kopecks": usage_row.calculated_kopecks,
                    "posted_kopecks": usage_row.posted_kopecks,
                    "discarded_kopecks": usage_row.discarded_kopecks,
                    "ledger_transaction_id": usage_row.ledger_transaction_id,
                    "source_events_hash": usage_row.source_events_hash,
                },
            )
        else:
            has_billable_session = (
                session_row is not None and session_row.closed_at is not None
            )
            nodes["usage"] = CorrelationFactNode(
                node_type="usage",
                label="Потребление (Usage)",
                present=False,
                mismatch=has_billable_session,
                mismatch_code="USAGE_NOT_FOUND" if has_billable_session else None,
                details="Суточный регистр потребления отсутствует",
                fact=None,
            )
            if has_billable_session:
                mismatches.append("USAGE_NOT_FOUND")

        # 6. Ledger / Payment Node
        has_ledger = ledger_tx is not None or payment_row is not None
        if has_ledger:
            l_mismatch = False
            l_code = None
            if ledger_tx and (
                ledger_tx.debit_kopecks != ledger_tx.credit_kopecks
                or ledger_tx.status != "posted"
            ):
                l_mismatch = True
                l_code = "LEDGER_IMBALANCED"
            if l_mismatch and l_code:
                mismatches.append(l_code)

            pay_fact: dict[str, Any] = {}
            if payment_row:
                pay_fact = {
                    "payment_id": payment_row.id,
                    "amount_kopecks": getattr(payment_row, "amount_kopecks", 0),
                    "status": getattr(payment_row, "status", "posted"),
                }
            tx_fact: dict[str, Any] = {}
            if ledger_tx:
                tx_fact = {
                    "transaction_id": ledger_tx.id,
                    "kind": ledger_tx.kind,
                    "status": ledger_tx.status,
                    "debit_kopecks": ledger_tx.debit_kopecks,
                    "credit_kopecks": ledger_tx.credit_kopecks,
                    "source_events_hash": ledger_tx.source_events_hash,
                }

            nodes["ledger_payment"] = CorrelationFactNode(
                node_type="ledger_payment",
                label="Проводка / Платёж",
                present=True,
                mismatch=l_mismatch,
                mismatch_code=l_code,
                details=f"Tx ID: {ledger_tx.id if ledger_tx else 'None'}, Pay ID: {payment_row.id if payment_row else 'None'}",
                fact={"transaction": tx_fact, "payment": pay_fact},
            )
        else:
            needs_posting = usage_row is not None and usage_row.posted_kopecks > 0
            nodes["ledger_payment"] = CorrelationFactNode(
                node_type="ledger_payment",
                label="Проводка / Платёж",
                present=False,
                mismatch=needs_posting,
                mismatch_code="LEDGER_POSTING_MISSING" if needs_posting else None,
                details="Проводка или платёж в subledger отсутствует",
                fact=None,
            )
            if needs_posting:
                mismatches.append("LEDGER_POSTING_MISSING")

        overall_status = "mismatch" if mismatches else "matched"

        return CorrelationDrilldownResponse(
            correlation_id=correlation_id,
            query_params=query_params,
            overall_status=overall_status,
            mismatch_codes=mismatches,
            nodes=nodes,
        )

    @staticmethod
    async def create_manual_payment_with_code11(
        db: AsyncSession,
        req: HubManualPaymentCreateRequest,
        actor: str,
        user_id: int,
    ) -> FinManualPaymentRead:
        """Create manual payment with mandatory confirmation code '11'."""
        if req.confirmation_code.strip() != "11":
            raise FinValidationError(
                "Confirmation code '11' is required for superuser financial operations"
            )

        corr = req.correlation_id or f"hub_pay_{uuid.uuid4().hex[:16]}"
        pmt = await FinManualPaymentService.create_manual_payment(
            db,
            creator_user_id=user_id,
            tenant_id=req.tenant_id,
            amount_rubles=req.amount_rubles,
            received_on=req.received_on,
            document_number=req.document_number,
            payer=req.payer,
            purpose=req.purpose,
            comment=req.comment,
            evidence_reference=req.evidence_reference,
            actor=actor,
            correlation_id=corr,
        )
        res = FinManualPaymentRead.model_validate(pmt)

        # Audit event with superuser confirmation 11
        audit = L4DeskAuditEvent(
            tenant_id=req.tenant_id,
            actor=actor,
            event_type="hub_manual_payment_created",
            subject_type="manual_payment",
            subject_id=str(res.id),
            outcome="success",
            details={
                "confirmation_code": "11",
                "amount_rubles": req.amount_rubles,
                "document_number": req.document_number,
                "payer": req.payer,
                "ledger_transaction_id": res.ledger_transaction_id,
            },
            correlation_id=corr,
            occurred_at=datetime.now(UTC),
        )
        db.add(audit)
        await db.flush()

        logger.info(
            "Hub manual payment created with confirmation 11: id=%s, tenant=%s, amount=%s",
            res.id,
            req.tenant_id,
            req.amount_rubles,
        )
        return res

    @staticmethod
    async def storno_manual_payment_with_code11(
        db: AsyncSession,
        manual_payment_id: int,
        req: HubManualPaymentStornoRequest,
        actor: str,
    ) -> FinManualPaymentRead:
        """Storno/reverse manual payment with mandatory confirmation code '11'."""
        if req.confirmation_code.strip() != "11":
            raise FinValidationError(
                "Confirmation code '11' is required for superuser financial operations"
            )

        corr = req.correlation_id or f"hub_storno_{uuid.uuid4().hex[:16]}"
        pmt = await FinManualPaymentService.storno_manual_payment(
            db,
            manual_payment_id=manual_payment_id,
            reversal_reason=req.reversal_reason,
            comment=req.comment,
            actor=actor,
            correlation_id=corr,
        )
        res = FinManualPaymentRead.model_validate(pmt)

        audit = L4DeskAuditEvent(
            tenant_id=res.tenant_id,
            actor=actor,
            event_type="hub_manual_payment_storno",
            subject_type="manual_payment",
            subject_id=str(manual_payment_id),
            outcome="success",
            details={
                "confirmation_code": "11",
                "reversal_reason": req.reversal_reason,
                "comment": req.comment,
            },
            correlation_id=corr,
            occurred_at=datetime.now(UTC),
        )
        db.add(audit)
        await db.flush()

        logger.info(
            "Hub manual payment storno executed with confirmation 11: id=%s, tenant=%s",
            manual_payment_id,
            res.tenant_id,
        )
        return res
