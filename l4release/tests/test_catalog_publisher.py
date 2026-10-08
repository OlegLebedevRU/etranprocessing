"""Synthetic signed fixtures model producer attestations; these are not stand evidence."""

from __future__ import annotations

import uuid
from pathlib import Path

import pytest
from cryptography.hazmat.primitives.asymmetric import rsa

from l4release import catalog_publisher as module
from l4release.catalog import validate_catalog
from l4release.catalog_evidence import (
    FILETIME_EPOCH,
    SERVICES,
    Endpoint,
    canonical,
    validate_acceptance,
)
from l4release.catalog_publisher import CURRENT, CatalogPublisher
from l4release.catalog_registry import digest
from l4release.common import ReleaseError
from l4release.metadata import key_id, sign_bytes

NOW = 1700000000


@pytest.fixture(scope="module")
def key():
    return rsa.generate_private_key(public_exponent=65537, key_size=3072)


def endpoint(version: str) -> Endpoint:
    seed = version.encode()
    return Endpoint(
        version,
        digest(seed),
        digest(seed + b"inventory"),
        {
            name: {"sha256": digest(seed + name.encode()), "size": 100 + i}
            for i, name in enumerate(SERVICES)
        },
        {"sha256": digest(seed + b"setup"), "size": 300},
    )


def acceptance(key, source, target):
    """Synthetic owner attestations; never an actual native pipeline receipt export."""

    def run(installed, restored):
        return {
            "operation_id": str(uuid.uuid4()),
            "plan_sha256": digest(("rollback" if restored else "forward").encode()),
            "proof_kind": 108 if restored else 102,
            "proof_sha256": digest(("proof108" if restored else "proof102").encode()),
            "outcome_sha256": digest(("restored103" if restored else "success103").encode()),
            "result": "RESTORED" if restored else "SUCCESS",
            "error": 1237 if restored else 0,
            "installed": installed.identity(),
            "finished_utc": FILETIME_EPOCH + (NOW - 30) * 10_000_000,
        }

    return {
        "schema": 1,
        "kind": "l4tools-pipeline-compatibility",
        "key_id": key_id(key.public_key()),
        "run_id": str(uuid.uuid4()),
        "terminal_id": 773,
        "tenant_id": 1,
        "arch": "x86",
        "platform": {
            "major": 10,
            "minor": 0,
            "build": 19045,
            "native_arch": "x64",
            "product_type": 1,
        },
        "from": source.identity(),
        "to": target.identity(),
        "producer": {"name": "l4release", "schema": 1},
        "started_at": NOW - 100,
        "finished_at": NOW - 20,
        "backward_compatibility": {
            "contract": "communication-backwards-v1",
            "report_sha256": digest(b"synthetic compatibility test output"),
        },
        "forward": run(target, False),
        "forced_rollback": run(source, True),
    }


def validated(report, key, source, target):
    data = canonical(report)
    return validate_acceptance(
        data, sign_bytes(data, key), key.public_key(), source, target, now=NOW
    )


def test_pipeline_forward_and_forced_restore_attestation(key):
    source, target = endpoint("1.13.7"), endpoint("1.13.8")
    edge = validated(acceptance(key, source, target), key, source, target)
    assert edge == {
        "from": "1.13.7",
        "to": "1.13.8",
        "arch": "x86",
        "profile": "windows-nt-10.0.19045-x64-client",
    }


@pytest.mark.parametrize(
    "fault",
    [
        "missing_rollback",
        "caller_pass",
        "stand",
        "bool_tenant",
        "root",
        "inventory",
        "wrong_proof",
        "wrong_result",
        "wrong_installed",
        "zero_rollback_error",
        "forward_error",
        "bool_error",
        "unknown_contract",
        "empty_check_hash",
        "duplicate_operation",
        "duplicate_plan",
        "future",
        "stale",
        "platform",
        "producer",
        "bool_schema",
        "old_sixpoint_kind",
    ],
)
def test_pipeline_refuses_invalid_signed_attestation(key, fault):
    source, target = endpoint("1.13.7"), endpoint("1.13.8")
    report = acceptance(key, source, target)
    if fault == "missing_rollback":
        del report["forced_rollback"]
    elif fault == "caller_pass":
        report["PASS"] = True
    elif fault == "stand":
        report["terminal_id"] = 774
    elif fault == "bool_tenant":
        report["tenant_id"] = True
    elif fault == "root":
        report["to"]["root_sha256"] = "1" * 64
    elif fault == "inventory":
        report["from"]["inventory_sha256"] = "1" * 64
    elif fault == "wrong_proof":
        report["forced_rollback"]["proof_kind"] = 102
    elif fault == "wrong_result":
        report["forward"]["result"] = "RESTORED"
    elif fault == "wrong_installed":
        report["forced_rollback"]["installed"] = target.identity()
    elif fault == "zero_rollback_error":
        report["forced_rollback"]["error"] = 0
    elif fault == "forward_error":
        report["forward"]["error"] = 5
    elif fault == "bool_error":
        report["forced_rollback"]["error"] = True
    elif fault == "unknown_contract":
        report["backward_compatibility"]["contract"] = "caller-PASS"
    elif fault == "empty_check_hash":
        report["backward_compatibility"]["report_sha256"] = "0" * 64
    elif fault == "duplicate_operation":
        report["forced_rollback"]["operation_id"] = report["forward"]["operation_id"]
    elif fault == "duplicate_plan":
        report["forced_rollback"]["plan_sha256"] = report["forward"]["plan_sha256"]
    elif fault == "future":
        report["finished_at"] = NOW + 1
    elif fault == "stale":
        report["started_at"] = report["finished_at"] = NOW - 15 * 86400
    elif fault == "platform":
        report["platform"]["product_type"] = 3
    elif fault == "producer":
        report["producer"]["name"] = "caller-PASS"
    elif fault == "bool_schema":
        report["schema"] = True
    elif fault == "old_sixpoint_kind":
        report["kind"] = "l4tools-measured-compatibility"
    with pytest.raises(ReleaseError):
        validated(report, key, source, target)


def test_signature_before_json(key):
    with pytest.raises(ReleaseError):
        validate_acceptance(
            b"not JSON",
            b"x" * 384,
            key.public_key(),
            endpoint("1.13.7"),
            endpoint("1.13.8"),
            now=NOW,
        )


class Registry:
    """Modeled storage only; no GAR availability/CAS acceptance claim."""

    def __init__(self):
        self.files = {}
        self.uploads = []
        self.fail_signature = False

    def download(self, relative, target, maximum):
        if relative not in self.files:
            return False
        data = self.files[relative]
        if len(data) > maximum:
            raise ReleaseError("bound")
        target.write_bytes(data)
        return True

    def upload(self, relative, data):
        if relative == CURRENT + ".sig" and self.fail_signature:
            raise ReleaseError("modeled fixed signature PUT failure")
        self.files[relative] = data
        self.uploads.append(relative)


@pytest.fixture
def publisher(tmp_path, monkeypatch, key):
    root = tmp_path / "repo"
    root.mkdir()
    state = tmp_path / "protected"
    source, target = endpoint("1.13.7"), endpoint("1.13.8")
    report_path = tmp_path / "acceptance.json"
    data = canonical(acceptance(key, source, target))
    report_path.write_bytes(data)
    Path(str(report_path) + ".sig").write_bytes(sign_bytes(data, key))
    instance = CatalogPublisher.__new__(CatalogPublisher)
    instance.root, instance.env, instance.env_path = root, {}, tmp_path / "env"
    instance.public, instance.state, instance.registry = key.public_key(), state, Registry()
    # Composition fixtures model external ACL/key/source/native authority explicitly.
    monkeypatch.setattr(module, "protect_state", lambda path: None)
    monkeypatch.setattr(module, "read_config", lambda root: {"dist": "dist"})
    monkeypatch.setattr(
        module,
        "_checkpoint",
        lambda root: {"git_sha": "a" * 40, "clean": True, "inputs_sha256": "b" * 64},
    )
    monkeypatch.setattr(module, "load_signing_key", lambda *args: key)
    monkeypatch.setattr(module, "verify_embedded_key", lambda *args: None)
    monkeypatch.setattr(
        module,
        "published_release",
        lambda root, registry, version, directory, public: {
            arch: source if version == source.version else target for arch in ("x86", "x64")
        },
    )
    return instance, report_path


def save_plan(tmp_path, plan):
    path = tmp_path / "reviewed.json"
    path.write_bytes(canonical(plan))
    return path


def test_readonly_plan_then_public_pair_floor(publisher, tmp_path):
    instance, report = publisher
    plan = instance.plan([report], now=NOW)
    assert not instance.state.exists() and not instance.registry.uploads
    assert plan["catalog"]["stable"] is None and plan["catalog"]["revision"] == 1
    result = instance.publish(save_plan(tmp_path, plan), now=NOW)
    assert result["public_pair_verified"] and result["stable"] is None
    assert instance._floor()["revision"] == 1 and instance._floor()["pending"] is None
    assert len(instance.registry.uploads) == 6


def test_partial_fixed_pair_exact_resume(publisher, tmp_path):
    instance, report = publisher
    path = save_plan(tmp_path, instance.plan([report], now=NOW))
    instance.registry.fail_signature = True
    with pytest.raises(ReleaseError, match="signature PUT"):
        instance.publish(path, now=NOW)
    assert instance._floor()["pending"]["revision"] == 1
    with pytest.raises(ReleaseError, match="pending"):
        instance.plan([report], now=NOW)
    instance.registry.fail_signature = False
    assert instance.publish(path, now=NOW)["revision"] == 1
    assert instance._floor()["pending"] is None


@pytest.mark.parametrize("mutation", ["stable", "edge", "evidence", "dirty", "path"])
def test_reviewed_plan_changed_refuses(publisher, tmp_path, mutation, monkeypatch):
    instance, report = publisher
    plan = instance.plan([report], now=NOW)
    if mutation == "stable":
        plan["catalog"]["stable"] = "1.13.8"
    elif mutation == "edge":
        plan["catalog"]["transitions"][0]["evidence_sha256"] = "c" * 64
    elif mutation == "evidence":
        plan["reports"][0]["sha256"] = "c" * 64
    elif mutation == "dirty":
        monkeypatch.setattr(
            module, "_checkpoint", lambda root: {**plan["source_checkpoint"], "clean": False}
        )
    elif mutation == "path":
        plan["reports"][0]["path"] = {}
    with pytest.raises(ReleaseError):
        instance.publish(save_plan(tmp_path, plan), now=NOW)
    assert not instance.registry.uploads


def test_pending_unknown_remote_bytes_refuses(publisher, tmp_path):
    instance, report = publisher
    path = save_plan(tmp_path, instance.plan([report], now=NOW))
    instance.registry.fail_signature = True
    with pytest.raises(ReleaseError):
        instance.publish(path, now=NOW)

    instance.registry.files[CURRENT] = b"unknown drift"
    instance.registry.fail_signature = False
    with pytest.raises(ReleaseError, match="unknown bytes"):
        instance.publish(path, now=NOW)


def test_reviewed_promotion_derives_only_accepted_public_target(publisher, tmp_path):
    instance, report = publisher
    instance.publish(save_plan(tmp_path, instance.plan([report], now=NOW)), now=NOW)
    promotion = instance.plan([report], now=NOW, promote=True)
    assert promotion["kind"] == "l4tools-catalog-promotion"
    assert promotion["catalog"]["stable"] == "1.13.8"
    assert promotion["catalog"]["revision"] == 2
    assert instance.publish(save_plan(tmp_path, promotion), now=NOW)["stable"] == "1.13.8"
    with pytest.raises(ReleaseError, match="already stable"):
        instance.plan([report], now=NOW, promote=True)


def test_promotion_requires_previously_admitted_exact_evidence(publisher):
    instance, report = publisher
    with pytest.raises(ReleaseError, match="already admitted"):
        instance.plan([report], now=NOW, promote=True)
    assert not instance.registry.uploads


def test_promotion_partial_pair_same_reviewed_resume(publisher, tmp_path):
    instance, report = publisher
    instance.publish(save_plan(tmp_path, instance.plan([report], now=NOW)), now=NOW)
    path = save_plan(tmp_path, instance.plan([report], now=NOW, promote=True))
    instance.registry.fail_signature = True
    with pytest.raises(ReleaseError):
        instance.publish(path, now=NOW)
    instance.registry.fail_signature = False
    assert instance.publish(path, now=NOW)["revision"] == 2


def test_floor_refuses_missing_or_rollback(publisher, tmp_path):
    instance, report = publisher
    instance.publish(save_plan(tmp_path, instance.plan([report], now=NOW)), now=NOW)
    instance.registry.files.clear()
    with pytest.raises(ReleaseError, match="404"):
        instance.plan([report], now=NOW)


def test_expired_base_only_publication_ancestry(publisher, key):
    instance, _ = publisher
    value = {
        "schema": 1,
        "key_id": key_id(key.public_key()),
        "revision": 1,
        "issued_at": NOW - 100,
        "expires_at": NOW - 1,
        "stable": None,
        "releases": [{"version": "1.13.7", "manifest_sha256": "a" * 64, "revoked": False}],
        "transitions": [],
    }
    data = canonical(value)
    assert instance._catalog((data, sign_bytes(data, key)), NOW)["revision"] == 1
    with pytest.raises(ReleaseError, match="expired"):
        validate_catalog(value, expected_key_id=key_id(key.public_key()), now=NOW)


def test_pending_changed_review_and_immutable_collision(publisher, tmp_path):
    instance, report = publisher
    plan = instance.plan([report], now=NOW)
    path = save_plan(tmp_path, plan)
    archive = f"l4tools/metadata/archive/{plan['catalog']['revision']}/catalog.json"
    instance.registry.files[archive] = b"different immutable bytes"
    with pytest.raises(ReleaseError):
        instance.publish(path, now=NOW)
    assert not instance._floor()["pending"]
    del instance.registry.files[archive]
    instance.registry.fail_signature = True
    with pytest.raises(ReleaseError):
        instance.publish(path, now=NOW)
    plan["catalog"]["stable"] = "1.13.8"
    with pytest.raises(ReleaseError, match="Another exact"):
        instance.publish(save_plan(tmp_path, plan), now=NOW)


def test_protected_floor_same_revision_changed_bytes(publisher, tmp_path, key):
    instance, report = publisher
    instance.publish(save_plan(tmp_path, instance.plan([report], now=NOW)), now=NOW)
    document = module.unique_json(instance.registry.files[CURRENT])
    document["stable"] = "1.13.8"
    data = canonical(document)
    instance.registry.files[CURRENT] = data
    instance.registry.files[CURRENT + ".sig"] = sign_bytes(data, key)
    with pytest.raises(ReleaseError, match="same revision"):
        instance.plan([report], now=NOW)


def test_clock_below_floor(publisher, tmp_path):
    instance, report = publisher
    instance.publish(save_plan(tmp_path, instance.plan([report], now=NOW)), now=NOW)
    with pytest.raises(ReleaseError):
        instance.plan([report], now=NOW - 1)
