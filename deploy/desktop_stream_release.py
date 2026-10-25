"""Scoped desktop rollout, preserving the running Compose cohort and environment.

Use with the exact revision's standard CI deploy.py/components.py. Only the
MenuBuilder image and the reviewed desktop environment keys may change. app1's
image, signer, camera-runtime and every other running container are preserved.
"""

import argparse
import hashlib
import json
import os
import sys
from pathlib import Path, PurePosixPath

FLAGS = {
    "menubuilder-backend": {
        "VIDEO_CONTRACT_V2_ENABLED": "true",
        "L4DESK_SESSION_ORCHESTRATION_ENABLED": "true",
        "VIDEO_PROXY_RTP_PORT": "5004",
        "VIDEO_PROXY_RTCP_PORT": "5005",
    },
    "app1": {
        "APP_CONFIG__REMOTE_INPUT__VIDEO_V2_ENABLED": "true",
        "APP_CONFIG__REMOTE_INPUT__VIDEO_HEVC_CHAIN_READY": "true",
    },
}


def environment(obj):
    return dict(item.split("=", 1) for item in obj["Config"]["Env"])


def compose(obj, overlay):
    labels = obj["Config"]["Labels"]
    files = labels["com.docker.compose.project.config_files"].split(",")
    if not files or any(not PurePosixPath(path).is_absolute() for path in files):
        raise ValueError("Expected the current absolute Compose file list")
    command = [
        "sudo",
        "-n",
        "docker",
        "compose",
        "--project-name",
        labels["com.docker.compose.project"],
    ]
    for filename in files:
        command.extend(["-f", filename])
    return [*command, "-f", str(overlay)]


def candidate(obj, service, image, restore=False):
    old = environment(obj)
    values = {key: old.get(key) for key in FLAGS[service]} if restore else FLAGS[service]
    return {"services": {service: {"image": image, "environment": values}}}


def check_environment(old, rendered, image_defaults, changes):
    effective = dict(image_defaults)
    for key, value in rendered.items():
        if value is None:
            effective.pop(key, None)
        else:
            effective[key] = str(value)
    expected = dict(old, **changes)
    if effective != expected:
        keys = sorted(
            key
            for key in effective.keys() | expected.keys()
            if effective.get(key) != expected.get(key)
        )
        raise RuntimeError("Unapproved environment differences: " + ", ".join(keys))


def mounts(obj):
    return sorted((m["Type"], m["Source"], m["Destination"], m["RW"]) for m in obj["Mounts"])


def main():
    import fcntl

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ci-dir", type=Path, required=True)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--backend-digest", required=True)
    parser.add_argument("--frontend-digest", required=True)
    parser.add_argument("--expected-backend", required=True)
    parser.add_argument("--expected-app1", required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.ci_dir))
    import deploy as release

    os.umask(0o077)
    backend_image = release.image_reference(
        "menubuilder-backend", args.revision, args.backend_digest
    )
    frontend_image = release.image_reference(
        "menubuilder-frontend", args.revision, args.frontend_digest
    )
    release.preflight()
    with (release.STATE / "deploy.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)

        def inspect(name):
            return json.loads(release.docker("inspect", name, capture=True))[0]

        def running():
            ids = release.docker("ps", "-q", capture=True).splitlines()
            return {
                obj["Name"]: obj["Id"]
                for obj in json.loads(release.docker("inspect", *ids, capture=True))
            }

        before = {name: inspect(name) for name in FLAGS}
        for name, expected in [
            ("menubuilder-backend", args.expected_backend),
            ("app1", args.expected_app1),
        ]:
            if before[name]["Id"] != expected:
                raise RuntimeError("Concurrent replacement of " + name)
        neighbors = {
            name: identity for name, identity in running().items() if name.lstrip("/") not in FLAGS
        }
        lease_check = """import asyncio
from core.config import settings
from redis.asyncio import Redis
async def check():
    client = Redis.from_url(str(settings.redis.url), decode_responses=True)
    try:
        slots = [key async for key in client.scan_iter(match='l4d:lease:active:*')]
        if slots:
            raise SystemExit('Active remote leases; release refused')
    finally:
        await client.aclose()
asyncio.run(check())
"""
        media_check = """import asyncio
from app.services.media_orchestrator_client import media_orchestrator_client
async def check():
    try:
        value = await media_orchestrator_client.video_v2('capabilities')
        ready = (value.get('contract_version') == 3 and value.get('dependencies_ready')
                 and value.get('hevc_runtime_verified') and 'h265' in value.get('codecs', []))
    except Exception:
        ready = False
    if not ready:
        raise SystemExit('Media3 HEVC chain readiness not confirmed')
asyncio.run(check())
"""
        release.docker(
            "exec", "-e", "PYTHONDONTWRITEBYTECODE=1", "app1", "python", "-c", lease_check
        )
        release.docker(
            "exec",
            "-e",
            "PYTHONDONTWRITEBYTECODE=1",
            "menubuilder-backend",
            "python",
            "-c",
            media_check,
        )
        directory = release.STATE / "runs" / "desktop-stream" / args.revision
        directory.mkdir(parents=True, exist_ok=True, mode=0o700)
        release.atomic_json(
            directory / "before.json",
            {
                name: {
                    "id": obj["Id"],
                    "image": obj["Image"],
                    "environment_sha256": hashlib.sha256(
                        json.dumps(environment(obj), sort_keys=True).encode()
                    ).hexdigest(),
                    "compose_files": obj["Config"]["Labels"][
                        "com.docker.compose.project.config_files"
                    ],
                }
                for name, obj in before.items()
            },
        )
        for image in (backend_image, frontend_image):
            release.docker("pull", image)
            obj = json.loads(release.docker("image", "inspect", image, capture=True))[0]
            if obj["Config"]["Labels"]["org.opencontainers.image.revision"] != args.revision:
                raise RuntimeError("Image revision mismatch")
        commands = {}
        restores = {}
        for name, obj in before.items():
            image = backend_image if name == "menubuilder-backend" else obj["Config"]["Image"]
            forward, backward = directory / (name + ".json"), directory / (name + "-rollback.json")
            release.atomic_json(forward, candidate(obj, name, image))
            release.atomic_json(
                backward, candidate(obj, name, obj["Config"]["Image"], restore=True)
            )
            commands[name], restores[name] = compose(obj, forward), compose(obj, backward)
            config = json.loads(
                release.run(*commands[name], "config", "--format", "json", capture=True)
            )
            defaults = environment(
                json.loads(release.docker("image", "inspect", image, capture=True))[0]
            )
            check_environment(
                environment(obj),
                config["services"][name].get("environment", {}),
                defaults,
                FLAGS[name],
            )
        applied = []
        try:
            # The code update is compatible with the old provider; enable the
            # unchanged provider image's desktop policy only after BFF is healthy.
            for name in ("menubuilder-backend", "app1"):
                if inspect(name)["Id"] != before[name]["Id"]:
                    raise RuntimeError("Concurrent replacement of " + name)
                release.docker(
                    "exec", "-e", "PYTHONDONTWRITEBYTECODE=1", "app1", "python", "-c", lease_check
                )
                applied.append(name)
                release.run(
                    *commands[name], "up", "-d", "--no-deps", "--no-build", "--pull", "never", name
                )
                release.wait_healthy(name, name)
                obj = inspect(name)
                expected_image = (
                    json.loads(release.docker("image", "inspect", backend_image, capture=True))[0][
                        "Id"
                    ]
                    if name == "menubuilder-backend"
                    else before[name]["Image"]
                )
                if (
                    obj["Image"] != expected_image
                    or environment(obj) != dict(environment(before[name]), **FLAGS[name])
                    or mounts(obj) != mounts(before[name])
                ):
                    raise RuntimeError(
                        "Runtime image/environment/mount verification failed for " + name
                    )
                after_running = running()
                if any(
                    after_running.get(neighbor) != identity
                    for neighbor, identity in neighbors.items()
                ):
                    raise RuntimeError("Neighbor container changed during release")
            release.deploy_frontend(frontend_image, args.revision)
        except Exception:
            for name in reversed(applied):
                release.run(
                    *restores[name], "up", "-d", "--no-deps", "--no-build", "--pull", "never", name
                )
                release.wait_healthy(name, name)
            raise
        release.atomic_json(
            directory / "release.json",
            {
                "revision": args.revision,
                "backend_image": backend_image,
                "frontend_image": frontend_image,
                "app1_image_unchanged": before["app1"]["Image"],
                "neighbors_unchanged": neighbors,
                "desktop_flags": FLAGS,
            },
        )
        print("Desktop code/config release verified; live browser acceptance remains pending.")


if __name__ == "__main__":
    main()
