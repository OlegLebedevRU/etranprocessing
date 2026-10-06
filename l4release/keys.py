"""One owner-managed RSA metadata key; no automatic rotation."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa

from .common import ReleaseError


def init_key(root: Path, env: dict[str, str], env_path: Path) -> str:
    value = env.get("L4TOOLS_METADATA_KEY_PATH", "")
    password = env.get("L4TOOLS_METADATA_KEY_PASSWORD", "")
    if not value or not password:
        raise ReleaseError("Set L4TOOLS_METADATA_KEY_PATH and L4TOOLS_METADATA_KEY_PASSWORD")
    path = Path(value)
    if not path.is_absolute():
        path = env_path.parent / path
    path = path.resolve()
    if path.is_relative_to(root.resolve()):
        raise ReleaseError("Private metadata key must be outside the repository")
    public_path = path.with_suffix(".public.pem")
    if path.exists() or public_path.exists():
        raise ReleaseError("Key already exists; keys init never overwrites or rotates keys")
    if not path.parent.is_dir():
        raise ReleaseError("Create a protected key directory before keys init")
    key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
    private = key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.BestAvailableEncryption(password.encode("utf-8")),
    )
    public = key.public_key()
    probe = b"l4tools-metadata-key-self-test-v1"
    public.verify(
        key.sign(probe, padding.PKCS1v15(), hashes.SHA256()),
        probe,
        padding.PKCS1v15(),
        hashes.SHA256(),
    )
    public_bytes = public.public_bytes(
        serialization.Encoding.PEM,
        serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    created = []
    try:
        for target, data in ((path, private), (public_path, public_bytes)):
            descriptor = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            created.append(target)
            with os.fdopen(descriptor, "wb") as stream:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
    except OSError as error:
        for target in created:
            target.unlink(missing_ok=True)
        raise ReleaseError("Key files could not be created; no key was retained") from error
    return hashlib.sha256(
        public.public_bytes(
            serialization.Encoding.DER,
            serialization.PublicFormat.SubjectPublicKeyInfo,
        )
    ).hexdigest()
