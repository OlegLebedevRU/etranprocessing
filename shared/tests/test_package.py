from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tarfile
import tomllib
import zipfile
from pathlib import Path

ROOT = Path(__file__).parents[1]


def test_source_package_provenance():
    for name in ("package-source-v011.json", "schema-v1.json", "schema-v1.md"):
        assert b"\r\n" not in (ROOT / "docs" / "l4desk" / name).read_bytes()
    manifest = json.loads(
        (ROOT / "docs" / "l4desk" / "package-source-v011.json").read_text(
            encoding="utf-8"
        )
    )
    config = tomllib.loads((ROOT / "pyproject.toml").read_text(encoding="utf-8"))
    assert manifest["package"] == config["project"]["name"]
    assert manifest["version"] == config["project"]["version"]
    assert manifest["delivery"] == "git-source"
    for name, digest in manifest["files"].items():
        assert (
            hashlib.sha256(
                (ROOT / name).read_bytes().replace(b"\r\n", b"\n")
            ).hexdigest()
            == digest
        )
    package_sources = {
        path.relative_to(ROOT).as_posix()
        for path in (ROOT / "etranprocessing_db").rglob("*.py")
    }
    assert package_sources <= manifest["files"].keys()


def test_wheel_sdist_metadata_and_isolated_import(tmp_path):
    config = tomllib.loads((ROOT / "pyproject.toml").read_text(encoding="utf-8"))
    version = config["project"]["version"]
    subprocess.run(
        ["uv", "build", "--out-dir", str(tmp_path)],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    wheel = tmp_path / f"etranprocessing_db-{version}-py3-none-any.whl"
    sdist = tmp_path / f"etranprocessing_db-{version}.tar.gz"
    with zipfile.ZipFile(wheel) as package:
        names = package.namelist()
        assert "etranprocessing_db/l4desk.py" in names
        assert "etranprocessing_db/models/finance.py" in names
        assert all(
            name.startswith(
                ("etranprocessing_db/", f"etranprocessing_db-{version}.dist-info/")
            )
            for name in names
        )
        metadata = package.read(
            f"etranprocessing_db-{version}.dist-info/METADATA"
        ).decode()
        assert f"Version: {version}\n" in metadata
        assert "Requires-Python: ==3.14.*" in metadata
        assert "requires-dist: fastapi" not in metadata.lower()
    with tarfile.open(sdist) as package:
        names = package.getnames()
        assert f"etranprocessing_db-{version}/docs/l4desk/schema-v1.json" in names
        assert f"etranprocessing_db-{version}/etranprocessing_db/models/iot.py" in names
        assert not any(
            ".venv" in name or "handoffs" in name or "etranprocessing_gauge" in name
            for name in names
        )
    script = f"""
import json
import sys
sys.path.insert(0, {str(wheel)!r})
import etranprocessing_db
assert etranprocessing_db.__file__.startswith({str(wheel)!r})
assert len(etranprocessing_db.Base.metadata.tables) == 31
from etranprocessing_db import l4desk
from sqlalchemy.orm import configure_mappers
configure_mappers()
assert len(etranprocessing_db.Base.metadata.tables) == 54
assert len(l4desk.__all__) == 23
print(json.dumps(sorted(etranprocessing_db.Base.metadata.tables)))
"""
    result = subprocess.run(
        [sys.executable, "-I", "-c", script],
        cwd=tmp_path,
        check=True,
        capture_output=True,
        text=True,
    )
    assert "fin_ledger_entries" in json.loads(result.stdout)
