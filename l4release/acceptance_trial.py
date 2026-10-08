"""Private signed trial revision reservation, separate from public catalog ancestry."""

from __future__ import annotations

from pathlib import Path

from cryptography.hazmat.primitives.asymmetric import rsa

from .catalog import validate_catalog
from .catalog_evidence import fields, integer, sha, unique_json
from .catalog_registry import digest
from .catalog_state import protect_state
from .common import ReleaseError
from .metadata import key_id, verify_bytes


def _read(path: Path, maximum: int) -> bytes:
    protect_state(path)
    with path.open("rb") as stream:
        data = stream.read(maximum + 1)
    if not 0 < len(data) <= maximum:
        raise ReleaseError("Protected trial artifact exceeds its fixed bound")
    return data


def reserved_revision(state: Path, public: rsa.RSAPublicKey) -> int:
    """Never invent a published floor from a privately consumed trial revision.

    Reservation must retain both owner-signed reviewed plan and private catalog.
    Expiry cannot reset its consumed revision; this is ancestry, not admission.
    """
    path = state / "trial-reservation.json"
    if not path.exists():
        return 0
    value = fields(
        unique_json(_read(path, 4096)),
        {"schema", "key_id", "revision", "catalog_sha256", "plan_sha256"},
    )
    if (
        type(value["schema"]) is not int
        or value["schema"] != 1
        or value["key_id"] != key_id(public)
    ):
        raise ReleaseError("Private trial reservation identity differs")
    revision = integer(value["revision"])
    sha(value["catalog_sha256"])
    sha(value["plan_sha256"])
    directory = state / f"trial-{revision}"
    protect_state(directory)
    plan_bytes = _read(directory / "plan.json", 65535)
    verify_bytes(plan_bytes, _read(directory / "plan.json.sig", 384), public)
    catalog_bytes = _read(directory / "catalog.json", 65535)
    verify_bytes(catalog_bytes, _read(directory / "catalog.json.sig", 384), public)
    if (
        digest(plan_bytes) != value["plan_sha256"]
        or digest(catalog_bytes) != value["catalog_sha256"]
    ):
        raise ReleaseError("Private trial reservation artifacts changed")
    plan, catalog = unique_json(plan_bytes), unique_json(catalog_bytes)
    if (
        type(plan.get("schema")) is not int
        or plan.get("schema") != 1
        or plan.get("kind") != "l4tools-local-acceptance"
        or plan.get("key_id") != key_id(public)
        or plan.get("catalog") != catalog
    ):
        raise ReleaseError("Reserved trial is not the reviewed owner-signed local acceptance plan")
    issued = integer(catalog.get("issued_at"), 253402300799)
    validate_catalog(catalog, expected_key_id=key_id(public), now=issued)
    if catalog["revision"] != revision:
        raise ReleaseError("Private trial revision does not match its signed catalog")
    return revision
