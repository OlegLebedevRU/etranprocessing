"""Unit tests for l4media archive guards:

retention window, path security, active streams, and verification invariants.
"""

import tempfile
from datetime import UTC, datetime
from pathlib import Path

import pytest

from archive.canonical import (
    RECORD_TYPE_SAMPLES,
    write_deterministic_jsonl_gz,
)
from archive.guards import (
    ActiveRecordsDetectedError,
    ActiveStreamsGuard,
    ChecksumMismatchError,
    CountMismatchError,
    CursorLagError,
    PathSecurityError,
    PathSecurityGuard,
    RetentionGuard,
    RetentionViolationError,
    VerificationGuard,
)


def test_retention_guard_boundary():
    # As of 2026-09-21:
    # 3 full closed months: August (08), July (07), June (06)
    # Cutoff month is May (05).
    as_of = datetime(2026, 9, 21, 1, 30, 0, tzinfo=UTC)

    # 2026-05 and older must PASS
    y, m = RetentionGuard.check_eligible_month("2026-05", as_of=as_of)
    assert (y, m) == (2026, 5)

    y, m = RetentionGuard.check_eligible_month("2026-04", as_of=as_of)
    assert (y, m) == (2026, 4)

    y, m = RetentionGuard.check_eligible_month("2025-12", as_of=as_of)
    assert (y, m) == (2025, 12)

    # 2026-06, 07, 08, 09 must FAIL with HOT_RETENTION_VIOLATION
    for month in ["2026-06", "2026-07", "2026-08", "2026-09"]:
        with pytest.raises(RetentionViolationError, match="hot retention window"):
            RetentionGuard.check_eligible_month(month, as_of=as_of)


def test_path_security_guard():
    with tempfile.TemporaryDirectory() as tmp:
        guard = PathSecurityGuard(tmp)
        guard.ensure_volume_available()

        safe_path = guard.resolve_safe_path("2026/04/l4media/batch-1")
        assert safe_path.is_relative_to(Path(tmp).resolve())

        # Traversal attempt
        with pytest.raises(PathSecurityError, match="Path traversal"):
            guard.resolve_safe_path("../../etc/passwd")


def test_active_streams_guard():
    # When active stream exists for source month
    def active_mock():
        return [{"session_id": "sess-active-1", "source_month": "2026-04", "state": "active"}]

    guard = ActiveStreamsGuard(active_checker=active_mock)
    with pytest.raises(ActiveRecordsDetectedError, match="Active stream detected"):
        guard.check_active_streams("2026-04")

    # When active stream is for different month
    guard.check_active_streams("2026-03")  # Must not raise


def test_verification_guard_checksum_mismatch():
    with tempfile.TemporaryDirectory() as tmp:
        batch_dir = Path(tmp)
        fpath = batch_dir / "samples.jsonl.gz"
        records = [
            {
                "record_id": "1",
                "record_type": RECORD_TYPE_SAMPLES,
                "occurred_at_utc": "2026-04-01T00:00:00Z",
                "source_project": "l4media",
                "payload": {},
            }
        ]
        write_deterministic_jsonl_gz(fpath, records)

        manifest = {
            "files": [
                {
                    "path": "samples.jsonl.gz",
                    "size_bytes": fpath.stat().st_size,
                    "sha256": "0" * 64,  # Intentionally wrong SHA
                    "record_count": 1,
                    "compression": "gzip",
                }
            ],
            "record_counts": {"total_records": 1},
        }

        with pytest.raises(ChecksumMismatchError, match="Checksum mismatch"):
            VerificationGuard.verify_batch_integrity(batch_dir, manifest)


def test_verification_guard_count_mismatch():
    with tempfile.TemporaryDirectory() as tmp:
        batch_dir = Path(tmp)
        fpath = batch_dir / "samples.jsonl.gz"
        records = [
            {
                "record_id": "1",
                "record_type": RECORD_TYPE_SAMPLES,
                "occurred_at_utc": "2026-04-01T00:00:00Z",
                "source_project": "l4media",
                "payload": {},
            }
        ]
        cnt, sz, sha = write_deterministic_jsonl_gz(fpath, records)

        manifest = {
            "files": [
                {
                    "path": "samples.jsonl.gz",
                    "size_bytes": sz,
                    "sha256": sha,
                    "record_count": 5,  # Manifest says 5, file only has 1
                    "compression": "gzip",
                }
            ],
            "record_counts": {"total_records": 5},
        }

        with pytest.raises(CountMismatchError, match="Record count mismatch"):
            VerificationGuard.verify_batch_integrity(batch_dir, manifest)


def test_verification_guard_cursor_lag():
    with tempfile.TemporaryDirectory() as tmp:
        batch_dir = Path(tmp)
        fpath = batch_dir / "samples.jsonl.gz"
        records = [
            {
                "record_id": "1",
                "record_type": RECORD_TYPE_SAMPLES,
                "occurred_at_utc": "2026-04-01T00:00:00Z",
                "source_project": "l4media",
                "cursor": 100,
                "payload": {},
            }
        ]
        cnt, sz, sha = write_deterministic_jsonl_gz(fpath, records)

        manifest = {
            "files": [
                {
                    "path": "samples.jsonl.gz",
                    "size_bytes": sz,
                    "sha256": sha,
                    "record_count": 1,
                    "compression": "gzip",
                }
            ],
            "record_counts": {"total_records": 1},
            "cursor_bounds": {
                "through_cursor": 100,
                "consumers_passed_cursor": 50,  # Lagging!
            },
        }

        with pytest.raises(CursorLagError, match="Cursor lag detected"):
            VerificationGuard.verify_batch_integrity(batch_dir, manifest)
