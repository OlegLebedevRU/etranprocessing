"""Background archive worker for l4media.

Monitors eligible closed months (> 3 full months) and executes the archive pipeline.
Controlled by feature flags (disabled by default in accordance with deployment safety):
- L4MEDIA_ARCHIVE_WORKER_ENABLED (default: false)
- L4MEDIA_ARCHIVE_DRY_RUN (default: false)
- L4MEDIA_ARCHIVE_VOLUME_ROOT (default: /mnt/l4desk-archive)
- L4MEDIA_HOT_TELEMETRY_DIR (default: /var/lib/l4media/telemetry)
"""

from __future__ import annotations

import logging
import os
import sys
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from .pipeline import MediaArchivePipeline
from .store import TelemetryStore

logging.basicConfig(
    level=logging.INFO,
    format="[%(asctime)s] [%(levelname)s] [l4media-archive] %(message)s",
)
logger = logging.getLogger("l4media-archive-worker")


def get_worker_config() -> dict[str, Any]:
    enabled_str = os.getenv("L4MEDIA_ARCHIVE_WORKER_ENABLED", "false").lower()
    enabled = enabled_str in ("true", "1", "yes")

    dry_run_str = os.getenv("L4MEDIA_ARCHIVE_DRY_RUN", "false").lower()
    dry_run = dry_run_str in ("true", "1", "yes")

    volume_root = os.getenv("L4MEDIA_ARCHIVE_VOLUME_ROOT", "/mnt/l4desk-archive")
    telemetry_dir = os.getenv("L4MEDIA_HOT_TELEMETRY_DIR", "/var/lib/l4media/telemetry")
    schema_path = os.getenv("L4MEDIA_ARCHIVE_SCHEMA_PATH")
    schemas_bundle_path = os.getenv("L4MEDIA_ARCHIVE_SCHEMAS_BUNDLE")

    return {
        "enabled": enabled,
        "dry_run": dry_run,
        "volume_root": volume_root,
        "telemetry_dir": telemetry_dir,
        "schema_path": Path(schema_path) if schema_path else None,
        "schemas_bundle_path": Path(schemas_bundle_path) if schemas_bundle_path else None,
    }


def find_eligible_months(as_of: datetime | None = None) -> list[str]:
    """Return past eligible closed months that are older than 3 full calendar months."""
    ref = as_of or datetime.now(UTC)
    curr_idx = ref.year * 12 + ref.month
    max_idx = curr_idx - 4

    eligible: list[str] = []
    # Check last 12 months up to cutoff
    for idx in range(max_idx - 11, max_idx + 1):
        y = idx // 12
        m = idx % 12
        if m == 0:
            y -= 1
            m = 12
        eligible.append(f"{y:04d}-{m:02d}")
    return eligible


def run_worker_cycle(
    as_of: datetime | None = None,
    specific_month: str | None = None,
    force_enabled: bool = False,
) -> int:
    """Run one archive worker pass.

    Returns:
        0 on success or if disabled, non-zero on failure.
    """
    cfg = get_worker_config()
    is_enabled = force_enabled or cfg["enabled"]

    if not is_enabled:
        logger.info("Worker is DISABLED (L4MEDIA_ARCHIVE_WORKER_ENABLED=false). Exiting cleanly.")
        return 0

    logger.info(
        "Worker is ENABLED. volume_root='%s', dry_run=%s",
        cfg["volume_root"],
        cfg["dry_run"],
    )

    db_path = Path(cfg["telemetry_dir"]) / "telemetry.db"
    store = TelemetryStore(db_path)

    pipeline = MediaArchivePipeline(
        store=store,
        volume_root=cfg["volume_root"],
        schema_path=cfg["schema_path"],
        schemas_bundle_path=cfg["schemas_bundle_path"],
    )

    target_months = [specific_month] if specific_month else find_eligible_months(as_of)

    processed = 0
    errors = 0
    for m in target_months:
        counts = store.get_counts_for_month(m)
        if counts["total_records"] == 0:
            continue

        logger.info("Processing eligible month %s (%d records)...", m, counts["total_records"])
        try:
            manifest = pipeline.run_archive(
                source_month=m,
                as_of=as_of,
                dry_run=cfg["dry_run"],
                purge=True,
            )
            logger.info(
                "Batch %s completed: state=%s, records=%d",
                manifest.archive_batch_id,
                manifest.state,
                manifest.record_counts.get("total_records", 0),
            )
            processed += 1
        except Exception as e:
            logger.error("Failed to archive month %s: %s", m, e, exc_info=True)
            errors += 1

    logger.info("Worker cycle finished: %d batches processed, %d errors.", processed, errors)
    return 1 if errors > 0 else 0


if __name__ == "__main__":
    sys.exit(run_worker_cycle())
