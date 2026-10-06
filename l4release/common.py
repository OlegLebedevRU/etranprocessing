"""Validation, credential loading and atomic local files."""

from __future__ import annotations

import hashlib
import json
import os
import re
import tempfile
from pathlib import Path
from urllib.parse import urlsplit


class ReleaseError(Exception):
    """An actionable release failure, safe to display after redaction."""


def version_value(value: str) -> str:
    if not re.fullmatch(
        r"(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)(?:-[A-Za-z0-9]+(?:[.-][A-Za-z0-9]+)*)?",
        value,
    ):
        raise ReleaseError("Version must be an explicit three-part version, never latest or a path")
    if any(int(part) > 65535 for part in value.split("-", 1)[0].split(".")):
        raise ReleaseError("Version exceeds the Windows PE version range")
    return value


def registry_value(value: str) -> str:
    parts = urlsplit(value)
    if parts.scheme != "https" or not parts.hostname or parts.username or parts.password:
        raise ReleaseError("Registry must be HTTPS without embedded credentials")
    if parts.query or parts.fragment:
        raise ReleaseError("Registry must not have a query or fragment")
    return value.rstrip("/")


def contained(root: Path, value: str) -> Path:
    path = (root / value).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ReleaseError("Release path escapes the repository")
    return path


def file_hash(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def atomic_bytes(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def atomic_json(path: Path, value: dict) -> None:
    atomic_bytes(path, (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8"))


def load_env(path: Path, *, required: bool = True) -> dict[str, str]:
    """Literal dotenv subset: no interpolation, evaluation or fallback to other env files."""
    if not path.is_file():
        if required:
            raise ReleaseError("Signing env file is missing; specify --env-file")
        return {}
    result = {}
    for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        key, separator, value = line.partition("=")
        key, value = key.strip(), value.strip()
        if not separator or not re.fullmatch(r"[A-Z][A-Z0-9_]*", key):
            raise ReleaseError(f"Invalid signing env assignment at line {number}")
        if value.startswith(("'", '"')):
            if len(value) < 2 or value[-1] != value[0]:
                raise ReleaseError(f"Invalid quoted env assignment at line {number}")
            value = value[1:-1]
        if key in result:
            raise ReleaseError(f"Duplicate signing env key: {key}")
        result[key] = value
    return result


class Redactor:
    def __init__(self, env: dict[str, str]):
        self.secrets = sorted(
            {
                value
                for key, value in env.items()
                if value
                and any(
                    label in key for label in ("PASSWORD", "SECRET", "API_KEY", "TOKEN", "KEY_ID")
                )
            },
            key=len,
            reverse=True,
        )

    def __call__(self, text: str) -> str:
        for value in self.secrets:
            text = text.replace(value, "[REDACTED]")
        return re.sub(r"(?i)(authorization\s*[:=]\s*)[^\r\n]+", r"\1[REDACTED]", text)
