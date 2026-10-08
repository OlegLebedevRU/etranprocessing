import json

import pytest
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa

from deploy.publish_l4tools import artifact_upload_order, publish_release, verify_artifacts
from l4release.common import ReleaseError, atomic_json, file_hash
from l4release.layout_payload import build_layout_payload
from l4release.metadata import (
    SIGNATURE_FILES,
    key_id,
    public_header,
    sign_release_metadata,
    verify_embedded_key,
)
from l4release.pipeline import Pipeline
from l4release.tests.test_layout_payload import make_stage


@pytest.fixture(scope="module")
def signing_key():
    return rsa.generate_private_key(public_exponent=65537, key_size=3072)


@pytest.fixture
def packet(tmp_path, signing_key):
    stage = tmp_path / "stage"
    make_stage(stage)
    dist = tmp_path / "dist"
    dist.mkdir()
    (dist / "l4setup.exe").write_bytes(b"not a signed executable; metadata-only test")
    manifest = {
        "version": "1.13.3",
        "publisher_certificate_sha256": "11" * 32,
        "files": {},
        "layout_payloads": {},
    }
    for arch in ("x86", "x64"):
        manifest["layout_payloads"][arch] = build_layout_payload(
            stage, dist, "1.13.3", arch, "11" * 32
        )
    for p in dist.iterdir():
        manifest["files"][p.name] = {"sha256": file_hash(p), "size": p.stat().st_size}
    atomic_json(dist / "l4tools-release.json", manifest)
    sign_release_metadata(dist, signing_key)
    return dist


def test_complete_signed_packet_and_metadata_last(packet, signing_key):
    manifest, digests, sizes = verify_artifacts(
        packet, trusted_public=signing_key.public_key(), require_metadata=True
    )
    assert manifest["metadata_signatures"]["key_id"] == key_id(signing_key.public_key())
    assert "l4tools-release.json.sig" not in manifest["files"]
    assert all(sizes[name] == 384 for name in SIGNATURE_FILES)
    order = artifact_upload_order(digests)
    assert len(order) == 10 and order[-1] == "l4tools-release.json"
    assert order.index("l4tools-release.json.sig") < order.index("SHA256SUMS")
    with pytest.raises(ValueError, match="locally trusted"):
        verify_artifacts(packet)


@pytest.mark.parametrize(
    "name",
    [
        "l4tools-release.json",
        "l4tools-layout-x86.json",
        "l4tools-layout-x64.json.sig",
        "l4tools-release.json.sig",
    ],
)
def test_tamper_recomputed_checksums_cannot_bypass_signature(packet, signing_key, name):
    p = packet / name
    data = p.read_bytes()
    p.write_bytes(data + b" " if name.endswith(".json") else bytes([data[0] ^ 1]) + data[1:])
    # An attacker can recompute checksums; authenticity still must fail.
    (packet / "SHA256SUMS").write_text(
        "".join(f"{file_hash(f)}  {f.name}\n" for f in packet.iterdir() if f.name != "SHA256SUMS"),
        encoding="ascii",
    )
    with pytest.raises(ValueError, match="signature verification"):
        verify_artifacts(packet, trusted_public=signing_key.public_key(), require_metadata=True)


def test_missing_signature_and_wrong_key_refuse(packet, signing_key):
    wrong = rsa.generate_private_key(public_exponent=65537, key_size=3072)
    with pytest.raises(ValueError, match="identity/format"):
        verify_artifacts(packet, trusted_public=wrong.public_key())
    (packet / "l4tools-release.json.sig").unlink()
    with pytest.raises(FileNotFoundError):
        verify_artifacts(packet, trusted_public=signing_key.public_key())


def test_unsigned_downgrade_refuses_required_metadata(packet, signing_key):
    p = packet / "l4tools-release.json"
    manifest = json.loads(p.read_text())
    manifest.pop("metadata_signatures")
    atomic_json(p, manifest)
    with pytest.raises(ValueError, match="required"):
        verify_artifacts(packet, trusted_public=signing_key.public_key(), require_metadata=True)


def test_publication_reads_only_configured_public_key(packet, signing_key, tmp_path, monkeypatch):
    key_path = tmp_path / "owner.pem"  # No private file is created or needed by publisher.
    key_path.with_suffix(".public.pem").write_bytes(
        signing_key.public_key().public_bytes(
            serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo
        )
    )
    env = tmp_path / "fixture.env"
    env.write_text(f"L4TOOLS_METADATA_KEY_PATH={key_path}\n", encoding="utf-8")
    monkeypatch.setattr("deploy.publish_l4tools.check_remote_version", lambda *a, **k: 0)
    assert publish_release(packet, env_file=env, dry_run=True, skip_check=True) == 0


def test_signature_set_cannot_be_partial():
    with pytest.raises(ValueError, match="Incomplete"):
        artifact_upload_order({"l4tools-layout-x86.json.sig": "fixture"})


def test_configured_key_must_match_embedded_root(tmp_path, signing_key):
    p = tmp_path / "tools/l4common/metadata_owner_key.h"
    p.parent.mkdir(parents=True)
    p.write_text(public_header(signing_key.public_key()), encoding="utf-8")
    verify_embedded_key(tmp_path, signing_key.public_key())
    p.write_text(p.read_text().replace("0x", "0X", 1), encoding="utf-8")
    with pytest.raises(ReleaseError, match="embedded"):
        verify_embedded_key(tmp_path, signing_key.public_key())


def test_signed_checkpoint_covers_signature_bytes(packet, tmp_path, monkeypatch):
    root = tmp_path / "repo"
    dist = root / "tools/dist"
    dist.parent.mkdir(parents=True)
    packet.rename(dist)
    for name in (
        "tools/dist/.stage/fixture",
        "tools/l4setup/res/payload_x86.bin",
        "tools/l4setup/res/payload_x64.bin",
        "tools/l4setup/bin/l4setup.exe",
    ):
        p = root / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(b"fixture")
    monkeypatch.setattr("l4release.pipeline.read_config", lambda root: {"dist": "tools/dist"})
    pipeline = Pipeline(root, "1.13.3", {}, tmp_path / "fixture.env")
    pipeline.report = {"checkpoints": {}}
    pipeline.inputs = {"source": "fixture"}
    pipeline.checkpoint("signed")
    assert pipeline.resumable("signed")
    outputs = pipeline.report["checkpoints"]["signed"]["outputs"]
    assert all(f"tools/dist/{name}" in outputs for name in SIGNATURE_FILES)
    (dist / SIGNATURE_FILES[0]).write_bytes(b"tampered")
    assert not pipeline.resumable("signed")


def test_unsigned_preparation_cannot_be_metadata_signed(packet, signing_key):
    p = packet / "l4tools-release.json"
    manifest = json.loads(p.read_text())
    manifest["publisher_certificate_sha256"] = None
    atomic_json(p, manifest)
    with pytest.raises(ReleaseError, match="unsigned"):
        sign_release_metadata(packet, signing_key)
