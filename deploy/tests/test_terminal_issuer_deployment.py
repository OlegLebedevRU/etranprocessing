"""Private distribution refuses traversal, duplicates and conflicting material."""

import importlib.util
import io
import tarfile
from pathlib import Path

import pytest

SOURCE = Path(__file__).resolve().parents[1] / "configure_terminal_issuer.py"
spec = importlib.util.spec_from_file_location("terminal_issuer_installer", SOURCE)
assert spec is not None and spec.loader is not None
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


def bundle(members):
    result = io.BytesIO()
    with tarfile.open(fileobj=result, mode="w") as archive:
        for name, data in members:
            member = tarfile.TarInfo(name)
            member.size = len(data)
            archive.addfile(member, io.BytesIO(data))
    result.seek(0)
    return result


@pytest.mark.parametrize(
    "name", ["../issuer.key.pem", "/issuer.key.pem", "unexpected.env"]
)
def test_private_bundle_rejects_unexpected_paths(name):
    with pytest.raises(ValueError, match="Unexpected"):
        installer.read_secret_bundle(bundle([(name, b"fixture")]))


def test_private_bundle_rejects_duplicates():
    with pytest.raises(ValueError, match="duplicate"):
        installer.read_secret_bundle(
            bundle([("issuer.key.pem", b"fixture"), ("issuer.key.pem", b"fixture")])
        )


def test_private_bundle_rejects_plaintext_keys():
    with pytest.raises(ValueError, match="encrypted"):
        installer.read_secret_bundle(
            bundle(
                [
                    ("issuer.key.pem", b"-----BEGIN PRIVATE KEY-----"),
                    ("issuer-password.env", b"fixture"),
                ]
            )
        )


def test_private_bundle_rejects_incomplete_distribution():
    with pytest.raises(ValueError, match="Incomplete"):
        installer.read_secret_bundle(bundle([("issuer-password.env", b"fixture")]))


def test_existing_secret_is_not_overwritten(tmp_path):
    directory = tmp_path / "secrets"
    directory.mkdir()
    key = directory / "issuer.key.pem"
    key.write_bytes(b"existing-fixture")
    with pytest.raises(ValueError, match="Different"):
        installer.provision_secrets(
            directory, {"issuer.key.pem": b"replacement-fixture"}
        )
    assert key.read_bytes() == b"existing-fixture"


def test_conflicting_ca_mount_is_rejected(tmp_path):
    service = {"volumes": ["/unapproved:/run/secrets/terminal-ca:rw"]}
    with pytest.raises(ValueError, match="unexpected"):
        installer.issuer_service(service, tmp_path)
