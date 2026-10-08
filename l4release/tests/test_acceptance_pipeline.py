"""Pipeline composition with modeled native SYSTEM proof/SDK/SCM authority, no stand runs."""

from __future__ import annotations

import copy
import subprocess
from contextlib import contextmanager
from pathlib import Path

import pytest

from l4release import acceptance_pipeline as module
from l4release import acceptance_trial
from l4release.catalog_evidence import (
    FILETIME_EPOCH,
    canonical,
    profile,
    unique_json,
    validate_acceptance,
)
from l4release.catalog_registry import digest
from l4release.common import ReleaseError
from l4release.tests.test_catalog_publisher import NOW, endpoint
from l4release.tests.test_catalog_publisher import key as key
from l4release.tests.test_catalog_publisher import publisher as publisher


@pytest.fixture
def pipeline(publisher, key, monkeypatch):
    public, _ = publisher
    instance = module.AcceptancePipeline.__new__(module.AcceptancePipeline)
    instance.publisher = public
    instance.root, instance.env, instance.env_path = public.root, {}, public.env_path
    source, target = endpoint("1.13.7"), endpoint("1.13.8")
    endpoints = {
        source.version: {a: source for a in ("x86", "x64")},
        target.version: {a: target for a in ("x86", "x64")},
    }
    monkeypatch.setattr(instance, "_endpoints", lambda *args: endpoints)
    monkeypatch.setattr(
        module,
        "windows_platform",
        lambda: {"major": 10, "minor": 0, "build": 19045, "native_arch": "x64", "product_type": 1},
    )
    monkeypatch.setattr(
        module,
        "_checkpoint",
        lambda root: {"git_sha": "a" * 40, "clean": True, "inputs_sha256": "b" * 64},
    )
    monkeypatch.setattr(module, "protect_state", lambda path: None)
    monkeypatch.setattr(acceptance_trial, "protect_state", lambda path: None)
    monkeypatch.setattr(module, "_elevated", lambda: True)
    monkeypatch.setattr(module, "load_signing_key", lambda *args: key)
    monkeypatch.setattr(module, "verify_embedded_key", lambda *args: None)
    monkeypatch.setattr(module.time, "time", lambda: NOW)
    steps = [
        {
            "name": f"setup-test-{a}",
            "kind": "cmd",
            "cwd": "tools/l4setup",
            "args": ["run_tests.cmd", a],
            "timeout": 900,
        }
        for a in ("x86", "x64")
    ]
    monkeypatch.setattr(module, "read_config", lambda root: {"dist": "dist", "steps": steps})

    class Checks:
        def __init__(self, root, env, logs):
            self.logs = logs

        def run(self, name, *args):
            self.logs.mkdir(parents=True, exist_ok=True)
            (self.logs / (name + ".log")).write_bytes(b"modeled native contract gate")

    monkeypatch.setattr(module, "Runner", Checks)
    instance.exports = []

    class Native:
        def __init__(self, source, directory):
            self.source, self.directory = source, directory

        def run(self, intent, catalog, authorization):
            forced = intent["force_rollback"]
            installed = source if forced else target
            now = FILETIME_EPOCH + (NOW - 10) * 10_000_000
            value = {
                "schema": 1,
                "kind": "l4tools-local-acceptance-result",
                "operation_id": intent["operation_id"],
                "arch": "x86",
                "profile": profile(module.windows_platform()),
                "platform": module.windows_platform(),
                "terminal_id": 773,
                "tenant_id": 1,
                "from": source.identity(),
                "to": target.identity(),
                "force_rollback": forced,
                "catalog": {
                    "revision": unique_json(catalog.read_bytes())["revision"],
                    "sha256": digest(catalog.read_bytes()),
                },
                "intent_sha256": digest(b"modeled operator receipt"),
                "authorization_sha256": digest(authorization.read_bytes()),
                "executor": {"version": source.version, **source.producer},
                "run": {
                    "operation_id": intent["operation_id"],
                    "plan_sha256": digest(intent["operation_id"].encode()),
                    "proof_kind": 108 if forced else 102,
                    "proof_sha256": digest(b"108" if forced else b"102"),
                    "outcome_sha256": digest(b"103restore" if forced else b"103success"),
                    "result": "RESTORED" if forced else "SUCCESS",
                    "error": 1223 if forced else 0,
                    "installed": installed.identity(),
                    "finished_utc": now,
                },
                "clear_generation": 2 if forced else 4,
                "configs": list(range(1, 13)),
                "epochs": [{"pid": i + 100, "birth_utc": now - 10000} for i in range(4)],
                "started_utc": now - 100000,
                "exported_utc": now + 10000,
            }
            data = canonical(value)
            instance.exports.append((data, value, intent))
            return data, value

    monkeypatch.setattr(module, "NativeExecutor", Native)
    return instance, source, target


def save(tmp_path, plan):
    path = tmp_path / "reviewed.json"
    path.write_bytes(canonical(plan))
    return path


def test_private_readonly_plan_forced_then_normal_sealed_acceptance(pipeline, tmp_path, key):
    instance, source, target = pipeline
    plan = instance.plan(source.version, target.version, now=NOW)
    assert not instance.publisher.state.exists()
    assert [op["force_rollback"] for op in plan["operations"]] == [True, False]
    result = instance.run(save(tmp_path, plan))
    assert [value["force_rollback"] for _, value, _ in instance.exports] == [True, False]
    path = Path(result["report"])
    edge = validate_acceptance(
        path.read_bytes(),
        Path(str(path) + ".sig").read_bytes(),
        key.public_key(),
        source,
        target,
        now=NOW,
    )
    assert edge["to"] == target.version
    assert result["next_public_revision_minimum"] == 2
    assert instance.publisher._floor()["revision"] == 0


@pytest.mark.parametrize("fault", ["source", "arch", "operations", "dirty"])
def test_reviewed_local_plan_drift_refuses_before_native(pipeline, tmp_path, fault):
    instance, source, target = pipeline
    plan = instance.plan(source.version, target.version, now=NOW)
    if fault == "source":
        plan["from"]["root_sha256"] = "1" * 64
    elif fault == "arch":
        plan["arch"] = "x64"
    elif fault == "operations":
        plan["operations"][0]["force_rollback"] = False
    elif fault == "dirty":
        plan["source_checkpoint"]["clean"] = False
    with pytest.raises(ReleaseError):
        instance.run(save(tmp_path, plan))
    assert not instance.exports


@pytest.mark.parametrize(
    "fault", ["auth", "catalog", "executor", "clear", "configs", "epoch", "operation", "result"]
)
def test_native_export_binding_refuses(pipeline, tmp_path, fault):
    instance, source, target = pipeline
    plan = instance.plan(source.version, target.version, now=NOW)
    instance.run(save(tmp_path, plan))
    data, original, intent = instance.exports[0]
    value = copy.deepcopy(original)
    directory = instance.publisher.state / "trial-1"
    authorization = (directory / (intent["operation_id"] + ".intent.json")).read_bytes()
    if fault == "auth":
        value["authorization_sha256"] = "1" * 64
    elif fault == "catalog":
        value["catalog"]["revision"] += 1
    elif fault == "executor":
        value["executor"]["sha256"] = "1" * 64
    elif fault == "clear":
        value["clear_generation"] = 0
    elif fault == "configs":
        value["configs"].pop()
    elif fault == "epoch":
        value["epochs"][0]["pid"] = 0
    elif fault == "operation":
        value["run"]["operation_id"] = plan["operations"][1]["operation_id"]
    elif fault == "result":
        value["run"]["result"] = "SUCCESS"
    with pytest.raises(ReleaseError):
        instance._export(canonical(value), value, plan, intent, authorization, source, target)


def test_same_private_plan_can_resume(pipeline, tmp_path):
    instance, source, target = pipeline
    path = save(tmp_path, instance.plan(source.version, target.version, now=NOW))
    first = instance.run(path)
    assert instance.run(path) == first


def test_native_timeout_preserves_controller_without_kill(monkeypatch, tmp_path):
    """SDK authority/image pin modeled; actual Popen timeout policy is exercised."""
    source = endpoint("1.13.7")
    image = b"modeled signed installed executor"
    source.producer.update(sha256=digest(image), size=len(image))
    monkeypatch.setattr(module, "roots", lambda: (tmp_path / "PF", tmp_path / "PD"))
    monkeypatch.setattr(module, "_elevated", lambda: True)

    @contextmanager
    def held(*args, **kwargs):
        yield lambda maximum: image

    monkeypatch.setattr(module, "held", held)
    observed = []

    class Process:
        def wait(self, timeout):
            observed.append(timeout)
            raise subprocess.TimeoutExpired("modeled-native", timeout)

        def kill(self):
            pytest.fail("Native controller must not be killed on Python timeout")

    monkeypatch.setattr(module.subprocess, "Popen", lambda *args, **kwargs: Process())
    intent = {
        "source_version": source.version,
        "target_version": "1.13.8",
        "operation_id": "c3378324-1a57-4430-bc11-0542e38f13ca",
        "arch": "x86",
    }
    with pytest.raises(ReleaseError, match="preserve operation"):
        module.NativeExecutor(source, tmp_path).run(
            intent, tmp_path / "catalog", tmp_path / "intent"
        )
    assert observed == [156 * 60]
