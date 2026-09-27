"""Authorized opaque host-local transfer; never print the provider credential.

Run with access to sudo docker and the private MenuBuilder env file. This does
not modify/recreate the provider. A caller-supplied private rollback directory
must already exist and contain no prior backup with this name.
"""

import argparse
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--env-file", type=Path, required=True)
    parser.add_argument("--backup-dir", type=Path, required=True)
    parser.add_argument("--provider-container", required=True)
    args = parser.parse_args()
    target = args.env_file.resolve(strict=True)
    backup_dir = args.backup_dir.resolve(strict=True)
    if not target.is_file() or not backup_dir.is_dir():
        raise SystemExit("Private env file and rollback directory required")
    if backup_dir.stat().st_mode & 0o077:
        raise SystemExit("Rollback directory must have mode 700")
    backup = backup_dir / "menubuilder.env"
    if backup.exists():
        raise SystemExit("Refusing to overwrite an existing rollback checkpoint")
    # Capture privately: no shell expansion, command argument or output carries a token.
    result = subprocess.run(
        [
            "sudo",
            "docker",
            "inspect",
            "--format",
            "{{json .Config.Env}}",
            args.provider_container,
        ],
        capture_output=True,
        check=True,
        text=True,
    )
    entries = json.loads(result.stdout)
    tokens = [
        entry.partition("=")[2]
        for entry in entries
        if entry.startswith("SERVICE_AUTH_TOKEN=")
    ]
    if (
        len(tokens) != 1
        or len(tokens[0]) < 32
        or any(c in tokens[0] for c in "\r\n\x00")
    ):
        raise SystemExit("Provider service credential unavailable or invalid")
    original = target.read_bytes()
    lines = original.decode("utf-8").splitlines()
    key = "PROCESSING_BACKEND_SERVICE_TOKEN"
    lines = [line for line in lines if not line.startswith(key + "=")]
    lines.append(key + "=" + tokens[0])
    old_umask = os.umask(0o077)
    try:
        with backup.open("xb") as stream:
            stream.write(original)
        shutil.copystat(target, backup)
        backup.chmod(0o600)
        fd, temp_name = tempfile.mkstemp(prefix=".mb-pin-", dir=target.parent)
        try:
            with os.fdopen(fd, "wb") as stream:
                stream.write(("\n".join(lines) + "\n").encode("utf-8"))
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temp_name, target)
        finally:
            Path(temp_name).unlink(missing_ok=True)
    finally:
        os.umask(old_umask)
    print("MenuBuilder private PIN credential configured; rollback copy saved")


if __name__ == "__main__":
    main()
