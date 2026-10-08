"""Ephemeral exact producer-to-native root fixtures; never sign/publish a candidate."""

import copy
import hashlib
import json
from pathlib import Path

from cryptography.hazmat.primitives.asymmetric import rsa

from l4release.catalog import catalog_bytes
from l4release.common import atomic_json, file_hash
from l4release.layout_payload import EXECUTABLES, build_layout_payload
from l4release.metadata import key_id, public_blob, sign_bytes, sign_release_metadata
from l4release.tests.test_layout_payload import make_stage


def generate(output: Path) -> None:
    stage = output / "stage"
    make_stage(stage)
    key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
    (output / "fixture-public.blob").write_bytes(public_blob(key.public_key()))
    (output / "fixture-key-id.txt").write_text(key_id(key.public_key()), encoding="ascii")
    (output / "l4setup.exe").write_bytes(b"MZ-not-a-release")
    inventory = [
        {
            "path": path.replace("/", "\\"),
            "product_version": "1.0.0",
            "size": 9,
            "sha256": hashlib.sha256(b"MZfixture").hexdigest(),
        }
        for path in sorted(EXECUTABLES)
    ]
    root = {
        "schema": 1,
        "version": "1.13.3",
        "git_sha": "a" * 40,
        "dirty": False,
        "built_at": "2026-10-06T00:00:00Z",
        "builder": "windows-dev",
        "signed": True,
        "signature_status": "Valid",
        "publisher_certificate_sha256": "11" * 32,
        "files": {},
        "payload_sha256": {a: "22" * 32 for a in ("x86", "x64")},
        "components": {Path(p).parts[0]: "1.0.0" for p in EXECUTABLES},
        "component_artifacts": {a: inventory for a in ("x86", "x64")},
        "min_os": "6.1",
        "arch": ["x86", "x64"],
        "source_checkpoint": {
            "schema": 1,
            "inputs_sha256": "33" * 32,
            "build_options_sha256": "44" * 32,
            "clean_at_start": True,
        },
        "layout_payloads": {
            a: build_layout_payload(stage, output, "1.13.3", a, "11" * 32) for a in ("x86", "x64")
        },
    }
    names = ["l4setup.exe"]
    for arch in ("x86", "x64"):
        names.extend((f"l4tools-layout-{arch}.json", f"l4tools-layout-{arch}.zip"))
    root["files"] = {
        n: {"sha256": file_hash(output / n), "size": (output / n).stat().st_size} for n in names
    }
    atomic_json(output / "l4tools-release.json", root)
    # Use the real finalizer, including descriptor signatures and root inventory.
    sign_release_metadata(output, key)
    root = json.loads((output / "l4tools-release.json").read_bytes())
    mutations = [
        {"schema": True},
        {"schema": 2},
        {"version": "1.13.4"},
        {"dirty": True},
        {"dirty": 0},
        {"signed": False},
        {"signed": 1},
        {"signature_status": "NotSigned"},
        {"min_os": "10.0"},
        {"publisher_certificate_sha256": None},
        {"publisher_certificate_sha256": "00" * 32},
        {"publisher_certificate_sha256": "GG" * 32},
        {"arch": ["x86", "x86"]},
        {"arch": ["x86"]},
        {"arch": ["x86", "arm64"]},
        {"unknown": "https://localhost/override"},
        {"metadata_signatures": {**root["metadata_signatures"], "key_id": "55" * 32}},
        {"metadata_signatures": {**root["metadata_signatures"], "algorithm": "RSA-PSS"}},
        {"metadata_signatures": {**root["metadata_signatures"], "signature": "../root.sig"}},
        {"source_checkpoint": {**root["source_checkpoint"], "clean_at_start": False}},
        {"source_checkpoint": {**root["source_checkpoint"], "inputs_sha256": "00" * 32}},
        {"layout_payloads": {"x86": root["layout_payloads"]["x86"]}},
    ]
    for arch in ("x86", "x64"):
        for field, value in (
            ("manifest", "../manifest.json"),
            ("archive", "https://localhost/file"),
            ("manifest_sha256", "66" * 32),
        ):
            layouts = copy.deepcopy(root["layout_payloads"])
            layouts[arch][field] = value
            mutations.append({"layout_payloads": layouts})
        for suffix, size in (("json", 0), ("json", 65536), ("json.sig", 383), ("zip", 1073741825)):
            files = copy.deepcopy(root["files"])
            files[f"l4tools-layout-{arch}.{suffix}"]["size"] = size
            mutations.append({"files": files})
    for index, mutation in enumerate(mutations):
        data = json.dumps({**root, **mutation}, separators=(",", ":")).encode()
        (output / f"bad-{index}.json").write_bytes(data)
        (output / f"bad-{index}.sig").write_bytes(sign_bytes(data, key))
    (output / "bad-count.txt").write_text(str(len(mutations)), encoding="ascii")
    # Signature-valid roots that parse but disagree with the signed descriptor.
    for kind in ("archive", "publisher"):
        alternate = copy.deepcopy(root)
        if kind == "archive":
            for arch in ("x86", "x64"):
                alternate["files"][f"l4tools-layout-{arch}.zip"]["sha256"] = "77" * 32
                alternate["layout_payloads"][arch]["archive_sha256"] = "77" * 32
        else:
            alternate["publisher_certificate_sha256"] = "88" * 32
        data = json.dumps(alternate, separators=(",", ":")).encode()
        (output / f"mismatch-{kind}.json").write_bytes(data)
        (output / f"mismatch-{kind}.sig").write_bytes(sign_bytes(data, key))
    duplicate = json.dumps(root).replace('"schema": 1', '"schema": 1, "schema": 1', 1).encode()
    (output / "duplicate.json").write_bytes(duplicate)
    (output / "duplicate.sig").write_bytes(sign_bytes(duplicate, key))
    catalog = {
        "schema": 1,
        "key_id": key_id(key.public_key()),
        "revision": 8,
        "issued_at": 100,
        "expires_at": 200,
        "stable": "1.13.3",
        "releases": [
            {"version": "1.13.2", "manifest_sha256": "22" * 32, "revoked": False},
            {
                "version": "1.13.3",
                "manifest_sha256": file_hash(output / "l4tools-release.json"),
                "revoked": False,
            },
        ],
        "transitions": [
            {
                "from": "1.13.2",
                "to": "1.13.3",
                "arch": arch,
                "profile": "windows-10-x64",
                "evidence_sha256": "aa" * 32,
            }
            for arch in ("x86", "x64")
        ],
    }
    data, signature = catalog_bytes(catalog, key, now=150)
    (output / "update-catalog.json").write_bytes(data)
    (output / "update-catalog.json.sig").write_bytes(signature)

    # Two real producer-generated metadata hops, same ephemeral signer. Payloads
    # remain synthetic/unsigned and can never authorize a production installation.
    hop2 = output / "hop2"
    hop2.mkdir(exist_ok=True)
    (hop2 / "l4setup.exe").write_bytes(b"MZ-not-a-release")
    second = copy.deepcopy(root)
    second.pop("metadata_signatures")
    second["version"] = "1.13.4"
    second["layout_payloads"] = {
        a: build_layout_payload(stage, hop2, "1.13.4", a, "11" * 32) for a in ("x86", "x64")
    }
    second["files"] = {
        n: {"sha256": file_hash(hop2 / n), "size": (hop2 / n).stat().st_size} for n in names
    }
    atomic_json(hop2 / "l4tools-release.json", second)
    sign_release_metadata(hop2, key)
    multi = copy.deepcopy(catalog)
    multi["revision"] = 9
    multi["stable"] = "1.13.4"
    multi["releases"].append(
        {
            "version": "1.13.4",
            "manifest_sha256": file_hash(hop2 / "l4tools-release.json"),
            "revoked": False,
        }
    )
    multi["transitions"].extend(
        {
            "from": "1.13.3",
            "to": "1.13.4",
            "arch": arch,
            "profile": "windows-10-x64",
            "evidence_sha256": "bb" * 32,
        }
        for arch in ("x86", "x64")
    )
    data, signature = catalog_bytes(multi, key, now=150)
    (output / "multi-catalog.json").write_bytes(data)
    (output / "multi-catalog.json.sig").write_bytes(signature)

    installed = output / "installed"
    installed.mkdir(exist_ok=True)
    (installed / "l4setup.exe").write_bytes(b"MZ-not-a-release")
    previous = copy.deepcopy(root)
    previous.pop("metadata_signatures")
    previous["version"] = "1.13.2"
    previous["layout_payloads"] = {
        a: build_layout_payload(stage, installed, "1.13.2", a, "11" * 32) for a in ("x86", "x64")
    }
    previous["files"] = {
        n: {"sha256": file_hash(installed / n), "size": (installed / n).stat().st_size}
        for n in names
    }
    atomic_json(installed / "l4tools-release.json", previous)
    sign_release_metadata(installed, key)
    operation = copy.deepcopy(multi)
    operation["revision"] = 10
    operation["releases"][0]["manifest_sha256"] = file_hash(installed / "l4tools-release.json")
    operation["releases"][0]["revoked"] = True  # leaving revoked source is permitted
    data, signature = catalog_bytes(operation, key, now=150)
    (output / "operation-catalog.json").write_bytes(data)
    (output / "operation-catalog.json.sig").write_bytes(signature)
    (output / "installed-root.sha256").write_bytes(
        hashlib.sha256((installed / "l4tools-release.json").read_bytes()).digest()
    )


if __name__ == "__main__":
    generate(Path("tools/dist/.release/root-fixture"))
