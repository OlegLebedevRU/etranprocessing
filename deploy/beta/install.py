"""Install versioned beta launcher/tools. Releases are triggered manually via launcher.py."""

import argparse
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import tarfile
import urllib.request
from pathlib import Path

SOURCE = Path(__file__).resolve().parent
HOME = Path("/home/github-runner")


def install_tool(path, data, uid, gid, *, upgrade=False):
    if path.is_symlink():
        raise ValueError("Tool destination must not be a symlink")
    if path.exists():
        previous = path.read_bytes()
        if previous == data:
            return
        if not upgrade:
            raise RuntimeError(f"Refusing to overwrite a different pre-existing tool: {path}")
        backup = path.with_name(
            path.name + ".previous-" + hashlib.sha256(previous).hexdigest()[:12]
        )
        if backup.is_symlink() or backup.exists() and backup.read_bytes() != previous:
            raise RuntimeError("Conflicting tool rollback backup")
        backup.write_bytes(previous)
        backup.chmod(0o700)
        if os.name == "posix":
            os.chown(backup, uid, gid)
    temporary = path.with_name(path.name + ".installing")
    with temporary.open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    temporary.chmod(0o755)
    if os.name == "posix":
        os.chown(temporary, uid, gid)
    temporary.replace(path)


def download(spec):
    with urllib.request.urlopen(spec["url"], timeout=120) as response:
        data = response.read()
    if hashlib.sha256(data).hexdigest() != spec["sha256"]:
        raise RuntimeError("Tool download SHA256 mismatch")
    return data


def main():
    import pwd

    parser = argparse.ArgumentParser()
    parser.add_argument("--revision", required=True)
    parser.add_argument("--deploy-key", type=Path, required=True)
    parser.add_argument("--known-hosts", type=Path, required=True)
    parser.add_argument(
        "--upgrade-tools", action="store_true", help="Back up and atomically replace existing tools"
    )
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
        install_tool(path, data, account.pw_uid, account.pw_gid, upgrade=args.upgrade_tools)
    subprocess.run(
        [
            "sudo",
            "-n",
            "-u",
            "github-runner",
            "-H",
            "env",
            f"UV_PYTHON_INSTALL_DIR={state / 'python'}",
            f"UV_CACHE_DIR={state / 'uv-cache'}",
            str(HOME / ".local/bin/uv"),
            "python",
            "install",
            versions["python"]["version"],
        ],
        check=True,
        cwd=HOME,
    )
    target = Path("/opt/etran-beta")
    target.mkdir(mode=0o755, parents=True, exist_ok=True)
    for name in ["launcher.py", "versions.json"]:
        shutil.copyfile(SOURCE / name, target / name)
        (target / name).chmod(0o644)
    (target / "installed-revision").write_text(args.revision + "\n")
    print(
        "Installed. Releases are triggered manually: "
        "sudo -n -u github-runner -H python3 /opt/etran-beta/launcher.py --component <component>. "
        "Provision approved keys and perform the trial first."
    )


if __name__ == "__main__":
    main()
