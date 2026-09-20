from __future__ import annotations

import logging
from datetime import date, datetime
from typing import Any

from fastapi import APIRouter, Depends, HTTPException, Query, status
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.routers.finance import require_internal_or_superuser
from app.services.financial_core import (
    FinReconciliationRequest,
    FinReconciliationRunRead,
    FinReconciliationService,
    FinValidationError,
)
from app.services.financial_core.hub_schemas import (
    CorrelationDrilldownResponse,
    HubAuditEventsResponse,
    HubFinanceOverviewResponse,
    HubManualPaymentCreateRequest,
    HubManualPaymentStornoRequest,
    HubNotificationsResponse,
    HubPaymentsResponse,
    HubRegistrationsResponse,
    HubSessionsResponse,
    HubTerminalsResponse,
    HubUsageResponse,
)
from app.services.financial_core.hub_service import HubService
from app.services.financial_core.schemas import FinManualPaymentRead

logger = logging.getLogger(__name__)

router = APIRouter(tags=["admin-hub"])


# =============================================================================
# 1. Registrations Tab
# =============================================================================


@router.get(
    "/api/internal/v1/hub/registrations",
    response_model=HubRegistrationsResponse,
)
@router.get(
    "/api/v1/admin/hub/registrations",
    response_model=HubRegistrationsResponse,
)
async def list_hub_registrations(
    tenant_id: int | None = Query(None, gt=0),
    email: str | None = Query(None),
    status_filter: str | None = Query(None, alias="status"),
    period_start: datetime | None = Query(None),
    period_end: datetime | None = Query(None),
    correlation_id: str | None = Query(None),
    only_errors: bool = Query(False),
    page: int = Query(1, ge=1),
    page_size: int = Query(50, ge=1, le=100),
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> HubRegistrationsResponse:
    """List tenant onboarding registrations with filters."""
    return await HubService.list_registrations(
        db,
        tenant_id=tenant_id,
        email=email,
        status=status_filter,
        period_start=period_start,
        period_end=period_end,
        correlation_id=correlation_id,
        only_errors=only_errors,
        page=page,
        page_size=page_size,
    )


# =============================================================================
# 2. Terminals Tab
# =============================================================================


@router.get(
    "/api/internal/v1/hub/terminals",
    response_model=HubTerminalsResponse,
)
@router.get(
    "/api/v1/admin/hub/terminals",
    response_model=HubTerminalsResponse,
)
async def list_hub_terminals(
    tenant_id: int | None = Query(None, gt=0),
    terminal_id: int | None = Query(None, gt=0),
    sn: str | None = Query(None),
    provisioning_state: str | None = Query(None),
    pin_state: str | None = Query(None),
    is_free: bool | None = Query(None),
    period_start: datetime | None = Query(None),
    period_end: datetime | None = Query(None),
    only_errors: bool = Query(False),
    page: int = Query(1, ge=1),
    page_size: int = Query(50, ge=1, le=100),
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> HubTerminalsResponse:
    """List terminals with certificate & provisioning readiness."""
    return await HubService.list_terminals(
        db,
        tenant_id=tenant_id,
        terminal_id=terminal_id,
        sn=sn,
        provisioning_state=provisioning_state,
        pin_state=pin_state,
        is_free=is_free,
        period_start=period_start,
        period_end=period_end,
        only_errors=only_errors,
        page=page,
        page_size=page_size,
    )


# =============================================================================
# 3. Sessions & Usage Tabs
# =============================================================================


@router.get(
    "/api/internal/v1/hub/sessions",
    response_model=HubSessionsResponse,
)
@router.get(
    "/api/v1/admin/hub/sessions",
    response_model=HubSessionsResponse,
)
async def list_hub_sessions(
    tenant_id: int | None = Query(None, gt=0),
    terminal_id: int | None = Query(None, gt=0),
    session_type: str | None = Query(None),
    state: str | None = Query(None),
    session_id: int | None = Query(None, gt=0),
    correlation_id: str | None = Query(None),
    period_start: datetime | None = Query(None),
    period_end: datetime | None = Query(None),
    only_errors: bool = Query(False),
    page: int = Query(1, ge=1),
    page_size: int = Query(50, ge=1, le=100),
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> HubSessionsResponse:
    """List remote sessions across terminals."""
    return await HubService.list_sessions(
        db,
        tenant_id=tenant_id,
        terminal_id=terminal_id,
        session_type=session_type,
        state=state,
        session_id=session_id,
        correlation_id=correlation_id,
        period_start=period_start,
        period_end=period_end,
        only_errors=only_errors,
        page=page,
        page_size=page_size,
    )


@router.get(
    "/api/internal/v1/hub/usage",
    response_model=HubUsageResponse,
)
@router.get(
    "/api/v1/admin/hub/usage",
    response_model=HubUsageResponse,
)
async def list_hub_usage(
    tenant_id: int | None = Query(None, gt=0),
    terminal_id: int | None = Query(None, gt=0),
    start_date: date | None = Query(None),
    end_date: date | None = Query(None),
    free_paid: str | None = Query(None),
    only_unreconciled: bool = Query(False),
    page: int = Query(1, ge=1),
    page_size: int = Query(50, ge=1, le=100),
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> HubUsageResponse:
    """List daily usage records across terminals."""
    return await HubService.list_usage(
        db,
        tenant_id=tenant_id,
        terminal_id=terminal_id,
        start_date=start_date,
        end_date=end_date,
        free_paid=free_paid,
        only_unreconciled=only_unreconciled,
        page=page,
        page_size=page_size,
    )


# =============================================================================
# 4. Finance & Payments Tabs
# =============================================================================


@router.get(
    "/api/internal/v1/hub/finance/overview",
    response_model=HubFinanceOverviewResponse,
)
@router.get(
    "/api/v1/admin/hub/finance/overview",
    response_model=HubFinanceOverviewResponse,
)
async def get_hub_finance_overview(
    tenant_id: int | None = Query(None, gt=0),
    entitlement: str | None = Query(None, alias="active_grace_blocked"),
    only_errors: bool = Query(False),
    page: int = Query(1, ge=1),
    page_size: int = Query(50, ge=1, le=100),
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> HubFinanceOverviewResponse:
    """Get finance and entitlement overview for all tenants."""
    return await HubService.get_finance_overview(
        db,
        tenant_id=tenant_id,
        active_grace_blocked=entitlement,
        only_errors=only_errors,
        page=page,
        page_size=page_size,
    )


@router.get(
    "/api/internal/v1/hub/finance/payments",
    response_model=HubPaymentsResponse,
)
@router.get(
    "/api/v1/admin/hub/finance/payments",
    response_model=HubPaymentsResponse,
)
async def list_hub_payments(
    tenant_id: int | None = Query(None, gt=0),
    payment_source: str | None = Query(None),
    status_filter: str | None = Query(None, alias="status"),
    provider_payment_id: str | None = Query(None),
    period_start: datetime | None = Query(None),
    period_end: datetime | None = Query(None),
    only_errors: bool = Query(False),
    page: int = Query(1, ge=1),
    page_size: int = Query(50, ge=1, le=100),
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> HubPaymentsResponse:
    """List YooKassa and manual legal entity payments."""
    return await HubService.list_payments(
        db,
        tenant_id=tenant_id,
        payment_source=payment_source,
        status=status_filter,
        provider_payment_id=provider_payment_id,
        period_start=period_start,
        period_end=period_end,
        only_errors=only_errors,
        page=page,
        page_size=page_size,
    )


# =============================================================================
# 5. Notifications & Audit Events Tabs
# =============================================================================


@router.get(
    "/api/internal/v1/hub/notifications",
    response_model=HubNotificationsResponse,
)
@router.get(
    "/api/v1/admin/hub/notifications",
    response_model=HubNotificationsResponse,
)
async def list_hub_notifications(
    tenant_id: int | None = Query(None, gt=0),
    notification_type: str | None = Query(None),
    status_filter: str | None = Query(None, alias="status"),
    only_errors: bool = Query(False),
    page: int = Query(1, ge=1),
    page_size: int = Query(50, ge=1, le=100),
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> HubNotificationsResponse:
    """List notification deliveries across tenants."""
    return await HubService.list_notifications(
        db,
        tenant_id=tenant_id,
        notification_type=notification_type,
        status=status_filter,
        only_errors=only_errors,
        page=page,
        page_size=page_size,
    )


@router.get(
    "/api/internal/v1/hub/audit-events",
    response_model=HubAuditEventsResponse,
)
@router.get(
    "/api/v1/admin/hub/audit-events",
    response_model=HubAuditEventsResponse,
)
async def list_hub_audit_events(
    tenant_id: int | None = Query(None, gt=0),
    event_type: str | None = Query(None),
    subject_type: str | None = Query(None),
    outcome: str | None = Query(None),
    period_start: datetime | None = Query(None),
    period_end: datetime | None = Query(None),
    only_errors: bool = Query(False),
    page: int = Query(1, ge=1),
    page_size: int = Query(50, ge=1, le=100),
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> HubAuditEventsResponse:
    """List system audit log events and errors."""
    return await HubService.list_audit_events(
        db,
        tenant_id=tenant_id,
        event_type=event_type,
        subject_type=subject_type,
        outcome=outcome,
        period_start=period_start,
        period_end=period_end,
        only_errors=only_errors,
        page=page,
        page_size=page_size,
    )


# =============================================================================
# 6. Correlation Drill-Down
# =============================================================================


@router.get(
    "/api/internal/v1/hub/correlation-drilldown",
    response_model=CorrelationDrilldownResponse,
)
@router.get(
    "/api/v1/admin/hub/correlation-drilldown",
    response_model=CorrelationDrilldownResponse,
)
async def get_hub_correlation_drilldown(
    correlation_id: str | None = Query(None),
    tenant_id: int | None = Query(None, gt=0),
    terminal_id: int | None = Query(None, gt=0),
    session_id: int | None = Query(None, gt=0),
    payment_id: int | None = Query(None, gt=0),
    registration_id: int | None = Query(None, gt=0),
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> CorrelationDrilldownResponse:
    """Trace end-to-end fact correlation chain."""
    return await HubService.get_correlation_drilldown(
        db,
        correlation_id=correlation_id,
        tenant_id=tenant_id,
        terminal_id=terminal_id,
        session_id=session_id,
        payment_id=payment_id,
        registration_id=registration_id,
    )


# =============================================================================
# 7. Manual Payments & Storno with Superuser Code 11 Confirmation
# =============================================================================


@router.post(
    "/api/internal/v1/hub/finance/manual-payment",
    response_model=FinManualPaymentRead,
    status_code=status.HTTP_201_CREATED,
)
@router.post(
    "/api/v1/admin/hub/finance/manual-payment",
    response_model=FinManualPaymentRead,
    status_code=status.HTTP_201_CREATED,
)
async def create_hub_manual_payment(
    body: HubManualPaymentCreateRequest,
    db: AsyncSession = Depends(get_db),
    auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> FinManualPaymentRead:
    """Register legal entity manual bank payment (requires confirmation code '11')."""
    actor = auth.get("sub") or auth.get("username") or "superuser"
    user_id = auth.get("userId") or auth.get("id") or 1
    try:
        res = await HubService.create_manual_payment_with_code11(
            db, body, actor=str(actor), user_id=int(user_id)
        )
        return res
    except FinValidationError as err:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST, detail=str(err)
        ) from err


@router.post(
    "/api/internal/v1/hub/finance/manual-payment/{manual_payment_id}/storno",
    response_model=FinManualPaymentRead,
)
@router.post(
    "/api/v1/admin/hub/finance/manual-payment/{manual_payment_id}/storno",
    response_model=FinManualPaymentRead,
)
async def storno_hub_manual_payment(
    manual_payment_id: int,
    body: HubManualPaymentStornoRequest,
    db: AsyncSession = Depends(get_db),
    auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> FinManualPaymentRead:
    """Storno/reverse manual bank payment (requires confirmation code '11')."""
    actor = auth.get("sub") or auth.get("username") or "superuser"
    try:
        res = await HubService.storno_manual_payment_with_code11(
            db, manual_payment_id, body, actor=str(actor)
        )
        return res
    except FinValidationError as err:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST, detail=str(err)
        ) from err


# =============================================================================
# 8. Reconciliation Trigger & Runs
# =============================================================================


@router.post(
    "/api/internal/v1/hub/reconciliation/run",
    response_model=FinReconciliationRunRead,
)
@router.post(
    "/api/v1/admin/hub/reconciliation/run",
    response_model=FinReconciliationRunRead,
)
async def trigger_hub_reconciliation(
    body: FinReconciliationRequest,
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> FinReconciliationRunRead:
    """Execute financial reconciliation run from Hub."""
    try:
        run = await FinReconciliationService.run_reconciliation(db, body)
        return FinReconciliationRunRead.model_validate(run)
    except FinValidationError as err:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST, detail=str(err)
        ) from err
