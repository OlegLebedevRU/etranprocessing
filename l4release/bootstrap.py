"""One-time sealing of approved recovery bits; ordinary kits reuse exact sealed bytes."""

from __future__ import annotations

import json
import shutil
import struct
from collections.abc import Callable
from pathlib import Path

from cryptography.hazmat.primitives.asymmetric import rsa

from .common import ReleaseError, file_hash
from .metadata import key_id, sign_bytes, verify_bytes, verify_embedded_key

# Owner-reviewed initial native bits. Changes require a justified bootstrap decision,
# never an ordinary suite/updater build. Authenticode modifies only a staged copy.
APPROVED = {
    "x86": (154112, "8a3146938a11e68e0f50d5e86d09f5d635162bbc697be191434edc8d06733ee2"),
    "x64": (185856, "d52dd106371533a49eb6512407e9ac3abdca07272b17f4640fee03bafc11dc7e"),
}


def _pe(path: Path, arch: str) -> None:
    with path.open("rb") as stream:
        header = stream.read(64)
        if len(header) != 64 or header[:2] != b"MZ":
            raise ReleaseError("Bootstrap asset is not a PE")
        stream.seek(struct.unpack_from("<I", header, 60)[0])
        pe = stream.read(6)
    if pe != b"PE\0\0" + struct.pack("<H", 0x14C if arch == "x86" else 0x8664):
        raise ReleaseError("Bootstrap native architecture differs")


def bootstrap_bytes(document: dict, key: rsa.RSAPrivateKey) -> tuple[bytes, bytes]:
    data = (json.dumps(document, sort_keys=True, separators=(",", ":")) + "\n").encode()
    return data, sign_bytes(data, key)


def seal_bootstrap(
    root: Path,
    output: Path,
    key: rsa.RSAPrivateKey,
    publisher: str,
    runner,
    checkpoint: Callable[[], None] | None = None,
):
    """Caller holds release lock and clean-source checkpoint. Never rebuild/re-sign."""
    verify_embedded_key(root, key.public_key())
    if output.exists():
        raise ReleaseError("Bootstrap seal already exists; never overwrite or re-sign")
    inputs = {}
    for arch, (size, digest) in APPROVED.items():
        path = root / f"tools/l4rollback/bin/{arch}/l4rollback.exe"
        if path.stat().st_size != size or file_hash(path) != digest:
            raise ReleaseError("Frozen native helper differs from owner-approved initial bits")
        _pe(path, arch)
        inputs[arch] = path
    output.mkdir(parents=True, exist_ok=False)
    helpers = {}
    for arch, source in inputs.items():
        name = f"l4rollback-{arch}.exe"
        staged = output / name
        shutil.copyfile(source, staged)
        if file_hash(staged) != APPROVED[arch][1]:
            raise ReleaseError("Bootstrap copy failed integrity verification")
        runner.run(
            f"bootstrap-sign-{arch}",
            "powershell",
            root,
            [str(root / "tools/release/Sign-Executables.ps1"), "-TargetPath", str(staged)],
            300,
        )
        runner.run(
            f"bootstrap-verify-{arch}",
            "powershell",
            root,
            [
                str(root / "tools/release/Verify-Bootstrap.ps1"),
                "-TargetPath",
                str(staged),
                "-PublisherSha256",
                publisher,
            ],
            300,
        )
        _pe(staged, arch)
        if file_hash(source) != APPROVED[arch][1]:
            raise ReleaseError("Signing changed the frozen source helper")
        helpers[arch] = {"name": name, "size": staged.stat().st_size, "sha256": file_hash(staged)}
    if checkpoint is not None:
        checkpoint()
    document = {
        "schema": 1,
        "kind": "frozen-supervisor-helper",
        "key_id": key_id(key.public_key()),
        "publisher_certificate_sha256": publisher,
        "helpers": helpers,
    }
    data, signature = bootstrap_bytes(document, key)
    (output / "bootstrap-seal.json").write_bytes(data)
    # Signature is the completion checkpoint, written last. Partial seals are not reusable.
    (output / "bootstrap-seal.json.sig").write_bytes(signature)
    return output


def kit_bootstrap(
    root: Path,
    env: dict[str, str],
    env_path: Path,
    manifest: dict,
    manifest_hash: str,
    key: rsa.RSAPrivateKey,
) -> tuple[bytes, bytes, dict[str, Path]]:
    value = env.get("L4TOOLS_BOOTSTRAP_DIR", "")
    if not value:
        raise ReleaseError("Set L4TOOLS_BOOTSTRAP_DIR to the immutable signed bootstrap seal")
    directory = Path(value)
    if not directory.is_absolute():
        directory = env_path.parent / directory
    directory = directory.resolve()
    if directory.is_relative_to(root.resolve()):
        raise ReleaseError("Bootstrap seal must be outside the repository")
    data = (directory / "bootstrap-seal.json").read_bytes()
    verify_bytes(data, (directory / "bootstrap-seal.json.sig").read_bytes(), key.public_key())

    def unique(pairs):
        result = {}
        for name, value in pairs:
            if name in result:
                raise ReleaseError("Duplicate bootstrap metadata field")
            result[name] = value
        return result

    doc = json.loads(data, object_pairs_hook=unique)
    if (
        set(doc) != {"schema", "kind", "key_id", "publisher_certificate_sha256", "helpers"}
        or doc["schema"] != 1
        or doc["kind"] != "frozen-supervisor-helper"
        or doc["key_id"] != key_id(key.public_key())
        or doc["publisher_certificate_sha256"] != manifest["publisher_certificate_sha256"]
        or set(doc["helpers"]) != {"x86", "x64"}
    ):
        raise ReleaseError("Bootstrap seal identity/publisher/schema differs")
    assets = {}
    for arch in ("x86", "x64"):
        item = doc["helpers"][arch]
        name = f"l4rollback-{arch}.exe"
        path = directory / name
        if (
            set(item) != {"name", "size", "sha256"}
            or item["name"] != name
            or type(item["size"]) is not int
            or not 0 < item["size"] <= 0xFFFFFFFF
            or path.stat().st_size != item["size"]
            or file_hash(path) != item["sha256"]
        ):
            raise ReleaseError("Frozen signed bootstrap asset differs from sealed receipt")
        _pe(path, arch)
        assets[name] = path
    doc["version"] = manifest["version"]
    doc["manifest_sha256"] = manifest_hash
    data, signature = bootstrap_bytes(doc, key)
    return data, signature, assets
