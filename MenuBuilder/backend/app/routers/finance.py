from __future__ import annotations

import contextlib
import logging
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
    FinConcurrencyError,
    FinDuplicatePostingError,
    FinImbalanceError,
    FinLedgerEntryRead,
    FinLedgerTransactionRead,
    FinPostingRequest,
    FinPostingService,
    FinProjectionService,
    FinReconciliationRequest,
    FinReconciliationRunRead,
    FinReconciliationService,
    FinReversalError,
    FinReversalRequest,
    FinReversalService,
    FinTenantIsolationError,
    FinValidationError,
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
