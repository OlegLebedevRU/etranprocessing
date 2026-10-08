"""Bounded owner-signed catalog; directed evidence-backed transitions only."""

from __future__ import annotations

import hashlib
import json
import re
from collections import deque

from cryptography.hazmat.primitives.asymmetric import rsa

from .common import ReleaseError, version_value
from .metadata import key_id, sign_bytes, verify_bytes

MAX_RELEASES = MAX_TRANSITIONS = 24
MAX_TIME = 253402300799


def _fields(value: object, expected: set[str]) -> dict:
    if not isinstance(value, dict) or set(value) != expected:
        raise ReleaseError("Catalog fields do not match schema1")
    return value


def _uint(value: object, maximum: int) -> int:
    if type(value) is not int or not 0 < value <= maximum:
        raise ReleaseError("Invalid catalog integer")
    return value


def _digest(value: object) -> str:
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value) or value == "0" * 64:
        raise ReleaseError("Invalid catalog SHA256")
    return value


def validate_catalog(document: dict, *, expected_key_id: str, now: int) -> dict:
    _fields(
        document,
        {
            "schema",
            "key_id",
            "revision",
            "issued_at",
            "expires_at",
            "stable",
            "releases",
            "transitions",
        },
    )
    if type(document["schema"]) is not int or document["schema"] != 1:
        raise ReleaseError("Unsupported catalog schema")
    if _digest(document["key_id"]) != expected_key_id:
        raise ReleaseError("Catalog key identity mismatch")
    _uint(document["revision"], 2**64 - 1)
    issued = _uint(document["issued_at"], MAX_TIME)
    expires = _uint(document["expires_at"], MAX_TIME)
    _uint(now, MAX_TIME)
    if not issued <= now < expires:
        raise ReleaseError("Catalog is future-dated or expired")
    releases = document["releases"]
    transitions = document["transitions"]
    if not isinstance(releases, list) or not 1 <= len(releases) <= MAX_RELEASES:
        raise ReleaseError("Catalog release count exceeds bound")
    if not isinstance(transitions, list) or len(transitions) > MAX_TRANSITIONS:
        raise ReleaseError("Catalog transition count exceeds bound")
    known = {}
    for release in releases:
        _fields(release, {"version", "manifest_sha256", "revoked"})
        if not isinstance(release["version"], str):
            raise ReleaseError("Invalid catalog version")
        if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", release["version"]):
            raise ReleaseError("Catalog version must match the native three-part layout")
        version = version_value(release["version"])
        if len(version) >= 64 or version in known or type(release["revoked"]) is not bool:
            raise ReleaseError("Duplicate/invalid catalog release")
        _digest(release["manifest_sha256"])
        known[version] = release
    stable = document["stable"]
    if stable is not None and not isinstance(stable, str):
        raise ReleaseError("Invalid catalog stable version")
    if stable is not None and (stable not in known or known[stable]["revoked"]):
        raise ReleaseError("Stable release is missing or revoked")
    edges = set()
    for edge in transitions:
        _fields(edge, {"from", "to", "arch", "profile", "evidence_sha256"})
        if any(not isinstance(edge[name], str) for name in ("from", "to", "arch", "profile")):
            raise ReleaseError("Invalid catalog transition strings")
        if edge["from"] not in known or edge["to"] not in known or edge["from"] == edge["to"]:
            raise ReleaseError("Invalid catalog transition endpoints")
        if edge["arch"] not in {"x86", "x64"} or not isinstance(edge["profile"], str):
            raise ReleaseError("Invalid catalog platform")
        if not re.fullmatch(r"[a-z0-9][a-z0-9_.-]{0,62}", edge["profile"]):
            raise ReleaseError("Invalid catalog evidence profile")
        _digest(edge["evidence_sha256"])
        identity = (edge["from"], edge["to"], edge["arch"], edge["profile"])
        if identity in edges:
            raise ReleaseError("Duplicate catalog transition")
        edges.add(identity)
    return document


def catalog_bytes(document: dict, key: rsa.RSAPrivateKey, *, now: int) -> tuple[bytes, bytes]:
    validate_catalog(document, expected_key_id=key_id(key.public_key()), now=now)
    data = (json.dumps(document, indent=2, ensure_ascii=True) + "\n").encode("ascii")
    return data, sign_bytes(data, key)


def read_catalog(
    data: bytes,
    signature: bytes,
    key: rsa.RSAPublicKey,
    *,
    now: int,
    floor_revision: int = 0,
    floor_digest: str | None = None,
    floor_time: int = 0,
) -> dict:
    verify_bytes(data, signature, key)  # No JSON interpretation before authentication.

    def unique(pairs):
        value = {}
        for name, item in pairs:
            if name in value:
                raise ReleaseError("Duplicate catalog JSON key")
            value[name] = item
        return value

    document = validate_catalog(
        json.loads(data, object_pairs_hook=unique), expected_key_id=key_id(key), now=now
    )
    digest = hashlib.sha256(data).hexdigest()
    if now < floor_time or document["revision"] < floor_revision:
        raise ReleaseError("Catalog revision/time rollback")
    if document["revision"] == floor_revision and digest != floor_digest:
        raise ReleaseError("Different catalog bytes at the same revision")
    return document


def route(
    document: dict, *, current: str, current_digest: str, requested: str, arch: str, profile: str
) -> list[dict]:
    """Use only a validated fresh catalog; returned releases include pinned root digests."""
    known = {r["version"]: r for r in document["releases"]}
    target = document["stable"] if requested == "latest" else requested
    if current not in known or known[current]["manifest_sha256"] != current_digest:
        raise ReleaseError("Installed release identity is absent from catalog")
    if target not in known or known[target]["revoked"]:
        raise ReleaseError("Requested release is absent or revoked")
    # A revoked installed version may leave through an explicitly permitted edge.
    queue = deque([(current, [])])
    visited = {current}
    while queue:
        version, path = queue.popleft()
        if version == target:
            return [known[name] for name in path]
        for edge in document["transitions"]:
            to = edge["to"]
            if (
                edge["from"] == version
                and edge["arch"] == arch
                and edge["profile"] == profile
                and not known[to]["revoked"]
                and to not in visited
            ):
                visited.add(to)
                queue.append((to, [*path, to]))
    raise ReleaseError("No admitted update route for this platform")
