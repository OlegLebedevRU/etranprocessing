"""Generate SHA-256 provenance for the source package published through Git."""

import hashlib
import json
import tomllib
from pathlib import Path

root = Path(__file__).parents[1]
config = tomllib.loads((root / "pyproject.toml").read_text(encoding="utf-8"))
paths = sorted((root / "etranprocessing_db").rglob("*.py")) + [
    root / "pyproject.toml",
    root / "uv.lock",
    root / "docs" / "l4desk" / "schema-v1.json",
    root / "docs" / "l4desk" / "schema-v1.md",
]
manifest = {
    "package": config["project"]["name"],
    "version": config["project"]["version"],
    "delivery": "git-source",
    "source_root": "shared",
    "normalization": "UTF-8 files, CRLF normalized to LF (Git blob bytes)",
    "files": {
        path.relative_to(root).as_posix(): hashlib.sha256(
            path.read_bytes().replace(b"\r\n", b"\n")
        ).hexdigest()
        for path in paths
    },
}
(root / "docs" / "l4desk" / "package-source-v011.json").write_text(
    json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
    encoding="utf-8",
    newline="\n",
)
