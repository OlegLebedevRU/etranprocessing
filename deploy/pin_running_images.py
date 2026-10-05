"""Pin verified running service images in the persistent Compose configuration."""

import argparse
import json
import re
import shutil
import subprocess
from datetime import UTC, datetime
from pathlib import Path

import yaml


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compose", type=Path, required=True)
    parser.add_argument("--services", nargs="+", required=True)
    args = parser.parse_args()
    allowed = {"processing-backend", "menubuilder-backend", "app1"}
    if not set(args.services) <= allowed:
        raise ValueError("Only reviewed FM service owners may be pinned")
    config = yaml.safe_load(args.compose.read_text())
    for name in args.services:
        container = config["services"][name]["container_name"]
        item = json.loads(
            subprocess.check_output(
                ["sudo", "-n", "docker", "inspect", container], text=True
            )
        )[0]
        image = item["Config"]["Image"]
        if not re.fullmatch(r"[^\s@]+@sha256:[0-9a-f]{64}", image):
            raise ValueError("Running image is not pinned to an immutable digest")
        actual = json.loads(
            subprocess.check_output(
                ["sudo", "-n", "docker", "image", "inspect", image], text=True
            )
        )[0]
        if actual["Id"] != item["Image"] or item["State"]["Status"] != "running":
            raise ValueError("Running service/image mismatch")
        config["services"][name]["image"] = image
        config["services"][name].pop("build", None)
    backup = args.compose.with_name(
        args.compose.name + ".fm-images-" + datetime.now(UTC).strftime("%Y%m%dT%H%M%SZ")
    )
    shutil.copy2(args.compose, backup)
    backup.chmod(0o600)
    candidate = args.compose.with_suffix(".fm-images.yaml")
    candidate.write_text(yaml.safe_dump(config, sort_keys=False))
    candidate.chmod(0o600)
    subprocess.run(
        ["sudo", "-n", "docker", "compose", "-f", str(candidate), "config", "--quiet"],
        check=True,
    )
    candidate.replace(args.compose)
    print("Verified running digests persisted; no service recreated.")


if __name__ == "__main__":
    main()
