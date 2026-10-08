"""Metadata/pipeline fixtures, not Authenticode or SYSTEM installation evidence."""

import json
import struct

import pytest

from l4release import bootstrap
from l4release.common import ReleaseError, file_hash
from l4release.metadata import key_id, verify_bytes
from l4release.tests.test_metadata_publication import signing_key as signing_key


def pe(arch):
    return (
        b"MZ"
        + bytes(58)
        + struct.pack("<I", 64)
        + b"PE\0\0"
        + struct.pack("<H", 0x14C if arch == "x86" else 0x8664)
        + b"unsigned fixture only"
    )


@pytest.fixture
def seal(tmp_path, signing_key):
    directory = tmp_path / "seal"
    directory.mkdir()
    helpers = {}
    for arch in ("x86", "x64"):
        name = f"l4rollback-{arch}.exe"
        path = directory / name
        path.write_bytes(pe(arch))
        helpers[arch] = {"name": name, "size": path.stat().st_size, "sha256": file_hash(path)}
    doc = {
        "schema": 1,
        "kind": "frozen-supervisor-helper",
        "key_id": key_id(signing_key.public_key()),
        "publisher_certificate_sha256": "11" * 32,
        "helpers": helpers,
    }
    data, signature = bootstrap.bootstrap_bytes(doc, signing_key)
    (directory / "bootstrap-seal.json").write_bytes(data)
    (directory / "bootstrap-seal.json.sig").write_bytes(signature)
    return directory


def test_kit_binds_current_root_without_changing_seal(seal, signing_key, tmp_path):
    before = {p.name: file_hash(p) for p in seal.iterdir()}
    root = tmp_path / "repo"
    root.mkdir()
    manifest = {"version": "1.13.3", "publisher_certificate_sha256": "11" * 32}
    data, sig, assets = bootstrap.kit_bootstrap(
        root,
        {"L4TOOLS_BOOTSTRAP_DIR": str(seal)},
        tmp_path / "test.env",
        manifest,
        "22" * 32,
        signing_key,
    )
    verify_bytes(data, sig, signing_key.public_key())
    doc = json.loads(data)
    assert doc["manifest_sha256"] == "22" * 32 and doc["version"] == "1.13.3"
    assert set(assets) == {"l4rollback-x86.exe", "l4rollback-x64.exe"}
    assert before == {p.name: file_hash(p) for p in seal.iterdir()}
    manifest["publisher_certificate_sha256"] = "33" * 32
    with pytest.raises(ReleaseError, match="publisher"):
        bootstrap.kit_bootstrap(
            root,
            {"L4TOOLS_BOOTSTRAP_DIR": str(seal)},
            tmp_path / "test.env",
            manifest,
            "22" * 32,
            signing_key,
        )


def test_seal_tamper_and_missing_assets_refuse(seal, signing_key, tmp_path):
    root = tmp_path / "repo"
    env = {"L4TOOLS_BOOTSTRAP_DIR": str(seal)}
    manifest = {"version": "1.13.3", "publisher_certificate_sha256": "11" * 32}
    path = seal / "l4rollback-x86.exe"
    path.write_bytes(path.read_bytes() + b"modified")
    with pytest.raises(ReleaseError, match="differs"):
        bootstrap.kit_bootstrap(root, env, tmp_path / "test.env", manifest, "22" * 32, signing_key)
    (seal / "bootstrap-seal.json.sig").unlink()
    with pytest.raises(FileNotFoundError):
        bootstrap.kit_bootstrap(root, env, tmp_path / "test.env", manifest, "22" * 32, signing_key)
    with pytest.raises(ReleaseError, match="L4TOOLS_BOOTSTRAP_DIR"):
        bootstrap.kit_bootstrap(root, {}, tmp_path / "test.env", manifest, "22" * 32, signing_key)


def test_initial_seal_only_signs_copy_and_refuses_reseal(tmp_path, signing_key, monkeypatch):
    root = tmp_path / "repo"
    approved = {}
    for arch in ("x86", "x64"):
        path = root / f"tools/l4rollback/bin/{arch}/l4rollback.exe"
        path.parent.mkdir(parents=True)
        path.write_bytes(pe(arch))
        approved[arch] = (path.stat().st_size, file_hash(path))
    monkeypatch.setattr(bootstrap, "APPROVED", approved)
    monkeypatch.setattr(bootstrap, "verify_embedded_key", lambda *args: None)
    calls = []

    class Runner:
        def run(self, name, kind, cwd, args, timeout):
            calls.append(name)
            if "-PublisherSha256" not in args:
                from pathlib import Path

                path = Path(args[args.index("-TargetPath") + 1])
                path.write_bytes(path.read_bytes() + b"modeled signature, not trust evidence")

    output = tmp_path / "sealed"
    bootstrap.seal_bootstrap(root, output, signing_key, "11" * 32, Runner())
    assert calls == [
        "bootstrap-sign-x86",
        "bootstrap-verify-x86",
        "bootstrap-sign-x64",
        "bootstrap-verify-x64",
    ]
    for arch in ("x86", "x64"):
        assert file_hash(root / f"tools/l4rollback/bin/{arch}/l4rollback.exe") == approved[arch][1]
        assert file_hash(output / f"l4rollback-{arch}.exe") != approved[arch][1]
    verify_bytes(
        (output / "bootstrap-seal.json").read_bytes(),
        (output / "bootstrap-seal.json.sig").read_bytes(),
        signing_key.public_key(),
    )
    with pytest.raises(ReleaseError, match="never overwrite"):
        bootstrap.seal_bootstrap(root, output, signing_key, "11" * 32, Runner())
    path = root / "tools/l4rollback/bin/x64/l4rollback.exe"
    path.write_bytes(path.read_bytes() + b"changed native bits")
    with pytest.raises(ReleaseError, match="owner-approved"):
        bootstrap.seal_bootstrap(root, tmp_path / "second", signing_key, "11" * 32, Runner())
    assert not (tmp_path / "second").exists()
