from __future__ import annotations

import hashlib
import json
from datetime import UTC, datetime
from pathlib import Path

from archive.pipeline import MediaArchivePipeline
from archive.restore import inspect_batch, restore_batch
from archive.store import TelemetryStore

root = Path("/work")
hot = root / "hot" / "telemetry.db"
volume = root / "volume"
source_month = "2026-09"
virtual_as_of = datetime(2027, 1, 1, 0, 0, tzinfo=UTC)
session_id = "17f-synthetic-archive"
sn = "synthetic_17f"

with TelemetryStore(hot) as store:
    for number in range(1, 5):
        store.add_sample(
            record_id=f"17f-sample-{number}",
            session_id=session_id,
            occurred_at_utc=f"2026-09-26T10:00:0{number}Z",
            source_month=source_month,
            sn=sn,
            tenant_id=0,
            terminal_id=0,
            cursor=number,
            rtp_packets=100 * number,
            bytes_count=10000 * number,
            bitrate_kbps=800.0,
            fps=25.0,
        )
    for number in range(1, 3):
        store.add_quality_event(
            record_id=f"17f-quality-{number}",
            session_id=session_id,
            occurred_at_utc=f"2026-09-26T10:01:0{number}Z",
            source_month=source_month,
            event_type="stream_degraded",
            sn=sn,
            tenant_id=0,
            terminal_id=0,
            cursor=10 + number,
            metric_value=0.1,
            reason="synthetic_probe",
        )
    store.record_session_summary(
        session_id=session_id,
        sn=sn,
        state="stopped",
        created_at_utc="2026-09-26T10:00:00Z",
        stopped_at_utc="2026-09-26T10:02:00Z",
        total_duration_sec=120,
        stop_reason="synthetic_probe",
    )
    original_counts = store.get_counts_for_month(source_month)
    pipeline = MediaArchivePipeline(store=store, volume_root=volume)
    manifest = pipeline.run_archive(
        source_month=source_month,
        as_of=virtual_as_of,
        dry_run=True,
        purge=False,
        batch_entropy="17fprobe",
    )
    hot_counts_after = store.get_counts_for_month(source_month)
    hot_summaries_after = store.get_session_summary_count()

batch_dir = volume / "2026" / "09" / "l4media" / manifest.archive_batch_id
inspection = inspect_batch(batch_dir)
with TelemetryStore(root / "restored" / "telemetry.db") as restored:
    restoration = restore_batch(batch_dir, target_store=restored)
    restored_counts = restored.get_counts_for_month(source_month)

manifest_bytes = (batch_dir / "manifest.json").read_bytes()
summary = {
    "source_month": source_month,
    "source_data_day": "2026-09-26",
    "virtual_as_of": virtual_as_of.isoformat(),
    "batch_id": manifest.archive_batch_id,
    "batch_state": manifest.state,
    "manifest_sha256": hashlib.sha256(manifest_bytes).hexdigest(),
    "files_verified": inspection["files_verified"],
    "file_count": len(inspection["file_statuses"]),
    "original_counts": original_counts,
    "hot_counts_after": hot_counts_after,
    "hot_summaries_after": hot_summaries_after,
    "restoration": restoration,
    "restored_counts": restored_counts,
}
assert inspection["files_verified"]
assert original_counts["total_records"] == 6
assert hot_counts_after == original_counts
assert hot_summaries_after == 1
assert restored_counts["total_records"] == 6
assert restoration["total_restored"] == 6
(root / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
print(json.dumps(summary, separators=(",", ":")))
