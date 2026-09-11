"""Install versioned beta launcher/tools. Does not enable automatic deployment."""

import argparse
import hashlib
import io
import json
import os
import pwd
import re
import shutil
import subprocess
import tarfile
import urllib.request
from pathlib import Path

SOURCE = Path(__file__).resolve().parent
HOME = Path("/home/github-runner")


def download(spec):
    with urllib.request.urlopen(spec["url"], timeout=120) as response:
        data = response.read()
    if hashlib.sha256(data).hexdigest() != spec["sha256"]:
        raise RuntimeError("Tool download SHA256 mismatch")
    return data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--revision", required=True)
    parser.add_argument("--deploy-key", type=Path, required=True)
    parser.add_argument("--known-hosts", type=Path, required=True)
    args = parser.parse_args()
    if (
        os.geteuid() != 0
        or subprocess.check_output(["hostname"], text=True).strip() != "leo4-free-tier"
    ):
        raise RuntimeError("Installer requires root on the explicitly selected builder")
    if not re.fullmatch(r"[0-9a-f]{40}", args.revision):
        raise ValueError("Expected reviewed source commit")
    account = pwd.getpwnam("github-runner")
    state = HOME / "etran-ci"
    for directory in [
        state,
        state / "keys",
        HOME / ".local/bin",
        HOME / ".docker/cli-plugins",
    ]:
        directory.mkdir(parents=True, exist_ok=True)
        directory.chmod(0o700)
        os.chown(directory, account.pw_uid, account.pw_gid)
    for source, name in [
        (args.deploy_key, "deploy"),
        (args.known_hosts, "known_hosts"),
    ]:
        path = state / "keys" / name
        if path.exists() and path.read_bytes() != source.read_bytes():
            raise RuntimeError(
                f"Key file already exists; use the explicit rotation procedure: {path}"
            )
        if not path.exists():
            shutil.copyfile(source, path)
        path.chmod(0o600)
        os.chown(path, account.pw_uid, account.pw_gid)
    versions = json.loads((SOURCE / "versions.json").read_text())
    tools = {HOME / ".docker/cli-plugins/docker-buildx": download(versions["buildx"])}
    with tarfile.open(fileobj=io.BytesIO(download(versions["uv"]))) as archive:
        member = archive.extractfile("uv-x86_64-unknown-linux-gnu/uv")
        if member is None:
            raise ValueError("uv binary missing from release archive")
        tools[HOME / ".local/bin/uv"] = member.read()
    for path, data in tools.items():
        if path.exists() and path.read_bytes() != data:
            raise RuntimeError(
                f"Refusing to overwrite a different pre-existing tool: {path}"
            )
        path.write_bytes(data)
        path.chmod(0o755)
        os.chown(path, account.pw_uid, account.pw_gid)
    target = Path("/opt/etran-beta")
    target.mkdir(mode=0o755, parents=True, exist_ok=True)
    for name in ["launcher.py", "versions.json"]:
        shutil.copyfile(SOURCE / name, target / name)
        (target / name).chmod(0o644)
    for name in ["etran-beta.service", "etran-beta.timer"]:
        shutil.copyfile(SOURCE / name, Path("/etc/systemd/system") / name)
        (Path("/etc/systemd/system") / name).chmod(0o644)
    (target / "installed-revision").write_text(args.revision + "\n")
    subprocess.run(
        [
            "systemd-analyze",
            "verify",
            "/etc/systemd/system/etran-beta.service",
            "/etc/systemd/system/etran-beta.timer",
        ],
        check=True,
    )
    subprocess.run(["systemctl", "daemon-reload"], check=True)
    print(
        "Installed; timer NOT enabled. Provision approved keys and perform the trial first."
    )


if __name__ == "__main__":
    main()
