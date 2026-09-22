from __future__ import annotations

import logging
from typing import Any

from fastapi import APIRouter, Depends, HTTPException, Query, status
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.routers.finance import require_internal_or_superuser
from app.services.financial_core import (
    ArchiveConflictError,
    ArchiveManifestImportRequest,
    ArchiveManifestImportResponse,
    ArchiveManifestValidationError,
    ArchiveRetentionCheckRequest,
    ArchiveRetentionCheckResponse,
    ArchiveService,
    ArchiveStorageUnavailableError,
    HubArchiveBatchesResponse,
    HubArchiveBatchItem,
    NoFinancialPurgeViolationError,
)
from app.services.financial_core.archive_schemas import ArchivePurgeRequest

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/internal/v1/archive", tags=["archive-manifests"])


@router.post(
    "/manifests",
    response_model=ArchiveManifestImportResponse,
    status_code=status.HTTP_201_CREATED,
)
async def import_archive_manifest(
    body: ArchiveManifestImportRequest,
    db: AsyncSession = Depends(get_db),
    auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> ArchiveManifestImportResponse:
    """Import and link external verified archive manifest idempotently."""
    actor = (
        body.actor or auth.get("sub") or auth.get("username") or "archive-coordinator"
    )
    try:
        batch, linked_counts, location_ref = await ArchiveService.import_manifest(
            db=db,
            manifest_data=body.manifest,
            actor=str(actor),
            correlation_id=body.correlation_id,
            check_volume_availability=body.check_volume_availability,
        )
        total_linked = sum(linked_counts.values())
        return ArchiveManifestImportResponse(
            archive_batch_id=batch.id,
            owner_project=batch.source_project,
            status=batch.status,
            state=(batch.manifest or {}).get("state", batch.status),
            linked_records_count=total_linked,
            linked_tables=linked_counts,
            location_reference=location_ref,
            message=f"Archive manifest {batch.id} imported and linked successfully",
        )
    except ArchiveConflictError as err:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail=str(err),
        ) from err
    except ArchiveStorageUnavailableError as err:
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail=str(err),
        ) from err
    except ArchiveManifestValidationError as err:
        raise HTTPException(
            status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
            detail=str(err),
        ) from err


@router.get(
    "/manifests",
    response_model=HubArchiveBatchesResponse,
)
async def list_archive_manifests(
    owner_project: str | None = Query(None),
    state: str | None = Query(None),
    source_month: str | None = Query(None),
    only_errors: bool = Query(False),
    page: int = Query(1, ge=1),
    page_size: int = Query(20, ge=1, le=100),
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> HubArchiveBatchesResponse:
    """List archive manifests for internal consumers with full details."""
    return await ArchiveService.list_archive_batches(
        db=db,
        owner_project=owner_project,
        state=state,
        source_month=source_month,
        only_errors=only_errors,
        is_superuser=True,
        page=page,
        page_size=page_size,
    )


@router.get(
    "/manifests/{archive_batch_id}",
    response_model=HubArchiveBatchItem,
)
async def get_archive_manifest_detail(
    archive_batch_id: str,
    owner_project: str | None = Query(None),
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> HubArchiveBatchItem:
    """Get single archive manifest details."""
    batch = await ArchiveService.get_archive_batch(
        db=db,
        archive_batch_id=archive_batch_id,
        owner_project=owner_project,
        is_superuser=True,
    )
    if not batch:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail=f"Archive manifest '{archive_batch_id}' not found",
        )
    return batch


@router.post(
    "/retention-check",
    response_model=ArchiveRetentionCheckResponse,
)
async def check_retention_and_storage(
    body: ArchiveRetentionCheckRequest,
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> ArchiveRetentionCheckResponse:
    """Check archive retention boundary, volume availability, and backup evidence."""
    try:
        return await ArchiveService.check_retention_and_storage(
            db=db,
            archive_batch_id=body.archive_batch_id,
            owner_project=body.owner_project,
            volume_root_override=body.volume_root,
        )
    except ArchiveManifestValidationError as err:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail=str(err),
        ) from err


@router.post(
    "/purge",
    status_code=status.HTTP_200_OK,
)
async def execute_or_dry_run_purge(
    body: ArchivePurgeRequest,
    db: AsyncSession = Depends(get_db),
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> dict[str, Any]:
    """Execute or dry-run purge operation.
    Guarantees No-Financial-Purge invariant: any financial tables targeted are strictly rejected.
    """
    try:
        ArchiveService.enforce_no_financial_purge(body.target_tables)
    except NoFinancialPurgeViolationError as err:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail=str(err),
        ) from err

    # If archive_batch_id provided, check retention and volume availability
    if body.archive_batch_id:
        ret_check = await ArchiveService.check_retention_and_storage(
            db=db,
            archive_batch_id=body.archive_batch_id,
        )
        if not ret_check.can_purge:
            raise HTTPException(
                status_code=status.HTTP_400_BAD_REQUEST,
                detail=f"Archive batch {body.archive_batch_id} cannot be purged: {', '.join(ret_check.issues)}",
            )

    return {
        "status": "success",
        "dry_run": body.dry_run,
        "target_tables": body.target_tables,
        "purged_records": 0,
        "message": "Purge validation succeeded. Financial invariants respected.",
    }
