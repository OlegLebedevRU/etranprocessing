"""Small installed launcher; executes the worker from the fetched main commit."""

import fcntl
import os
import subprocess
import sys
from pathlib import Path

HOME = Path("/home/github-runner/etran-ci")
REMOTE = "https://github.com/OlegLebedevRU/etranprocessing.git"


def run(*args, capture=False):
    result = subprocess.run(
        args, check=True, text=True, stdout=subprocess.PIPE if capture else None
    )
    return result.stdout.strip() if capture else ""


def main():
    if os.geteuid() == 0:
        raise RuntimeError("Run as github-runner, not root")
    os.environ.update(
        {
            "GIT_TERMINAL_PROMPT": "0",
            "PYTHONUNBUFFERED": "1",
            "CI": "1",
            "UV_CACHE_DIR": str(HOME / "uv-cache"),
            "UV_PYTHON_INSTALL_DIR": str(HOME / "python"),
            "ETRAN_LAUNCHER_REVISION": Path(__file__)
            .with_name("installed-revision")
            .read_text()
            .strip(),
        }
    )
    with (HOME / "builder.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print(
                "Another beta release is active; not starting a duplicate", flush=True
            )
            return 75
        repository = HOME / "repository.git"
        if not repository.exists():
            run("git", "clone", "--bare", "--filter=blob:none", REMOTE, str(repository))
        run(
            "git",
            "--git-dir",
            str(repository),
            "fetch",
            "origin",
            "+refs/heads/main:refs/heads/main",
        )
        revision = run(
            "git",
            "--git-dir",
            str(repository),
            "rev-parse",
            "refs/heads/main",
            capture=True,
        )
        checkout = HOME / "checkouts" / revision
        if not checkout.exists():
            run(
                "git",
                "--git-dir",
                str(repository),
                "worktree",
                "add",
                "--detach",
                "--no-checkout",
                str(checkout),
                revision,
            )
            run(
                "git",
                "-C",
                str(checkout),
                "sparse-checkout",
                "set",
                ".github",
                "deploy/beta",
                "MenuBuilder",
                "ProcessingBackend/backend",
                "ProcessingBackend/nginx-mutual-legacy/nginx-configs",
                "shared",
                "l4media",
                "nginx-configs",
                "docs",
            )
            run("git", "-C", str(checkout), "checkout", "--detach", revision)
        run("python3", str(checkout / ".github/ci/beta.py"), *sys.argv[1:])
    return 0


if __name__ == "__main__":
    sys.exit(main())
