"""Tests for l4media restore tooling, archive inspection, and 3-year retention audit."""

import json
import tempfile
from datetime import UTC, datetime
from pathlib import Path

import pytest

from archive.canonical import RECORD_TYPE_QUALITY, RECORD_TYPE_SAMPLES
from archive.pipeline import MediaArchivePipeline
from archive.restore import check_retention_and_backups, inspect_batch, restore_batch
from archive.store import TelemetryStore
from tests.test_archive_pipeline import seed_telemetry_store

REPO_ROOT = Path(__file__).resolve().parents[2]
CONTRACT_DIR = REPO_ROOT / "l4desk-service" / "docs" / "prompts" / "contracts" / "archive-manifest-v1"
SCHEMAS_BUNDLE_PATH = CONTRACT_DIR / "schemas.json"


@pytest.fixture
def prepared_archive_batch():
    as_of = datetime(2026, 9, 21, 1, 30, 0, tzinfo=UTC)
    with tempfile.TemporaryDirectory() as tmp_vol:
        store = TelemetryStore(":memory:")
        seed_telemetry_store(store, month="2026-04", sample_count=30, quality_count=10)

        pipeline = MediaArchivePipeline(store=store, volume_root=tmp_vol)
        manifest = pipeline.run_archive("2026-04", as_of=as_of, dry_run=False, purge=True, batch_entropy="rest01")
        batch_dir = Path(tmp_vol) / "2026" / "04" / "l4media" / manifest.archive_batch_id
        yield tmp_vol, batch_dir, manifest


def test_inspect_batch(prepared_archive_batch):
    tmp_vol, batch_dir, manifest = prepared_archive_batch
    info = inspect_batch(batch_dir)

    assert info["archive_batch_id"] == manifest.archive_batch_id
    assert info["owner_project"] == "l4media"
    assert info["files_verified"] is True
    assert len(info["file_statuses"]) == 2
    assert all(f["sha256_match"] for f in info["file_statuses"])
    assert info["retention_years"] == 3
    assert info["backup_required"] is True


def test_restore_batch_into_store(prepared_archive_batch):
    tmp_vol, batch_dir, manifest = prepared_archive_batch
    fresh_store = TelemetryStore(":memory:")

    res = restore_batch(
        batch_dir,
        target_store=fresh_store,
        schemas_bundle_path=SCHEMAS_BUNDLE_PATH if SCHEMAS_BUNDLE_PATH.is_file() else None,
    )

    assert res["status"] == "success"
    assert res["total_restored"] == 40
    assert res["restored_counts"][RECORD_TYPE_SAMPLES] == 30
    assert res["restored_counts"][RECORD_TYPE_QUALITY] == 10

    # Verify records present in fresh store
    counts = fresh_store.get_counts_for_month("2026-04")
    assert counts["total_records"] == 40
    assert counts[RECORD_TYPE_SAMPLES] == 30
    assert counts[RECORD_TYPE_QUALITY] == 10


def test_restore_batch_to_jsonl_with_limit_and_filter(prepared_archive_batch):
    tmp_vol, batch_dir, manifest = prepared_archive_batch
    with tempfile.TemporaryDirectory() as tmp_out:
        out_file = Path(tmp_out) / "restored.jsonl"

        # Restore only quality events with limit 5
        res = restore_batch(
            batch_dir,
            target_jsonl_file=out_file,
            record_types=[RECORD_TYPE_QUALITY],
            limit=5,
            schemas_bundle_path=SCHEMAS_BUNDLE_PATH if SCHEMAS_BUNDLE_PATH.is_file() else None,
        )

        assert res["total_restored"] == 5
        assert res["restored_counts"][RECORD_TYPE_QUALITY] == 5
        assert res["restored_counts"][RECORD_TYPE_SAMPLES] == 0

        # Verify lines written to JSONL
        with open(out_file, encoding="utf-8") as f:
            lines = [json.loads(line) for line in f if line.strip()]
        assert len(lines) == 5
        assert all(rec["record_type"] == RECORD_TYPE_QUALITY for rec in lines)


def test_check_retention_and_backups_read_only(prepared_archive_batch):
    tmp_vol, batch_dir, manifest = prepared_archive_batch
    audit = check_retention_and_backups(tmp_vol)

    assert audit["status"] == "passed"
    assert audit["total_batches"] == 1
    assert audit["valid_retention_batches"] == 1
    assert audit["backup_visibility_status"] == "visible"

    batch_meta = audit["batches"][0]
    assert batch_meta["retention_years_valid"] is True
    assert batch_meta["backup_required_valid"] is True
    assert batch_meta["retain_until_valid"] is True
    assert batch_meta["files_integrity_valid"] is True
    assert batch_meta["overall_retention_valid"] is True

    # Confirm read-only: files still exist and untouched
    assert (batch_dir / "manifest.json").is_file()
