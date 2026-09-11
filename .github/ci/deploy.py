"""Serialized production deployment without copying secrets or base Compose."""

import argparse
import copy
import io
import json
import os
import re
import shutil
import subprocess
import tarfile
import tempfile
import time
from pathlib import Path

from components import COMPONENTS, REGISTRY

STATE = Path("/home/user1/.etran-ci")
STATIC = Path("/home/user1/MenuBuilder/frontend/dist")


def run(*args, capture=False):
    result = subprocess.run(
        args, check=True, text=True, stdout=subprocess.PIPE if capture else None
    )
    return result.stdout.strip() if capture else ""


def docker(*args, capture=False):
    return run("sudo", "-n", "docker", *args, capture=capture)


def image_reference(component, revision, digest):
    if component not in COMPONENTS:
        raise ValueError("Unknown component")
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("A full Git SHA is required")
    if not re.fullmatch(r"sha256:[0-9a-f]{64}", digest):
        raise ValueError("An immutable registry digest is required")
    return f"{REGISTRY}/{component}@{digest}"


def update_override(previous, service, image):
    result = copy.deepcopy(previous)
    result.setdefault("services", {})[service] = {"image": image}
    return result


def atomic_json(path, data):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(data, indent=2) + "\n")
    temporary.replace(path)


def compose_command(component, override):
    spec = COMPONENTS[component]
    return [
        "sudo",
        "-n",
        "docker",
        "compose",
        "--project-name",
        spec["project"],
        "-f",
        spec["compose"],
        "-f",
        str(override),
    ]


def publish_static(source, destination):
    files = list(source.rglob("*"))
    if not (source / "index.html").is_file() or not (source / "assets").is_dir():
        raise ValueError("Incomplete frontend artifact")
    if any(path.is_symlink() for path in files):
        raise ValueError("Symlinks are not allowed in the frontend artifact")
    for path in sorted(
        files, key=lambda item: item.relative_to(source).as_posix() == "index.html"
    ):
        target = destination / path.relative_to(source)
        if path.is_dir():
            target.mkdir(parents=True, exist_ok=True)
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = target.with_name(target.name + ".ci-tmp")
        shutil.copyfile(path, temporary)
        temporary.chmod(0o644)
        temporary.replace(target)


def health(component, container):
    if component in {"processingbackend", "menubuilder-backend"}:
        endpoint = "api/health" if component == "processingbackend" else "openapi.json"
        docker(
            "exec",
            container,
            "python",
            "-c",
            "import urllib.request; "
            f"r=urllib.request.urlopen('http://127.0.0.1:8000/{endpoint}', timeout=5); "
            "assert r.status == 200",
        )
    elif component == "l4media-ingress":
        docker(
            "exec",
            container,
            "curl",
            "--fail",
            "--silent",
            "--max-time",
            "5",
            "http://127.0.0.1:9100/health",
        )
    elif component == "l4media-nginx":
        docker("exec", container, "nginx", "-t")
    state = json.loads(
        docker("inspect", "--format", "{{json .State}}", container, capture=True)
    )
    if not state["Running"] or state.get("Restarting"):
        raise RuntimeError("Container is not running")


def wait_healthy(component, container):
    for attempt in range(24):
        try:
            health(component, container)
            return
        except (subprocess.CalledProcessError, RuntimeError):
            if attempt == 23:
                raise
            time.sleep(5)


def deploy_service(component, image, revision):
    spec = COMPONENTS[component]
    override = STATE / (spec["project"] + "-images.json")
    previous = (
        json.loads(override.read_text()) if override.exists() else {"services": {}}
    )
    base = [
        "sudo",
        "-n",
        "docker",
        "compose",
        "--project-name",
        spec["project"],
        "-f",
        spec["compose"],
    ]
    container = run(*base, "ps", "-q", spec["service"], capture=True)
    if not container or "\n" in container:
        raise RuntimeError("Expected exactly one existing service container")
    old_image = docker(
        "inspect", "--format", "{{.Config.Image}}", container, capture=True
    )
    previous = update_override(previous, spec["service"], old_image)
    candidate = STATE / (component + "-candidate.json")
    atomic_json(candidate, update_override(previous, spec["service"], image))
    command = compose_command(component, candidate)
    run(*command, "config", "--quiet")
    if component == "processingbackend":
        # Migrate using the new image before replacing the running application.
        # Image rollback deliberately never downgrades the database schema.
        run(
            *command,
            "run",
            "--rm",
            "--no-deps",
            "--entrypoint",
            "alembic",
            spec["service"],
            "upgrade",
            "head",
        )
    try:
        run(
            *command,
            "up",
            "-d",
            "--no-deps",
            "--no-build",
            "--pull",
            "never",
            spec["service"],
        )
        container = run(*command, "ps", "-q", spec["service"], capture=True)
        wait_healthy(component, container)
        actual = docker("inspect", "--format", "{{.Image}}", container, capture=True)
        expected = docker(
            "image", "inspect", "--format", "{{.Id}}", image, capture=True
        )
        if actual != expected:
            raise RuntimeError("Running image does not match the published digest")
    except Exception:
        print(
            "Deployment failed; restoring the previous service image "
            "(no schema downgrade).",
            flush=True,
        )
        atomic_json(candidate, previous)
        run(
            *command,
            "up",
            "-d",
            "--no-deps",
            "--no-build",
            "--pull",
            "never",
            spec["service"],
        )
        container = run(*command, "ps", "-q", spec["service"], capture=True)
        wait_healthy(component, container)
        raise
    atomic_json(override, update_override(previous, spec["service"], image))
    atomic_json(STATE / (component + "-previous.json"), previous)
    print(f"Verified {component}: revision={revision} image={image}", flush=True)


def deploy_frontend(image, revision):
    mounts = json.loads(
        docker("inspect", "--format", "{{json .Mounts}}", "nginx-default", capture=True)
    )
    if not any(
        mount["Source"] == str(STATIC)
        and mount["Destination"] == "/usr/share/nginx/html"
        for mount in mounts
    ):
        raise RuntimeError("Unexpected frontend mount; refusing to change live files")
    with tempfile.TemporaryDirectory(dir=STATE) as temporary:
        release = Path(temporary) / "dist"
        container = docker("create", "--entrypoint", "/unused", image, capture=True)
        try:
            archive = subprocess.check_output(
                ["sudo", "-n", "docker", "cp", container + ":/dist/.", "-"]
            )
            with tarfile.open(fileobj=io.BytesIO(archive)) as bundle:
                bundle.extractall(release, filter="data")
        finally:
            docker("rm", container)
        backup = STATE / ("frontend-before-" + revision + ".html")
        shutil.copyfile(STATIC / "index.html", backup)
        try:
            publish_static(release, STATIC)
            served = docker(
                "exec",
                "nginx-default",
                "curl",
                "--fail",
                "--silent",
                "--show-error",
                "--max-time",
                "15",
                "--resolve",
                "dev.leo4.ru:3000:127.0.0.1",
                "https://dev.leo4.ru:3000/",
                capture=True,
            )
            if served != (release / "index.html").read_text().strip():
                raise RuntimeError("Nginx is not serving the new frontend")
        except Exception:
            shutil.copyfile(backup, STATIC / "index.html.ci-tmp")
            (STATIC / "index.html.ci-tmp").replace(STATIC / "index.html")
            raise
    print(
        f"Verified frontend: revision={revision}; nginx-default was not restarted.",
        flush=True,
    )


def preflight():
    # The public address is NATed; SSH pins the host key and public destination.
    if run("hostname", capture=True) != "etranprocessing":
        raise RuntimeError("This script is restricted to production 87.242.100.34")
    available = next(
        int(line.split()[1])
        for line in Path("/proc/meminfo").read_text().splitlines()
        if line.startswith("MemAvailable:")
    )
    disk = shutil.disk_usage("/")
    if (
        available <= 300 * 1024
        or disk.used / disk.total >= 0.9
        or os.getloadavg()[0] >= 2.0
    ):
        raise RuntimeError(
            "Production resource safety thresholds exceeded; no deployment performed"
        )
    docker("compose", "version", "--short")


def main():
    import fcntl

    parser = argparse.ArgumentParser()
    parser.add_argument("component", choices=COMPONENTS)
    parser.add_argument("revision")
    parser.add_argument("digest")
    args = parser.parse_args()
    image = image_reference(args.component, args.revision, args.digest)
    preflight()
    STATE.mkdir(mode=0o700, parents=True, exist_ok=True)
    with (STATE / "deploy.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        docker("pull", image)
        revision = docker(
            "image",
            "inspect",
            "--format",
            '{{index .Config.Labels "org.opencontainers.image.revision"}}',
            image,
            capture=True,
        )
        if revision != args.revision:
            raise RuntimeError(
                "Image revision label does not match the workflow commit"
            )
        if args.component == "menubuilder-frontend":
            deploy_frontend(image, args.revision)
        else:
            deploy_service(args.component, image, args.revision)
        atomic_json(
            STATE / (args.component + "-release.json"),
            {"revision": args.revision, "image": image},
        )


if __name__ == "__main__":
    main()
