"""Reviewed private trial catalog, shared native executor and protected receipt sealing."""

from __future__ import annotations

import ctypes
import os
import subprocess
import tempfile
import time
import uuid
from pathlib import Path

from .acceptance_trial import reserved_revision
from .acceptance_windows import held, roots, windows_platform
from .catalog import catalog_bytes
from .catalog_artifacts import published_release
from .catalog_evidence import (
    FILETIME_EPOCH,
    _run,
    canonical,
    fields,
    integer,
    profile,
    sha,
    unique_json,
    validate_acceptance,
)
from .catalog_publisher import CatalogPublisher, _checkpoint, _file
from .catalog_registry import digest
from .catalog_state import protect_state
from .common import ReleaseError, atomic_bytes, atomic_json, version_value
from .metadata import key_id, load_signing_key, sign_bytes, verify_bytes, verify_embedded_key
from .pipeline import read_config
from .runner import Runner, workspace_lock

NATIVE_WAIT_SECONDS = 156 * 60


def _elevated() -> bool:
    return os.name == "nt" and bool(ctypes.WinDLL("shell32").IsUserAnAdmin())


class NativeExecutor:
    def __init__(self, source, directory: Path):
        self.source, self.directory = source, directory
        self.programs, self.data = roots()
        self.image = self.programs / "setup" / source.version / "l4setup.exe"

    def run(self, intent: dict, catalog: Path, authorization: Path) -> tuple[bytes, dict]:
        if not _elevated():
            raise ReleaseError("Acceptance runner requires an elevated physical console")
        operation = self.data / "update" / "operations" / intent["operation_id"]
        export_path = operation / "acceptance.result.json"
        if export_path.exists():
            with held(export_path, self.data, system=True, export=True) as read:
                data = read(8192)
                return data, unique_json(data)
        args = [
            str(self.image),
            "--acceptance-local",
            "--source-version",
            intent["source_version"],
            "--target-version",
            intent["target_version"],
            "--operation",
            intent["operation_id"],
            "--arch",
            intent["arch"],
            "--trial-catalog",
            str(catalog),
            "--trial-signature",
            str(catalog) + ".sig",
            "--intent",
            str(authorization),
            "--intent-signature",
            str(authorization) + ".sig",
        ]
        env = {
            k: v
            for k, v in os.environ.items()
            if not k.startswith(
                (
                    "SW_SIGN_",
                    "W_SIGN_",
                    "L4TOOLS_SIGN_",
                    "AR_GENERIC_",
                    "IOT_API_KEY",
                    "L4TOOLS_METADATA_",
                )
            )
        }
        with held(self.image, self.programs) as read:
            image = read(int(self.source.producer["size"]))
            if (
                len(image) != self.source.producer["size"]
                or digest(image) != self.source.producer["sha256"]
            ):
                raise ReleaseError(
                    "Installed local acceptance executor differs from signed source root"
                )
            log = self.directory / f"{intent['operation_id']}.native.log"
            with log.open("xb") as output:
                try:
                    process = subprocess.Popen(
                        args, env=env, stdout=output, stderr=subprocess.STDOUT, cwd=self.programs
                    )
                    returncode = process.wait(timeout=NATIVE_WAIT_SECONDS)
                except subprocess.TimeoutExpired as error:
                    # Python timeout must NEVER kill the native parent/controller.
                    raise ReleaseError(
                        "Native acceptance exceeded its bounded wait; preserve operation"
                    ) from error
            if returncode:
                raise ReleaseError(
                    "Native acceptance did not produce a verified retired/cleared result"
                )
        with held(export_path, self.data, system=True, export=True) as read:
            data = read(8192)
            return data, unique_json(data)


class AcceptancePipeline:
    def __init__(self, root: Path, env: dict[str, str], env_path: Path):
        self.publisher = CatalogPublisher(root, env, env_path)
        self.root, self.env, self.env_path = self.publisher.root, env, self.publisher.env_path

    def _endpoints(self, source: str, target: str, scratch: Path):
        return {
            version: published_release(
                self.root,
                self.publisher.registry,
                version,
                scratch / version,
                self.publisher.public,
            )
            for version in (source, target)
        }

    def plan(self, source: str, target: str, arch: str = "x86", *, now: int | None = None) -> dict:
        source, target = version_value(source), version_value(target)
        if source == target or arch not in {"x86", "x64"}:
            raise ReleaseError("Invalid local acceptance endpoints/architecture")
        now = int(time.time()) if now is None else now
        facts = windows_platform()
        native_profile = profile(facts)
        if arch == "x64" and facts["native_arch"] != "x64":
            raise ReleaseError("Native architecture differs")
        with tempfile.TemporaryDirectory(prefix="l4acceptance-plan-") as temporary:
            scratch = Path(temporary)
            base, _ = self.publisher._base(self.publisher._floor(), now, scratch)
            revision = (
                max(
                    base["revision"] if base else 0,
                    reserved_revision(self.publisher.state, self.publisher.public),
                )
                + 1
            )
            endpoints = self._endpoints(source, target, scratch)
            scope = {
                "from": endpoints[source][arch].identity(),
                "to": endpoints[target][arch].identity(),
                "arch": arch,
                "platform": facts,
                "terminal_id": 773,
                "tenant_id": 1,
                "revision": revision,
            }
            document = {
                "schema": 1,
                "key_id": key_id(self.publisher.public),
                "revision": revision,
                "issued_at": now,
                "expires_at": now + 86400,
                "stable": None,
                "releases": [
                    {
                        "version": v,
                        "manifest_sha256": endpoints[v][arch].root_sha256,
                        "revoked": False,
                    }
                    for v in (source, target)
                ],
                "transitions": [
                    {
                        "from": source,
                        "to": target,
                        "arch": arch,
                        "profile": native_profile,
                        "evidence_sha256": digest(
                            canonical({"kind": "l4tools-local-acceptance-intent", **scope})
                        ),
                    }
                ],
            }
            return {
                "schema": 1,
                "kind": "l4tools-local-acceptance",
                "key_id": key_id(self.publisher.public),
                "run_id": str(uuid.uuid4()),
                **scope,
                "catalog": document,
                "operations": [
                    {"operation_id": str(uuid.uuid4()), "force_rollback": force}
                    for force in (True, False)
                ],
                "source_checkpoint": _checkpoint(self.root),
            }

    def _validate_plan(self, plan: dict, now: int, scratch: Path):
        fields(
            plan,
            {
                "schema",
                "kind",
                "key_id",
                "run_id",
                "from",
                "to",
                "arch",
                "platform",
                "terminal_id",
                "tenant_id",
                "revision",
                "catalog",
                "operations",
                "source_checkpoint",
            },
        )
        if (
            plan["kind"] != "l4tools-local-acceptance"
            or type(plan["schema"]) is not int
            or plan["schema"] != 1
        ):
            raise ReleaseError("Reviewed local acceptance plan differs")
        try:
            run_id = uuid.UUID(plan["run_id"])
        except (ValueError, TypeError, AttributeError) as error:
            raise ReleaseError("Reviewed local acceptance run UUID differs") from error
        if not run_id.int or str(run_id) != plan["run_id"]:
            raise ReleaseError("Reviewed local acceptance run UUID differs")
        actual = self.plan(
            plan["from"]["version"],
            plan["to"]["version"],
            plan["arch"],
            now=plan["catalog"]["issued_at"],
        )
        resumed = False
        reserved = reserved_revision(self.publisher.state, self.publisher.public)
        if reserved == plan["revision"]:
            retained = self.publisher.state / f"trial-{reserved}" / "plan.json"
            protect_state(retained)
            if _file(retained, 65535) != canonical(plan):
                raise ReleaseError("Another exact reviewed private trial is reserved")
            actual["revision"] = plan["revision"]
            actual["catalog"] = plan["catalog"]
            resumed = True
        for name in ("run_id", "operations"):
            actual[name] = plan[name]
        if canonical(actual) != canonical(plan) or not plan["source_checkpoint"]["clean"]:
            raise ReleaseError("Reviewed local acceptance inputs/roots/profile/floor changed")
        operations = plan["operations"]
        if not isinstance(operations, list) or len(operations) != 2:
            raise ReleaseError("Exactly forced-first and normal shared-executor attempts required")
        seen = set()
        for index, item in enumerate(operations):
            fields(item, {"operation_id", "force_rollback"})
            value = uuid.UUID(item["operation_id"])
            if (
                str(value) != item["operation_id"]
                or not value.int
                or item["operation_id"] in seen
                or type(item["force_rollback"]) is not bool
                or item["force_rollback"] != (index == 0)
            ):
                raise ReleaseError("Local acceptance operation intent differs")
            seen.add(item["operation_id"])
        if not plan["catalog"]["issued_at"] <= now < plan["catalog"]["expires_at"]:
            raise ReleaseError("Reviewed local acceptance plan expired/future")
        return self._endpoints(plan["from"]["version"], plan["to"]["version"], scratch), resumed

    def _export(
        self,
        data: bytes,
        value: dict,
        plan: dict,
        intent: dict,
        authorization: bytes,
        source,
        target,
    ):
        fields(
            value,
            {
                "schema",
                "kind",
                "operation_id",
                "arch",
                "profile",
                "platform",
                "terminal_id",
                "tenant_id",
                "from",
                "to",
                "force_rollback",
                "catalog",
                "intent_sha256",
                "authorization_sha256",
                "executor",
                "run",
                "clear_generation",
                "configs",
                "epochs",
                "started_utc",
                "exported_utc",
            },
        )
        if (
            type(value["schema"]) is not int
            or value["schema"] != 1
            or value["kind"] != "l4tools-local-acceptance-result"
            or value["operation_id"] != intent["operation_id"]
            or value["authorization_sha256"] != digest(authorization)
            or type(value["force_rollback"]) is not bool
            or value["force_rollback"] != intent["force_rollback"]
            or any(
                value[name] != plan[name]
                for name in ("from", "to", "arch", "platform", "terminal_id", "tenant_id")
            )
            or value["profile"] != profile(plan["platform"])
        ):
            raise ReleaseError("Protected native acceptance export does not match signed intent")
        sha(value["intent_sha256"])
        integer(value["clear_generation"])
        catalog_data = (
            __import__("json").dumps(plan["catalog"], indent=2, ensure_ascii=True) + "\n"
        ).encode("ascii")
        if value["catalog"] != {"revision": plan["revision"], "sha256": digest(catalog_data)}:
            raise ReleaseError("Actual native floor/catalog differs from reserved private trial")
        if value["executor"] != {"version": source.version, **source.producer}:
            raise ReleaseError(
                "Native export executor differs from authenticated held source image"
            )
        configs = value["configs"]
        epochs = value["epochs"]
        if (
            not isinstance(configs, list)
            or len(configs) != 12
            or len(set(integer(n) for n in configs)) != 12
            or not isinstance(epochs, list)
            or len(epochs) != 4
        ):
            raise ReleaseError("Actual native config/epoch proof set is incomplete")
        for epoch in epochs:
            fields(epoch, {"pid", "birth_utc"})
            integer(epoch["pid"], 2**32 - 1)
            integer(epoch["birth_utc"])
        started, exported = integer(value["started_utc"]), integer(value["exported_utc"])
        if not started <= integer(value["run"]["finished_utc"]) <= exported:
            raise ReleaseError("Native acceptance export chronology differs")
        if value["run"].get("operation_id") != intent["operation_id"]:
            raise ReleaseError("Native outcome belongs to another original operation")
        _run(
            value["run"],
            source if intent["force_rollback"] else target,
            restored=intent["force_rollback"],
            seen=set(),
            earliest=started,
            latest=exported,
        )
        # Existing typed native102/108+103 authority is verified by SYSTEM exporter.
        # Python never upgrades mutable caller hashes/JSON into that authority.
        return {
            "bytes_sha256": digest(data),
            "run": value["run"],
            "started": started,
            "finished": exported,
        }

    def run(self, plan_path: Path) -> dict:
        if not _elevated():
            raise ReleaseError("Run acceptance-run from an elevated physical console")
        reviewed = unique_json(_file(plan_path, 65535))
        now = int(time.time())
        p = self.publisher
        config = read_config(self.root)
        with workspace_lock(self.root / config["dist"] / ".release" / "workspace.lock"):
            p.state.mkdir(mode=0o700, exist_ok=True)
            protect_state(p.state)
            with (
                workspace_lock(p.state / "publisher.lock"),
                tempfile.TemporaryDirectory(prefix="l4acceptance-run-") as tmp,
            ):
                endpoints, resumed = self._validate_plan(reviewed, now, Path(tmp))
                source = endpoints[reviewed["from"]["version"]][reviewed["arch"]]
                target = endpoints[reviewed["to"]["version"]][reviewed["arch"]]
                key = load_signing_key(self.root, self.env, self.env_path)
                verify_embedded_key(self.root, key.public_key())
                directory = p.state / f"trial-{reviewed['revision']}"
                directory.mkdir(exist_ok=resumed)
                protect_state(directory)
                plan_data = canonical(reviewed)
                catalog_data, catalog_sig = catalog_bytes(reviewed["catalog"], key, now=now)
                for name, data in (
                    ("plan.json", plan_data),
                    ("plan.json.sig", sign_bytes(plan_data, key)),
                    ("catalog.json", catalog_data),
                    ("catalog.json.sig", catalog_sig),
                ):
                    if resumed:
                        protect_state(directory / name)
                        if _file(directory / name, len(data)) != data:
                            raise ReleaseError("Protected reserved trial bytes changed")
                    else:
                        atomic_bytes(directory / name, data)
                    protect_state(directory / name)
                atomic_json(
                    p.state / "trial-reservation.json",
                    {
                        "schema": 1,
                        "key_id": key_id(key.public_key()),
                        "revision": reviewed["revision"],
                        "catalog_sha256": digest(catalog_data),
                        "plan_sha256": digest(plan_data),
                    },
                )
                protect_state(p.state / "trial-reservation.json")
                native = NativeExecutor(source, directory)
                results = []
                for operation in reviewed["operations"]:
                    intent = {
                        "schema": 1,
                        "kind": "l4tools-local-acceptance",
                        **operation,
                        "source_version": source.version,
                        "target_version": target.version,
                        "arch": reviewed["arch"],
                        "catalog_sha256": digest(catalog_data),
                        "expires_at": min(
                            reviewed["catalog"]["expires_at"], int(time.time()) + 4 * 3600
                        ),
                        "terminal_id": 773,
                        "tenant_id": 1,
                    }
                    authorization = canonical(intent)
                    path = directory / f"{operation['operation_id']}.intent.json"
                    signature_path = Path(str(path) + ".sig")
                    if path.exists():
                        protect_state(path)
                        protect_state(signature_path)
                        authorization = _file(path, 4096)
                        verify_bytes(authorization, _file(signature_path, 384), p.public)
                        retained = fields(unique_json(authorization), set(intent))
                        expiry = integer(retained["expires_at"], 253402300799)
                        if expiry > reviewed["catalog"]["expires_at"]:
                            raise ReleaseError("Retained acceptance authorization expiry differs")
                        intent["expires_at"] = expiry
                        if retained != intent:
                            raise ReleaseError("Retained acceptance authorization differs")
                    else:
                        atomic_bytes(path, authorization)
                        atomic_bytes(signature_path, sign_bytes(authorization, key))
                    data, value = native.run(intent, directory / "catalog.json", path)
                    results.append(
                        self._export(data, value, reviewed, intent, authorization, source, target)
                    )
                # Real fixed pipeline pair-contract gates, bound to checked source.
                runner = Runner(self.root, self.env, directory / "checks")
                checks = []
                for step in config["steps"]:
                    if step["name"] not in {"setup-test-x86", "setup-test-x64"}:
                        continue
                    runner.run(
                        step["name"],
                        step["kind"],
                        self.root / step["cwd"],
                        step["args"],
                        step["timeout"],
                    )
                    log = runner.logs / (step["name"] + ".log")
                    checks.append({"name": step["name"], "log_sha256": digest(log.read_bytes())})
                if {c["name"] for c in checks} != {
                    "setup-test-x86",
                    "setup-test-x64",
                } or _checkpoint(self.root) != reviewed["source_checkpoint"]:
                    raise ReleaseError("Required real pair-contract gates/source checkpoint differ")
                backwards = {
                    "contract": "communication-backwards-v1",
                    "inference": "gated-shared-executor-forward102",
                    "forward_export_sha256": results[1]["bytes_sha256"],
                    "executor": source.producer,
                    "from": source.identity(),
                    "to": target.identity(),
                    "source_checkpoint": reviewed["source_checkpoint"],
                    "checks": checks,
                }
                backwards_data = canonical(backwards)
                atomic_bytes(directory / "backward-check.json", backwards_data)
                report = {
                    "schema": 1,
                    "kind": "l4tools-pipeline-compatibility",
                    "key_id": key_id(key.public_key()),
                    "run_id": reviewed["run_id"],
                    "terminal_id": 773,
                    "tenant_id": 1,
                    "arch": reviewed["arch"],
                    "platform": reviewed["platform"],
                    "from": source.identity(),
                    "to": target.identity(),
                    "producer": {"name": "l4release", "schema": 1},
                    "started_at": (min(r["started"] for r in results) - FILETIME_EPOCH)
                    // 10_000_000,
                    "finished_at": (max(r["finished"] for r in results) - FILETIME_EPOCH)
                    // 10_000_000,
                    "backward_compatibility": {
                        "contract": "communication-backwards-v1",
                        "report_sha256": digest(backwards_data),
                    },
                    "forward": results[1]["run"],
                    "forced_rollback": results[0]["run"],
                }
                report_data = canonical(report)
                signature = sign_bytes(report_data, key)
                validate_acceptance(
                    report_data, signature, key.public_key(), source, target, now=int(time.time())
                )
                output = directory / "acceptance.json"
                atomic_bytes(output, report_data)
                atomic_bytes(Path(str(output) + ".sig"), signature)
                return {
                    "report": str(output),
                    "trial_revision": reviewed["revision"],
                    "next_public_revision_minimum": reviewed["revision"] + 1,
                }
