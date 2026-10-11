"""Set native CAPTCHA in protected host env files without printing contents."""

import argparse
import os
import secrets
from pathlib import Path


def enable(path: Path, backup_dir: Path) -> None:
    if not path.is_absolute() or not path.is_file() or path.is_symlink():
        raise ValueError("Expected an existing absolute regular env-file path")
    backup = backup_dir / (path.parent.name + "-" + path.name + ".before")
    original = path.read_bytes()
    descriptor = os.open(backup, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "wb") as stream:
        stream.write(original)
    lines = original.decode().splitlines()
    lines = [
        line
        for line in lines
        if not line.startswith(("CAPTCHA_PROVIDER=", "CAPTCHA_NATIVE_SECRET="))
    ]
    lines.extend(["CAPTCHA_PROVIDER=native", "CAPTCHA_NATIVE_SECRET=" + secrets.token_urlsafe(48)])
    path.write_text("\n".join(lines) + "\n")
    path.chmod(0o600)
    print(f"Native CAPTCHA enabled in {path}; protected backup={backup}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--backup-dir", type=Path, required=True)
    parser.add_argument("env_files", type=Path, nargs="+")
    args = parser.parse_args()
    args.backup_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
    for env_file in args.env_files:
        enable(env_file, args.backup_dir)
