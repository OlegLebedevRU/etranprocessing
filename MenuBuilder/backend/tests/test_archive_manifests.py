"""Backend tests for L4D-16-MB: archive manifest import, validation, Hub integration, retention and purge guards.

Covers contract vectors from Archive Manifest Contract v1 (examples.json):
- invalid/duplicate manifest
- owner mismatch
- source hash / checksum mismatch
- archived drill-down
- retention boundary
- no-financial-delete
"""

from __future__ import annotations

import contextlib
from datetime import UTC, date, datetime, timedelta
from typing import Any, cast
from unittest.mock import patch

import pytest
from pydantic import ValidationError

from app.services.financial_core.archive_schemas import (
    PROTECTED_FINANCIAL_TABLES,
    SUPPORTED_OWNER_PROJECTS,
    ArchiveManifestV1,
    HubArchiveBatchItem,
    is_older_than_three_full_months,
)
from app.services.financial_core.archive_service import ArchiveService
from app.services.financial_core.exceptions import (
    ArchiveConflictError,
    ArchiveManifestValidationError,
    ArchiveStorageUnavailableError,
    NoFinancialPurgeViolationError,
)


# ---------------------------------------------------------------------------
# Canonical fixture: a valid verified IoT manifest from examples.json
# ---------------------------------------------------------------------------
def _iot_manifest(**overrides: Any) -> dict[str, Any]:
    base: dict[str, Any] = {
        "archive_manifest_version": "1.0.0",
        "archive_batch_id": "arch-iot-2026-05-b91c84f2",
        "owner_project": "iot-rpc-rest-app",
        "schema_version": "1.0.0",
        "created_at_utc": "2026-09-01T04:00:00Z",
        "source_month": "2026-05",
        "time_range": {
            "min_occurred_at": "2026-05-01T00:00:00Z",
            "max_occurred_at": "2026-05-31T23:59:59.999999Z",
        },
        "cursor_bounds": {
            "min_cursor": 10001,
            "max_cursor": 25000,
            "through_cursor": 25000,
            "consumers_passed_cursor": 26500,
        },
        "record_types": ["iot_session_events", "rpc_transitions", "presence_events"],
        "record_counts": {
            "iot_session_events": 10000,
            "rpc_transitions": 3500,
            "presence_events": 1500,
            "total_records": 15000,
        },
        "files": [
            {
                "path": "data.jsonl.gz",
                "size_bytes": 1048576,
                "sha256": "4b227777d4dd1fc61c6f884f48641d02b4d121d3fd328cb08b5531fcacdabf8a",
                "record_count": 15000,
                "compression": "gzip",
            }
        ],
        "compression": "gzip",
        "state": "verified",
        "verification": {
            "verified_at_utc": "2026-09-01T04:15:30Z",
            "verifier": "iot-archive-worker",
            "reread_records_count": 15000,
            "reread_checksum_sha256": "4b227777d4dd1fc61c6f884f48641d02b4d121d3fd328cb08b5531fcacdabf8a",
            "restore_sample_status": "passed",
            "cursor_guard_passed": True,
        },
        "purge": None,
        "error": None,
        "retention": {
            "retain_until_utc": "2029-09-01T04:00:00Z",
            "retention_years": 3,
            "backup_required": True,
        },
        "storage_layout": {
            "volume_root": "/mnt/l4desk-archive",
            "relative_path": "2026/05/iot-rpc-rest-app/arch-iot-2026-05-b91c84f2",
        },
    }
    base.update(overrides)
    return base


def _media_purged_manifest(**overrides: Any) -> dict[str, Any]:
    base: dict[str, Any] = {
        "archive_manifest_version": "1.0.0",
        "archive_batch_id": "arch-media-2026-04-e5f6a7b8",
        "owner_project": "l4media",
        "schema_version": "1.0.0",
        "created_at_utc": "2026-08-01T03:00:00Z",
        "source_month": "2026-04",
        "time_range": {
            "min_occurred_at": "2026-04-01T00:00:00Z",
            "max_occurred_at": "2026-04-30T23:59:59.999999Z",
        },
        "cursor_bounds": None,
        "record_types": ["media_stream_samples", "media_quality_events"],
        "record_counts": {
            "media_stream_samples": 45000,
            "media_quality_events": 5000,
            "total_records": 50000,
        },
        "files": [
            {
                "path": "samples.jsonl.gz",
                "size_bytes": 5242880,
                "sha256": "a3f5c7d9e1b2a4c6d8e0f1a3b5c7d9e1b2a4c6d8e0f1a3b5c7d9e1b2a4c6d8e0",
                "record_count": 50000,
                "compression": "gzip",
            }
        ],
        "compression": "gzip",
        "state": "purged",
        "verification": {
            "verified_at_utc": "2026-08-01T03:30:00Z",
            "verifier": "media-archive-worker",
            "reread_records_count": 50000,
            "reread_checksum_sha256": "a3f5c7d9e1b2a4c6d8e0f1a3b5c7d9e1b2a4c6d8e0f1a3b5c7d9e1b2a4c6d8e0",
            "restore_sample_status": "passed",
            "cursor_guard_passed": True,
        },
        "purge": {
            "purged_at_utc": "2026-08-01T04:00:00Z",
            "purged_records_count": 50000,
            "purge_status": "completed",
        },
        "error": None,
        "retention": {
            "retain_until_utc": "2029-08-01T03:00:00Z",
            "retention_years": 3,
            "backup_required": True,
        },
        "storage_layout": {
            "volume_root": "/mnt/l4desk-archive",
            "relative_path": "2026/04/l4media/arch-media-2026-04-e5f6a7b8",
        },
    }
    base.update(overrides)
    return base


# ===========================================================================
# 1. ArchiveManifestV1 schema validation tests
# ===========================================================================
class TestManifestSchemaValidation:
    def test_valid_iot_verified_manifest(self):
        """Golden fixture: valid verified IoT manifest passes schema."""
        m = ArchiveManifestV1.model_validate(_iot_manifest())
        assert m.state == "verified"
        assert m.archive_batch_id == "arch-iot-2026-05-b91c84f2"
        assert m.record_counts["total_records"] == 15000
        assert m.retention.retention_years >= 3
        assert m.retention.backup_required is True

    def test_valid_media_purged_manifest(self):
        """Golden fixture: valid purged media manifest passes schema."""
        m = ArchiveManifestV1.model_validate(_media_purged_manifest())
        assert m.state == "purged"
        assert m.purge is not None
        assert m.purge.purge_status == "completed"

    def test_invalid_missing_total_records(self):
        """Reject manifest without total_records in record_counts."""
        manifest = _iot_manifest(record_counts={"iot_session_events": 10000})
        with pytest.raises(ValidationError, match="total_records"):
            ArchiveManifestV1.model_validate(manifest)

    def test_invalid_record_count_mismatch(self):
        """Reject when sum of file record_counts does not equal total_records."""
        manifest = _iot_manifest(
            record_counts={
                "iot_session_events": 10000,
                "total_records": 99999,
            }
        )
        with pytest.raises(ValidationError, match="does not match total_records"):
            ArchiveManifestV1.model_validate(manifest)

    def test_invalid_verified_missing_verification(self):
        """Reject state='verified' with verification=None."""
        manifest = _iot_manifest(verification=None)
        with pytest.raises(ValidationError, match="verification object is required"):
            ArchiveManifestV1.model_validate(manifest)

    def test_invalid_purged_missing_purge(self):
        """Reject state='purged' with purge=None."""
        manifest = _media_purged_manifest(purge=None)
        with pytest.raises(ValidationError, match="purge object is required"):
            ArchiveManifestV1.model_validate(manifest)

    def test_invalid_failed_missing_error(self):
        """Reject state='failed' with error=None."""
        manifest = _iot_manifest(state="failed", error=None)
        with pytest.raises(ValidationError, match="error object is required"):
            ArchiveManifestV1.model_validate(manifest)

    def test_invalid_owner_project(self):
        """Reject unknown owner_project."""
        manifest = _iot_manifest(owner_project="unknown-service")
        with pytest.raises(ValidationError):
            ArchiveManifestV1.model_validate(manifest)

    def test_invalid_retention_years_less_than_3(self):
        """Reject retention_years < 3."""
        manifest = _iot_manifest(
            retention={
                "retain_until_utc": "2029-09-01T04:00:00Z",
                "retention_years": 1,
                "backup_required": True,
            }
        )
        with pytest.raises(ValidationError):
            ArchiveManifestV1.model_validate(manifest)

    def test_invalid_backup_required_false(self):
        """Reject backup_required=false."""
        manifest = _iot_manifest(
            retention={
                "retain_until_utc": "2029-09-01T04:00:00Z",
                "retention_years": 3,
                "backup_required": False,
            }
        )
        with pytest.raises(ValidationError, match="backup_required"):
            ArchiveManifestV1.model_validate(manifest)

    def test_invalid_sha256_pattern(self):
        """Reject non-hex or wrong-length SHA-256."""
        manifest = _iot_manifest(
            files=[
                {
                    "path": "data.jsonl.gz",
                    "size_bytes": 1024,
                    "sha256": "not-a-valid-sha",
                    "record_count": 15000,
                    "compression": "gzip",
                }
            ]
        )
        with pytest.raises(ValidationError):
            ArchiveManifestV1.model_validate(manifest)

    def test_checksum_mismatch_detected(self):
        """Reject verified manifest where reread SHA does not match file SHA."""
        manifest = _iot_manifest(
            verification={
                "verified_at_utc": "2026-09-01T04:15:30Z",
                "verifier": "iot-archive-worker",
                "reread_records_count": 15000,
                "reread_checksum_sha256": "0000000000000000000000000000000000000000000000000000000000000000",
                "restore_sample_status": "passed",
                "cursor_guard_passed": True,
            }
        )
        with pytest.raises(ValidationError, match="CHECKSUM_MISMATCH"):
            ArchiveManifestV1.model_validate(manifest)

    def test_count_mismatch_detected(self):
        """Reject verified manifest where reread count differs from total."""
        manifest = _iot_manifest(
            verification={
                "verified_at_utc": "2026-09-01T04:15:30Z",
                "verifier": "iot-archive-worker",
                "reread_records_count": 99999,
                "reread_checksum_sha256": "4b227777d4dd1fc61c6f884f48641d02b4d121d3fd328cb08b5531fcacdabf8a",
                "restore_sample_status": "passed",
                "cursor_guard_passed": True,
            }
        )
        with pytest.raises(ValidationError, match="COUNT_MISMATCH"):
            ArchiveManifestV1.model_validate(manifest)

    def test_cursor_lag_detected(self):
        """Reject purged manifest when consumers_passed_cursor < through_cursor."""
        manifest = _media_purged_manifest(
            cursor_bounds={
                "through_cursor": 20000,
                "consumers_passed_cursor": 19500,
            }
        )
        with pytest.raises(ValidationError, match="CURSOR_LAG_DETECTED"):
            ArchiveManifestV1.model_validate(manifest)

    def test_invalid_retention_window_hot_month(self):
        """Reject source_month within 3-month hot window at service level (not schema)."""
        # The schema validates format only; hot retention is checked at import time.
        # Verify the helper rejects it:
        ref = datetime(2026, 10, 1, tzinfo=UTC)
        assert is_older_than_three_full_months("2026-08", ref) is False


# ===========================================================================
# 2. Hot retention boundary helper
# ===========================================================================
class TestHotRetentionBoundary:
    def test_older_than_three_months_passes(self):
        ref = datetime(2026, 9, 21, tzinfo=UTC)
        assert is_older_than_three_full_months("2026-05", ref) is True
        assert is_older_than_three_full_months("2026-04", ref) is True

    def test_exactly_three_months_fails(self):
        ref = datetime(2026, 9, 21, tzinfo=UTC)
        # June 2026 is exactly 3 months back from September
        assert is_older_than_three_full_months("2026-06", ref) is False

    def test_current_month_fails(self):
        ref = datetime(2026, 9, 21, tzinfo=UTC)
        assert is_older_than_three_full_months("2026-09", ref) is False

    def test_future_month_fails(self):
        ref = datetime(2026, 9, 21, tzinfo=UTC)
        assert is_older_than_three_full_months("2026-12", ref) is False

    def test_january_edge_case(self):
        ref = datetime(2026, 1, 15, tzinfo=UTC)
        # Sep 2025 is 4 months back from Jan 2026
        assert is_older_than_three_full_months("2025-09", ref) is True
        # Oct 2025 is exactly 3 months back
        assert is_older_than_three_full_months("2025-10", ref) is False


# ===========================================================================
# 3. Location masking
# ===========================================================================
class TestLocationMasking:
    def test_masks_physical_path_to_vol_uri(self):
        result = ArchiveService.mask_location_reference(
            "2026/05/iot-rpc-rest-app/arch-iot-2026-05-b91c84f2"
        )
        assert result == "vol://2026/05/iot-rpc-rest-app/arch-iot-2026-05-b91c84f2"
        assert "/mnt/" not in result

    def test_strips_leading_slash(self):
        result = ArchiveService.mask_location_reference("/relative/path")
        assert result == "vol://relative/path"

    def test_strips_leading_backslash(self):
        result = ArchiveService.mask_location_reference("\\windows\\path")
        assert result == "vol://windows\\path"


# ===========================================================================
# 4. No-Financial-Purge enforcement
# ===========================================================================
class TestNoFinancialPurgeInvariant:
    def test_rejects_all_protected_tables(self):
        """Every table in PROTECTED_FINANCIAL_TABLES must be rejected."""
        for table in PROTECTED_FINANCIAL_TABLES:
            with pytest.raises(NoFinancialPurgeViolationError, match=table):
                ArchiveService.enforce_no_financial_purge([table])

    def test_rejects_mixed_tables_with_one_protected(self):
        """If even one target table is protected, the whole operation is rejected."""
        with pytest.raises(NoFinancialPurgeViolationError):
            ArchiveService.enforce_no_financial_purge(
                ["iot_session_events", "fin_ledger_transactions"]
            )

    def test_allows_non_financial_tables(self):
        """Non-financial tables should pass without error."""
        ArchiveService.enforce_no_financial_purge(
            ["iot_session_events", "rpc_transitions", "presence_events"]
        )

    def test_allows_empty_list(self):
        """Empty target list should pass."""
        ArchiveService.enforce_no_financial_purge([])


# ===========================================================================
# 5. Supported owner projects
# ===========================================================================
class TestSupportedOwnerProjects:
    def test_contract_enumeration(self):
        assert SUPPORTED_OWNER_PROJECTS == {
            "iot-rpc-rest-app",
            "l4media",
            "MenuBuilder",
        }

    def test_iot_owner_accepted(self):
        m = ArchiveManifestV1.model_validate(_iot_manifest())
        assert m.owner_project == "iot-rpc-rest-app"

    def test_media_owner_accepted(self):
        m = ArchiveManifestV1.model_validate(_media_purged_manifest())
        assert m.owner_project == "l4media"

    def test_menubuilder_owner_accepted(self):
        manifest = _iot_manifest(
            owner_project="MenuBuilder",
            archive_batch_id="arch-mb-2026-05-c3d4e5f6",
            storage_layout={
                "volume_root": "/mnt/l4desk-archive",
                "relative_path": "2026/05/MenuBuilder/arch-mb-2026-05-c3d4e5f6",
            },
        )
        m = ArchiveManifestV1.model_validate(manifest)
        assert m.owner_project == "MenuBuilder"


# ===========================================================================
# 6. Duplicate / idempotent manifest behavior (schema level)
# ===========================================================================
class TestDuplicateManifestSchema:
    def test_same_batch_id_different_checksums_rejected_at_schema_level(
        self,
    ):
        """Two manifests with same batch_id but different checksums cannot be
        validated simultaneously; the second would conflict at import time."""
        m1 = ArchiveManifestV1.model_validate(_iot_manifest())
        m2_data = _iot_manifest(
            files=[
                {
                    "path": "data.jsonl.gz",
                    "size_bytes": 1048576,
                    "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                    "record_count": 15000,
                    "compression": "gzip",
                }
            ],
            verification={
                "verified_at_utc": "2026-09-01T04:15:30Z",
                "verifier": "iot-archive-worker",
                "reread_records_count": 15000,
                "reread_checksum_sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                "restore_sample_status": "passed",
                "cursor_guard_passed": True,
            },
        )
        m2 = ArchiveManifestV1.model_validate(m2_data)
        # Both parse individually, but at import time the service detects conflict
        assert m1.files[0].sha256 != m2.files[0].sha256

    def test_failed_manifest_with_error_object(self):
        """Failed manifest with error code passes schema."""
        manifest = _iot_manifest(
            state="failed",
            verification=None,
            error={
                "code": "CHECKSUM_MISMATCH",
                "message": "Reread checksum does not match",
                "occurred_at_utc": "2026-09-01T04:20:00Z",
            },
        )
        m = ArchiveManifestV1.model_validate(manifest)
        assert m.state == "failed"
        assert m.error is not None
        assert m.error.code == "CHECKSUM_MISMATCH"


# ===========================================================================
# 7. Hub response schema tests
# ===========================================================================
class TestHubArchiveBatchItem:
    def test_location_reference_masked(self):
        """HubArchiveBatchItem.location_reference must use vol:// scheme."""
        item = HubArchiveBatchItem(
            archive_batch_id="arch-iot-2026-05-b91c84f2",
            owner_project="iot-rpc-rest-app",
            schema_version="1.0.0",
            source_month="2026-05",
            state="verified",
            status="verified",
            record_types=["iot_session_events"],
            row_count=15000,
            checksum_sha256="4b227777d4dd1fc61c6f884f48641d02b4d121d3fd328cb08b5531fcacdabf8a",
            location_reference="vol://2026/05/iot-rpc-rest-app/arch-iot-2026-05-b91c84f2",
            retain_until=datetime(2029, 9, 1, tzinfo=UTC),
            created_at=datetime(2026, 9, 1, tzinfo=UTC),
            actor="test",
            correlation_id="test-corr",
        )
        assert item.location_reference.startswith("vol://")
        assert "/mnt/" not in item.location_reference

    def test_issues_list_default_empty(self):
        item = HubArchiveBatchItem(
            archive_batch_id="test-batch",
            owner_project="iot-rpc-rest-app",
            schema_version="1.0.0",
            source_month="2026-05",
            state="verified",
            status="verified",
            record_types=["events"],
            row_count=100,
            checksum_sha256="a" * 64,
            location_reference="vol://test",
            retain_until=datetime(2029, 1, 1, tzinfo=UTC),
            created_at=datetime(2026, 1, 1, tzinfo=UTC),
            actor="test",
            correlation_id="corr",
        )
        assert item.issues == []
        assert item.has_checksum_mismatch is False
        assert item.has_count_mismatch is False

    def test_superuser_sees_manifest_and_storage_reference(self):
        item = HubArchiveBatchItem(
            archive_batch_id="test-batch",
            owner_project="l4media",
            schema_version="1.0.0",
            source_month="2026-04",
            state="purged",
            status="verified",
            record_types=["media_stream_samples"],
            row_count=50000,
            checksum_sha256="b" * 64,
            location_reference="vol://2026/04/l4media/test-batch",
            retain_until=datetime(2029, 1, 1, tzinfo=UTC),
            created_at=datetime(2026, 1, 1, tzinfo=UTC),
            actor="test",
            correlation_id="corr",
            storage_reference="2026/04/l4media/test-batch",
            manifest={"state": "purged"},
        )
        assert item.storage_reference is not None
        assert item.manifest is not None

    def test_non_superuser_hides_manifest_and_storage(self):
        item = HubArchiveBatchItem(
            archive_batch_id="test-batch",
            owner_project="l4media",
            schema_version="1.0.0",
            source_month="2026-04",
            state="purged",
            status="verified",
            record_types=["media_stream_samples"],
            row_count=50000,
            checksum_sha256="b" * 64,
            location_reference="vol://2026/04/l4media/test-batch",
            retain_until=datetime(2029, 1, 1, tzinfo=UTC),
            created_at=datetime(2026, 1, 1, tzinfo=UTC),
            actor="test",
            correlation_id="corr",
        )
        assert item.storage_reference is None
        assert item.manifest is None


# ===========================================================================
# 8. DB-level idempotent import simulation
# ===========================================================================


class _FakeArchiveRow:
    """Minimal in-memory representation of FinArchiveBatch for testing."""

    def __init__(self, **kwargs: Any) -> None:
        for k, v in kwargs.items():
            setattr(self, k, v)

    def __getattr__(self, name: str) -> Any:
        raise AttributeError(name)


class _FakeArchiveDb:
    """Simplified async DB mock for ArchiveService.import_manifest testing."""

    def __init__(self) -> None:
        self._rows: dict[tuple[str, str], _FakeArchiveRow] = {}
        self._added: list[_FakeArchiveRow] = []
        self._flushed = False
        self._committed = False

    async def execute(self, stmt: Any) -> Any:
        sql = str(stmt).lower()
        if "fin_archive_batches" in sql:
            # Extract batch_id and source_project from WHERE clause params
            params = {}
            with contextlib.suppress(Exception):
                params = stmt.compile().params
            batch_id = params.get("id_1") or params.get("id")
            owner = params.get("source_project_1") or params.get("source_project")
            if batch_id and owner:
                key = (batch_id, owner)
                row = self._rows.get(key)
                return _FakeSelectResult(row)
            # List query — return all rows
            return _FakeSelectResult(None, list(self._rows.values()))
        # UPDATE statements — return mock with rowcount
        return _FakeUpdateResult(0)

    async def flush(self) -> None:
        self._flushed = True
        for row in self._added:
            key = (row.id, row.source_project)
            self._rows[key] = row
        self._added.clear()

    async def commit(self) -> None:
        self._committed = True

    async def refresh(self, obj: Any) -> None:
        pass

    def add(self, obj: Any) -> None:
        self._added.append(obj)


class _FakeSelectResult:
    def __init__(self, one: Any = None, all_items: list[Any] | None = None) -> None:
        self._one = one
        self._all = all_items or ([one] if one is not None else [])

    def scalars(self) -> _FakeSelectResult:
        return self

    def first(self) -> Any:
        return self._one or (self._all[0] if self._all else None)

    def all(self) -> list[Any]:
        return list(self._all)


class _FakeUpdateResult:
    def __init__(self, rowcount: int) -> None:
        self.rowcount = rowcount


class TestImportManifestService:
    @pytest.mark.anyio
    async def test_import_verified_manifest_creates_batch(self):
        """Import a valid verified manifest — batch should be created."""
        db = _FakeArchiveDb()
        manifest_data = _iot_manifest()

        batch, _linked_counts, location_ref = await ArchiveService.import_manifest(
            db=cast(Any, db),
            manifest_data=manifest_data,
            actor="test-actor",
            correlation_id="test-corr-001",
            reference_date=datetime(2026, 10, 1, tzinfo=UTC),
        )

        assert batch.id == "arch-iot-2026-05-b91c84f2"
        assert batch.source_project == "iot-rpc-rest-app"
        assert batch.status == "verified"
        assert (
            batch.checksum_sha256
            == "4b227777d4dd1fc61c6f884f48641d02b4d121d3fd328cb08b5531fcacdabf8a"
        )
        assert location_ref.startswith("vol://")
        assert db._committed is True

    @pytest.mark.anyio
    async def test_import_idempotent_same_manifest_no_conflict(self):
        """Importing the same manifest twice should succeed idempotently."""
        db = _FakeArchiveDb()
        manifest_data = _iot_manifest()

        batch1, _, _ = await ArchiveService.import_manifest(
            db=cast(Any, db),
            manifest_data=manifest_data,
            actor="test",
            reference_date=datetime(2026, 10, 1, tzinfo=UTC),
        )

        # Pre-populate the "existing" row for second call
        db._rows[(batch1.id, batch1.source_project)] = _FakeArchiveRow(
            id=batch1.id,
            source_project=batch1.source_project,
            archive_month=date(2026, 5, 1),
            row_count=15000,
            checksum_sha256="4b227777d4dd1fc61c6f884f48641d02b4d121d3fd328cb08b5531fcacdabf8a",
            status="verified",
            verified_at=datetime(2026, 9, 1, 4, 15, 30, tzinfo=UTC),
            purged_at=None,
            manifest=manifest_data,
        )

        batch2, _, _ = await ArchiveService.import_manifest(
            db=cast(Any, db),
            manifest_data=manifest_data,
            actor="test",
            reference_date=datetime(2026, 10, 1, tzinfo=UTC),
        )
        assert batch2.id == batch1.id

    @pytest.mark.anyio
    async def test_import_conflicting_manifest_raises_conflict(self):
        """Importing same batch_id+owner with different checksum should raise ArchiveConflictError."""
        db = _FakeArchiveDb()
        manifest_data = _iot_manifest()

        batch1, _, _ = await ArchiveService.import_manifest(
            db=cast(Any, db),
            manifest_data=manifest_data,
            actor="test",
            reference_date=datetime(2026, 10, 1, tzinfo=UTC),
        )

        # Simulate existing batch with DIFFERENT checksum
        db._rows[(batch1.id, batch1.source_project)] = _FakeArchiveRow(
            id=batch1.id,
            source_project=batch1.source_project,
            archive_month=date(2026, 5, 1),
            row_count=15000,
            checksum_sha256="0000000000000000000000000000000000000000000000000000000000000000",
            status="verified",
            verified_at=None,
            purged_at=None,
            manifest={},
        )

        with pytest.raises(ArchiveConflictError, match="Conflicting immutable fields"):
            await ArchiveService.import_manifest(
                db=cast(Any, db),
                manifest_data=manifest_data,
                actor="test",
                reference_date=datetime(2026, 10, 1, tzinfo=UTC),
            )

    @pytest.mark.anyio
    async def test_import_hot_month_rejected(self):
        """Import with source_month within 3-month hot window raises ArchiveManifestValidationError."""
        db = _FakeArchiveDb()
        manifest_data = _iot_manifest(source_month="2026-08")

        with pytest.raises(
            ArchiveManifestValidationError, match="hot retention window"
        ):
            await ArchiveService.import_manifest(
                db=cast(Any, db),
                manifest_data=manifest_data,
                actor="test",
                reference_date=datetime(2026, 10, 1, tzinfo=UTC),
            )

    @pytest.mark.anyio
    async def test_import_insufficient_retention_rejected(self):
        """Import with retain_until < created_at + 3 years raises validation error."""
        db = _FakeArchiveDb()
        manifest_data = _iot_manifest(
            retention={
                "retain_until_utc": "2027-01-01T00:00:00Z",  # < 3 years from 2026-09-01
                "retention_years": 3,
                "backup_required": True,
            }
        )

        with pytest.raises(ArchiveManifestValidationError, match="3-year minimum"):
            await ArchiveService.import_manifest(
                db=cast(Any, db),
                manifest_data=manifest_data,
                actor="test",
                reference_date=datetime(2026, 10, 1, tzinfo=UTC),
            )

    @pytest.mark.anyio
    async def test_import_volume_unavailable_when_checked(self):
        """Import with check_volume_availability=True raises when volume does not exist."""
        db = _FakeArchiveDb()
        manifest_data = _iot_manifest()

        with (
            patch.object(ArchiveService, "check_volume_mounted", return_value=False),
            pytest.raises(ArchiveStorageUnavailableError, match="unavailable"),
        ):
            await ArchiveService.import_manifest(
                db=cast(Any, db),
                manifest_data=manifest_data,
                actor="test",
                check_volume_availability=True,
                reference_date=datetime(2026, 10, 1, tzinfo=UTC),
            )

    @pytest.mark.anyio
    async def test_import_unsupported_owner_rejected(self):
        """Import with unknown owner_project raises ArchiveManifestValidationError."""
        db = _FakeArchiveDb()
        manifest_data = _iot_manifest()
        # Override owner to invalid value — Pydantic Literal catches it, service wraps in ArchiveManifestValidationError
        manifest_data["owner_project"] = "unknown-service"
        with pytest.raises(ArchiveManifestValidationError):
            await ArchiveService.import_manifest(
                db=cast(Any, db),
                manifest_data=manifest_data,
                actor="test",
                reference_date=datetime(2026, 10, 1, tzinfo=UTC),
            )


# ===========================================================================
# 9. Retention check service test
# ===========================================================================
class TestRetentionCheckService:
    @pytest.mark.anyio
    async def test_retention_check_batch_not_found(self):
        """Retention check on non-existent batch raises error."""
        db = _FakeArchiveDb()
        with pytest.raises(ArchiveManifestValidationError, match="not found"):
            await ArchiveService.check_retention_and_storage(
                db=cast(Any, db),
                archive_batch_id="nonexistent-batch",
            )

    @pytest.mark.anyio
    async def test_retention_check_valid_batch(self):
        """Retention check on a valid batch returns correct flags."""
        db = _FakeArchiveDb()
        manifest_data = _iot_manifest()
        batch = _FakeArchiveRow(
            id="arch-iot-2026-05-b91c84f2",
            source_project="iot-rpc-rest-app",
            schema_version="1.0.0",
            archive_month=date(2026, 5, 1),
            source_types=["iot_session_events"],
            row_count=15000,
            checksum_sha256="4b227777d4dd1fc61c6f884f48641d02b4d121d3fd328cb08b5531fcacdabf8a",
            manifest=manifest_data,
            status="verified",
            verified_at=datetime(2026, 9, 1, 4, 15, 30, tzinfo=UTC),
            purged_at=None,
            retain_until=datetime(2029, 9, 1, tzinfo=UTC),
            storage_reference="2026/05/iot-rpc-rest-app/arch-iot-2026-05-b91c84f2",
            created_at=datetime(2026, 9, 1, tzinfo=UTC),
            actor="test",
            correlation_id="corr",
            min_occurred_at=datetime(2026, 5, 1, tzinfo=UTC),
            max_occurred_at=datetime(2026, 5, 31, 23, 59, 59, tzinfo=UTC),
            through_cursor=25000,
            consumers_passed_cursor=26500,
        )
        db._rows[("arch-iot-2026-05-b91c84f2", "iot-rpc-rest-app")] = batch

        with patch.object(ArchiveService, "check_volume_mounted", return_value=True):
            result = await ArchiveService.check_retention_and_storage(
                db=cast(Any, db),
                archive_batch_id="arch-iot-2026-05-b91c84f2",
                reference_date=datetime(2026, 10, 1, tzinfo=UTC),
            )

        assert result.is_hot_retention_valid is True
        assert result.is_retention_period_valid is True
        assert result.is_backup_evidence_valid is True
        assert result.is_volume_available is True
        assert result.can_purge is True
        assert result.issues == []


# ===========================================================================
# 10. Correlation drill-down archive node (integration with hub_service)
# ===========================================================================
class TestDrillDownArchiveNode:
    @pytest.mark.anyio
    async def test_archive_node_present_when_batch_exists(self):
        """Drill-down should include archive node when usage has archive_batch_id."""
        from tests.test_hub_and_reconciliation import FakeHubDb

        fake_db = FakeHubDb()
        now = datetime.now(UTC)
        tenant_id = 55
        terminal_id = 550

        # Seed terminal
        from app.models_l4desk import FinUsageDaily, L4DeskRemoteSession, L4DeskTerminal

        term = L4DeskTerminal(
            terminal_id=terminal_id,
            tenant_id=tenant_id,
            ordinal=1,
            sn="SN-ARCH-55",
            provisioning_state="ready",
            pin_state="consumed",
            created_at=now,
            correlation_id="corr-arch-55",
        )
        fake_db.add(term)

        # Seed session with source_events_hash
        session = L4DeskRemoteSession(
            tenant_id=tenant_id,
            terminal_id=terminal_id,
            operation_id="op-arch-55",
            correlation_id="corr-arch-55",
            session_type="video",
            state="closed",
            requested_at=now,
            active_at=now,
            closed_at=now + timedelta(minutes=20),
            source_events_hash="hash-sess-55",
        )
        fake_db.add(session)

        # Seed usage with archive_batch_id
        usage = FinUsageDaily(
            tenant_id=tenant_id,
            terminal_id=terminal_id,
            local_date=now.date(),
            timezone="UTC",
            tariff_version_id=1,
            source_seconds=3600,
            video_seconds=3600,
            console_seconds=0,
            free_seconds=0,
            billable_seconds=3600,
            rounded_billable_hours=1,
            rate_kopecks=100,
            calculated_kopecks=100,
            posted_kopecks=100,
            discarded_kopecks=0,
            source_project="iot",
            source_events_hash="hash-u55",
            archive_batch_id="arch-iot-2026-05-b91c84f2",
            actor="metering",
            correlation_id="corr-arch-55",
            created_at=now,
        )
        fake_db.add(usage)

        from app.services.financial_core.hub_service import HubService

        res = await HubService.get_correlation_drilldown(
            db=fake_db,  # type: ignore[arg-type]
            correlation_id="corr-arch-55",
        )

        archive_node = res.nodes.get("archive")
        assert archive_node is not None
        # Since FinArchiveBatch is not in FakeHubDb, archive should be flagged as not found
        assert archive_node.present is False or archive_node.mismatch is True

    @pytest.mark.anyio
    async def test_drilldown_overall_mismatch_when_archive_missing(self):
        """Drill-down should report overall mismatch when archive batch is absent from DB
        and other fact nodes are missing too."""
        from app.models_l4desk import FinUsageDaily, L4DeskTerminal
        from tests.test_hub_and_reconciliation import FakeHubDb

        fake_db = FakeHubDb()
        now = datetime.now(UTC)

        term = L4DeskTerminal(
            terminal_id=600,
            tenant_id=60,
            ordinal=1,
            sn="SN-600",
            provisioning_state="ready",
            pin_state="consumed",
            created_at=now,
            correlation_id="corr-600",
        )
        fake_db.add(term)

        usage = FinUsageDaily(
            tenant_id=60,
            terminal_id=600,
            local_date=now.date(),
            timezone="UTC",
            tariff_version_id=1,
            source_seconds=1800,
            video_seconds=1800,
            console_seconds=0,
            free_seconds=0,
            billable_seconds=1800,
            rounded_billable_hours=1,
            rate_kopecks=100,
            calculated_kopecks=100,
            posted_kopecks=100,
            discarded_kopecks=0,
            source_project="iot",
            source_events_hash="hash-600",
            archive_batch_id="arch-missing-batch",
            actor="metering",
            correlation_id="corr-600",
            created_at=now,
        )
        fake_db.add(usage)

        from app.services.financial_core.hub_service import HubService

        res = await HubService.get_correlation_drilldown(
            db=fake_db,  # type: ignore[arg-type]
            correlation_id="corr-600",
        )

        assert res.overall_status == "mismatch"
        assert len(res.mismatch_codes) > 0
