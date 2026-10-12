"""Configure private event75 delivery in the two supplied runtime env files."""

import argparse
import os
import secrets
from pathlib import Path
from urllib.parse import urlsplit


def value(path: Path, key: str) -> str:
    for line in path.read_text().splitlines():
        if line.startswith(key + "="):
            return line.split("=", 1)[1].strip().strip("\"'")
    return ""


def update(path: Path, entries: dict[str, str]) -> None:
    if path.is_symlink() or not path.is_file():
        raise ValueError("Expected a regular runtime env file")
    lines = [
        line for line in path.read_text().splitlines() if line.partition("=")[0] not in entries
    ]
    lines.extend(f"{key}={item}" for key, item in entries.items())
    temporary = path.with_name(path.name + ".inventory.tmp")
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "w") as stream:
        stream.write("\n".join(lines) + "\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--menubuilder-env", type=Path, required=True)
    parser.add_argument("--iot-env", type=Path, required=True)
    parser.add_argument("--url", required=True)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    target = urlsplit(args.url)
    if (
        target.scheme not in {"http", "https"}
        or not target.hostname
        or target.username
        or target.password
        or target.query
        or target.fragment
    ):
        raise ValueError("Expected a credential-free internal receiver URL")
    if not target.path.endswith("/api/internal/v1/terminal-inventory/events"):
        raise ValueError("Unexpected receiver path")
    token = value(args.menubuilder_env, "INVENTORY_DELIVERY_TOKEN") or secrets.token_urlsafe(48)
    if not args.apply:
        print("Validated event75-only delivery configuration; private values omitted")
        return
    # Receiver first: a partial update denies delivery until the producer is configured.
    update(args.menubuilder_env, {"INVENTORY_DELIVERY_TOKEN": token})
    update(
        args.iot_env,
        {
            "APP_CONFIG__INVENTORY_DELIVERY__TOKEN": token,
            "APP_CONFIG__INVENTORY_DELIVERY__URL": args.url,
        },
    )
    print("Private event75 receiver and producer configured; values omitted")


if __name__ == "__main__":
    main()
