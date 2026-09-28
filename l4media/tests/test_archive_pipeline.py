"""Integration tests for l4media deterministic archive pipeline lifecycle."""

import tempfile
from datetime import UTC, datetime
from pathlib import Path

import pytest

from archive.canonical import (
    RECORD_TYPE_QUALITY,
    RECORD_TYPE_SAMPLES,
    validate_manifest_schema,
)
from archive.guards import (
    ActiveRecordsDetectedError,
    RetentionViolationError,
)
from archive.pipeline import MediaArchivePipeline
from archive.store import TelemetryStore

REPO_ROOT = Path(__file__).resolve().parents[2]
CONTRACT_DIR = REPO_ROOT / "l4desk-service" / "docs" / "prompts" / "contracts" / "archive-manifest-v1"
SCHEMA_PATH = CONTRACT_DIR / "archive-manifest.schema.json"
SCHEMAS_BUNDLE_PATH = CONTRACT_DIR / "schemas.json"


def seed_telemetry_store(
    store: TelemetryStore, month: str = "2026-04", sample_count: int = 50, quality_count: int = 10
) -> None:
    for i in range(1, sample_count + 1):
        store.add_sample(
            record_id=f"samp-{month}-{i:04d}",
            session_id="sess-test-01",
            occurred_at_utc=f"{month}-15T10:{i % 60:02d}:00Z",
            source_month=month,
            sn="test_sn_001",
            tenant_id=1,
            terminal_id=101,
            cursor=1000 + i,
            rtp_packets=100 * i,
            rtcp_packets=2 * i,
            bytes_count=10000 * i,
            bitrate_kbps=1500.0,
            fps=30.0,
        )

    for j in range(1, quality_count + 1):
        store.add_quality_event(
            record_id=f"qevt-{month}-{j:04d}",
            session_id="sess-test-01",
            occurred_at_utc=f"{month}-15T11:{j % 60:02d}:00Z",
            source_month=month,
            event_type="stream_degraded",
            sn="test_sn_001",
            tenant_id=1,
            terminal_id=101,
            cursor=2000 + j,
            metric_value=0.05 * j,
            reason="packet_loss_spike",
        )

    # Seed operational session summary that must strictly remain hot!
    store.record_session_summary(
        session_id="sess-test-01",
        sn="test_sn_001",
        state="stopped",
        created_at_utc=f"{month}-15T10:00:00Z",
        started_at_utc=f"{month}-15T10:01:00Z",
        stopped_at_utc=f"{month}-15T11:30:00Z",
        total_duration_sec=5340,
        final_rtp_packets=50000,
        final_bytes=5000000,
        stop_reason="normal_shutdown",
    )


def test_full_pipeline_lifecycle_purge():
    as_of = datetime(2026, 9, 21, 1, 30, 0, tzinfo=UTC)
    with tempfile.TemporaryDirectory() as tmp_vol:
        store = TelemetryStore(":memory:")
        seed_telemetry_store(store, month="2026-04", sample_count=40, quality_count=10)

        assert store.get_session_summary_count() == 1
        assert store.get_counts_for_month("2026-04")["total_records"] == 50

        pipeline = MediaArchivePipeline(
            store=store,
            volume_root=tmp_vol,
            schema_path=SCHEMA_PATH if SCHEMA_PATH.is_file() else None,
            schemas_bundle_path=SCHEMAS_BUNDLE_PATH if SCHEMAS_BUNDLE_PATH.is_file() else None,
        )

        manifest = pipeline.run_archive(
            source_month="2026-04",
            as_of=as_of,
            dry_run=False,
            purge=True,
            batch_entropy="testbatch1",
        )

        # 1. Manifest assertions
        assert manifest.state == "purged"
        assert manifest.owner_project == "l4media"
        assert manifest.source_month == "2026-04"
        assert manifest.record_counts["total_records"] == 50
        assert manifest.record_counts[RECORD_TYPE_SAMPLES] == 40
        assert manifest.record_counts[RECORD_TYPE_QUALITY] == 10
        assert manifest.verification is not None
        assert manifest.verification.restore_sample_status == "passed"
        assert manifest.verification.cursor_guard_passed is True
        assert manifest.purge is not None
        assert manifest.purge.purge_status == "completed"
        assert manifest.purge.purged_records_count == 50

        # 2. Schema compliance
        schema_errs = validate_manifest_schema(
            manifest.to_dict(), schema_path=SCHEMA_PATH if SCHEMA_PATH.is_file() else None
        )
        assert len(schema_errs) == 0, f"Schema errors: {schema_errs}"

        # 3. Storage layout and files on volume
        batch_dir = Path(tmp_vol) / "2026" / "04" / "l4media" / manifest.archive_batch_id
        assert batch_dir.is_dir()
        assert (batch_dir / "samples.jsonl.gz").is_file()
        assert (batch_dir / "quality.jsonl.gz").is_file()
        assert (batch_dir / "checksum.sha256").is_file()
        assert (batch_dir / "manifest.json").is_file()

        # 4. Hot store invariant: verified technical samples purged, but session summaries remain hot!
        remaining = store.get_counts_for_month("2026-04")
        assert remaining["total_records"] == 0, "All technical samples for 2026-04 should have been purged"
        assert store.get_session_summary_count() == 1, "Session summary must NOT be purged!"


def test_pipeline_dry_run():
    as_of = datetime(2026, 9, 21, 1, 30, 0, tzinfo=UTC)
    with tempfile.TemporaryDirectory() as tmp_vol:
        store = TelemetryStore(":memory:")
        seed_telemetry_store(store, month="2026-04", sample_count=20, quality_count=5)

        pipeline = MediaArchivePipeline(
            store=store,
            volume_root=tmp_vol,
            schema_path=SCHEMA_PATH if SCHEMA_PATH.is_file() else None,
            schemas_bundle_path=SCHEMAS_BUNDLE_PATH if SCHEMAS_BUNDLE_PATH.is_file() else None,
        )

        manifest = pipeline.run_archive(
            source_month="2026-04",
            as_of=as_of,
            dry_run=True,
            purge=True,
        )

        # In dry run: state is verified, purge is pending, hot records are preserved
        assert manifest.state == "verified"
        assert manifest.purge is not None
        assert manifest.purge.purge_status == "pending"
        assert manifest.purge.purged_records_count == 0

        counts = store.get_counts_for_month("2026-04")
        assert counts["total_records"] == 25, "Dry run must NOT purge records from hot store"


def test_pipeline_hot_retention_violation():
    as_of = datetime(2026, 9, 21, 1, 30, 0, tzinfo=UTC)
    with tempfile.TemporaryDirectory() as tmp_vol:
        store = TelemetryStore(":memory:")
        pipeline = MediaArchivePipeline(store=store, volume_root=tmp_vol)

        # Attempt to archive month within 3-month window
        with pytest.raises(RetentionViolationError):
            pipeline.run_archive("2026-07", as_of=as_of)


def test_pipeline_active_stream_guard_blocks_archive():
    as_of = datetime(2026, 9, 21, 1, 30, 0, tzinfo=UTC)
    with tempfile.TemporaryDirectory() as tmp_vol:
        store = TelemetryStore(":memory:")
        seed_telemetry_store(store, month="2026-04", sample_count=10, quality_count=2)

        # Stream active for 2026-04
        def active_fn():
            return [{"session_id": "sess-test-01", "source_month": "2026-04", "state": "active"}]

        pipeline = MediaArchivePipeline(
            store=store,
            volume_root=tmp_vol,
            active_checker=active_fn,
        )

        with pytest.raises(ActiveRecordsDetectedError):
            pipeline.run_archive("2026-04", as_of=as_of)

        # Ensure no records purged
        assert store.get_counts_for_month("2026-04")["total_records"] == 12
