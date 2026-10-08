"""Bounded child commands, redacted logs and a cross-version workspace lock."""

from __future__ import annotations

import os
import subprocess
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path

from .common import Redactor, ReleaseError, atomic_bytes, signing_password


@contextmanager
def workspace_lock(path: Path) -> Iterator[None]:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a+b") as stream:
        stream.seek(0, os.SEEK_END)
        if stream.tell() == 0:
            stream.write(b"0")
            stream.flush()
        stream.seek(0)
        try:
            if os.name == "nt":
                import msvcrt

                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl

                fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            raise ReleaseError("Another release is using this workspace") from error
        try:
            yield
        finally:
            stream.seek(0)
            if os.name == "nt":
                import msvcrt

                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl

                fcntl.flock(stream.fileno(), fcntl.LOCK_UN)


class Runner:
    def __init__(self, root: Path, env: dict[str, str], logs: Path):
        self.root, self.logs = root, logs
        self.redact = Redactor(env)
        self.env = os.environ.copy()
        # The supplied env is authoritative; inherited signing/registry overrides are removed.
        for key in tuple(self.env):
            if (
                key.startswith(("AR_GENERIC_", "L4TOOLS_SIGN_", "IOT_API_KEY", "SW_SIGN_"))
                or key == "W_SIGN_PFX_PASSWORD"
                or key == "L4TOOLS_POLICY_BOOTSTRAP_IP"
            ):
                self.env.pop(key)
        allowed = {"AR_GENERIC_KEY_ID", "AR_GENERIC_KEY_SECRET", "L4TOOLS_POLICY_BOOTSTRAP_IP"}
        self.env.update({key: value for key, value in env.items() if key in allowed})
        self.env["L4TOOLS_SIGN_PFX"] = env.get("SW_SIGN_PFX", "")
        self.env["L4TOOLS_SIGN_PFX_PASSWORD"] = signing_password(env)

    def run(self, name: str, kind: str, cwd: Path, args: list[str], timeout: int) -> None:
        child_env = self.env.copy()
        if kind == "powershell":
            # Windows PowerShell 5 must discover its own modules, not inherit pwsh 7's.
            for key in tuple(child_env):
                if key.upper() == "PSMODULEPATH":
                    child_env.pop(key)
        if kind == "cmd":
            command = ["cmd.exe", "/d", "/s", "/c", subprocess.list2cmdline(args)]
        elif kind == "powershell":
            command = [
                "powershell.exe",
                "-NoProfile",
                "-NonInteractive",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                *args,
            ]
        else:
            command = args
        print(f"[{name}] running (limit {timeout}s)", flush=True)
        process = subprocess.Popen(
            command,
            cwd=cwd,
            env=child_env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        timed_out = False
        try:
            output, _ = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired, KeyboardInterrupt:
            if os.name == "nt":
                subprocess.run(
                    ["taskkill.exe", "/PID", str(process.pid), "/T", "/F"],
                    capture_output=True,
                    check=False,
                    timeout=30,
                )
            else:
                process.kill()
            output, _ = process.communicate(timeout=30)
            timed_out = True
        output = self.redact(output)
        atomic_bytes(self.logs / f"{name}.log", output.encode("utf-8"))
        if timed_out or process.returncode:
            # Redacted detail stays in the stage log; avoid dumping entire compiler output.
            raise ReleaseError(
                f"Stage {name} {'timed out/interrupted' if timed_out else 'failed'}; see {name}.log"
            )
        print(f"[{name}] passed", flush=True)
