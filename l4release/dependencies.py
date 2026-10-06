"""Import and verify the exact recovered native build dependency, without execution."""

import json
from pathlib import Path

from .common import ReleaseError, atomic_bytes, contained, file_hash


def dependency_lock(root: Path) -> dict:
    return json.loads((root / "l4release/openh264.lock.json").read_text(encoding="utf-8-sig"))


def verify_openh264(root: Path) -> None:
    vendor = root / "tools/l4capture/vendor/openh264"
    for name, expected in dependency_lock(root)["files"].items():
        path = contained(vendor, name)
        if not path.is_file() or file_hash(path) != expected:
            raise ReleaseError(f"OpenH264 locked dependency missing or changed: {name}")


def import_openh264(root: Path, source: Path) -> None:
    vendor = root / "tools/l4capture/vendor/openh264"
    files = dependency_lock(root)["files"]
    # Validate every source and destination before copying any file.
    for name, expected in files.items():
        path = contained(source, name)
        target = contained(vendor, name)
        if not path.is_file() or file_hash(path) != expected:
            raise ReleaseError(f"OpenH264 source does not match locked dependency: {name}")
        if target.exists() and (not target.is_file() or file_hash(target) != expected):
            raise ReleaseError(f"Refusing to overwrite different dependency: {name}")
    for name in files:
        atomic_bytes(contained(vendor, name), contained(source, name).read_bytes())
    verify_openh264(root)
