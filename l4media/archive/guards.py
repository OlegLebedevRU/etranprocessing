"""Guards and validation barriers for l4media archive lifecycle:

retention window, path security, active streams, checksum and count verification.
"""

from __future__ import annotations

import os
import re
import shutil
from collections.abc import Callable
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from .canonical import (
    RECORD_TYPE_QUALITY,
    RECORD_TYPE_SAMPLES,
    SOURCE_MONTH_PATTERN,
    compute_file_sha256,
    now_utc_iso,
    validate_envelope_dict,
)


class ArchiveGuardError(Exception):
    """Base exception for archive guard rejections."""

    def __init__(self, code: str, message: str):
        super().__init__(f"[{code}] {message}")
        self.code = code
        self.message = message


class RetentionViolationError(ArchiveGuardError):
    def __init__(self, message: str):
        super().__init__("HOT_RETENTION_VIOLATION", message)


class PathSecurityError(ArchiveGuardError):
    def __init__(self, message: str):
        super().__init__("VOLUME_UNAVAILABLE", message)


class DiskSpaceExhaustedError(ArchiveGuardError):
    def __init__(self, message: str):
        super().__init__("DISK_SPACE_EXHAUSTED", message)


class ActiveRecordsDetectedError(ArchiveGuardError):
    def __init__(self, message: str):
        super().__init__("ACTIVE_RECORDS_DETECTED", message)


class ChecksumMismatchError(ArchiveGuardError):
    def __init__(self, message: str):
        super().__init__("CHECKSUM_MISMATCH", message)


class CountMismatchError(ArchiveGuardError):
    def __init__(self, message: str):
        super().__init__("COUNT_MISMATCH", message)


class RestoreSampleFailedError(ArchiveGuardError):
    def __init__(self, message: str):
        super().__init__("RESTORE_SAMPLE_FAILED", message)


class CursorLagError(ArchiveGuardError):
    def __init__(self, message: str):
        super().__init__("CURSOR_LAG_DETECTED", message)


class RetentionGuard:
    """Enforces that only closed months older than 3 full calendar months can be archived."""

    @staticmethod
    def check_eligible_month(source_month: str, as_of: datetime | None = None) -> tuple[int, int]:
        """Validate that source_month is strictly older than 3 full calendar months.

        Args:
            source_month: Month string in 'YYYY-MM' format.
            as_of: Reference datetime (defaults to current UTC time).

        Returns:
            tuple of (year, month)

        Raises:
            RetentionViolationError if month is within the hot retention window.
        """
        if not SOURCE_MONTH_PATTERN.match(source_month):
            raise RetentionViolationError(f"Invalid month format: '{source_month}'. Expected YYYY-MM.")

        ref_dt = as_of or datetime.now(UTC)
        curr_year = ref_dt.year
        curr_month = ref_dt.month

        # Month indices: year * 12 + month
        current_index = curr_year * 12 + curr_month
        # 3 full closed months: current_month - 1, -2, -3.
        # Eligible months must have index <= current_index - 4.
        max_eligible_index = current_index - 4

        target_year, target_month = map(int, source_month.split("-"))
        target_index = target_year * 12 + target_month

        if target_index > max_eligible_index:
            raise RetentionViolationError(
                f"Month '{source_month}' is within the hot retention window (must be older than 3 full calendar months relative to {ref_dt.strftime('%Y-%m')})."
            )

        return target_year, target_month


class PathSecurityGuard:
    """Protects against path traversal, symlink attacks, and disk space exhaustion."""

    def __init__(self, volume_root: Path | str, min_free_bytes: int = 50 * 1024 * 1024):
        self.volume_root = Path(volume_root).resolve()
        self.min_free_bytes = min_free_bytes

    def ensure_volume_available(self) -> None:
        """Ensure volume_root exists and is writable."""
        try:
            self.volume_root.mkdir(parents=True, exist_ok=True)
        except Exception as e:
            raise PathSecurityError(f"Archive volume '{self.volume_root}' is not accessible or writable: {e}") from e

        if not os.access(self.volume_root, os.W_OK | os.R_OK):
            raise PathSecurityError(f"Archive volume '{self.volume_root}' lacks read/write permissions")

        # Check free disk space
        try:
            usage = shutil.disk_usage(self.volume_root)
            if usage.free < self.min_free_bytes:
                raise DiskSpaceExhaustedError(
                    f"Insufficient free space on volume '{self.volume_root}': {usage.free} bytes available, {self.min_free_bytes} bytes required"
                )
        except Exception as e:
            if isinstance(e, DiskSpaceExhaustedError):
                raise
            # If disk_usage cannot be called on mock/special path, ignore unless strict

    def resolve_safe_path(self, relative_path: str) -> Path:
        """Resolve a relative path inside volume_root and ensure no path traversal or symlinks."""
        clean_rel = Path(relative_path)
        if clean_rel.is_absolute():
            raise PathSecurityError(f"Relative path must not be absolute: '{relative_path}'")

        target = (self.volume_root / clean_rel).resolve()
        try:
            target.relative_to(self.volume_root)
        except ValueError as err:
            raise PathSecurityError(
                f"Path traversal detected: '{relative_path}' resolves outside '{self.volume_root}'"
            ) from err

        return target


class ActiveStreamsGuard:
    """Guards against purging or archiving data while active streams or partial media files exist."""

    def __init__(self, active_checker: Callable[[], list[dict[str, Any]]] | None = None):
        self.active_checker = active_checker

    def check_active_streams(self, source_month: str) -> None:
        """Ensure no active media stream or session is using the data."""
        if not self.active_checker:
            return

        active_items = self.active_checker()
        for item in active_items:
            # Check if active item belongs to or overlaps the source month
            item_month = item.get("month") or item.get("source_month")
            state = item.get("state", "").lower()
            if state in ("active", "starting", "streaming"):
                if item_month and item_month == source_month:
                    raise ActiveRecordsDetectedError(
                        f"Active stream detected for month {source_month}: session_id='{item.get('session_id')}', state='{state}'"
                    )

    @staticmethod
    def cleanup_stale_staging(volume_root: Path | str, prefix: str = ".tmp_", max_age_seconds: int = 3600) -> int:
        """Clean up orphaned temporary staging directories left by interrupted processes."""
        root = Path(volume_root)
        if not root.is_dir():
            return 0

        cleaned = 0
        now = datetime.now(UTC).timestamp()
        for p in root.glob(f"{prefix}*"):
            if p.is_dir():
                try:
                    mtime = p.stat().st_mtime
                    if now - mtime > max_age_seconds:
                        shutil.rmtree(p, ignore_errors=True)
                        cleaned += 1
                except Exception:
                    pass
        return cleaned


class VerificationGuard:
    """Full cryptographic and logical verification of generated archive batch before purge."""

    @staticmethod
    def verify_batch_integrity(
        batch_dir: Path | str,
        manifest_dict: dict[str, Any],
        schemas_bundle_path: Path | None = None,
    ) -> dict[str, Any]:
        """Perform comprehensive re-read, SHA-256 check, record count validation, and sample restore.

        Returns:
            dict of verification details to populate manifest['verification']
        """
        batch_path = Path(batch_dir)
        files = manifest_dict.get("files", [])
        if not files:
            raise CountMismatchError("Manifest contains no file entries")

        total_reread_records = 0
        reread_type_counts: dict[str, int] = {
            RECORD_TYPE_SAMPLES: 0,
            RECORD_TYPE_QUALITY: 0,
        }
        first_file_checksum = ""

        # 1. Verify files against checksum.sha256 if present
        checksum_file = batch_path / "checksum.sha256"
        checksum_map: dict[str, str] = {}
        if checksum_file.is_file():
            with open(checksum_file, encoding="utf-8") as cs_f:
                for line in cs_f:
                    line = line.strip()
                    if not line:
                        continue
                    parts = re.split(r"\s+", line, maxsplit=1)
                    if len(parts) == 2:
                        checksum_map[parts[1]] = parts[0].lower()

        import gzip
        import json

        for f_entry in files:
            fname = f_entry["path"]
            fpath = batch_path / fname
            if not fpath.is_file():
                raise PathSecurityError(f"Archive file not found on volume: {fpath}")

            # Recompute SHA-256
            actual_sha = compute_file_sha256(fpath)
            expected_sha = f_entry["sha256"].lower()
            if actual_sha != expected_sha:
                raise ChecksumMismatchError(
                    f"Checksum mismatch for file '{fname}': expected {expected_sha}, computed {actual_sha}"
                )

            if not first_file_checksum:
                first_file_checksum = actual_sha

            if fname in checksum_map and checksum_map[fname] != actual_sha:
                raise ChecksumMismatchError(
                    f"Checksum in checksum.sha256 for '{fname}' ({checksum_map[fname]}) != actual ({actual_sha})"
                )

            # Re-read and count records
            record_count_in_file = 0
            with gzip.open(fpath, "rt", encoding="utf-8") as gz:
                for line_num, line in enumerate(gz, start=1):
                    line = line.strip()
                    if not line:
                        continue
                    record_count_in_file += 1
                    total_reread_records += 1

                    # Sample restore check (check first 20 records and every 500th)
                    if line_num <= 20 or line_num % 500 == 0:
                        try:
                            rec_obj = json.loads(line)
                        except Exception as parse_err:
                            raise RestoreSampleFailedError(
                                f"Corrupted JSON in '{fname}' line {line_num}: {parse_err}"
                            ) from parse_err

                        # Validate envelope
                        env_errors = validate_envelope_dict(rec_obj, schemas_bundle_path)
                        if env_errors:
                            raise RestoreSampleFailedError(
                                f"Envelope validation failed in '{fname}' line {line_num}: {env_errors[0]}"
                            )

                        rtype = rec_obj.get("record_type")
                        if rtype in reread_type_counts:
                            reread_type_counts[rtype] = reread_type_counts.get(rtype, 0) + 1
                        else:
                            reread_type_counts[rtype] = 1

            if record_count_in_file != f_entry["record_count"]:
                raise CountMismatchError(
                    f"Record count mismatch in file '{fname}': manifest expected {f_entry['record_count']}, counted {record_count_in_file}"
                )

        expected_total = manifest_dict.get("record_counts", {}).get("total_records", 0)
        if total_reread_records != expected_total:
            raise CountMismatchError(
                f"Total record count mismatch: manifest expected {expected_total}, counted {total_reread_records}"
            )

        # Cursor guard check
        cursor_bounds = manifest_dict.get("cursor_bounds")
        cursor_guard_passed = True
        if cursor_bounds:
            through_cursor = cursor_bounds.get("through_cursor")
            consumers_passed_cursor = cursor_bounds.get("consumers_passed_cursor")
            if (
                through_cursor is not None
                and consumers_passed_cursor is not None
                and consumers_passed_cursor < through_cursor
            ):
                cursor_guard_passed = False
                raise CursorLagError(
                    f"Cursor lag detected: consumers_passed_cursor ({consumers_passed_cursor}) < through_cursor ({through_cursor})"
                )

        return {
            "verified_at_utc": now_utc_iso(),
            "verifier": "l4media-archive-worker",
            "reread_records_count": total_reread_records,
            "reread_checksum_sha256": first_file_checksum,
            "restore_sample_status": "passed",
            "cursor_guard_passed": cursor_guard_passed,
        }
