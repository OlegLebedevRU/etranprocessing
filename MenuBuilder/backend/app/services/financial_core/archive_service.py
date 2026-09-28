from __future__ import annotations

import contextlib
import logging
from datetime import UTC, date, datetime, timedelta
from pathlib import Path
from typing import Any

from pydantic import ValidationError
from sqlalchemy import func, select, update
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.models_l4desk import (
    FinArchiveBatch,
    FinLedgerTransaction,
    FinTerminalMonthlyCharge,
    FinUsageDaily,
)
from app.services.financial_core.archive_schemas import (
    PROTECTED_FINANCIAL_TABLES,
    SUPPORTED_OWNER_PROJECTS,
    ArchiveManifestV1,
    ArchiveRetentionCheckResponse,
    HubArchiveBatchesResponse,
    HubArchiveBatchItem,
    is_older_than_three_full_months,
)
from app.services.financial_core.exceptions import (
    ArchiveConflictError,
    ArchiveManifestValidationError,
    ArchiveStorageUnavailableError,
    NoFinancialPurgeViolationError,
)

logger = logging.getLogger(__name__)


class ArchiveService:
    @staticmethod
    def mask_location_reference(relative_path: str) -> str:
        """Return safe canonical location URI (e.g. vol://...) hiding local filesystem root."""
        clean_rel = relative_path.lstrip("/\\")
        return f"vol://{clean_rel}"

    @classmethod
    def check_volume_mounted(cls, volume_root: str) -> bool:
        """Check if mounted volume root directory is available and accessible."""
        with contextlib.suppress(OSError, ValueError, TypeError):
            p = Path(volume_root)
            return p.exists() and p.is_dir()
        return False

    @classmethod
    def enforce_no_financial_purge(cls, target_tables: list[str]) -> None:
        """Enforce No-Financial-Purge invariant:
        Financial subledger, payments, cycles, balances, aggregates, and session summaries
        are strictly protected and must never be deleted.
        """
        for tbl in target_tables:
            normalized = tbl.strip().lower()
            if normalized in PROTECTED_FINANCIAL_TABLES:
                raise NoFinancialPurgeViolationError(
                    f"Table '{tbl}' is part of financial subledger/summaries and strictly "
                    "protected from purge (No-Financial-Purge invariant)"
                )

    @classmethod
    async def import_manifest(
        cls,
        db: AsyncSession,
        manifest_data: dict[str, Any],
        actor: str = "archive-coordinator",
        correlation_id: str | None = None,
        check_volume_availability: bool = False,
        reference_date: datetime | None = None,
    ) -> tuple[FinArchiveBatch, dict[str, int], str]:
        """Import archive manifest idempotently, validating schema, hash metadata, status,
        and link with local source events/hashes without FKs.
        """
        # 1. Schema & Contract validation
        try:
            manifest = ArchiveManifestV1.model_validate(manifest_data)
        except ValidationError as err:
            raise ArchiveManifestValidationError(
                f"Archive manifest schema validation failed: {err}"
            ) from err

        batch_id = manifest.archive_batch_id
        owner = manifest.owner_project
        corr_id = correlation_id or f"arch-import-{batch_id}"

        # 2. Gate check: Owner project verification
        if owner not in SUPPORTED_OWNER_PROJECTS:
            raise ArchiveManifestValidationError(
                f"Unsupported owner_project '{owner}'. Must be one of {sorted(SUPPORTED_OWNER_PROJECTS)}"
            )

        # 3. Hot retention check (older than 3 full calendar months)
        if not is_older_than_three_full_months(manifest.source_month, reference_date):
            raise ArchiveManifestValidationError(
                f"Source month '{manifest.source_month}' violates 3 full months hot retention window"
            )

        # 4. Retention duration check (minimum 3 years)
        min_retain_until = manifest.created_at_utc + timedelta(days=3 * 365)
        if manifest.retention.retain_until_utc < min_retain_until:
            raise ArchiveManifestValidationError(
                f"Retention retain_until_utc ({manifest.retention.retain_until_utc.isoformat()}) "
                f"is less than required 3-year minimum ({min_retain_until.isoformat()})"
            )

        # 5. Volume availability check (when requested or volume check enabled)
        if check_volume_availability:
            volume_root = manifest.storage_layout.volume_root
            if not cls.check_volume_mounted(volume_root):
                raise ArchiveStorageUnavailableError(
                    f"Archive storage volume root '{volume_root}' is unavailable; cannot proceed with manifest"
                )

        # Parse dates
        parts = manifest.source_month.split("-")
        archive_month_date = date(int(parts[0]), int(parts[1]), 1)

        primary_checksum = manifest.files[0].sha256
        location_ref = cls.mask_location_reference(
            manifest.storage_layout.relative_path
        )
        canonical_storage_ref = manifest.storage_layout.relative_path

        # Determine DB status
        # Constraints: status IN ('pending', 'verified', 'failed')
        # status != 'verified' OR verified_at IS NOT NULL
        # purged_at IS NULL OR (status = 'verified' AND verified_at IS NOT NULL ...)
        if manifest.state in ("verified", "purged"):
            db_status = "verified"
            verified_at = (
                manifest.verification.verified_at_utc
                if manifest.verification
                else datetime.now(UTC)
            )
            purged_at = (
                manifest.purge.purged_at_utc
                if manifest.state == "purged" and manifest.purge
                else None
            )
        elif manifest.state == "failed":
            db_status = "failed"
            verified_at = None
            purged_at = None
        else:  # prepared
            db_status = "pending"
            verified_at = None
            purged_at = None

        through_cursor = (
            manifest.cursor_bounds.through_cursor if manifest.cursor_bounds else None
        )
        consumers_passed_cursor = (
            manifest.cursor_bounds.consumers_passed_cursor
            if manifest.cursor_bounds
            else None
        )

        # 6. Idempotent check
        stmt = select(FinArchiveBatch).where(
            FinArchiveBatch.id == batch_id,
            FinArchiveBatch.source_project == owner,
        )
        res = await db.execute(stmt)
        existing = res.scalars().first()

        batch: FinArchiveBatch
        if existing:
            # Check immutability of core fields
            if (
                existing.archive_month != archive_month_date
                or existing.row_count != manifest.record_counts["total_records"]
                or existing.checksum_sha256.lower() != primary_checksum.lower()
            ):
                raise ArchiveConflictError(
                    f"Conflicting immutable fields for existing archive batch {batch_id} (owner: {owner})"
                )

            # Advance state idempotently if valid
            existing.manifest = manifest_data
            if db_status == "verified":
                existing.status = "verified"
                if verified_at and not existing.verified_at:
                    existing.verified_at = verified_at
                if purged_at and not existing.purged_at:
                    existing.purged_at = purged_at
            elif db_status == "failed":
                existing.status = "failed"

            batch = existing
        else:
            batch = FinArchiveBatch(
                id=batch_id,
                source_project=owner,
                schema_version=manifest.schema_version,
                archive_month=archive_month_date,
                source_types=manifest.record_types,
                row_count=manifest.record_counts["total_records"],
                min_occurred_at=manifest.time_range.min_occurred_at,
                max_occurred_at=manifest.time_range.max_occurred_at,
                through_cursor=through_cursor,
                consumers_passed_cursor=consumers_passed_cursor,
                storage_reference=canonical_storage_ref,
                checksum_sha256=primary_checksum,
                manifest=manifest_data,
                status=db_status,
                verified_at=verified_at,
                purged_at=purged_at,
                retain_until=manifest.retention.retain_until_utc,
                actor=actor,
                correlation_id=corr_id,
            )
            db.add(batch)

        await db.flush()

        # 7. Link with local source event ids / hashes without FK
        linked_counts = await cls._link_local_records(
            db=db,
            owner_project=owner,
            archive_batch_id=batch_id,
            min_occurred_at=manifest.time_range.min_occurred_at,
            max_occurred_at=manifest.time_range.max_occurred_at,
        )

        await db.commit()
        await db.refresh(batch)

        return batch, linked_counts, location_ref

    @staticmethod
    async def _link_local_records(
        db: AsyncSession,
        owner_project: str,
        archive_batch_id: str,
        min_occurred_at: datetime,
        max_occurred_at: datetime,
    ) -> dict[str, int]:
        """Link matching local records to archive_batch_id without foreign keys."""
        linked: dict[str, int] = {
            "fin_ledger_transactions": 0,
            "fin_usage_daily": 0,
            "fin_terminal_monthly_charges": 0,
        }

        min_d = min_occurred_at.date()
        max_d = max_occurred_at.date()

        # 1. FinLedgerTransaction
        tx_stmt = (
            update(FinLedgerTransaction)
            .where(
                FinLedgerTransaction.source_project == owner_project,
                FinLedgerTransaction.archive_batch_id.is_(None),
                FinLedgerTransaction.posted_at >= min_occurred_at,
                FinLedgerTransaction.posted_at <= max_occurred_at,
            )
            .values(archive_batch_id=archive_batch_id)
        )
        tx_res = await db.execute(tx_stmt)
        linked["fin_ledger_transactions"] = int(getattr(tx_res, "rowcount", 0) or 0)

        # 2. FinUsageDaily
        u_stmt = (
            update(FinUsageDaily)
            .where(
                FinUsageDaily.source_project == owner_project,
                FinUsageDaily.archive_batch_id.is_(None),
                FinUsageDaily.local_date >= min_d,
                FinUsageDaily.local_date <= max_d,
            )
            .values(archive_batch_id=archive_batch_id)
        )
        u_res = await db.execute(u_stmt)
        linked["fin_usage_daily"] = int(getattr(u_res, "rowcount", 0) or 0)

        # 3. FinTerminalMonthlyCharge
        c_stmt = (
            update(FinTerminalMonthlyCharge)
            .where(
                FinTerminalMonthlyCharge.source_project == owner_project,
                FinTerminalMonthlyCharge.archive_batch_id.is_(None),
                FinTerminalMonthlyCharge.created_at >= min_occurred_at,
                FinTerminalMonthlyCharge.created_at <= max_occurred_at,
            )
            .values(archive_batch_id=archive_batch_id)
        )
        c_res = await db.execute(c_stmt)
        linked["fin_terminal_monthly_charges"] = int(getattr(c_res, "rowcount", 0) or 0)

        return linked

    @classmethod
    async def list_archive_batches(
        cls,
        db: AsyncSession,
        owner_project: str | None = None,
        state: str | None = None,
        source_month: str | None = None,
        only_errors: bool = False,
        is_superuser: bool = False,
        page: int = 1,
        page_size: int = 20,
    ) -> HubArchiveBatchesResponse:
        """List archive batches for Hub with RBAC masking of physical paths."""
        stmt = select(FinArchiveBatch)

        if owner_project:
            stmt = stmt.where(FinArchiveBatch.source_project == owner_project)

        if source_month:
            parts = source_month.split("-")
            if len(parts) == 2:
                with contextlib.suppress(ValueError):
                    m_date = date(int(parts[0]), int(parts[1]), 1)
                    stmt = stmt.where(FinArchiveBatch.archive_month == m_date)

        if state:
            if state == "purged":
                stmt = stmt.where(FinArchiveBatch.purged_at.isnot(None))
            elif state == "verified":
                stmt = stmt.where(
                    FinArchiveBatch.status == "verified",
                    FinArchiveBatch.purged_at.is_(None),
                )
            elif state == "failed":
                stmt = stmt.where(FinArchiveBatch.status == "failed")
            elif state == "prepared":
                stmt = stmt.where(FinArchiveBatch.status == "pending")

        # Total count query
        count_stmt = select(func.count()).select_from(stmt.subquery())
        total_res = await db.execute(count_stmt)
        total = total_res.scalar() or 0

        # Order by creation desc
        stmt = (
            stmt.order_by(FinArchiveBatch.created_at.desc())
            .offset((page - 1) * page_size)
            .limit(page_size)
        )
        rows_res = await db.execute(stmt)
        batches = rows_res.scalars().all()

        items: list[HubArchiveBatchItem] = []
        for b in batches:
            manifest_dict = b.manifest or {}
            manifest_state = manifest_dict.get("state")
            if not manifest_state:
                if b.purged_at:
                    manifest_state = "purged"
                elif b.status == "verified":
                    manifest_state = "verified"
                elif b.status == "failed":
                    manifest_state = "failed"
                else:
                    manifest_state = "prepared"

            issues: list[str] = []
            has_checksum_mismatch = False
            has_count_mismatch = False

            verification = manifest_dict.get("verification")
            if verification:
                reread_sha = verification.get("reread_checksum_sha256", "")
                if reread_sha and reread_sha.lower() != b.checksum_sha256.lower():
                    has_checksum_mismatch = True
                    issues.append("CHECKSUM_MISMATCH")

                reread_count = verification.get("reread_records_count")
                if reread_count is not None and reread_count != b.row_count:
                    has_count_mismatch = True
                    issues.append("COUNT_MISMATCH")

            err_obj = manifest_dict.get("error")
            if err_obj and err_obj.get("code"):
                err_code = str(err_obj["code"])
                if err_code not in issues:
                    issues.append(err_code)

            if b.status == "failed" and not issues:
                issues.append("FAILED_STATUS")

            if (
                only_errors
                and not issues
                and not has_checksum_mismatch
                and not has_count_mismatch
            ):
                continue

            # Masked location reference: vol://<relative_path>
            # Raw volume root is NEVER disclosed to normal users
            storage_layout = manifest_dict.get("storage_layout") or {}
            relative_path = storage_layout.get("relative_path") or b.storage_reference
            location_ref = cls.mask_location_reference(relative_path)

            items.append(
                HubArchiveBatchItem(
                    archive_batch_id=b.id,
                    owner_project=b.source_project,
                    schema_version=b.schema_version,
                    source_month=b.archive_month.strftime("%Y-%m"),
                    state=manifest_state,
                    status=b.status,
                    record_types=list(b.source_types or []),
                    row_count=b.row_count,
                    checksum_sha256=b.checksum_sha256,
                    min_occurred_at=b.min_occurred_at,
                    max_occurred_at=b.max_occurred_at,
                    through_cursor=b.through_cursor,
                    consumers_passed_cursor=b.consumers_passed_cursor,
                    location_reference=location_ref,
                    storage_reference=b.storage_reference if is_superuser else None,
                    verified_at=b.verified_at,
                    purged_at=b.purged_at,
                    retain_until=b.retain_until,
                    created_at=b.created_at,
                    actor=b.actor,
                    correlation_id=b.correlation_id,
                    has_checksum_mismatch=has_checksum_mismatch,
                    has_count_mismatch=has_count_mismatch,
                    issues=issues,
                    linked_records_count=0,
                    manifest=manifest_dict if is_superuser else None,
                )
            )

        return HubArchiveBatchesResponse(
            items=items,
            total=total,
            page=page,
            page_size=page_size,
        )

    @classmethod
    async def get_archive_batch(
        cls,
        db: AsyncSession,
        archive_batch_id: str,
        owner_project: str | None = None,
        is_superuser: bool = False,
    ) -> HubArchiveBatchItem | None:
        """Get single archive batch details."""
        stmt = select(FinArchiveBatch).where(FinArchiveBatch.id == archive_batch_id)
        if owner_project:
            stmt = stmt.where(FinArchiveBatch.source_project == owner_project)

        res = await db.execute(stmt)
        b = res.scalars().first()
        if not b:
            return None

        manifest_dict = b.manifest or {}
        manifest_state = manifest_dict.get("state")
        if not manifest_state:
            if b.purged_at:
                manifest_state = "purged"
            elif b.status == "verified":
                manifest_state = "verified"
            elif b.status == "failed":
                manifest_state = "failed"
            else:
                manifest_state = "prepared"

        issues: list[str] = []
        has_checksum_mismatch = False
        has_count_mismatch = False

        verification = manifest_dict.get("verification")
        if verification:
            reread_sha = verification.get("reread_checksum_sha256", "")
            if reread_sha and reread_sha.lower() != b.checksum_sha256.lower():
                has_checksum_mismatch = True
                issues.append("CHECKSUM_MISMATCH")

            reread_count = verification.get("reread_records_count")
            if reread_count is not None and reread_count != b.row_count:
                has_count_mismatch = True
                issues.append("COUNT_MISMATCH")

        err_obj = manifest_dict.get("error")
        if err_obj and err_obj.get("code"):
            err_code = str(err_obj["code"])
            if err_code not in issues:
                issues.append(err_code)

        storage_layout = manifest_dict.get("storage_layout") or {}
        relative_path = storage_layout.get("relative_path") or b.storage_reference
        location_ref = cls.mask_location_reference(relative_path)

        return HubArchiveBatchItem(
            archive_batch_id=b.id,
            owner_project=b.source_project,
            schema_version=b.schema_version,
            source_month=b.archive_month.strftime("%Y-%m"),
            state=manifest_state,
            status=b.status,
            record_types=list(b.source_types or []),
            row_count=b.row_count,
            checksum_sha256=b.checksum_sha256,
            min_occurred_at=b.min_occurred_at,
            max_occurred_at=b.max_occurred_at,
            through_cursor=b.through_cursor,
            consumers_passed_cursor=b.consumers_passed_cursor,
            location_reference=location_ref,
            storage_reference=b.storage_reference if is_superuser else None,
            verified_at=b.verified_at,
            purged_at=b.purged_at,
            retain_until=b.retain_until,
            created_at=b.created_at,
            actor=b.actor,
            correlation_id=b.correlation_id,
            has_checksum_mismatch=has_checksum_mismatch,
            has_count_mismatch=has_count_mismatch,
            issues=issues,
            linked_records_count=0,
            manifest=manifest_dict if is_superuser else None,
        )

    @classmethod
    async def check_retention_and_storage(
        cls,
        db: AsyncSession,
        archive_batch_id: str,
        owner_project: str | None = None,
        volume_root_override: str | None = None,
        reference_date: datetime | None = None,
    ) -> ArchiveRetentionCheckResponse:
        """Validate retention boundaries (hot retention, 3-year minimum) and storage volume availability."""
        batch = await cls.get_archive_batch(
            db, archive_batch_id, owner_project, is_superuser=True
        )
        if not batch or not batch.manifest:
            raise ArchiveManifestValidationError(
                f"Archive batch {archive_batch_id} not found"
            )

        manifest = batch.manifest
        source_month = batch.source_month
        issues: list[str] = []

        # 1. Hot retention check
        is_hot_valid = is_older_than_three_full_months(source_month, reference_date)
        if not is_hot_valid:
            issues.append("HOT_RETENTION_VIOLATION")

        # 2. Minimum 3-year retention check
        retention = manifest.get("retention") or {}
        retain_until_str = retention.get("retain_until_utc")
        created_at_str = manifest.get("created_at_utc")
        is_retention_valid = False
        if retain_until_str and created_at_str:
            with contextlib.suppress(ValueError, TypeError):
                ret_dt = datetime.fromisoformat(retain_until_str)
                cr_dt = datetime.fromisoformat(created_at_str)
                if ret_dt >= cr_dt + timedelta(days=3 * 365):
                    is_retention_valid = True
        if not is_retention_valid:
            issues.append("RETENTION_BOUNDARY_VIOLATION")

        # 3. Backup evidence check
        is_backup_valid = bool(retention.get("backup_required"))
        if not is_backup_valid:
            issues.append("BACKUP_EVIDENCE_MISSING")

        # 4. Volume availability check
        storage_layout = manifest.get("storage_layout") or {}
        volume_root = (
            volume_root_override
            or storage_layout.get("volume_root")
            or getattr(settings, "archive_volume_root", "/mnt/l4desk-archive")
        )
        is_vol_avail = cls.check_volume_mounted(volume_root)
        if not is_vol_avail:
            issues.append("VOLUME_UNAVAILABLE")

        can_purge = (
            is_hot_valid
            and is_retention_valid
            and is_backup_valid
            and is_vol_avail
            and batch.state == "verified"
            and not batch.has_checksum_mismatch
            and not batch.has_count_mismatch
        )

        return ArchiveRetentionCheckResponse(
            archive_batch_id=batch.archive_batch_id,
            owner_project=batch.owner_project,
            source_month=source_month,
            is_hot_retention_valid=is_hot_valid,
            is_retention_period_valid=is_retention_valid,
            is_volume_available=is_vol_avail,
            is_backup_evidence_valid=is_backup_valid,
            can_purge=can_purge,
            issues=issues,
        )
