"""Unit tests for l4media canonical archive models, serialization, and JSON schema compliance."""

import json
import tempfile
from pathlib import Path

import pytest

from archive.canonical import (
    RECORD_TYPE_SAMPLES,
    RecordEnvelope,
    generate_batch_id,
    validate_envelope_dict,
    validate_manifest_schema,
    write_deterministic_jsonl_gz,
)

REPO_ROOT = Path(__file__).resolve().parents[2]
CONTRACT_DIR = REPO_ROOT / "l4desk-service" / "docs" / "prompts" / "contracts" / "archive-manifest-v1"
SCHEMA_PATH = CONTRACT_DIR / "archive-manifest.schema.json"
SCHEMAS_BUNDLE_PATH = CONTRACT_DIR / "schemas.json"
EXAMPLES_PATH = CONTRACT_DIR / "examples.json"


def test_batch_id_generator():
    batch_id = generate_batch_id("2026-04", seed="a1b2c3d4")
    assert batch_id == "arch-media-2026-04-a1b2c3d4"
    with pytest.raises(ValueError, match="Invalid source_month"):
        generate_batch_id("invalid-month")


def test_deterministic_gzip_and_sha256():
    records = [
        {
            "record_id": 1,
            "record_type": RECORD_TYPE_SAMPLES,
            "occurred_at_utc": "2026-04-01T00:00:00Z",
            "source_project": "l4media",
            "payload": {"fps": 30.0},
        },
        {
            "record_id": 2,
            "record_type": RECORD_TYPE_SAMPLES,
            "occurred_at_utc": "2026-04-01T00:00:01Z",
            "source_project": "l4media",
            "payload": {"fps": 30.0},
        },
    ]
    with tempfile.TemporaryDirectory() as tmp:
        p1 = Path(tmp) / "f1.jsonl.gz"
        p2 = Path(tmp) / "f2.jsonl.gz"

        c1, s1, sha1 = write_deterministic_jsonl_gz(p1, records)
        c2, s2, sha2 = write_deterministic_jsonl_gz(p2, records)

        assert c1 == 2 and c2 == 2
        assert s1 == s2
        assert sha1 == sha2  # Deterministic gzip ensures exact SHA-256 match regardless of timestamp
        assert len(sha1) == 64


def test_canonical_golden_fixture_media_archive_purged():
    """Verify that canonical fixture media-archive-purged from examples.json validates cleanly."""
    if not EXAMPLES_PATH.is_file():
        pytest.skip("examples.json not accessible")

    with open(EXAMPLES_PATH, encoding="utf-8") as f:
        data = json.load(f)

    media_case = next((c for c in data["cases"] if c["id"] == "media-archive-purged"), None)
    assert media_case is not None, "media-archive-purged fixture not found"
    body = media_case["body"]

    errors = validate_manifest_schema(body, schema_path=SCHEMA_PATH if SCHEMA_PATH.is_file() else None)
    assert len(errors) == 0, f"Validation errors on canonical fixture: {errors}"


def test_record_envelope_validation():
    env = RecordEnvelope(
        record_type=RECORD_TYPE_SAMPLES,
        record_id="samp-001",
        occurred_at_utc="2026-04-10T12:00:00Z",
        source_project="l4media",
        session_id="sess-001",
        sn="sn-001",
        payload={"bitrate_kbps": 1500.0, "fps": 30.0},
    ).to_dict()

    errors = validate_envelope_dict(
        env, schemas_bundle_path=SCHEMAS_BUNDLE_PATH if SCHEMAS_BUNDLE_PATH.is_file() else None
    )
    assert len(errors) == 0, f"Envelope errors: {errors}"

    # Negative case: invalid source_project
    bad_env = dict(env)
    bad_env["source_project"] = "unknown_project"
    bad_errors = validate_envelope_dict(
        bad_env, schemas_bundle_path=SCHEMAS_BUNDLE_PATH if SCHEMAS_BUNDLE_PATH.is_file() else None
    )
    assert len(bad_errors) > 0


def test_manifest_validation_negative_cases():
    # Missing required field
    bad_manifest = {
        "archive_manifest_version": "1.0.0",
        "archive_batch_id": "arch-media-2026-04-12345678",
        # owner_project missing
        "schema_version": "1.0.0",
        "created_at_utc": "2026-08-01T00:00:00Z",
        "source_month": "2026-04",
        "time_range": {"min_occurred_at": "2026-04-01T00:00:00Z", "max_occurred_at": "2026-04-30T23:59:59Z"},
        "cursor_bounds": None,
        "record_types": [RECORD_TYPE_SAMPLES],
        "record_counts": {"total_records": 10},
        "files": [
            {
                "path": "samples.jsonl.gz",
                "size_bytes": 100,
                "sha256": "a" * 64,
                "record_count": 10,
                "compression": "gzip",
            }
        ],
        "compression": "gzip",
        "state": "verified",
        "retention": {"retain_until_utc": "2029-08-01T00:00:00Z", "retention_years": 3, "backup_required": True},
        "storage_layout": {
            "volume_root": "/mnt/l4desk-archive",
            "relative_path": "2026/04/l4media/arch-media-2026-04-12345678",
        },
    }
    errs = validate_manifest_schema(bad_manifest, schema_path=SCHEMA_PATH if SCHEMA_PATH.is_file() else None)
    assert any("owner_project" in e for e in errs)

    # Retention years < 3
    bad_retention = dict(bad_manifest)
    bad_retention["owner_project"] = "l4media"
    bad_retention["verification"] = {
        "verified_at_utc": "2026-08-01T00:05:00Z",
        "verifier": "l4media-archive-worker",
        "reread_records_count": 10,
        "reread_checksum_sha256": "a" * 64,
        "restore_sample_status": "passed",
        "cursor_guard_passed": True,
    }
    bad_retention["retention"] = {
        "retain_until_utc": "2027-08-01T00:00:00Z",
        "retention_years": 1,
        "backup_required": True,
    }
    errs = validate_manifest_schema(bad_retention, schema_path=SCHEMA_PATH if SCHEMA_PATH.is_file() else None)
    assert any("retention_years" in e for e in errs)
