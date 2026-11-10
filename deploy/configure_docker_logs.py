"""Add bounded JSON logging to existing Compose files without rewriting their settings."""

import argparse
import json
import os
import re
import shutil
import subprocess
import tempfile
from datetime import UTC, datetime
from pathlib import Path

LOGGING = {"driver": "json-file", "options": {"max-size": "20m", "max-file": "3"}}
HEADER = (
    "x-etran-logging: &etran-logging\n"
    "  driver: json-file\n"
    "  options:\n"
    '    max-size: "20m"\n'
    '    max-file: "3"\n\n'
)


def with_rotation(text: str) -> str:
    if text.startswith(HEADER):
        return text
    if "etran-logging" in text:
        raise ValueError("Conflicting logging anchor; review manually")
    section = re.search(r"(?ms)^services:[ \t]*\n(.*?)(?=^[^\s#]|\Z)", text)
    if section is None or re.search(r"(?m)^    logging:", section[1]):
        raise ValueError("Expected block-style services without a logging override")
    body, count = re.subn(
        r"(?m)^(  [\w-]+:[ \t]*\n)",
        r"\1    logging: *etran-logging\n",
        section[1],
    )
    if not count:
        raise ValueError("No service blocks found")
    return HEADER + text[: section.start(1)] + body + text[section.end(1) :]


def resolved(path: Path) -> dict:
    result = subprocess.run(
        [
            "sudo",
            "-n",
            "docker",
            "compose",
            "--project-directory",
            str(path.parent),
            "-f",
            str(path),
            "config",
            "--format",
            "json",
        ],
        capture_output=True,
        text=True,
        check=True,
        timeout=30,
    )
    return json.loads(result.stdout)


def configure(path: Path, apply: bool) -> None:
    if path.is_symlink() or not path.is_file():
        raise ValueError("Expected a regular Compose file")
    original = path.read_text()
    updated = with_rotation(original)
    before = resolved(path)
    descriptor, name = tempfile.mkstemp(prefix=".docker-logging-", suffix=".yaml", dir=path.parent)
    candidate = Path(name)
    try:
        with os.fdopen(descriptor, "w") as stream:
            stream.write(updated)
            stream.flush()
            os.fsync(stream.fileno())
        after = resolved(candidate)
        # Compose retains top-level extension keys in JSON; the anchor is metadata.
        before.pop("x-etran-logging", None)
        after.pop("x-etran-logging", None)
        for service, config in after["services"].items():
            if config.pop("logging", None) != LOGGING:
                raise ValueError("Unexpected logging policy")
            before["services"][service].pop("logging", None)
        if before != after:
            raise ValueError("Compose configuration changed beyond logging")
        if apply and updated != original:
            backup = path.with_name(
                path.name + ".pre-logging-" + datetime.now(UTC).strftime("%Y%m%dT%H%M%SZ")
            )
            # Backups may contain private environment settings: create privately first.
            backup.touch(mode=0o600, exist_ok=False)
            shutil.copyfile(path, backup)
            candidate.replace(path)
        print(f"{path.name}: logging-only configuration verified; applied={apply}")
    finally:
        candidate.unlink(missing_ok=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compose", type=Path, nargs="+", required=True)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    for path in args.compose:
        configure(path.absolute(), args.apply)


if __name__ == "__main__":
    main()
