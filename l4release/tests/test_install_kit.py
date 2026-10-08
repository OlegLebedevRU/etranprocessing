import json

import pytest

from l4release import install_kit
from l4release.catalog import read_catalog
from l4release.common import ReleaseError
from l4release.tests.test_bootstrap import seal as seal
from l4release.tests.test_metadata_publication import packet as packet
from l4release.tests.test_metadata_publication import signing_key as signing_key


def test_kit_exact_release_expiry_and_no_overwrite(
    packet, signing_key, seal, tmp_path, monkeypatch
):
    monkeypatch.setattr(install_kit, "load_signing_key", lambda *args: signing_key)
    monkeypatch.setattr(install_kit, "verify_embedded_key", lambda *args: None)
    root = __import__("pathlib").Path(__file__).resolve().parents[2]
    output = tmp_path / "kit"
    install_kit.create_install_kit(
        root,
        packet,
        "1.13.3",
        output,
        {"L4TOOLS_BOOTSTRAP_DIR": str(seal)},
        tmp_path / "test.env",
        now=100,
    )
    data = (output / "setup-catalog.json").read_bytes()
    signature = (output / "setup-catalog.json.sig").read_bytes()
    catalog = read_catalog(data, signature, signing_key.public_key(), now=100)
    assert catalog["stable"] is None and catalog["transitions"] == []
    assert catalog["releases"][0]["version"] == "1.13.3"
    assert (output / "l4setup.exe").read_bytes() == (packet / "l4setup.exe").read_bytes()
    with pytest.raises(ReleaseError, match="expired"):
        read_catalog(data, signature, signing_key.public_key(), now=100 + 7 * 86400)
    with pytest.raises(ReleaseError, match="already exists"):
        install_kit.create_install_kit(root, packet, "1.13.3", output, {}, tmp_path / "test.env")
    manifest = json.loads((packet / "l4tools-release.json").read_text())
    assert catalog["key_id"] == manifest["metadata_signatures"]["key_id"]
    assert (
        json.loads((output / "l4tools-bootstrap.json").read_bytes())["manifest_sha256"]
        == catalog["releases"][0]["manifest_sha256"]
    )
    assert (output / "l4rollback-x86.exe").read_bytes() == (
        seal / "l4rollback-x86.exe"
    ).read_bytes()


def test_wrong_version_and_corruption_never_create_kit(packet, signing_key, tmp_path, monkeypatch):
    monkeypatch.setattr(install_kit, "load_signing_key", lambda *args: signing_key)
    monkeypatch.setattr(install_kit, "verify_embedded_key", lambda *args: None)
    root = __import__("pathlib").Path(__file__).resolve().parents[2]
    output = tmp_path / "kit"
    with pytest.raises(ReleaseError, match="differs"):
        install_kit.create_install_kit(root, packet, "1.13.4", output, {}, tmp_path / "test.env")
    assert not output.exists()
    with (packet / "l4setup.exe").open("ab") as stream:
        stream.write(b"corrupt")
    with pytest.raises(ValueError):
        install_kit.create_install_kit(root, packet, "1.13.3", output, {}, tmp_path / "test.env")
    assert not output.exists()


def test_sealed_source_drift_cannot_return_verified_kit(
    packet, signing_key, seal, tmp_path, monkeypatch
):
    monkeypatch.setattr(install_kit, "load_signing_key", lambda *args: signing_key)
    monkeypatch.setattr(install_kit, "verify_embedded_key", lambda *args: None)
    original = install_kit.kit_bootstrap

    def drifting(*args, **kwargs):
        result = original(*args, **kwargs)
        helper = seal / "l4rollback-x86.exe"
        helper.write_bytes(helper.read_bytes() + b"drift after authenticated seal read")
        return result

    monkeypatch.setattr(install_kit, "kit_bootstrap", drifting)
    root = __import__("pathlib").Path(__file__).resolve().parents[2]
    with pytest.raises(ReleaseError, match="Bootstrap kit copy"):
        install_kit.create_install_kit(
            root,
            packet,
            "1.13.3",
            tmp_path / "drift-kit",
            {"L4TOOLS_BOOTSTRAP_DIR": str(seal)},
            tmp_path / "test.env",
        )
