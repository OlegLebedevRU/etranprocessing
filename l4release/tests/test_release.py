"""Release safety boundaries, checkpoints and secret handling (no live services)."""

import json
import os
import struct
import subprocess
import time
from pathlib import Path

import pytest
from cryptography.hazmat.primitives import serialization

from l4release.__main__ import main
from l4release.common import Redactor, ReleaseError, contained, file_hash, load_env, version_value
from l4release.dependencies import import_openh264, verify_openh264
from l4release.keys import init_key
from l4release.pipeline import Pipeline, check_pe_outputs, outputs_match, snapshot_digest
from l4release.runner import Runner, workspace_lock

ROOT = Path(__file__).resolve().parents[2]


def test_dependency_import_checks_entire_source_before_copy(tmp_path):
    root = tmp_path / "repo"
    source = tmp_path / "source"
    (root / "l4release").mkdir(parents=True)
    source.mkdir()
    (source / "first").write_bytes(b"valid")
    (source / "second").write_bytes(b"changed")
    (root / "l4release/openh264.lock.json").write_text(
        json.dumps({"files": {"first": file_hash(source / "first"), "second": "wrong"}})
    )
    with pytest.raises(ReleaseError, match="does not match"):
        import_openh264(root, source)
    assert not (root / "tools").exists()


def test_dependency_import_and_tamper_detection(tmp_path):
    root, source = tmp_path / "repo", tmp_path / "source"
    (root / "l4release").mkdir(parents=True)
    source.mkdir()
    (source / "header").write_bytes(b"locked header")
    (root / "l4release/openh264.lock.json").write_text(
        json.dumps({"files": {"header": file_hash(source / "header")}})
    )
    import_openh264(root, source)
    verify_openh264(root)
    target = root / "tools/l4capture/vendor/openh264/header"
    target.write_bytes(b"tampered")
    with pytest.raises(ReleaseError, match="missing or changed"):
        verify_openh264(root)
    with pytest.raises(ReleaseError, match="Refusing to overwrite"):
        import_openh264(root, source)
    assert target.read_bytes() == b"tampered"


@pytest.mark.parametrize("version", ["latest", "../1.2.3", "1.2.3&whoami", "01.2.3", "65536.1.0"])
def test_version_cannot_inject_commands_or_escape_checkpoint(version):
    with pytest.raises(ReleaseError):
        version_value(version)


def test_env_literal_windows_paths_and_no_interpolation(tmp_path):
    path = tmp_path / "sw_sign.env"
    path.write_text(
        "SW_SIGN_PFX='D:\\keys\\code.pfx'\nW_SIGN_PFX_PASSWORD=abc$()#def\n", encoding="utf-8"
    )
    env = load_env(path)
    assert env["SW_SIGN_PFX"] == "D:\\keys\\code.pfx"
    assert env["W_SIGN_PFX_PASSWORD"] == "abc$()#def"


def test_duplicate_env_is_rejected_without_echoing_value(tmp_path):
    path = tmp_path / "sw_sign.env"
    path.write_text("AR_GENERIC_KEY_SECRET=first-secret\nAR_GENERIC_KEY_SECRET=second-secret\n")
    with pytest.raises(ReleaseError, match="Duplicate") as raised:
        load_env(path)
    assert "first-secret" not in str(raised.value)


def test_redactor_covers_credentials_and_authorization():
    env = {
        "W_SIGN_PFX_PASSWORD": "a-password",
        "AR_GENERIC_KEY_SECRET": "registry-secret",
        "IOT_API_KEY_773": "iot-secret",
    }
    output = Redactor(env)("a-password registry-secret iot-secret\nAuthorization: Basic abcdef")
    assert all(secret not in output for secret in env.values())
    assert "abcdef" not in output


def test_path_and_workspace_lock_exclude_concurrent_mutation(tmp_path):
    with pytest.raises(ReleaseError):
        contained(tmp_path, "../escape")
    with (
        workspace_lock(tmp_path / "workspace.lock"),
        pytest.raises(ReleaseError),
        workspace_lock(tmp_path / "workspace.lock"),
    ):
        pytest.fail("Concurrent release acquired the lock")


def test_checkpoint_requires_same_bytes_and_inputs(tmp_path):
    output = tmp_path / "setup.exe"
    output.write_bytes(b"signed-installer")
    snapshot = {"setup.exe": file_hash(output)}
    assert outputs_match(tmp_path, snapshot)
    output.write_bytes(b"tampered-installer")
    assert not outputs_match(tmp_path, snapshot)
    assert snapshot_digest({"a": "1", "b": "2"}) == snapshot_digest({"b": "2", "a": "1"})
    assert snapshot_digest({"a": "1"}) != snapshot_digest({"a": "2"})


def test_wrong_or_stale_native_architecture_is_rejected(tmp_path):
    path = tmp_path / "bin/x64/tool.exe"
    path.parent.mkdir(parents=True)
    header = bytearray(64)
    header[:2] = b"MZ"
    struct.pack_into("<I", header, 60, 64)
    path.write_bytes(header + b"PE\0\0" + struct.pack("<H", 0x14C))
    with pytest.raises(ReleaseError, match="architecture"):
        check_pe_outputs(tmp_path, ["bin/x64/tool.exe"], time.time())
    path.write_bytes(header + b"PE\0\0" + struct.pack("<H", 0x8664))
    check_pe_outputs(tmp_path, ["bin/x64/tool.exe"], time.time())
    os.utime(path, (1, 1))
    with pytest.raises(ReleaseError, match="fresh"):
        check_pe_outputs(tmp_path, ["bin/x64/tool.exe"], time.time())


def test_key_init_encrypted_key_and_no_overwrite(tmp_path):
    root = tmp_path / "repo"
    root.mkdir()
    path = tmp_path / "metadata.pem"
    env = {
        "L4TOOLS_METADATA_KEY_PATH": str(path),
        "L4TOOLS_METADATA_KEY_PASSWORD": "test-only-password",
    }
    key_id = init_key(root, env, tmp_path / "sw_sign.env")
    assert len(key_id) == 64
    private_bytes = path.read_bytes()
    assert b"BEGIN ENCRYPTED PRIVATE KEY" in private_bytes
    key = serialization.load_pem_private_key(private_bytes, b"test-only-password")
    assert key.key_size == 3072
    with pytest.raises(ReleaseError, match="never overwrites"):
        init_key(root, env, tmp_path / "sw_sign.env")
    assert path.read_bytes() == private_bytes
    env["L4TOOLS_METADATA_KEY_PATH"] = str(root / "private.pem")
    with pytest.raises(ReleaseError, match="outside"):
        init_key(root, env, tmp_path / "sw_sign.env")


def test_plan_and_unimplemented_terminal_gate_have_no_mutating_steps(monkeypatch, capsys):
    monkeypatch.setattr(
        Pipeline, "execute", lambda *a, **k: pytest.fail("execution must not start")
    )
    assert main(["plan", "--version", "1.13.2"]) == 0
    plan = json.loads(capsys.readouterr().out)
    assert plan["version"] == "1.13.2"
    assert "setup-test-x86" in {step["name"] for step in plan["steps"]}
    assert "setup-test-x64" in {step["name"] for step in plan["steps"]}
    assert main(["release", "--version", "1.13.2", "--with-terminal-gate"]) == 1
    assert main(["promote", "--version", "1.13.2"]) == 1


def test_runner_env_file_is_authoritative(monkeypatch, tmp_path):
    monkeypatch.setenv("AR_GENERIC_KEY_SECRET", "inherited-unrelated-secret")
    monkeypatch.setenv("L4TOOLS_SIGN_PFX", "inherited.pfx")
    runner = Runner(
        tmp_path,
        {"SW_SIGN_PFX": "configured.pfx", "W_SIGN_PFX_PASSWORD": "configured-password"},
        tmp_path,
    )
    assert "AR_GENERIC_KEY_SECRET" not in runner.env
    assert runner.env["L4TOOLS_SIGN_PFX"] == "configured.pfx"
    assert runner.env["L4TOOLS_SIGN_PFX_PASSWORD"] == "configured-password"


def test_child_error_logs_are_redacted(monkeypatch, tmp_path):
    class Child:
        returncode = 1

        def communicate(self, timeout):
            return "error: a-secret", None

    monkeypatch.setattr(subprocess, "Popen", lambda *a, **k: Child())
    runner = Runner(tmp_path, {"AR_GENERIC_KEY_SECRET": "a-secret"}, tmp_path / "logs")
    with pytest.raises(ReleaseError, match="failed"):
        runner.run("publish", "python", tmp_path, ["publisher.py"], 1)
    assert "a-secret" not in (tmp_path / "logs/publish.log").read_text()


def test_windows_powershell_does_not_inherit_pwsh_module_path(monkeypatch, tmp_path):
    monkeypatch.setenv("PSModulePath", "incompatible-pwsh-modules")
    captured = []

    class Child:
        returncode = 0

        def communicate(self, timeout):
            return "OK", None

    def spawn(*args, **kwargs):
        captured.append(kwargs["env"])
        return Child()

    monkeypatch.setattr(subprocess, "Popen", spawn)
    runner = Runner(tmp_path, {}, tmp_path / "logs")
    runner.run("powershell", "powershell", tmp_path, ["probe.ps1"], 1)
    assert not any(key.upper() == "PSMODULEPATH" for key in captured[0])
    runner.run("python", "python", tmp_path, ["probe.py"], 1)
    assert any(key.upper() == "PSMODULEPATH" for key in captured[1])


@pytest.fixture
def pipeline(tmp_path, monkeypatch):
    config = {
        "schema": 1,
        "registry": "https://example.test",
        "timestamp_url": "http://example.test",
        "dist": "tools/dist",
        "steps": [],
        "source_paths": [],
        "generated_inputs": [],
        "required_assets": [],
    }
    monkeypatch.setattr("l4release.pipeline.read_config", lambda root: config)
    monkeypatch.setattr(
        "l4release.pipeline.input_snapshot", lambda root, config: {"source": "unchanged"}
    )
    monkeypatch.setattr(
        "l4release.pipeline.git", lambda root, *args: "revision" if args[0] == "rev-parse" else ""
    )
    signing_fixture = tmp_path.parent / f"{tmp_path.name}-test-signer.pfx"
    signing_fixture.write_bytes(b"test fixture, not a certificate")
    subject = Pipeline(
        tmp_path, "1.2.3", {"SW_SIGN_PFX": str(signing_fixture)}, tmp_path / "sw_sign.env"
    )
    monkeypatch.setattr(subject, "preflight", lambda signed, publish: None)
    return subject


def test_failure_stops_before_sign_and_publication(pipeline, monkeypatch):
    def fail_prepare():
        raise ReleaseError("component-test-failed")

    monkeypatch.setattr(pipeline, "prepare", fail_prepare)
    monkeypatch.setattr(
        pipeline, "ps", lambda *a, **k: pytest.fail("must not sign after failed tests")
    )
    with pytest.raises(ReleaseError, match="component-test-failed"):
        pipeline.execute(signed=True, publish=True)
    report = json.loads(pipeline.report_path.read_text())
    assert report["status"] == "failed"
    assert not report["checkpoints"]


def test_dirty_prepare_checkpoint_cannot_be_promoted_to_signed(pipeline, monkeypatch):
    pipeline.work.mkdir(parents=True)
    pipeline.report_path.write_text(
        json.dumps(
            {
                "schema": 1,
                "version": "1.2.3",
                "inputs_digest": snapshot_digest({"source": "unchanged"}),
                "clean_at_start": False,
                "build_options_digest": snapshot_digest({"L4TOOLS_POLICY_BOOTSTRAP_IP": ""}),
                "steps": [],
                "checkpoints": {},
            }
        )
    )
    monkeypatch.setattr(
        pipeline, "prepare", lambda: pytest.fail("must reject dirty input before build")
    )
    with pytest.raises(ReleaseError, match="clean source checkpoint"):
        pipeline.execute(signed=True, publish=False)


def test_changed_source_checkpoint_is_not_silently_rebuilt(pipeline, monkeypatch):
    pipeline.work.mkdir(parents=True)
    pipeline.report_path.write_text(
        json.dumps({"schema": 1, "version": "1.2.3", "inputs_digest": "old-digest"})
    )
    monkeypatch.setattr(pipeline, "prepare", lambda: pytest.fail("must not overwrite checkpoint"))
    with pytest.raises(ReleaseError, match="inputs changed"):
        pipeline.execute(signed=False, publish=False)


def test_source_change_mid_command_invalidates_run(pipeline, monkeypatch):
    pipeline.inputs = {"source": "unchanged"}
    pipeline.report = {"steps": []}

    def mutate(*args):
        monkeypatch.setattr("l4release.pipeline.input_snapshot", lambda *a: {"source": "changed"})

    monkeypatch.setattr(pipeline.runner, "run", mutate)
    with pytest.raises(ReleaseError, match="changed during"):
        pipeline.command("build", "cmd", pipeline.root, ["build.cmd"], 1)
    assert pipeline.report["steps"][0]["status"] == "failed"


def test_failed_signing_retries_from_retained_stage_without_component_rebuild(
    pipeline, monkeypatch
):
    stage = pipeline.root / "tools/dist/.stage/x86"
    stage.mkdir(parents=True)
    staged_exe = stage / "fixture.exe"
    staged_exe.write_bytes(b"partially signed fixture")
    pipeline.report = {
        "schema": 1,
        "version": pipeline.version,
        "source_revision": "revision",
        "clean_at_start": True,
        "build_options_digest": snapshot_digest({"L4TOOLS_POLICY_BOOTSTRAP_IP": ""}),
        "inputs_digest": snapshot_digest({"source": "unchanged"}),
        "steps": [],
        "checkpoints": {},
    }
    pipeline.inputs = {"source": "unchanged"}
    pipeline.checkpoint("signing")
    monkeypatch.setattr(
        pipeline, "prepare", lambda: pytest.fail("must not rebuild signed components")
    )
    calls = []

    def signing(name, *args, **kwargs):
        calls.append(name)
        if name == "sign":
            raise ReleaseError("timestamp unavailable")

    monkeypatch.setattr(pipeline, "ps", signing)
    with pytest.raises(ReleaseError, match="timestamp unavailable"):
        pipeline.execute(signed=True, publish=False)
    assert calls == ["sign"]
    assert pipeline.resumable("signing")


def test_signed_checkpoint_still_verifies_before_publishing(pipeline, monkeypatch):
    calls = []
    monkeypatch.setattr(pipeline, "resumable", lambda phase: phase == "signed")
    monkeypatch.setattr(pipeline, "prepare", lambda: pytest.fail("must not rebuild"))
    monkeypatch.setattr(pipeline, "ps", lambda name, *a, **k: calls.append(name))

    class Publisher:
        @staticmethod
        def verify_artifacts(path):
            calls.append("checksums")

    monkeypatch.setattr("l4release.pipeline.publisher_module", lambda root: Publisher())
    monkeypatch.setattr(pipeline, "command", lambda name, *a, **k: calls.append(name))
    result = pipeline.execute(signed=True, publish=True)
    assert calls == ["verify-signed", "checksums", "publish"]
    assert result["status"] == "published_candidate"
    assert result["terminal_gate"] == result["promotion"] == "not_run"
