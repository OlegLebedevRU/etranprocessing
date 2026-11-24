"""Point the existing MenuBuilder registration screens at a verified manual installer."""

import argparse
import fcntl
import hashlib
import json
import os
import re
import sys
import urllib.request
from pathlib import Path
from tempfile import mkdtemp

from desktop_stream_release import check_environment, compose, environment, mounts

SERVICE = "menubuilder-backend"
REGISTRY = "https://l4tools-generic.ar.cloud.ru/l4tools"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--ci-dir", required=True, type=Path)
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", args.version):
        parser.error("Expected a numeric release version")
    if not re.fullmatch(r"[0-9a-f]{64}", args.sha256):
        parser.error("Expected the verified installer SHA-256")
    url = f"{REGISTRY}/{args.version}/l4setup.exe"
    with urllib.request.urlopen(url, timeout=60) as response:
        if hashlib.file_digest(response, "sha256").hexdigest() != args.sha256:
            raise ValueError("Registry installer differs from the signed release")
    sys.path.insert(0, str(args.ci_dir.resolve()))
    import deploy as release

    def inspect():
        return json.loads(release.docker("inspect", SERVICE, capture=True))[0]

    def verify_status():
        read = (
            "import json,urllib.request; "
            "print(json.dumps(json.load(urllib.request.urlopen("
            "'http://localhost:8000/api/settings/terminals/onboard/status',timeout=5))))"
        )
        status = json.loads(release.docker("exec", SERVICE, "python3", "-c", read, capture=True))
        if status["agent_version"] != args.version or status["agent_release_url"] != url:
            raise RuntimeError("Onboarding status does not match the release")

    os.umask(0o077)
    release.preflight()
    with (release.STATE / "deploy.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        before = inspect()
        previous = environment(before)
        changes = {"AGENT_RELEASE_VERSION": args.version, "AGENT_RELEASE_URL": url}
        if all(previous.get(k) == v for k, v in changes.items()):
            verify_status()
            return
        directory = Path(mkdtemp(prefix="l4desk-onboarding-", dir=release.STATE / "runs"))
        overlay = directory / "registration.json"
        release.atomic_json(overlay, {"services": {SERVICE: {"environment": changes}}})
        command = compose(before, overlay)
        baseline = command[:-2]
        config = json.loads(release.run(*command, "config", "--format", "json", capture=True))
        defaults = json.loads(release.docker("image", "inspect", before["Image"], capture=True))[0]
        check_environment(previous, config["services"][SERVICE]["environment"], environment(defaults), changes)
        if inspect()["Id"] != before["Id"]:
            raise RuntimeError("Concurrent MenuBuilder replacement")
        up = ["up", "-d", "--no-deps", "--no-build", "--pull", "never", SERVICE]
        try:
            release.run(*command, *up)
            release.wait_healthy(SERVICE, SERVICE)
            after = inspect()
            if after["Image"] != before["Image"] or mounts(after) != mounts(before):
                raise RuntimeError("Unexpected image or mounts")
            if environment(after) != dict(previous, **changes):
                raise RuntimeError("Unexpected runtime environment")
            verify_status()
        except Exception:
            release.run(*baseline, *up)
            release.wait_healthy(SERVICE, SERVICE)
            raise
        print(f"Verified onboarding {args.version} {url}")


if __name__ == "__main__":
    main()
