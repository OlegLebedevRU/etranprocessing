"""Deterministic archive lifecycle pipeline for l4media technical samples and quality events.

Lifecycle steps:
1. Validate eligible closed month (> 3 full calendar months).
2. Path security, disk space, and process cleanup.
3. Active streams & partial media files guard.
4. Stream export to deterministic gzip JSONL files (mtime=0.0).
5. Generate SHA-256, checksum.sha256, and initial manifest (state="prepared").
6. Cryptographic re-read, record count verification, sample restore check.
7. Atomic promotion (os.replace) from staging to canonical path (state="verified").
8. Purge verified hot details in bounded chunks while preserving session summaries (state="purged").
"""

from __future__ import annotations

import os
import shutil
from collections.abc import Callable
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from .canonical import (
    MANIFEST_VERSION,
    OWNER_PROJECT,
    RECORD_TYPE_QUALITY,
    RECORD_TYPE_SAMPLES,
    SCHEMA_VERSION,
    ArchiveError,
    ArchiveManifest,
    FileEntry,
    PurgeResult,
    RetentionPolicy,
    StorageLayout,
    VerificationResult,
    canonical_json_dumps,
    generate_batch_id,
    now_utc_iso,
    validate_manifest_schema,
    write_deterministic_jsonl_gz,
)
from .guards import (
    ActiveStreamsGuard,
    ArchiveGuardError,
    PathSecurityGuard,
    RetentionGuard,
    VerificationGuard,
)
from .store import TelemetryStore


def compute_retention_until(created_at_iso: str, years: int = 3) -> str:
    """Compute retention cutoff timestamp strictly >= years after created_at."""
    # Parse ISO timestamp
    clean_ts = created_at_iso.replace("Z", "+00:00")
    dt = datetime.fromisoformat(clean_ts)
    # Add years
    try:
        retain_dt = dt.replace(year=dt.year + years)
    except ValueError:
        # Leap year handling (Feb 29)
        retain_dt = dt.replace(year=dt.year + years, day=28)
    return retain_dt.strftime("%Y-%m-%dT%H:%M:%SZ")


class MediaArchivePipeline:
    """Manages the full deterministic archive lifecycle for l4media."""

    def __init__(
        self,
        store: TelemetryStore,
        volume_root: Path | str = "/mnt/l4desk-archive",
        active_checker: Callable[[], list[dict[str, Any]]] | None = None,
        schema_path: Path | None = None,
        schemas_bundle_path: Path | None = None,
        min_free_bytes: int = 50 * 1024 * 1024,
    ):
        self.store = store
        self.volume_root = Path(volume_root)
        self.path_guard = PathSecurityGuard(self.volume_root, min_free_bytes=min_free_bytes)
        self.active_guard = ActiveStreamsGuard(active_checker=active_checker)
        self.schema_path = schema_path
        self.schemas_bundle_path = schemas_bundle_path

    def run_archive(
        self,
        source_month: str,
        as_of: datetime | None = None,
        dry_run: bool = False,
        purge: bool = True,
        batch_entropy: str | None = None,
    ) -> ArchiveManifest:
        """Execute the complete deterministic archive lifecycle for a given month.

        Args:
            source_month: 'YYYY-MM' (must be older than 3 full calendar months).
            as_of: Reference time for retention check (defaults to current UTC time).
            dry_run: If True, executes through verification and atomic promotion but skips purge.
            purge: If True and dry_run=False, executes purge of verified records.
            batch_entropy: Optional deterministic seed for batch ID generation.

        Returns:
            ArchiveManifest representing the final state of the archive batch.
        """
        # 1. Gate: Retention window check (> 3 full calendar months)
        target_year, target_month = RetentionGuard.check_eligible_month(source_month, as_of=as_of)

        # 2. Gate: Path security & volume availability
        self.path_guard.ensure_volume_available()

        # 3. Clean up stale staging directories from previous interrupted runs
        ActiveStreamsGuard.cleanup_stale_staging(self.volume_root)

        # 4. Gate: Active streams check
        self.active_guard.check_active_streams(source_month)

        # 5. Fetch operational telemetry counts
        counts = self.store.get_counts_for_month(source_month)
        samples_count = counts[RECORD_TYPE_SAMPLES]
        quality_count = counts[RECORD_TYPE_QUALITY]
        total_records = counts["total_records"]

        # 6. Prepare paths
        batch_id = generate_batch_id(source_month, seed=batch_entropy)
        timestamp_slug = int(datetime.now(UTC).timestamp())
        staging_dir = self.volume_root / f".tmp_{batch_id}_{timestamp_slug}"
        staging_dir.mkdir(parents=True, exist_ok=True)

        rel_path = f"{target_year:04d}/{target_month:02d}/l4media/{batch_id}"
        canonical_dest_dir = self.volume_root / f"{target_year:04d}" / f"{target_month:02d}" / "l4media" / batch_id

        created_at_utc = now_utc_iso()
        retain_until_utc = compute_retention_until(created_at_utc, years=3)

        time_range = self.store.get_time_range_for_month(source_month) or {
            "min_occurred_at": f"{source_month}-01T00:00:00Z",
            "max_occurred_at": f"{source_month}-28T23:59:59Z",
        }
        cursor_bounds = self.store.get_cursor_bounds_for_month(source_month)

        try:
            # 7. Write data files (deterministic gzip JSONL with mtime=0.0)
            samples = self.store.fetch_samples_for_month(source_month)
            quality_events = self.store.fetch_quality_events_for_month(source_month)

            samples_file = staging_dir / "samples.jsonl.gz"
            quality_file = staging_dir / "quality.jsonl.gz"

            s_count, s_bytes, s_sha = write_deterministic_jsonl_gz(samples_file, samples)
            q_count, q_bytes, q_sha = write_deterministic_jsonl_gz(quality_file, quality_events)

            file_entries = [
                FileEntry(
                    path="samples.jsonl.gz",
                    size_bytes=s_bytes,
                    sha256=s_sha,
                    record_count=s_count,
                    compression="gzip",
                ),
                FileEntry(
                    path="quality.jsonl.gz",
                    size_bytes=q_bytes,
                    sha256=q_sha,
                    record_count=q_count,
                    compression="gzip",
                ),
            ]

            # 8. Write checksum.sha256 file
            checksum_path = staging_dir / "checksum.sha256"
            with open(checksum_path, "w", encoding="utf-8") as cs_f:
                cs_f.write(f"{s_sha}  samples.jsonl.gz\n")
                cs_f.write(f"{q_sha}  quality.jsonl.gz\n")

            # 9. Create manifest in state="prepared"
            manifest = ArchiveManifest(
                archive_manifest_version=MANIFEST_VERSION,
                archive_batch_id=batch_id,
                owner_project=OWNER_PROJECT,
                schema_version=SCHEMA_VERSION,
                created_at_utc=created_at_utc,
                source_month=source_month,
                time_range=time_range,
                cursor_bounds=cursor_bounds,
                record_types=[RECORD_TYPE_SAMPLES, RECORD_TYPE_QUALITY],
                record_counts={
                    RECORD_TYPE_SAMPLES: s_count,
                    RECORD_TYPE_QUALITY: q_count,
                    "total_records": s_count + q_count,
                },
                files=file_entries,
                compression="gzip",
                state="prepared",
                verification=None,
                purge=None,
                error=None,
                retention=RetentionPolicy(
                    retain_until_utc=retain_until_utc,
                    retention_years=3,
                    backup_required=True,
                ),
                storage_layout=StorageLayout(
                    volume_root=str(self.volume_root),
                    relative_path=rel_path,
                ),
            )

            manifest_path = staging_dir / "manifest.json"
            with open(manifest_path, "w", encoding="utf-8") as mf:
                mf.write(canonical_json_dumps(manifest.to_dict()) + "\n")

            # Validate prepared manifest against schemas
            schema_errs = validate_manifest_schema(manifest.to_dict(), self.schema_path)
            if schema_errs:
                raise ArchiveGuardError("VALIDATION_ERROR", f"Manifest schema validation failed: {schema_errs[0]}")

            # 10. Verification Phase: Full re-read, SHA-256 check, record count validation, sample restore
            v_dict = VerificationGuard.verify_batch_integrity(
                staging_dir,
                manifest.to_dict(),
                schemas_bundle_path=self.schemas_bundle_path,
            )
            verification_obj = VerificationResult(
                verified_at_utc=v_dict["verified_at_utc"],
                verifier=v_dict["verifier"],
                reread_records_count=v_dict["reread_records_count"],
                reread_checksum_sha256=v_dict["reread_checksum_sha256"],
                restore_sample_status=v_dict["restore_sample_status"],
                cursor_guard_passed=v_dict["cursor_guard_passed"],
            )

            # 11. Atomic promotion: staging -> canonical target directory
            canonical_dest_dir.parent.mkdir(parents=True, exist_ok=True)
            if canonical_dest_dir.exists():
                shutil.rmtree(canonical_dest_dir)
            os.replace(staging_dir, canonical_dest_dir)

            # Update manifest to verified state in canonical directory
            manifest.state = "verified"
            manifest.verification = verification_obj
            manifest_final_path = canonical_dest_dir / "manifest.json"
            with open(manifest_final_path, "w", encoding="utf-8") as mf:
                mf.write(canonical_json_dumps(manifest.to_dict()) + "\n")

            # 12. Purge Phase (Strict Invariant: executed only if verified, not dry_run, and purge=True)
            if dry_run or not purge:
                manifest.purge = PurgeResult(
                    purged_at_utc=None,
                    purged_records_count=0,
                    purge_status="pending",
                )
                with open(manifest_final_path, "w", encoding="utf-8") as mf:
                    mf.write(canonical_json_dumps(manifest.to_dict()) + "\n")
                return manifest

            # Re-check active streams immediately prior to purge
            self.active_guard.check_active_streams(source_month)

            # Execute bounded chunked purge from hot store
            purged_records_count = self.store.purge_records_chunked(source_month, chunk_size=1000)

            # Promote manifest to state="purged"
            manifest.state = "purged"
            manifest.purge = PurgeResult(
                purged_at_utc=now_utc_iso(),
                purged_records_count=purged_records_count,
                purge_status="completed",
            )
            with open(manifest_final_path, "w", encoding="utf-8") as mf:
                mf.write(canonical_json_dumps(manifest.to_dict()) + "\n")

            return manifest

        except Exception as e:
            # NO-PURGE ON ANY MISMATCH INVARIANT
            # Clean up staging directory if it still exists
            if staging_dir.exists():
                shutil.rmtree(staging_dir, ignore_errors=True)

            code = getattr(e, "code", "PURGE_OPERATION_FAILED")
            # If batch was promoted before failure, record failed state in manifest
            if canonical_dest_dir.exists():
                failed_manifest = ArchiveManifest(
                    archive_manifest_version=MANIFEST_VERSION,
                    archive_batch_id=batch_id,
                    owner_project=OWNER_PROJECT,
                    schema_version=SCHEMA_VERSION,
                    created_at_utc=created_at_utc,
                    source_month=source_month,
                    time_range=time_range,
                    cursor_bounds=cursor_bounds,
                    record_types=[RECORD_TYPE_SAMPLES, RECORD_TYPE_QUALITY],
                    record_counts={
                        RECORD_TYPE_SAMPLES: samples_count,
                        RECORD_TYPE_QUALITY: quality_count,
                        "total_records": total_records,
                    },
                    files=[],
                    compression="gzip",
                    state="failed",
                    verification=None,
                    purge=PurgeResult(
                        purged_at_utc=None,
                        purged_records_count=0,
                        purge_status="failed",
                    ),
                    error=ArchiveError(
                        code=code,
                        message=str(e),
                        occurred_at_utc=now_utc_iso(),
                    ),
                    retention=RetentionPolicy(
                        retain_until_utc=retain_until_utc,
                        retention_years=3,
                        backup_required=True,
                    ),
                    storage_layout=StorageLayout(
                        volume_root=str(self.volume_root),
                        relative_path=rel_path,
                    ),
                )
                with open(canonical_dest_dir / "manifest.json", "w", encoding="utf-8") as mf:
                    mf.write(canonical_json_dumps(failed_manifest.to_dict()) + "\n")

            raise
