"""CLI tests for l4media-archive."""

import json
import tempfile
from pathlib import Path
from unittest.mock import patch

from archive.cli import main
from archive.store import TelemetryStore
from tests.test_archive_pipeline import seed_telemetry_store


def test_cli_status(capsys):
    with patch("sys.argv", ["l4media-archive", "status"]):
        code = main()
        assert code == 0
        captured = capsys.readouterr()
        data = json.loads(captured.out)
        assert "worker_config" in data
        assert "eligible_closed_months" in data


def test_cli_archive_and_verify(capsys):
    with tempfile.TemporaryDirectory() as tmp_vol:
        db_path = Path(tmp_vol) / "telemetry.db"
        store = TelemetryStore(db_path)
        seed_telemetry_store(store, month="2026-04", sample_count=10, quality_count=2)
        store.close()

        # Run archive via CLI
        with patch(
            "sys.argv",
            [
                "l4media-archive",
                "archive",
                "--month",
                "2026-04",
                "--volume-root",
                tmp_vol,
                "--telemetry-db",
                str(db_path),
            ],
        ):
            code = main()
            assert code == 0
            captured = capsys.readouterr()
            assert "Archive succeeded" in captured.out

        # Find batch dir
        batch_dirs = list((Path(tmp_vol) / "2026" / "04" / "l4media").glob("arch-*"))
        assert len(batch_dirs) == 1
        batch_dir = batch_dirs[0]

        # Verify batch via CLI
        with patch(
            "sys.argv",
            [
                "l4media-archive",
                "verify",
                "--batch-dir",
                str(batch_dir),
            ],
        ):
            code = main()
            assert code == 0
            captured = capsys.readouterr()
            data = json.loads(captured.out)
            assert data["files_verified"] is True

        # Check retention via CLI
        with patch(
            "sys.argv",
            [
                "l4media-archive",
                "check-retention",
                "--volume-root",
                tmp_vol,
            ],
        ):
            code = main()
            assert code == 0
            captured = capsys.readouterr()
            data = json.loads(captured.out)
            assert data["status"] == "passed"
