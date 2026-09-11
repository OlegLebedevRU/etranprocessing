"""Standalone beta release worker. Invoked from a locked, immutable checkout."""

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from datetime import UTC, datetime
from pathlib import Path

from components import COMPONENTS, REGISTRY, select_components
from deploy import atomic_json, image_reference

ROOT = Path(__file__).resolve().parents[2]
HOME = Path("/home/github-runner/etran-ci")
PRODUCTION = "user1@87.242.100.34"
BUILDKIT = "moby/buildkit@sha256:040d34121c27906c4ff9ac152a30d52bf2c5d328d3bb748916bb3d2743c02528"


def execute(*args, capture=False, cwd=ROOT):
    print("+ " + shlex.join(str(arg) for arg in args), flush=True)
    result = subprocess.run(
        [str(arg) for arg in args],
        cwd=cwd,
        check=True,
        text=True,
        stdout=subprocess.PIPE if capture else None,
    )
    return result.stdout.strip() if capture else ""


def require_revision(revision):
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("Expected full Git SHA")
    return revision


def affected(component, paths):
    if any(
        path.startswith("deploy/beta/") and not path.endswith(".md") for path in paths
    ):
        return True
    return any(
        item["name"] == component and item["trigger"]
        for item in select_components(paths, "push")["include"]
    )


def needs_release(component, revision, state, changed_paths):
    previous = state["releases"].get(component, {})
    baseline = previous.get("revision", state["bootstrap_revision"])
    require_revision(baseline)
    return baseline != revision and affected(
        component, changed_paths(baseline, revision)
    )


def successful_state(state, component, artifact):
    updated = json.loads(json.dumps(state))
    updated["releases"][component] = artifact
    return updated


def git_paths(before, after):
    return execute(
        "git",
        "diff",
        "--name-only",
        before,
        after,
        "--",
        ".github",
        "deploy/beta",
        "MenuBuilder",
        "ProcessingBackend/backend",
        "shared",
        "l4media",
        ".dockerignore",
        ".gitattributes",
        capture=True,
    ).splitlines()


def check_resources():
    disk = shutil.disk_usage(HOME)
    available = next(
        int(line.split()[1])
        for line in Path("/proc/meminfo").read_text().splitlines()
        if line.startswith("MemAvailable:")
    )
    if (
        disk.free < 2 * 1024**3
        or disk.used / disk.total >= 0.90
        or available < 800 * 1024
    ):
        raise RuntimeError(
            "Builder needs >=2 GiB free disk, <90% disk usage and >=800 MiB available RAM; no automatic prune"
        )


def prepare_builder():
    result = subprocess.run(
        ["docker", "buildx", "inspect", "etran-beta"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if result.returncode:
        execute(
            "docker",
            "buildx",
            "create",
            "--name",
            "etran-beta",
            "--driver",
            "docker-container",
            "--driver-opt",
            "image=" + BUILDKIT,
            "--driver-opt",
            "memory=1536m",
            "--buildkitd-config",
            ROOT / "deploy/beta/buildkitd.toml",
        )
    execute("docker", "buildx", "inspect", "etran-beta", "--bootstrap")


def test_component(component):
    execute(sys.executable, "-m", "unittest", "discover", "-s", ".github/ci", "-v")
    if component in {"processingbackend", "menubuilder-backend"}:
        directory = ROOT / COMPONENTS[component]["directory"]
        uv = "/home/github-runner/.local/bin/uv"
        execute(uv, "sync", "--locked", cwd=directory)
        execute(
            uv,
            "run",
            "--locked",
            "python",
            "-m",
            "compileall",
            "-q",
            "app",
            "tests",
            cwd=directory,
        )
        execute(uv, "run", "--locked", "ruff", "check", "app", cwd=directory)
        execute(
            uv, "run", "--locked", "ruff", "format", "--check", "app", cwd=directory
        )
        execute(uv, "run", "--locked", "pyright", "app", cwd=directory)
        execute(uv, "run", "--locked", "pytest", cwd=directory)


def load_artifact(path, component, revision):
    artifact = json.loads(path.read_text())
    if artifact["component"] != component or artifact["revision"] != revision:
        raise ValueError("Artifact identity mismatch")
    image_reference(component, revision, artifact["digest"])
    return artifact


def build(component, revision):
    directory = HOME / "artifacts" / component
    directory.mkdir(parents=True, exist_ok=True)
    record = directory / (revision + ".json")
    if record.exists():
        artifact = load_artifact(record, component, revision)
        execute(
            "docker",
            "buildx",
            "imagetools",
            "inspect",
            image_reference(component, revision, artifact["digest"]),
        )
        print("Reusing the already tested/published immutable artifact", flush=True)
        return artifact
    test_component(component)
    prepare_builder()
    stamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%S%fZ")
    # Each attempt has a unique tag: even a lost push acknowledgement cannot overwrite it.
    tag = revision + "-" + stamp
    metadata = directory / (tag + "-metadata.json")
    spec = COMPONENTS[component]
    execute(
        "docker",
        "buildx",
        "build",
        "--builder",
        "etran-beta",
        "--platform",
        "linux/amd64",
        "--file",
        spec["directory"] + "/Dockerfile",
        "--tag",
        f"{REGISTRY}/{component}:{tag}",
        "--label",
        "org.opencontainers.image.revision=" + revision,
        "--label",
        "org.opencontainers.image.source=https://github.com/OlegLebedevRU/etranprocessing",
        "--label",
        "ru.leo4.etran.flow-revision=" + revision,
        "--metadata-file",
        metadata,
        "--provenance=mode=min",
        "--push",
        spec["context"],
    )
    digest = json.loads(metadata.read_text())["containerimage.digest"]
    image_reference(component, revision, digest)
    artifact = {
        "component": component,
        "revision": revision,
        "flow_revision": revision,
        "launcher_revision": require_revision(os.environ["ETRAN_LAUNCHER_REVISION"]),
        "tag": tag,
        "digest": digest,
        "built_at": stamp,
    }
    atomic_json(record, artifact)
    return artifact


def deploy_artifact(component, revision, artifact):
    ssh = [
        "-i",
        str(HOME / "keys/deploy"),
        "-o",
        "IdentitiesOnly=yes",
        "-o",
        "BatchMode=yes",
        "-o",
        "StrictHostKeyChecking=yes",
        "-o",
        "UserKnownHostsFile=" + str(HOME / "keys/known_hosts"),
        "-o",
        "ConnectTimeout=15",
    ]
    remote = f"/home/user1/.etran-ci/runs/beta/{revision}/{component}"
    execute("ssh", "-n", *ssh, PRODUCTION, "install -d -m 700 " + shlex.quote(remote))
    execute(
        "scp",
        *ssh,
        ROOT / ".github/ci/deploy.py",
        ROOT / ".github/ci/components.py",
        PRODUCTION + ":" + remote + "/",
    )
    execute(
        "ssh",
        "-n",
        *ssh,
        PRODUCTION,
        shlex.join(
            ["python3", remote + "/deploy.py", component, revision, artifact["digest"]]
        ),
    )


def release(component, revision, state, state_file, deploy=True):
    artifact = build(component, revision)
    if not deploy:
        return state
    deploy_artifact(component, revision, artifact)
    state = successful_state(state, component, artifact)
    atomic_json(state_file, state)
    with (HOME / "releases.jsonl").open("a") as journal:
        journal.write(
            json.dumps({**artifact, "deployed_at": datetime.now(UTC).isoformat()})
            + "\n"
        )
        journal.flush()
        os.fsync(journal.fileno())
    return state


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--component", choices=[*COMPONENTS, "all", "auto"], default="auto"
    )
    parser.add_argument("--build-only", action="store_true")
    parser.add_argument("--initialize", action="store_true")
    args = parser.parse_args()
    revision = require_revision(execute("git", "rev-parse", "HEAD", capture=True))
    execute("git", "diff", "--exit-code", "HEAD", "--")
    state_file = HOME / "state.json"
    if args.initialize:
        if state_file.exists():
            raise RuntimeError(
                "State already exists; refusing to reset successful releases"
            )
        atomic_json(
            state_file, {"schema": 1, "bootstrap_revision": revision, "releases": {}}
        )
        return
    state = json.loads(state_file.read_text())
    if state.get("schema") != 1:
        raise ValueError("Unsupported state schema")
    for component in COMPONENTS:
        selected = args.component in {component, "all"} or (
            args.component == "auto"
            and needs_release(component, revision, state, git_paths)
        )
        if selected:
            check_resources()
            state = release(component, revision, state, state_file, not args.build_only)


if __name__ == "__main__":
    main()
