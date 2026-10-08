import copy
import hashlib
import json

import pytest
from cryptography.hazmat.primitives.asymmetric import rsa

from l4release.catalog import catalog_bytes, read_catalog, route, validate_catalog
from l4release.common import ReleaseError
from l4release.metadata import key_id, sign_bytes


def sample(identity):
    versions = ["1.0.0", "1.1.0", "1.2.0"]
    return {
        "schema": 1,
        "key_id": identity,
        "revision": 8,
        "issued_at": 100,
        "expires_at": 200,
        "stable": versions[-1],
        "releases": [
            {"version": v, "manifest_sha256": str(i + 1) * 64, "revoked": False}
            for i, v in enumerate(versions)
        ],
        "transitions": [
            {
                "from": a,
                "to": b,
                "arch": arch,
                "profile": "windows-10-x64",
                "evidence_sha256": "44" * 32,
            }
            for a, b, arch in [
                (versions[0], versions[1], "x86"),
                (versions[1], versions[2], "x86"),
                (versions[0], versions[2], "x64"),
            ]
        ],
    }


@pytest.fixture(scope="module")
def key():
    return rsa.generate_private_key(public_exponent=65537, key_size=3072)


def test_signature_freshness_and_route(key):
    data, sig = catalog_bytes(sample(key_id(key.public_key())), key, now=150)
    document = read_catalog(data, sig, key.public_key(), now=150)
    kwargs = {
        "current": "1.0.0",
        "current_digest": "1" * 64,
        "requested": "latest",
        "arch": "x86",
        "profile": "windows-10-x64",
    }
    assert [r["version"] for r in route(document, **kwargs)] == ["1.1.0", "1.2.0"]
    assert [r["version"] for r in route(document, **{**kwargs, "arch": "x64"})] == ["1.2.0"]
    assert route(document, **{**kwargs, "requested": "1.0.0"}) == []
    for changed in (
        {"profile": "windows-7-x86"},
        {"current_digest": "0" * 64},
        {"requested": "2.0.0"},
    ):
        with pytest.raises(ReleaseError):
            route(document, **{**kwargs, **changed})
    digest = hashlib.sha256(data).hexdigest()
    read_catalog(
        data, sig, key.public_key(), now=151, floor_revision=8, floor_digest=digest, floor_time=150
    )
    for changed in (
        {"floor_revision": 9},
        {"floor_revision": 8, "floor_digest": "5" * 64},
        {"floor_time": 151},
        {"now": 99},
        {"now": 200},
    ):
        with pytest.raises(ReleaseError):
            read_catalog(data, sig, key.public_key(), **{"now": 150, **changed})
    with pytest.raises(ReleaseError):
        read_catalog(data + b" ", sig, key.public_key(), now=150)


@pytest.mark.parametrize(
    "kind",
    [
        "schema",
        "integer",
        "float",
        "key",
        "stable",
        "duplicate",
        "digest",
        "version",
        "endpoint",
        "loop",
        "arch",
        "profile",
        "evidence",
        "duplicate_edge",
        "extra",
        "limit",
    ],
)
def test_malformed_catalog_refuses(key, kind):
    doc = sample(key_id(key.public_key()))
    if kind == "schema":
        doc["schema"] = 2
    elif kind == "integer":
        doc["revision"] = True
    elif kind == "float":
        doc["revision"] = 1.0
    elif kind == "key":
        doc["key_id"] = "22" * 32
    elif kind == "stable":
        doc["releases"][-1]["revoked"] = True
    elif kind == "duplicate":
        doc["releases"].append(copy.deepcopy(doc["releases"][0]))
    elif kind == "digest":
        doc["releases"][0]["manifest_sha256"] = "0" * 64
    elif kind == "version":
        doc["releases"][0]["version"] = "../1.0.0"
    elif kind == "endpoint":
        doc["transitions"][0]["to"] = "2.0.0"
    elif kind == "loop":
        doc["transitions"][0]["to"] = "1.0.0"
    elif kind == "arch":
        doc["transitions"][0]["arch"] = "arm64"
    elif kind == "profile":
        doc["transitions"][0]["profile"] = "../profile"
    elif kind == "evidence":
        doc["transitions"][0]["evidence_sha256"] = "0" * 64
    elif kind == "duplicate_edge":
        doc["transitions"].append(copy.deepcopy(doc["transitions"][0]))
    elif kind == "extra":
        doc["waiver"] = True
    elif kind == "limit":
        doc["transitions"] *= 9
    with pytest.raises(ReleaseError):
        validate_catalog(doc, expected_key_id=key_id(key.public_key()), now=150)


def test_revocation_cycles_and_no_stable(key):
    doc = sample(key_id(key.public_key()))
    kwargs = {
        "current": "1.0.0",
        "current_digest": "1" * 64,
        "requested": "latest",
        "arch": "x86",
        "profile": "windows-10-x64",
    }
    doc["transitions"].append({**doc["transitions"][0], "from": "1.1.0", "to": "1.0.0"})
    validate_catalog(doc, expected_key_id=key_id(key.public_key()), now=150)
    assert len(route(doc, **kwargs)) == 2
    doc["releases"][1]["revoked"] = True
    with pytest.raises(ReleaseError):
        route(doc, **kwargs)
    doc["stable"] = None
    validate_catalog(doc, expected_key_id=key_id(key.public_key()), now=150)
    with pytest.raises(ReleaseError):
        route(doc, **{**kwargs, "arch": "x64"})


def test_signed_duplicate_json_keys_refuse(key):
    raw = (
        json.dumps(sample(key_id(key.public_key())))
        .replace('"schema": 1', '"schema": 1, "schema": 1')
        .encode()
    )
    with pytest.raises(ReleaseError, match="Duplicate"):
        read_catalog(raw, sign_bytes(raw, key), key.public_key(), now=150)
