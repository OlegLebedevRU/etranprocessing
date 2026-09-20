from __future__ import annotations

import contextlib
import logging
from datetime import date
from typing import Any

from fastapi import APIRouter, Depends, Header, HTTPException, Query, Request, status
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import get_current_user
from app.config import settings
from app.database import get_db
from app.repositories.l4desk_repository import L4DeskRepository
from app.services.financial_core import (
    FinAccountNotFoundError,
    FinBalanceRead,
    FinBillingCycleRead,
    FinBillingCycleService,
    FinBillingProfileRead,
    FinConcurrencyError,
    FinDailyCloseRequest,
    FinDuplicatePostingError,
    FinImbalanceError,
    FinLedgerEntryRead,
    FinLedgerTransactionRead,
    FinManualPaymentCreate,
    FinManualPaymentRead,
    FinManualPaymentService,
    FinManualPaymentStornoRequest,
    FinMeteringService,
    FinPaymentCreateRequest,
    FinPaymentRead,
    FinPaymentService,
    FinPostingRequest,
    FinPostingService,
    FinProcessOnlineEventRequest,
    FinProjectionService,
    FinReconciliationRequest,
    FinReconciliationRunRead,
    FinReconciliationService,
    FinRecordUsageRequest,
    FinReversalError,
    FinReversalRequest,
    FinReversalService,
    FinTariffService,
    FinTariffVersionCreate,
    FinTariffVersionRead,
    FinTenantIsolationError,
    FinTerminalMonthlyChargeRead,
    FinTerminalService,
    FinUsageDailyRead,
    FinValidationError,
    FinYooKassaWebhookPayload,
    YooKassaApiError,
    YooKassaNetworkError,
)

logger = logging.getLogger(__name__)

router = APIRouter(tags=["finance"])


async def require_internal_or_superuser(
    request: Request,
    x_internal_service_key: str | None = Header(None, alias="X-Internal-Service-Key"),
) -> dict[str, Any]:
    """Authorize via X-Internal-Service-Key or superuser session."""
    expected_key = settings.internal_service_key_value
    if expected_key and x_internal_service_key == expected_key:
        return {"source": "internal_service"}

    auth_header = request.headers.get("Authorization")
    token: str | None = None
    if auth_header and auth_header.startswith("Bearer "):
        token = auth_header[7:].strip()
    elif "accessToken" in request.cookies:
        token = request.cookies["accessToken"]

    if token:
        with contextlib.suppress(Exception):
            from app.auth import decode_token

            payload = decode_token(token)
            role = str(payload.get("role", "")).lower()
            if (
                payload.get("is_superuser")
                or role in ("superuser", "admin")
                or payload.get("roleId") == 1
            ):
                return payload

    raise HTTPException(
        status_code=status.HTTP_403_FORBIDDEN,
        detail="Superuser privilege or valid X-Internal-Service-Key required",
    )


# =============================================================================
# Tenant-Facing Financial Endpoints (/api/v1/finance)
# =============================================================================


@router.get("/api/v1/finance/balance", response_model=FinBalanceRead)
async def get_tenant_balance(
    db: AsyncSession = Depends(get_db),
    user: dict[str, Any] = Depends(get_current_user),
) -> FinBalanceRead:
    """Get the current tenant's balance projection."""
    org_id = user.get("org_id")
    if org_id is None or int(org_id) <= 0:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Active organization context required",
        )
    tenant_id = int(org_id)
    proj = await FinProjectionService.get_projection(db, tenant_id)
    return FinBalanceRead(
        tenant_id=proj.tenant_id,
        account_id=proj.account_id,
        balance_kopecks=proj.balance_kopecks,
        balance_rubles=proj.balance_kopecks / 100.0,
        version=proj.version,
        last_transaction_id=proj.last_transaction_id,
        updated_at=proj.updated_at,
    )


@router.get(
    "/api/v1/finance/transactions", response_model=list[FinLedgerTransactionRead]
)
async def list_tenant_transactions(
    limit: int = Query(50, ge=1, le=100),
    offset: int = Query(0, ge=0),
    db: AsyncSession = Depends(get_db),
    user: dict[str, Any] = Depends(get_current_user),
) -> list[FinLedgerTransactionRead]:
    """List financial transactions for the active tenant."""
    org_id = user.get("org_id")
    if org_id is None or int(org_id) <= 0:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Active organization context required",
        )
    tenant_id = int(org_id)
    repo = L4DeskRepository(db)
    tx_list = await repo.list_ledger_transactions(tenant_id, limit=limit, offset=offset)

    result: list[FinLedgerTransactionRead] = []
    for tx in tx_list:
        entries = await repo.list_ledger_entries(tx.id)
        result.append(
            FinLedgerTransactionRead(
                id=tx.id,
                tenant_id=tx.tenant_id,
                operation_id=tx.operation_id,
                kind=tx.kind,
                status=tx.status,
                corrects_transaction_id=tx.corrects_transaction_id,
                debit_kopecks=tx.debit_kopecks,
                credit_kopecks=tx.credit_kopecks,
                source_project=tx.source_project,
                source_type=tx.source_type,
                source_id=tx.source_id,
                source_event_id=tx.source_event_id,
                source_events_hash=tx.source_events_hash,
                archive_batch_id=tx.archive_batch_id,
                calculation_snapshot=tx.calculation_snapshot,
                actor=tx.actor,
                correlation_id=tx.correlation_id,
                created_at=tx.created_at,
                posted_at=tx.posted_at,
                entries=[
                    FinLedgerEntryRead(
                        id=e.id,
                        transaction_id=e.transaction_id,
                        line_number=e.line_number,
                        account_id=e.account_id,
                        debit_kopecks=e.debit_kopecks,
                        credit_kopecks=e.credit_kopecks,
                    )
                    for e in entries
                ],
            )
        )
    return result


@router.get("/api/v1/finance/profile", response_model=FinBillingProfileRead)
async def get_tenant_billing_profile(
    db: AsyncSession = Depends(get_db),
    user: dict[str, Any] = Depends(get_current_user),
) -> FinBillingProfileRead:
    """Get the current tenant's billing profile (anchor, cycle schedule, entitlement)."""
    org_id = user.get("org_id")
    if org_id is None or int(org_id) <= 0:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Active organization context required",
        )
    tenant_id = int(org_id)
    profile = await FinBillingCycleService.ensure_billing_profile(db, tenant_id)
    return FinBillingProfileRead.model_validate(profile)


@router.get("/api/v1/finance/cycles", response_model=list[FinBillingCycleRead])
async def list_tenant_cycles(
    limit: int = Query(50, ge=1, le=100),
    db: AsyncSession = Depends(get_db),
    user: dict[str, Any] = Depends(get_current_user),
) -> list[FinBillingCycleRead]:
    """List billing cycles for the current tenant."""
    org_id = user.get("org_id")
    if org_id is None or int(org_id) <= 0:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Active organization context required",
        )
    tenant_id = int(org_id)
    repo = L4DeskRepository(db)
    cycles = await repo.list_billing_cycles(tenant_id, limit=limit)
    return [FinBillingCycleRead.model_validate(c) for c in cycles]


@router.get("/api/v1/finance/tariffs/current", response_model=FinTariffVersionRead)
async def get_current_tariff(
    db: AsyncSession = Depends(get_db),
    _user: dict[str, Any] = Depends(get_current_user),
) -> FinTariffVersionRead:
    """Get the currently effective tariff version."""
    tariff = await FinTariffService.get_effective_tariff(db)
    return FinTariffVersionRead.model_validate(tariff)


@router.get("/api/v1/finance/usage", response_model=list[FinUsageDailyRead])
async def list_tenant_daily_usage(
    terminal_id: int | None = Query(None, gt=0),
    start_date: date | None = Query(None),
    end_date: date | None = Query(None),
    limit: int = Query(50, ge=1, le=100),
    db: AsyncSession = Depends(get_db),
    user: dict[str, Any] = Depends(get_current_user),
) -> list[FinUsageDailyRead]:
    """List daily usage entries for the current tenant."""
    org_id = user.get("org_id")
    if org_id is None or int(org_id) <= 0:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Active organization context required",
        )
    tenant_id = int(org_id)
    repo = L4DeskRepository(db)
    items = await repo.list_usage_daily(
        tenant_id,
        terminal_id=terminal_id,
        start_date=start_date,
        end_date=end_date,
        limit=limit,
    )
    return [FinUsageDailyRead.model_validate(i) for i in items]


@router.get(
    "/api/v1/finance/monthly-charges",
    response_model=list[FinTerminalMonthlyChargeRead],
)
async def list_tenant_monthly_charges(
    billing_cycle_id: int | None = Query(None, gt=0),
    limit: int = Query(50, ge=1, le=100),
    db: AsyncSession = Depends(get_db),
    user: dict[str, Any] = Depends(get_current_user),
) -> list[FinTerminalMonthlyChargeRead]:
    """List monthly charges for the current tenant."""
    org_id = user.get("org_id")
    if org_id is None or int(org_id) <= 0:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Active organization context required",
        )
    tenant_id = int(org_id)
    repo = L4DeskRepository(db)
    charges = await repo.list_monthly_charges(
        tenant_id, billing_cycle_id=billing_cycle_id, limit=limit
    )
    return [FinTerminalMonthlyChargeRead.model_validate(c) for c in charges]


# =============================================================================
# Internal / Service Financial Subledger Endpoints (/api/internal/v1/finance)
# =============================================================================


@router.post(
    "/api/internal/v1/finance/post",
    response_model=FinLedgerTransactionRead,
    status_code=status.HTTP_201_CREATED,
)
async def post_ledger_transaction(
    body: FinPostingRequest,
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> FinLedgerTransactionRead:
    """Post an immutable transaction with double-entry entries into the ledger."""
    try:
        tx = await FinPostingService.post_transaction(db, body)
        repo = L4DeskRepository(db)
        entries = await repo.list_ledger_entries(tx.id)
        return FinLedgerTransactionRead(
            id=tx.id,
            tenant_id=tx.tenant_id,
            operation_id=tx.operation_id,
            kind=tx.kind,
            status=tx.status,
            corrects_transaction_id=tx.corrects_transaction_id,
            debit_kopecks=tx.debit_kopecks,
            credit_kopecks=tx.credit_kopecks,
            source_project=tx.source_project,
            source_type=tx.source_type,
            source_id=tx.source_id,
            source_event_id=tx.source_event_id,
            source_events_hash=tx.source_events_hash,
            archive_batch_id=tx.archive_batch_id,
            calculation_snapshot=tx.calculation_snapshot,
            actor=tx.actor,
            correlation_id=tx.correlation_id,
            created_at=tx.created_at,
            posted_at=tx.posted_at,
            entries=[
                FinLedgerEntryRead(
                    id=e.id,
                    transaction_id=e.transaction_id,
                    line_number=e.line_number,
                    account_id=e.account_id,
                    debit_kopecks=e.debit_kopecks,
                    credit_kopecks=e.credit_kopecks,
                )
                for e in entries
            ],
        )
    except (FinValidationError, FinImbalanceError) as err:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST, detail=str(err)
        ) from err
    except (FinDuplicatePostingError, FinConcurrencyError) as err:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT, detail=str(err)
        ) from err
    except FinAccountNotFoundError as err:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail=str(err)
        ) from err
    except FinTenantIsolationError as err:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN, detail=str(err)
        ) from err


@router.post(
    "/api/internal/v1/finance/reversal",
    response_model=FinLedgerTransactionRead,
    status_code=status.HTTP_201_CREATED,
)
async def reverse_ledger_transaction(
    body: FinReversalRequest,
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> FinLedgerTransactionRead:
    """Reverse an existing transaction with inverted double-entry entries."""
    try:
        tx = await FinReversalService.reverse_transaction(db, body)
        repo = L4DeskRepository(db)
        entries = await repo.list_ledger_entries(tx.id)
        return FinLedgerTransactionRead(
            id=tx.id,
            tenant_id=tx.tenant_id,
            operation_id=tx.operation_id,
            kind=tx.kind,
            status=tx.status,
            corrects_transaction_id=tx.corrects_transaction_id,
            debit_kopecks=tx.debit_kopecks,
            credit_kopecks=tx.credit_kopecks,
            source_project=tx.source_project,
            source_type=tx.source_type,
            source_id=tx.source_id,
            source_event_id=tx.source_event_id,
            source_events_hash=tx.source_events_hash,
            archive_batch_id=tx.archive_batch_id,
            calculation_snapshot=tx.calculation_snapshot,
            actor=tx.actor,
            correlation_id=tx.correlation_id,
            created_at=tx.created_at,
            posted_at=tx.posted_at,
            entries=[
                FinLedgerEntryRead(
                    id=e.id,
                    transaction_id=e.transaction_id,
                    line_number=e.line_number,
                    account_id=e.account_id,
                    debit_kopecks=e.debit_kopecks,
                    credit_kopecks=e.credit_kopecks,
                )
                for e in entries
            ],
        )
    except FinReversalError as err:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST, detail=str(err)
        ) from err
    except (FinDuplicatePostingError, FinConcurrencyError) as err:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT, detail=str(err)
        ) from err


@router.post(
    "/api/internal/v1/finance/rebuild-projection/{tenant_id}",
    response_model=FinBalanceRead,
)
async def rebuild_tenant_projection(
    tenant_id: int,
    db: AsyncSession = Depends(get_db),
    auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> FinBalanceRead:
    """Rebuild a tenant's balance projection strictly from ledger entries."""
    actor = auth.get("sub") or auth.get("username") or "admin"
    proj, _, _ = await FinProjectionService.rebuild_projection(
        db, tenant_id, actor=actor
    )
    return FinBalanceRead(
        tenant_id=proj.tenant_id,
        account_id=proj.account_id,
        balance_kopecks=proj.balance_kopecks,
        balance_rubles=proj.balance_kopecks / 100.0,
        version=proj.version,
        last_transaction_id=proj.last_transaction_id,
        updated_at=proj.updated_at,
    )


@router.post(
    "/api/internal/v1/finance/reconciliation",
    response_model=FinReconciliationRunRead,
)
async def trigger_reconciliation(
    body: FinReconciliationRequest,
    db: AsyncSession = Depends(get_db),
    auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> FinReconciliationRunRead:
    """Execute a reconciliation run across subledger transactions and projections."""
    try:
        run = await FinReconciliationService.run_reconciliation(db, body)
        return FinReconciliationRunRead.model_validate(run)
    except FinValidationError as err:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST, detail=str(err)
        ) from err


@router.get(
    "/api/internal/v1/finance/reconciliation/runs",
    response_model=list[FinReconciliationRunRead],
)
async def list_reconciliation_runs(
    tenant_id: int | None = Query(None, gt=0),
    limit: int = Query(50, ge=1, le=100),
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> list[FinReconciliationRunRead]:
    """List subledger reconciliation runs."""
    repo = L4DeskRepository(db)
    runs = await repo.list_reconciliation_runs(tenant_id=tenant_id, limit=limit)
    return [FinReconciliationRunRead.model_validate(r) for r in runs]


@router.get(
    "/api/internal/v1/finance/tariffs", response_model=list[FinTariffVersionRead]
)
async def internal_list_tariffs(
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> list[FinTariffVersionRead]:
    """List all tariff versions."""
    tariffs = await FinTariffService.list_tariffs(db)
    return [FinTariffVersionRead.model_validate(t) for t in tariffs]


@router.post(
    "/api/internal/v1/finance/tariffs",
    response_model=FinTariffVersionRead,
    status_code=status.HTTP_201_CREATED,
)
async def internal_create_tariff(
    body: FinTariffVersionCreate,
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> FinTariffVersionRead:
    """Create a new immutable tariff version."""
    try:
        tariff = await FinTariffService.create_tariff_version(db, body)
        return FinTariffVersionRead.model_validate(tariff)
    except FinValidationError as err:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST, detail=str(err)
        ) from err


@router.post(
    "/api/internal/v1/finance/metering/online",
    response_model=FinTerminalMonthlyChargeRead | None,
)
async def internal_process_device_online(
    body: FinProcessOnlineEventRequest,
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> FinTerminalMonthlyChargeRead | None:
    """Process device_online event for monthly terminal charge."""
    charge = await FinTerminalService.process_device_online_monthly_charge(
        db,
        tenant_id=body.tenant_id,
        terminal_id=body.terminal_id,
        event_id=body.event_id,
        occurred_at=body.occurred_at,
        actor=body.actor,
        correlation_id=body.correlation_id,
    )
    if charge is None:
        return None
    return FinTerminalMonthlyChargeRead.model_validate(charge)


@router.post(
    "/api/internal/v1/finance/metering/record-usage",
    response_model=list[FinUsageDailyRead],
)
async def internal_record_usage(
    body: FinRecordUsageRequest,
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> list[FinUsageDailyRead]:
    """Record session usage across local calendar days."""
    rows = await FinMeteringService.record_session_usage(
        db,
        tenant_id=body.tenant_id,
        terminal_id=body.terminal_id,
        session_type=body.session_type,
        start_utc=body.start_utc,
        end_utc=body.end_utc,
        event_id=body.event_id,
        actor=body.actor,
        correlation_id=body.correlation_id,
    )
    return [FinUsageDailyRead.model_validate(r) for r in rows]


@router.post(
    "/api/internal/v1/finance/metering/close-day",
    response_model=list[FinUsageDailyRead],
)
async def internal_close_day(
    body: FinDailyCloseRequest,
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> list[FinUsageDailyRead]:
    """Close and post daily usage rows for tenant and date."""
    rows = await FinMeteringService.close_and_post_daily_usage(
        db,
        tenant_id=body.tenant_id,
        local_date=body.local_date,
        actor=body.actor,
    )
    return [FinUsageDailyRead.model_validate(r) for r in rows]


# =============================================================================
# YooKassa Payments Endpoints (L4D-11-MB)
# =============================================================================


@router.post(
    "/api/v1/finance/payments",
    response_model=FinPaymentRead,
    status_code=status.HTTP_201_CREATED,
)
async def create_yookassa_payment(
    body: FinPaymentCreateRequest,
    db: AsyncSession = Depends(get_db),
    user: dict[str, Any] = Depends(get_current_user),
) -> FinPaymentRead:
    """Create a new payment with YooKassa to top-up tenant balance."""
    org_id = user.get("org_id")
    if org_id is None or int(org_id) <= 0:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Active organization context required",
        )
    tenant_id = int(org_id)
    user_id = user.get("sub") or user.get("user_id") or user.get("id")
    user_id_int = int(user_id) if user_id and str(user_id).isdigit() else None
    customer_email = user.get("email")

    try:
        payment = await FinPaymentService.create_payment(
            db,
            tenant_id=tenant_id,
            user_id=user_id_int,
            amount_rubles=body.amount_rubles,
            return_url=body.return_url,
            idempotence_key=body.idempotence_key,
            customer_email=customer_email,
            correlation_id=f"pay-init-{tenant_id}",
        )
        return FinPaymentRead.model_validate(payment)
    except FinValidationError as err:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST, detail=str(err)
        ) from err
    except FinConcurrencyError as err:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT, detail=str(err)
        ) from err
    except (YooKassaNetworkError, YooKassaApiError) as err:
        raise HTTPException(
            status_code=status.HTTP_502_BAD_GATEWAY, detail=str(err)
        ) from err


@router.get("/api/v1/finance/payments/{payment_id}", response_model=FinPaymentRead)
async def get_tenant_payment(
    payment_id: int,
    db: AsyncSession = Depends(get_db),
    user: dict[str, Any] = Depends(get_current_user),
) -> FinPaymentRead:
    """Get payment status and details."""
    org_id = user.get("org_id")
    if org_id is None or int(org_id) <= 0:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Active organization context required",
        )
    tenant_id = int(org_id)
    repo = L4DeskRepository(db)
    payment = await repo.get_payment(payment_id)
    if not payment:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Payment not found"
        )
    if payment.tenant_id != tenant_id and not user.get("is_superuser"):
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN, detail="Access denied"
        )
    return FinPaymentRead.model_validate(payment)


@router.get("/api/v1/finance/payments", response_model=list[FinPaymentRead])
async def list_tenant_payments(
    status_filter: str | None = Query(None, alias="status"),
    limit: int = Query(50, ge=1, le=100),
    offset: int = Query(0, ge=0),
    db: AsyncSession = Depends(get_db),
    user: dict[str, Any] = Depends(get_current_user),
) -> list[FinPaymentRead]:
    """List payments for the current tenant."""
    org_id = user.get("org_id")
    if org_id is None or int(org_id) <= 0:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Active organization context required",
        )
    tenant_id = int(org_id)
    repo = L4DeskRepository(db)
    payments = await repo.list_payments(
        tenant_id=tenant_id, status=status_filter, limit=limit, offset=offset
    )
    return [FinPaymentRead.model_validate(p) for p in payments]


@router.post(
    "/api/v1/finance/payments/{payment_id}/poll", response_model=FinPaymentRead
)
async def poll_payment_status(
    payment_id: int,
    db: AsyncSession = Depends(get_db),
    user: dict[str, Any] = Depends(get_current_user),
) -> FinPaymentRead:
    """Fallback polling to synchronize payment status with YooKassa."""
    org_id = user.get("org_id")
    if org_id is None or int(org_id) <= 0:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Active organization context required",
        )
    tenant_id = int(org_id)
    repo = L4DeskRepository(db)
    payment = await repo.get_payment(payment_id)
    if not payment:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Payment not found"
        )
    if payment.tenant_id != tenant_id and not user.get("is_superuser"):
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN, detail="Access denied"
        )

    try:
        updated = await FinPaymentService.sync_payment_status(
            db,
            payment_id=payment_id,
            trigger_source="polling",
            actor=f"user_{user.get('sub') or 'poll'}",
            correlation_id=f"poll-{payment_id}",
        )
        return FinPaymentRead.model_validate(updated)
    except FinValidationError as err:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST, detail=str(err)
        ) from err
    except FinTenantIsolationError as err:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN, detail=str(err)
        ) from err
    except (YooKassaNetworkError, YooKassaApiError) as err:
        raise HTTPException(
            status_code=status.HTTP_502_BAD_GATEWAY, detail=str(err)
        ) from err


@router.post("/api/v1/finance/yookassa/webhook", status_code=status.HTTP_200_OK)
async def yookassa_webhook(
    request: Request,
    body: FinYooKassaWebhookPayload,
    db: AsyncSession = Depends(get_db),
    x_yookassa_webhook_secret: str | None = Header(
        None, alias="X-YooKassa-Webhook-Secret"
    ),
) -> dict[str, Any]:
    """Public webhook endpoint for incoming YooKassa payment notifications."""
    client_ip = request.headers.get("X-Forwarded-For") or (
        request.client.host if request.client else None
    )
    try:
        payment = await FinPaymentService.process_webhook(
            db,
            body.model_dump(),
            client_ip=client_ip,
            webhook_secret=x_yookassa_webhook_secret,
            correlation_id=request.headers.get("X-Correlation-ID") or "",
        )
        return {
            "status": "ok",
            "payment_id": payment.id,
            "payment_status": payment.status,
        }
    except FinValidationError as err:
        logger.warning("YooKassa webhook validation failed: %s", err)
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST, detail=str(err)
        ) from err
    except FinTenantIsolationError as err:
        logger.error("YooKassa webhook tenant isolation failed: %s", err)
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN, detail=str(err)
        ) from err
    except (YooKassaNetworkError, YooKassaApiError) as err:
        logger.error("YooKassa webhook provider error: %s", err)
        raise HTTPException(
            status_code=status.HTTP_502_BAD_GATEWAY, detail=str(err)
        ) from err


# =============================================================================
# Manual B2B Bank Payments & Storno Endpoints (L4D-11-MB)
# =============================================================================


@router.post(
    "/api/internal/v1/finance/manual-payments",
    response_model=FinManualPaymentRead,
    status_code=status.HTTP_201_CREATED,
)
async def internal_create_manual_payment(
    body: FinManualPaymentCreate,
    db: AsyncSession = Depends(get_db),
    auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> FinManualPaymentRead:
    """Register an immutable B2B bank payment by superuser."""
    actor = auth.get("sub") or auth.get("username") or "superuser"
    user_id = auth.get("sub") or auth.get("user_id") or auth.get("id") or 1
    user_id_int = int(user_id) if str(user_id).isdigit() else 1

    try:
        payment = await FinManualPaymentService.create_manual_payment(
            db,
            creator_user_id=user_id_int,
            tenant_id=body.tenant_id,
            amount_rubles=body.amount_rubles,
            received_on=body.received_on,
            document_number=body.document_number,
            payer=body.payer,
            purpose=body.purpose,
            comment=body.comment,
            evidence_reference=body.evidence_reference,
            operation_id=body.operation_id,
            actor=actor,
            correlation_id=f"manual-pay-{body.tenant_id}",
        )
        return FinManualPaymentRead.model_validate(payment)
    except FinValidationError as err:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST, detail=str(err)
        ) from err
    except (FinDuplicatePostingError, FinConcurrencyError) as err:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT, detail=str(err)
        ) from err


@router.post(
    "/api/internal/v1/finance/manual-payments/{manual_payment_id}/storno",
    response_model=FinManualPaymentRead,
    status_code=status.HTTP_201_CREATED,
)
async def internal_storno_manual_payment(
    manual_payment_id: int,
    body: FinManualPaymentStornoRequest,
    db: AsyncSession = Depends(get_db),
    auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> FinManualPaymentRead:
    """Storno (reverse) an existing manual payment record."""
    actor = auth.get("sub") or auth.get("username") or "superuser"
    user_id = auth.get("sub") or auth.get("user_id") or auth.get("id") or 1
    user_id_int = int(user_id) if str(user_id).isdigit() else 1
    try:
        storno = await FinManualPaymentService.storno_manual_payment(
            db,
            manual_payment_id=manual_payment_id,
            reversal_reason=body.reversal_reason,
            comment=body.comment,
            actor=actor,
            creator_user_id=user_id_int,
            correlation_id=f"storno-{manual_payment_id}",
        )
        return FinManualPaymentRead.model_validate(storno)
    except (FinValidationError, FinReversalError) as err:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST, detail=str(err)
        ) from err
    except (FinDuplicatePostingError, FinConcurrencyError) as err:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT, detail=str(err)
        ) from err


@router.get(
    "/api/internal/v1/finance/manual-payments",
    response_model=list[FinManualPaymentRead],
)
async def internal_list_manual_payments(
    tenant_id: int | None = Query(None, gt=0),
    limit: int = Query(50, ge=1, le=100),
    offset: int = Query(0, ge=0),
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> list[FinManualPaymentRead]:
    """List manual payments (superuser view)."""
    repo = L4DeskRepository(db)
    payments = await repo.list_manual_payments(
        tenant_id=tenant_id, limit=limit, offset=offset
    )
    return [FinManualPaymentRead.model_validate(p) for p in payments]


@router.get(
    "/api/internal/v1/finance/manual-payments/{manual_payment_id}",
    response_model=FinManualPaymentRead,
)
async def internal_get_manual_payment(
    manual_payment_id: int,
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> FinManualPaymentRead:
    """Get manual payment by ID (superuser view)."""
    repo = L4DeskRepository(db)
    payment = await repo.get_manual_payment(manual_payment_id)
    if not payment:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Manual payment not found"
        )
    return FinManualPaymentRead.model_validate(payment)
