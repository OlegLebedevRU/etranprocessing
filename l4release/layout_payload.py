"""Complete immutable payloads for the new layout; never copy runtime data."""

from __future__ import annotations

import hashlib
import json
import re
import zipfile
from pathlib import Path

from .common import ReleaseError, atomic_json, file_hash, version_value

COMPONENTS = frozenset(
    (
        "leo4proxy",
        "mosquitto",
        "l4con",
        "l4superv",
        "l4pin",
        "l4desk",
        "l4capture",
        "ffmpeg",
        "l4sql",
        "l4launch",
        "crt",
    )
)
EXECUTABLES = tuple(
    f"{name}/{name}.exe" if name != "l4capture" else "l4capture/bin/l4capture.exe"
    for name in sorted(COMPONENTS - {"crt"})
)
TEMPLATES = {
    "l4superv.json": "templates/l4superv.json",
    "mosquitto/acl.conf": "templates/mosquitto/acl.conf",
    "l4capture/bin/idle_refresh.ini": "templates/l4capture/idle_refresh.ini",
}


def _destination(relative: str) -> str | None:
    if relative in TEMPLATES:
        return TEMPLATES[relative]
    if relative in {"example_mosquitto.conf", "term_tool-user-guide.md"}:
        return "suite/" + relative
    if relative == "l4superv/package-components.json":
        return None  # Legacy inventory is replaced by the complete outer inventory.
    parts = relative.split("/")
    if any(not re.fullmatch(r"[A-Za-z0-9_.-]+", p) or p in {".", ".."} for p in parts):
        raise ReleaseError(f"Unsafe staged path: {relative}")
    reserved = {
        "CON",
        "PRN",
        "AUX",
        "NUL",
        *(f"COM{i}" for i in range(1, 10)),
        *(f"LPT{i}" for i in range(1, 10)),
    }
    if any(p.endswith(".") or p.split(".", 1)[0].upper() in reserved for p in parts):
        raise ReleaseError(f"Windows alias in staged path: {relative}")
    if len(parts) < 2 or parts[0] not in COMPONENTS:
        raise ReleaseError(f"Unknown staged component: {relative}")
    suffix = Path(relative).suffix.lower()
    if suffix in {".cmd", ".ps1"}:
        # Old scripts are not entry points for the new installation.
        return None
    if suffix == ".exe" and relative not in EXECUTABLES:
        raise ReleaseError(f"Unexpected staged executable: {relative}")
    if suffix == ".json" and relative != "l4capture/SBOM.json":
        raise ReleaseError(f"Unclassified configuration file: {relative}")
    if suffix not in {".exe", ".md", ".txt", ".json", ".crt"} and parts[-1] != "LICENSE":
        raise ReleaseError(f"Unclassified staged file: {relative}")
    if any(p.lower() in {"log", "logs", "state", "cache"} for p in parts):
        raise ReleaseError(f"Runtime data in stage: {relative}")
    return relative


def build_layout_payload(
    stage: Path, output: Path, version: str, arch: str, publisher: str | None
) -> dict:
    """Caller owns the trusted build stage. ZIP/manifest are local release artifacts."""
    version_value(version)
    if arch not in {"x86", "x64"}:
        raise ReleaseError("Invalid layout architecture")
    if publisher is not None and not re.fullmatch(r"[0-9a-f]{64}", publisher):
        raise ReleaseError("Invalid publisher certificate SHA256")
    if stage.is_symlink() or not stage.is_dir():
        raise ReleaseError("Layout stage must be a real directory")
    entries: dict[str, bytes] = {}
    for source in sorted(stage.rglob("*")):
        if source.is_symlink() or source.is_junction():
            raise ReleaseError("Reparse point in layout stage")
        if not source.is_file():
            continue
        if source.stat().st_nlink != 1:
            raise ReleaseError("Hardlinked layout input")
        relative = source.relative_to(stage).as_posix()
        target = _destination(relative)
        if target is None:
            continue
        if len(target) >= 260:
            raise ReleaseError("Layout path exceeds native MAX_PATH bound")
        content = source.read_bytes()
        if relative == "l4superv.json":
            config = json.loads(content)
            config.pop("base_path", None)  # Executable-derived release and ProgramData resolution.
            content = (json.dumps(config, indent=2) + "\n").encode("utf-8")
        if target.casefold() in {p.casefold() for p in entries}:
            raise ReleaseError("Duplicate layout destination")
        if len(content) > 512 * 1024 * 1024:
            raise ReleaseError("Layout file exceeds native bound")
        entries[target] = content
    if not set(EXECUTABLES).issubset(entries):
        raise ReleaseError("Incomplete native tools bundle")
    if not set(TEMPLATES.values()).issubset(entries):
        raise ReleaseError("Missing default configuration templates")
    if sum(map(len, entries.values())) > 1024 * 1024 * 1024:
        raise ReleaseError("Layout expansion exceeds native 1GiB bundle bound")
    if len(entries) > 64:
        raise ReleaseError("Layout inventory exceeds native 64-file bound")
    output.mkdir(parents=True, exist_ok=True)
    name = f"l4tools-layout-{arch}.zip"
    temporary = output / (name + ".pending")
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for path, content in sorted(entries.items()):
                info = zipfile.ZipInfo(path, (2000, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                archive.writestr(info, content)
        temporary.replace(output / name)
    finally:
        temporary.unlink(missing_ok=True)
    manifest = {
        "schema": 1,
        "version": version,
        "arch": arch,
        "publisher_certificate_sha256": publisher,
        "archive_sha256": file_hash(output / name),
        "files": [
            {"path": path, "size": len(content), "sha256": hashlib.sha256(content).hexdigest()}
            for path, content in sorted(entries.items())
        ],
    }
    manifest_name = f"l4tools-layout-{arch}.json"
    atomic_json(output / manifest_name, manifest)
    return {
        "archive": name,
        "manifest": manifest_name,
        "archive_sha256": manifest["archive_sha256"],
        "manifest_sha256": file_hash(output / manifest_name),
    }
