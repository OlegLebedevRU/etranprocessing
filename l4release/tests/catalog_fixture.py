"""Ephemeral test catalog signatures, never owner-signed admission or publication."""

import copy
import json
from pathlib import Path

from cryptography.hazmat.primitives.asymmetric import rsa

from l4release.catalog import catalog_bytes
from l4release.metadata import key_id, public_blob, sign_bytes
from l4release.tests.test_catalog import sample

if __name__ == "__main__":
    output = Path("tools/dist/.release/catalog-fixture")
    output.mkdir(parents=True, exist_ok=True)
    key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
    identity = key_id(key.public_key())
    (output / "public.blob").write_bytes(public_blob(key.public_key()))
    (output / "key-id.txt").write_text(identity, encoding="ascii")
    base = sample(identity)
    for name, doc in (
        ("catalog", base),
        ("older", {**base, "revision": 7}),
        ("higher", {**base, "revision": 9}),
        ("different", {**base, "expires_at": 201}),
    ):
        data, sig = catalog_bytes(doc, key, now=150)
        (output / (name + ".json")).write_bytes(data)
        (output / (name + ".json.sig")).write_bytes(sig)
    invalid = []
    for field, value in (
        ("schema", 2),
        ("revision", 0),
        ("revision", True),
        ("issued_at", 151),
        ("expires_at", 150),
        ("stable", "2.0.0"),
    ):
        invalid.append({**base, field: value})
    for item, field, value in (
        ("releases", "manifest_sha256", "0" * 64),
        ("releases", "version", "../1.0.0"),
        ("transitions", "profile", "../profile"),
        ("transitions", "arch", "arm64"),
        ("transitions", "evidence_sha256", "0" * 64),
        ("transitions", "to", "1.0.0"),
    ):
        doc = copy.deepcopy(base)
        doc[item][0][field] = value
        invalid.append(doc)
    invalid.extend(
        [
            {**base, "waiver": True},
            {**base, "releases": []},
            {**base, "transitions": base["transitions"] * 9},
        ]
    )
    for version in ("65536.0.0", "1.0.0-beta", "١.0.0"):
        doc = copy.deepcopy(base)
        doc["releases"][0]["version"] = version
        invalid.append(doc)
    maximum = copy.deepcopy(base)
    maximum["releases"] = [
        {"version": f"1.{i}.0", "manifest_sha256": "11" * 32, "revoked": False} for i in range(24)
    ]
    maximum["stable"] = "1.23.0"
    maximum["transitions"] = [
        {**base["transitions"][0], "from": f"1.{i}.0", "to": f"1.{i + 1}.0"} for i in range(23)
    ] + [{**base["transitions"][2], "from": "1.0.0", "to": "1.23.0"}]
    data, sig = catalog_bytes(maximum, key, now=150)
    (output / "maximum.json").write_bytes(data)
    (output / "maximum.json.sig").write_bytes(sig)
    for index, doc in enumerate(invalid):
        data = json.dumps(doc).encode()
        (output / f"bad-{index}.json").write_bytes(data)
        (output / f"bad-{index}.json.sig").write_bytes(sign_bytes(data, key))
