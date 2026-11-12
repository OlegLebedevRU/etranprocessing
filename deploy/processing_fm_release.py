"""Scoped FM server rollout preserving all current Compose overlays and runtime policy.

Run alongside this revision's .github/ci/deploy.py and components.py. Never prints
environment values. Only the chosen service is recreated; rollback keeps policy.
"""

import argparse
import importlib
import json
import os
import sys
from pathlib import Path

from desktop_stream_release import check_environment, compose, environment, mounts


def verify_runtime(before, after, expected_image):
    if after["Image"] != expected_image:
        raise RuntimeError("Running image mismatch")
    if environment(after) != environment(before):
        raise RuntimeError("Runtime environment changed")
    if mounts(after) != mounts(before):
        raise RuntimeError("Runtime mounts changed")
    if after["HostConfig"]["NetworkMode"] != before["HostConfig"]["NetworkMode"]:
        raise RuntimeError("Runtime network changed")


def main():
    import fcntl

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ci-dir", type=Path, required=True)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--digest", required=True)
    parser.add_argument("--expected-container", required=True)
    parser.add_argument(
        "--component",
        choices=("processingbackend", "menubuilder-backend", "app1"),
        default="processingbackend",
    )
    parser.add_argument("--migrate", action="store_true")
    args = parser.parse_args()
    sys.path.insert(0, str(args.ci_dir))
    release = importlib.import_module("deploy")

    os.umask(0o077)
    if args.migrate and args.component != "processingbackend":
        raise ValueError("Only PB owns shared migrations")
    image = release.image_reference(args.component, args.revision, args.digest)
    service = release.DEPLOY_COMPONENTS[args.component]["service"]
    release.preflight()
    with (release.STATE / "deploy.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)

        def inspect(name):
            return json.loads(release.docker("inspect", name, capture=True))[0]

        def neighbors():
            ids = release.docker("ps", "-q", capture=True).splitlines()
            return {
                obj["Name"]: obj["Id"]
                for obj in json.loads(release.docker("inspect", *ids, capture=True))
                if obj["Name"] != "/" + service
            }

        before = inspect(service)
        if before["Id"] != args.expected_container:
            raise RuntimeError("Concurrent PB replacement")
        previous_neighbors = neighbors()
        release.docker("pull", image)
        image_obj = json.loads(release.docker("image", "inspect", image, capture=True))[0]
        if image_obj["Config"]["Labels"]["org.opencontainers.image.revision"] != args.revision:
            raise RuntimeError("Image revision mismatch")
        directory = release.STATE / "runs" / "fm-on-demand" / args.revision / args.component
        directory.mkdir(parents=True, exist_ok=True, mode=0o700)
        forward, rollback = directory / "images.json", directory / "rollback.json"
        # Preserve existing literal dollars against Compose interpolation.
        escaped = {
            key: value.replace("$", "$$")
            for key, value in environment(before).items()
            if "$" in value
        }
        for path, reference in ((forward, image), (rollback, before["Config"]["Image"])):
            release.atomic_json(
                path,
                {
                    "services": {
                        service: {
                            "image": reference,
                            "environment": escaped,
                        }
                    }
                },
            )
        command = compose(before, forward)
        rendered = json.loads(release.run(*command, "config", "--format", "json", capture=True))
        values = rendered["services"][service].get("environment", {})
        defaults = dict(item.split("=", 1) for item in image_obj["Config"].get("Env", []))
        check_environment(environment(before), values, defaults, {})
        if inspect(service)["Id"] != args.expected_container:
            raise RuntimeError("Concurrent PB replacement before rollout")
        if args.migrate:
            # Deploy the MB034/035 bridge before invoking this option. Image
            # rollback never downgrades this additive schema.
            release.run(
                *command,
                "run",
                "--rm",
                "--no-deps",
                "--entrypoint",
                "alembic",
                service,
                "upgrade",
                "head",
            )
        try:
            release.run(
                *command,
                "up",
                "-d",
                "--no-deps",
                "--no-build",
                "--pull",
                "never",
                service,
            )
            release.wait_healthy(args.component, service)
            verify_runtime(before, inspect(service), image_obj["Id"])
            if neighbors() != previous_neighbors:
                raise RuntimeError("Neighbor containers changed")
        except Exception:
            release.run(
                *compose(before, rollback),
                "up",
                "-d",
                "--no-deps",
                "--no-build",
                "--pull",
                "never",
                service,
            )
            release.wait_healthy(args.component, service)
            verify_runtime(before, inspect(service), before["Image"])
            raise
        release.atomic_json(
            directory / "verified.json",
            {
                "revision": args.revision,
                "image": image,
                "previous_image": before["Config"]["Image"],
                "container": inspect(service)["Id"],
                "component": args.component,
                "environment_unchanged": True,
                "mounts_unchanged": True,
                "neighbors_unchanged": True,
            },
        )
        print("Verified " + args.component + " revision=" + args.revision + " image=" + image)


if __name__ == "__main__":
    main()
