"""Command-line interface for l4media archive operations.

Commands:
  archive         Archive closed month technical samples/events
  verify          Verify batch integrity and checksums
  restore         Restore archived samples/events from batch
  check-retention Check 3-year retention rules and backup visibility across volume
  status          Show worker configuration and flags
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .canonical import canonical_json_dumps
from .pipeline import MediaArchivePipeline
from .restore import check_retention_and_backups, inspect_batch, restore_batch
from .store import TelemetryStore
from .worker import find_eligible_months, get_worker_config


def main() -> int:
    parser = argparse.ArgumentParser(
        prog="l4media-archive",
        description="l4media deterministic archive and retention CLI",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    # 1. archive command
    p_arch = subparsers.add_parser("archive", help="Archive a closed month")
    p_arch.add_argument("--month", required=True, help="Target month in YYYY-MM format")
    p_arch.add_argument("--dry-run", action="store_true", help="Prepare and verify without purging")
    p_arch.add_argument("--no-purge", action="store_true", help="Skip purging even if verified")
    p_arch.add_argument("--volume-root", default=None, help="Root path of archive volume")
    p_arch.add_argument("--telemetry-db", default=None, help="Path to telemetry SQLite DB")

    # 2. verify command
    p_ver = subparsers.add_parser("verify", help="Verify archive batch integrity")
    p_ver.add_argument("--batch-dir", required=True, help="Path to archive batch directory")

    # 3. restore command
    p_rest = subparsers.add_parser("restore", help="Restore records from archive batch")
    p_rest.add_argument("--batch-dir", required=True, help="Path to archive batch directory")
    p_rest.add_argument("--output-jsonl", default=None, help="Output JSONL file path")
    p_rest.add_argument("--target-db", default=None, help="Target SQLite DB path to restore into")
    p_rest.add_argument("--limit", type=int, default=None, help="Limit number of records restored")

    # 4. check-retention command
    p_ret = subparsers.add_parser("check-retention", help="Check 3-year retention and backups")
    p_ret.add_argument("--volume-root", default=None, help="Root path of archive volume")

    # 5. status command
    subparsers.add_parser("status", help="Show worker configuration and eligible months")

    args = parser.parse_args()

    cfg = get_worker_config()
    vol_root = getattr(args, "volume_root", None) or cfg["volume_root"]

    if args.command == "status":
        eligible = find_eligible_months()
        print(
            json.dumps(
                {
                    "worker_config": cfg,
                    "eligible_closed_months": eligible,
                },
                indent=2,
            )
        )
        return 0

    if args.command == "archive":
        db_path = args.telemetry_db or (Path(cfg["telemetry_dir"]) / "telemetry.db")
        with TelemetryStore(db_path) as store:
            pipeline = MediaArchivePipeline(
                store=store,
                volume_root=vol_root,
                schema_path=cfg["schema_path"],
                schemas_bundle_path=cfg["schemas_bundle_path"],
            )
            try:
                purge_enabled = not args.dry_run and not args.no_purge
                manifest = pipeline.run_archive(
                    source_month=args.month,
                    dry_run=args.dry_run,
                    purge=purge_enabled,
                )
                print(f"Archive succeeded for month {args.month}!")
                print(canonical_json_dumps(manifest.to_dict()))
                return 0
            except Exception as e:
                print(f"Archive failed: {e}", file=sys.stderr)
                return 1

    if args.command == "verify":
        try:
            res = inspect_batch(args.batch_dir)
            print(json.dumps(res, indent=2))
            return 0 if res.get("files_verified") else 1
        except Exception as e:
            print(f"Verification failed: {e}", file=sys.stderr)
            return 1

    if args.command == "restore":
        target_store = TelemetryStore(args.target_db) if args.target_db else None
        try:
            res = restore_batch(
                batch_dir=args.batch_dir,
                target_store=target_store,
                target_jsonl_file=args.output_jsonl,
                limit=args.limit,
                schemas_bundle_path=cfg["schemas_bundle_path"],
            )
            print(json.dumps(res, indent=2))
            return 0
        except Exception as e:
            print(f"Restore failed: {e}", file=sys.stderr)
            return 1
        finally:
            if target_store:
                target_store.close()

    if args.command == "check-retention":
        res = check_retention_and_backups(vol_root)
        print(json.dumps(res, indent=2))
        return 0 if res.get("status") in ("passed", "empty") else 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
