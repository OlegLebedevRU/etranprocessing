"""Provision FM environment on the deployment host without logging credentials."""

import argparse
import json
import os
import secrets
import shutil
import subprocess
from datetime import UTC, datetime
from pathlib import Path
from urllib.parse import urlsplit

import yaml


def container_env(name):
    item = json.loads(
        subprocess.check_output(["sudo", "-n", "docker", "inspect", name], text=True)
    )[0]
    return dict(value.split("=", 1) for value in item["Config"]["Env"] if "=" in value)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compose", type=Path, required=True)
    parser.add_argument("--env-file", type=Path, required=True)
    parser.add_argument("--s3-config", type=Path, required=True)
    parser.add_argument("--pb-base-url", required=True)
    args = parser.parse_args()
    if urlsplit(args.pb_base_url).scheme not in ("http", "https"):
        raise ValueError("Invalid internal PB base URL")
    os.umask(0o077)
    mb = container_env("menubuilder-backend")
    iot = container_env("app1")
    iot_url = mb.get("LEO4_INTERNAL_API_BASE_URL") or mb.get("IOT_RPC_BASE_URL")
    iot_key = (
        mb.get("LEO4_INTERNAL_SERVICE_TOKEN")
        or mb.get("INTERNAL_SERVICE_KEY")
        or mb.get("IOT_RPC_SERVICE_TOKEN")
    )
    if (
        not iot_url
        or not iot_key
        or iot_key != iot.get("APP_CONFIG__AUTH__INTERNAL_SERVICE_KEY")
    ):
        raise RuntimeError("Existing MB/IoT internal authentication mismatch")
    values = json.loads(args.s3_config.read_text())
    expected = {
        "FILE_MANAGER_S3_ENDPOINT",
        "FILE_MANAGER_S3_REGION",
        "FILE_MANAGER_S3_BUCKET",
        "FILE_MANAGER_S3_ACCESS_KEY",
        "FILE_MANAGER_S3_SECRET_KEY",
        "FILE_MANAGER_READ_ROOTS",
        "FILE_MANAGER_WRITE_ROOTS",
    }
    if (
        set(values) != expected
        or not all(values.values())
        or not values["FILE_MANAGER_S3_ENDPOINT"].startswith("https://")
    ):
        raise ValueError("Invalid FM storage configuration")
    old = {}
    if args.env_file.exists():
        old = dict(
            line.split("=", 1) for line in args.env_file.read_text().splitlines()
        )
    values.update(
        FILE_MANAGER_SERVICE_KEY=old.get("FILE_MANAGER_SERVICE_KEY")
        or secrets.token_urlsafe(48),
        FILE_MANAGER_IOT_URL=iot_url,
        FILE_MANAGER_IOT_KEY=iot_key,
        FILE_MANAGER_PB_URL=args.pb_base_url,
    )
    args.env_file.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.env_file.with_suffix(".tmp")
    # Compose raw format preserves JSON list backslashes and treats credentials literally.
    temporary.write_text("".join(f"{k}={v}\n" for k, v in sorted(values.items())))
    temporary.chmod(0o600)
    if old:
        backup_env = args.env_file.with_suffix(".previous.env")
        shutil.copy2(args.env_file, backup_env)
        backup_env.chmod(0o600)
    temporary.replace(args.env_file)
    mb_env = args.env_file.with_name(args.env_file.stem + "-mb.env")
    mb_env.write_text(
        "".join(
            f"{k}={values[k]}\n"
            for k in ("FILE_MANAGER_SERVICE_KEY", "FILE_MANAGER_PB_URL")
        )
    )
    mb_env.chmod(0o600)
    config = yaml.safe_load(args.compose.read_text())
    for service in ("processing-backend", "menubuilder-backend"):
        entry = config["services"][service]
        files = entry.get("env_file", [])
        if isinstance(files, str):
            files = [files]
        target_env = args.env_file if service == "processing-backend" else mb_env
        files = [
            item
            for item in files
            if (item if isinstance(item, str) else item["path"]) != str(target_env)
        ]
        entry["env_file"] = [*files, {"path": str(target_env), "format": "raw"}]
    backup = args.compose.with_name(
        args.compose.name + ".fm-" + datetime.now(UTC).strftime("%Y%m%dT%H%M%SZ")
    )
    shutil.copy2(args.compose, backup)
    backup.chmod(0o600)
    candidate = args.compose.with_suffix(".fm-candidate.yaml")
    candidate.write_text(yaml.safe_dump(config, sort_keys=False))
    candidate.chmod(0o600)
    subprocess.run(
        ["sudo", "-n", "docker", "compose", "-f", str(candidate), "config", "--quiet"],
        check=True,
    )
    candidate.replace(args.compose)
    args.s3_config.unlink()
    print(
        "FM environment provisioned; Compose validated. Service recreation belongs to the release flow."
    )


if __name__ == "__main__":
    main()
