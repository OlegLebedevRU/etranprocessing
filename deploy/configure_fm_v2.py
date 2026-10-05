"""Enable the approved FM v2 drive/read policy without exposing stored credentials."""

import argparse
import os
import shutil
from pathlib import Path

import yaml


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compose", type=Path, required=True)
    args = parser.parse_args()
    config = yaml.safe_load(args.compose.read_text())
    entries = config["services"]["processing-backend"]["env_file"]
    candidates = []
    for entry in entries:
        path = Path(entry if isinstance(entry, str) else entry["path"])
        if not path.is_absolute():
            path = args.compose.parent / path
        if path.is_symlink():
            raise RuntimeError("Refusing symlink env file")
        text = path.read_text()
        if any(line.startswith("FILE_MANAGER_S3_BUCKET=") for line in text.splitlines()):
            candidates.append((path, text))
    if len(candidates) != 1:
        raise RuntimeError("Expected one existing FM environment")
    path, text = candidates[0]
    keys = {"FILE_MANAGER_LOCAL_DRIVES": "true", "FILE_MANAGER_PRIVILEGED_READ": "true"}
    lines = [line for line in text.splitlines() if line.split("=", 1)[0] not in keys]
    updated = "\n".join(lines + [f"{key}={value}" for key, value in keys.items()]) + "\n"
    if updated == text:
        print("FM v2 policy already enabled")
        return
    backup = path.with_name(path.name + ".before-fm-v2")
    if backup.exists():
        raise RuntimeError("Existing policy backup; inspect before overwriting")
    os.umask(0o077)
    shutil.copy2(path, backup)
    backup.chmod(0o600)
    candidate = path.with_name(path.name + ".fm-v2-next")
    with candidate.open("x") as output:
        output.write(updated)
        output.flush()
        os.fsync(output.fileno())
    candidate.chmod(0o600)
    candidate.replace(path)
    print("FM v2 local drives and read-only privileged fallback enabled; user-only write unchanged")


if __name__ == "__main__":
    main()
