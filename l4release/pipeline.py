"""Ordered native preparation, signing, verification and immutable publication."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import shutil
import struct
import subprocess
import sys
import time
import tomllib
from datetime import UTC, datetime
from pathlib import Path
from types import ModuleType

from .common import (
    ReleaseError,
    atomic_bytes,
    atomic_json,
    contained,
    file_hash,
    registry_value,
    version_value,
)
from .dependencies import dependency_lock, verify_openh264
from .runner import Runner, workspace_lock


def read_config(root: Path) -> dict:
    config = tomllib.loads((root / "l4release/config.toml").read_text(encoding="utf-8"))
    if config["schema"] != 1:
        raise ReleaseError("Unsupported release config")
    registry_value(config["registry"])
    names = set()
    for step in config["steps"]:
        if step["name"] in names or step["kind"] not in ("cmd", "powershell"):
            raise ReleaseError("Duplicate/unsupported build step")
        names.add(step["name"])
        contained(root, step["cwd"])
        if not 0 < step["timeout"] <= 3600:
            raise ReleaseError("Invalid build timeout")
    return config


def publisher_module(root: Path) -> ModuleType:
    spec = importlib.util.spec_from_file_location(
        "l4tools_publisher", root / "deploy/publish_l4tools.py"
    )
    if spec is None or spec.loader is None:
        raise ReleaseError("Existing publisher is unavailable")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def git(root: Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(root), *args], encoding="utf-8").strip()


def input_snapshot(root: Path, config: dict) -> dict[str, str]:
    paths = git(
        root,
        "ls-files",
        "-z",
        "--cached",
        "--others",
        "--exclude-standard",
        "--",
        *config["source_paths"],
    ).split("\0")
    excluded = set(config["generated_inputs"])
    result = {}
    for name in sorted(set(paths) | set(config["required_assets"])):
        if (
            not name
            or name in excluded
            or any(
                part.lower() in ("bin", "obj", "dist", "__pycache__") for part in Path(name).parts
            )
        ):
            continue
        path = contained(root, name)
        result[name] = file_hash(path) if path.is_file() else "missing"
    # Ignored native dependency libraries are inputs too, never treated as build output.
    for name in config["required_assets"]:
        path = contained(root, name)
        result[name] = file_hash(path) if path.is_file() else "missing"
    for name in dependency_lock(root)["files"]:
        relative = f"tools/l4capture/vendor/openh264/{name}"
        path = contained(root, relative)
        result[relative] = file_hash(path) if path.is_file() else "missing"
    return result


def snapshot_digest(snapshot: dict[str, str]) -> str:
    return hashlib.sha256(json.dumps(snapshot, sort_keys=True).encode()).hexdigest()


def output_snapshot(root: Path, paths: list[str]) -> dict[str, str]:
    result = {}
    for name in paths:
        path = contained(root, name)
        if not path.exists():
            raise ReleaseError(f"Missing output: {name}")
        files = sorted(path.rglob("*")) if path.is_dir() else [path]
        for file in files:
            if file.is_file():
                result[file.relative_to(root).as_posix()] = file_hash(file)
    if not result:
        raise ReleaseError("Empty output checkpoint")
    return result


def outputs_match(root: Path, snapshot: dict[str, str]) -> bool:
    return bool(snapshot) and all(
        contained(root, name).is_file() and file_hash(contained(root, name)) == digest
        for name, digest in snapshot.items()
    )


def check_pe_outputs(root: Path, names: list[str], started: float) -> None:
    for name in names:
        path = contained(root, name)
        if not path.is_file() or path.stat().st_mtime < started - 2:
            raise ReleaseError(f"Build did not produce a fresh output: {name}")
        with path.open("rb") as stream:
            header = stream.read(64)
            if len(header) < 64 or header[:2] != b"MZ":
                raise ReleaseError(f"Invalid PE executable: {name}")
            stream.seek(struct.unpack_from("<I", header, 60)[0])
            pe = stream.read(6)
        expected = 0x8664 if "/x64/" in name else 0x14C
        if len(pe) != 6 or pe[:4] != b"PE\0\0" or struct.unpack_from("<H", pe, 4)[0] != expected:
            raise ReleaseError(f"Wrong native architecture: {name}")


class Pipeline:
    def __init__(self, root: Path, version: str, env: dict[str, str], env_path: Path):
        self.root, self.version = root.resolve(), version_value(version)
        self.config = read_config(self.root)
        self.dist = contained(self.root, self.config["dist"])
        self.work = self.dist / ".release" / self.version
        self.env, self.env_path = env.copy(), env_path
        pfx = self.env.get("SW_SIGN_PFX")
        if pfx:
            pfx_path = Path(pfx)
            if not pfx_path.is_absolute():
                pfx_path = env_path.parent / pfx_path
            self.env["SW_SIGN_PFX"] = str(pfx_path.resolve())
        self.runner = Runner(self.root, self.env, self.work / "logs")
        self.report_path = self.work / "report.json"
        self.report: dict = {}
        self.inputs: dict[str, str] = {}

    def plan(self, *, signed: bool = True, publish: bool = True) -> dict:
        steps = [
            {"name": step["name"], "timeout_seconds": step["timeout"]}
            for step in self.config["steps"]
        ]
        steps += [
            {"name": "unsigned-manifest-verification", "timeout_seconds": 180},
            {"name": "unsigned-payload-verification", "timeout_seconds": 180},
        ]
        if signed:
            steps += [
                {"name": "sign-and-repack-setup", "timeout_seconds": 900},
                {"name": "signed-payload-and-authenticode-verification", "timeout_seconds": 300},
            ]
        if publish:
            steps += [{"name": "immutable-publication-and-full-download", "timeout_seconds": 900}]
        assets = [
            name
            for name in self.config["required_assets"]
            if not contained(self.root, name).is_file()
        ]
        return {
            "schema": 1,
            "version": self.version,
            "steps": steps,
            "maximum_command_seconds": sum(step["timeout_seconds"] for step in steps),
            "budget_note": (
                "Command limits only; hashing, filesystem I/O and cleanup are additional"
            ),
            "missing_assets": assets,
            "terminal_gate": "not implemented; never invoked",
            "promotion": "not implemented; candidate only",
        }

    def save(self) -> None:
        self.report["updated_at"] = datetime.now(UTC).isoformat()
        atomic_json(self.report_path, self.report)

    def checked_inputs(self) -> None:
        if input_snapshot(self.root, self.config) != self.inputs:
            raise ReleaseError(
                "Release sources/assets changed during the pipeline; start a new run"
            )

    def command(
        self,
        name: str,
        kind: str,
        cwd: Path,
        args: list[str],
        timeout: int,
        outputs: list[str] | None = None,
    ) -> None:
        self.checked_inputs()
        record: dict[str, object] = {
            "name": name,
            "status": "running",
            "started_at": datetime.now(UTC).isoformat(),
        }
        self.report["steps"].append(record)
        self.save()
        started = time.monotonic()
        output_started = time.time()
        try:
            self.runner.run(name, kind, cwd, args, timeout)
            self.checked_inputs()
            check_pe_outputs(self.root, outputs or [], output_started)
        except Exception:
            record["status"] = "failed"
            raise
        finally:
            record["duration_seconds"] = round(time.monotonic() - started, 3)
            self.save()
        record["status"] = "passed"
        self.save()

    def ps(self, name: str, script: str, *args: str, timeout: int = 180) -> None:
        self.command(
            name,
            "powershell",
            self.root,
            [str(contained(self.root, f"tools/release/{script}")), *args],
            timeout,
        )

    def checkpoint(self, phase: str) -> None:
        paths = [
            f"tools/dist/{name}" for name in ("l4setup.exe", "l4tools-release.json", "SHA256SUMS")
        ]
        paths += [
            "tools/dist/.stage",
            "tools/l4setup/res/payload_x86.bin",
            "tools/l4setup/res/payload_x64.bin",
            "tools/l4setup/bin/l4setup.exe",
        ]
        if phase == "signing":
            # A failed timestamp/sign operation may leave partial signatures or ZIPs.
            # Retry from exact retained staging, not by rebuilding signed components.
            paths = ["tools/dist/.stage"]
        self.report["checkpoints"][phase] = {
            "inputs_digest": snapshot_digest(self.inputs),
            "outputs": output_snapshot(self.root, paths),
        }
        self.save()

    def resumable(self, phase: str) -> bool:
        saved = self.report["checkpoints"].get(phase, {})
        return saved.get("inputs_digest") == snapshot_digest(self.inputs) and outputs_match(
            self.root,
            saved.get("outputs", {}),
        )

    def preflight(self, signed: bool, publish: bool) -> None:
        if os.name != "nt":
            raise ReleaseError("Native release execution requires Windows; plan/tests are portable")
        if not shutil.which("powershell.exe") or not shutil.which("cmd.exe"):
            raise ReleaseError("Windows PowerShell/cmd are required")
        missing = self.plan(signed=signed, publish=publish)["missing_assets"]
        if missing:
            raise ReleaseError("Missing native build assets: " + ", ".join(missing))
        verify_openh264(self.root)
        if signed:
            pfx = self.env.get("SW_SIGN_PFX", "")
            if not pfx or not Path(pfx).is_file():
                raise ReleaseError(
                    "SW_SIGN_PFX must name an existing certificate outside the repository"
                )
            if Path(pfx).resolve().is_relative_to(self.root):
                raise ReleaseError("Signing PFX must be outside the repository")
        if publish and not all(
            self.env.get(name) for name in ("AR_GENERIC_KEY_ID", "AR_GENERIC_KEY_SECRET")
        ):
            raise ReleaseError("Set AR_GENERIC_KEY_ID and AR_GENERIC_KEY_SECRET in sw_sign.env")

    def prepare(self) -> None:
        # No signing during native compilation: signing belongs to its own checkpoint.
        old_pfx = self.runner.env.pop("L4TOOLS_SIGN_PFX", None)
        try:
            for step in self.config["steps"]:
                args = [arg.replace("{version}", self.version) for arg in step["args"]]
                if step["kind"] == "powershell":
                    args[0] = str(contained(self.root, args[0]))
                self.command(
                    step["name"],
                    step["kind"],
                    contained(self.root, step["cwd"]),
                    args,
                    step["timeout"],
                    step.get("outputs"),
                )
            shutil.copy2(self.root / "tools/l4setup/bin/l4setup.exe", self.dist / "l4setup.exe")
            self.ps("unsigned-manifest", "New-ReleaseManifest.ps1", "-Version", self.version)
            self.ps("unsigned-payload", "Test-PayloadIntegrity.ps1")
            publisher_module(self.root).verify_artifacts(self.dist, allow_dirty=True)
            self.checkpoint("prepared")
        finally:
            if old_pfx is not None:
                self.runner.env["L4TOOLS_SIGN_PFX"] = old_pfx

    def finalize_provenance(self) -> None:
        """Generated binaries/version headers do not make clean source inputs dirty."""
        self.checked_inputs()
        manifest_path = self.dist / "l4tools-release.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
        if manifest["version"] != self.version:
            raise ReleaseError("Wrong prepared manifest version")
        manifest["dirty"] = False
        manifest["git_sha"] = self.report["source_revision"]
        manifest["source_checkpoint"] = {
            "schema": 1,
            "inputs_sha256": snapshot_digest(self.inputs),
            "clean_at_start": True,
            "build_options_sha256": self.report["build_options_digest"],
        }
        atomic_json(manifest_path, manifest)
        sums = "".join(
            f"{file_hash(self.dist / name)}  {name}\n"
            for name in ("l4setup.exe", "l4tools-release.json")
        )
        atomic_bytes(self.dist / "SHA256SUMS", sums.encode("ascii"))

    def execute(self, *, signed: bool, publish: bool) -> dict:
        with workspace_lock(self.dist / ".release/workspace.lock"):
            self.inputs = input_snapshot(self.root, self.config)
            options_digest = snapshot_digest(
                {"L4TOOLS_POLICY_BOOTSTRAP_IP": self.env.get("L4TOOLS_POLICY_BOOTSTRAP_IP", "")}
            )
            if self.report_path.exists():
                self.report = json.loads(self.report_path.read_text(encoding="utf-8"))
                if self.report.get("inputs_digest") != snapshot_digest(self.inputs):
                    raise ReleaseError(
                        "Checkpoint inputs changed; use a new version or explicitly remove "
                        "its local checkpoint after reviewing the change"
                    )
                if self.report.get("build_options_digest") != options_digest:
                    raise ReleaseError("Build options changed; use a fresh release checkpoint")
                if self.report.get("version") != self.version or self.report.get("schema") != 1:
                    raise ReleaseError("Invalid release checkpoint")
            else:
                self.report = {
                    "schema": 1,
                    "version": self.version,
                    "source_revision": git(self.root, "rev-parse", "HEAD"),
                    "inputs_digest": snapshot_digest(self.inputs),
                    "build_options_digest": options_digest,
                    "clean_at_start": not bool(git(self.root, "status", "--porcelain")),
                    "steps": [],
                    "checkpoints": {},
                }
            try:
                self.report["status"] = "running"
                self.save()
                if signed and not self.report["clean_at_start"]:
                    raise ReleaseError(
                        "Signed release requires a clean source checkpoint; prepare is "
                        "available for local validation, never publication"
                    )
                self.preflight(signed, publish)
                if signed:
                    identity = file_hash(Path(self.env["SW_SIGN_PFX"]))
                    previous_identity = self.report.get("signing_certificate_file_sha256")
                    if previous_identity and previous_identity != identity:
                        raise ReleaseError(
                            "Signing identity changed; do not replace an existing release"
                        )
                    self.report["signing_certificate_file_sha256"] = identity
                    self.save()
                if not any(self.resumable(phase) for phase in ("signed", "prepared", "signing")):
                    self.prepare()
                if signed:
                    if not self.resumable("signed"):
                        try:
                            self.ps(
                                "sign",
                                "Complete-SignedRelease.ps1",
                                "-PfxPath",
                                self.env["SW_SIGN_PFX"],
                                "-Version",
                                self.version,
                                "-TimestampUrl",
                                self.config["timestamp_url"],
                                timeout=900,
                            )
                        except Exception:
                            self.checkpoint("signing")
                            raise
                        self.finalize_provenance()
                        self.checkpoint("signed")
                    # Always reverify, even when a signing checkpoint is reused.
                    self.ps(
                        "verify-signed",
                        "Verify-SignedRelease.ps1",
                        "-Version",
                        self.version,
                        "-PfxPath",
                        self.env["SW_SIGN_PFX"],
                        timeout=300,
                    )
                    publisher_module(self.root).verify_artifacts(self.dist)
                    if publish:
                        self.command(
                            "publish",
                            "python",
                            self.root,
                            [
                                sys.executable,
                                str(self.root / "deploy/publish_l4tools.py"),
                                "--env-file",
                                str(self.env_path),
                                "--registry",
                                self.config["registry"],
                                "--version",
                                self.version,
                                "publish",
                                str(self.dist),
                            ],
                            900,
                        )
                self.report["status"] = (
                    "published_candidate" if publish else "signed" if signed else "prepared"
                )
                self.report["terminal_gate"] = "not_run"
                self.report["promotion"] = "not_run"
                self.save()
            except Exception as error:
                self.report["status"] = "failed"
                self.report["error"] = self.runner.redact(str(error))
                self.save()
                raise
            return self.report
