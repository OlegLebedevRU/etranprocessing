"""Exact-byte detached metadata signatures; one owner-managed RSA3072 key."""

from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa

from .common import ReleaseError, atomic_bytes, atomic_json, file_hash

MAX_DOCUMENT = 65535
SIGNATURE_SIZE = 384
SIGNATURE_FILES = (
    "l4tools-layout-x86.json.sig",
    "l4tools-layout-x64.json.sig",
    "l4tools-release.json.sig",
)


def key_id(key: rsa.RSAPublicKey) -> str:
    _public(key)
    return hashlib.sha256(
        key.public_bytes(
            serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo
        )
    ).hexdigest()


def load_public_key(env: dict[str, str], env_path: Path) -> rsa.RSAPublicKey:
    value = env.get("L4TOOLS_METADATA_KEY_PATH", "")
    if not value:
        raise ReleaseError("Set L4TOOLS_METADATA_KEY_PATH to locate its .public.pem")
    path = Path(value)
    if not path.is_absolute():
        path = env_path.parent / path
    try:
        with path.with_suffix(".public.pem").open("rb") as stream:
            data = stream.read(16385)
        if len(data) > 16384:
            raise ReleaseError("Metadata public key exceeds16384 bytes")
        key = serialization.load_pem_public_key(data)
    except (OSError, ValueError) as error:
        raise ReleaseError("Cannot load the configured metadata public key") from error
    if not isinstance(key, rsa.RSAPublicKey):
        raise ReleaseError("Metadata public key must be RSA")
    _public(key)
    return key


def public_header(key: rsa.RSAPublicKey) -> str:
    blob = public_blob(key)
    lines = [", ".join(f"0x{b:02x}" for b in blob[i : i + 12]) for i in range(0, len(blob), 12)]
    return (
        "/* Public owner trust root. Replacement requires an owner-approved signed bootstrap. */\n"
        "#pragma once\n"
        f'#define L4_METADATA_OWNER_KEY_ID "{key_id(key)}"\n'
        "static const BYTE L4_METADATA_OWNER_KEY[] = {\n    " + ",\n    ".join(lines) + "\n};\n"
    )


def verify_embedded_key(root: Path, key: rsa.RSAPublicKey) -> None:
    path = root / "tools/l4common/metadata_owner_key.h"
    if path.read_text(encoding="utf-8") != public_header(key):
        raise ReleaseError("Configured metadata key differs from embedded owner trust root")


def sign_release_metadata(dist: Path, key: rsa.RSAPrivateKey) -> None:
    """Caller holds the pipeline lock and owns sealed signed staging. No upload."""
    manifest_path = dist / "l4tools-release.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    if set(manifest.get("layout_payloads", {})) != {"x86", "x64"}:
        raise ReleaseError("Metadata signing requires both layout payloads")
    if not manifest.get("publisher_certificate_sha256"):
        raise ReleaseError("Cannot sign unsigned release metadata")
    for arch in ("x86", "x64"):
        name = f"l4tools-layout-{arch}.json.sig"
        atomic_bytes(dist / name, sign_bytes((dist / name.removesuffix(".sig")).read_bytes(), key))
        manifest["files"][name] = {"sha256": file_hash(dist / name), "size": SIGNATURE_SIZE}
    manifest["metadata_signatures"] = {
        "schema": 1,
        "algorithm": "RSA3072-PKCS1v1.5-SHA256",
        "key_id": key_id(key.public_key()),
        "signature": "l4tools-release.json.sig",
    }
    atomic_json(manifest_path, manifest)
    atomic_bytes(dist / "l4tools-release.json.sig", sign_bytes(manifest_path.read_bytes(), key))
    # Root signature cannot be inventoried inside its own signed document.
    names = (*manifest["files"], "l4tools-release.json.sig", "l4tools-release.json")
    atomic_bytes(
        dist / "SHA256SUMS",
        "".join(f"{file_hash(dist / name)}  {name}\n" for name in names).encode("ascii"),
    )


def _public(key: rsa.RSAPublicKey) -> rsa.RSAPublicNumbers:
    numbers = key.public_numbers()
    if key.key_size != 3072 or numbers.e != 65537:
        raise ReleaseError("Metadata key must be RSA3072 with exponent65537")
    return numbers


def public_blob(key: rsa.RSAPublicKey) -> bytes:
    """Windows BCRYPT_RSAPUBLIC_BLOB. Public data only; never a downloaded trust root."""
    numbers = _public(key)
    return (
        struct.pack("<6I", 0x31415352, 3072, 3, 384, 0, 0)
        + b"\x01\x00\x01"
        + (numbers.n.to_bytes(384, "big"))
    )


def _document(document: bytes) -> None:
    if not 0 < len(document) <= MAX_DOCUMENT:
        raise ReleaseError("Metadata document must contain1..65535 exact bytes")


def sign_bytes(document: bytes, key: rsa.RSAPrivateKey) -> bytes:
    _document(document)
    _public(key.public_key())
    signature = key.sign(document, padding.PKCS1v15(), hashes.SHA256())
    verify_bytes(document, signature, key.public_key())
    return signature


def verify_bytes(document: bytes, signature: bytes, key: rsa.RSAPublicKey) -> None:
    _document(document)
    _public(key)
    if len(signature) != SIGNATURE_SIZE:
        raise ReleaseError("Metadata signature must contain384 raw bytes")
    try:
        key.verify(signature, document, padding.PKCS1v15(), hashes.SHA256())
    except InvalidSignature as error:
        raise ReleaseError("Metadata signature verification failed") from error


def load_signing_key(root: Path, env: dict[str, str], env_path: Path) -> rsa.RSAPrivateKey:
    """Use the existing external encrypted key; never create or rotate one here."""
    value, password = env.get("L4TOOLS_METADATA_KEY_PATH"), env.get("L4TOOLS_METADATA_KEY_PASSWORD")
    if not value or not password:
        raise ReleaseError("Set L4TOOLS_METADATA_KEY_PATH and L4TOOLS_METADATA_KEY_PASSWORD")
    path = Path(value)
    if not path.is_absolute():
        path = env_path.parent / path
    path = path.resolve()
    if path.is_relative_to(root.resolve()):
        raise ReleaseError("Private metadata key must be outside the repository")
    try:
        if path.stat().st_size > 16384:
            raise ReleaseError("Metadata key file exceeds16384 bytes")
        key = serialization.load_pem_private_key(
            path.read_bytes(), password=password.encode("utf-8")
        )
    except (OSError, ValueError, TypeError) as error:
        raise ReleaseError("Cannot load the external encrypted metadata key") from error
    if not isinstance(key, rsa.RSAPrivateKey):
        raise ReleaseError("Metadata signing key must be RSA")
    _public(key.public_key())
    return key
