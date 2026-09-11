"""Append the reviewed beta public key on production, preserving existing keys."""

import argparse
import fcntl
import os
import re
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("public_key", type=Path)
    args = parser.parse_args()
    if subprocess.check_output(["hostname"], text=True).strip() != "etranprocessing":
        raise RuntimeError("Production host required")
    public = args.public_key.read_text().strip()
    if not re.fullmatch(r"ssh-ed25519 [A-Za-z0-9+/=]+ etran-beta-deploy", public):
        raise ValueError("Only the reviewed beta ED25519 key is accepted")
    entry = ("restrict " + public).encode()
    descriptor = os.open(
        Path.home() / ".ssh/authorized_keys", os.O_RDWR | os.O_APPEND | os.O_NOFOLLOW
    )
    with os.fdopen(descriptor, "r+b") as stream:
        fcntl.flock(stream, fcntl.LOCK_EX)
        previous = stream.read()
        if entry in previous.splitlines():
            print("Beta key already present; unchanged")
            return
        stream.write(
            (b"\n" if previous and not previous.endswith(b"\n") else b"")
            + entry
            + b"\n"
        )
        stream.flush()
        os.fsync(stream.fileno())
    print("Reviewed beta key appended; existing entries preserved")


if __name__ == "__main__":
    main()
