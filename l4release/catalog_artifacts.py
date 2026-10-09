"""Authenticate public release roots and every byte of both native layout payloads."""

from __future__ import annotations

import hashlib
import re
import zipfile
import zlib
from pathlib import Path

from cryptography.hazmat.primitives.asymmetric import rsa

from .catalog_evidence import SERVICES, Endpoint, fields, integer, sha, unique_json
from .catalog_registry import CatalogRegistry, digest
from .common import ReleaseError, file_hash, version_value
from .layout_payload import EXECUTABLES, TEMPLATES
from .metadata import key_id, verify_bytes
from .pipeline import publisher_module

MAX_ARCHIVE = 1024**3
SERVICE_PATHS = (
    "leo4proxy/leo4proxy.exe",
    "mosquitto/mosquitto.exe",
    "l4con/l4con.exe",
    "l4superv/l4superv.exe",
)


def _path(value: object) -> str:
    if (
        not isinstance(value, str)
        or len(value) > 120
        or not re.fullmatch(r"[A-Za-z0-9_./-]+", value)
    ):
        raise ReleaseError("Invalid native layout inventory path")
    reserved = {
        "CON",
        "NUL",
        "PRN",
        "AUX",
        *(f"COM{i}" for i in range(1, 10)),
        *(f"LPT{i}" for i in range(1, 10)),
    }
    if any(
        part in {"", ".", ".."} or part.endswith(".") or part.split(".", 1)[0].upper() in reserved
        for part in value.split("/")
    ):
        raise ReleaseError("Native layout path aliases an unsafe Windows name")
    return value


def _inventory_checked(descriptor: dict, archive_path: Path) -> dict[str, dict[str, int | str]]:
    entries = descriptor["files"]
    if not isinstance(entries, list) or not 1 <= len(entries) <= 64:
        raise ReleaseError("Native signed layout inventory exceeds64 entries")
    inventory = {}
    aliases = set()
    total = 0
    for value in entries:
        item = fields(value, {"path", "size", "sha256"})
        name = _path(item["path"])
        size = integer(item["size"], MAX_ARCHIVE)
        checksum = sha(item["sha256"])
        if name.casefold() in aliases:
            raise ReleaseError("Duplicate/case-aliased signed native layout path")
        aliases.add(name.casefold())
        total += size
        inventory[name] = {"size": size, "sha256": checksum}
    if total > MAX_ARCHIVE or not (set(EXECUTABLES) | set(TEMPLATES.values())).issubset(inventory):
        raise ReleaseError("Native signed layout is incomplete or exceeds1GiB")
    with zipfile.ZipFile(archive_path) as archive:
        members = archive.infolist()
        if len(members) != len(inventory) or {m.filename for m in members} != set(inventory):
            raise ReleaseError("Public layout archive differs from full signed inventory")
        for member in members:
            item = inventory[member.filename]
            mode = (member.external_attr >> 16) & 0o170000
            if (
                member.flag_bits & 1
                or mode not in {0, 0o100000}
                or member.compress_type not in {zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED}
                or member.file_size != item["size"]
            ):
                raise ReleaseError("Native ZIP member type/encryption/size differs")
            with archive.open(member) as stream:
                hasher = hashlib.sha256()
                decoded = 0
                while block := stream.read(1024 * 1024):
                    decoded += len(block)
                    if decoded > member.file_size:
                        raise ReleaseError("Native ZIP decompression exceeds signed member size")
                    hasher.update(block)
                checksum = hasher.hexdigest()
                if decoded != member.file_size:
                    raise ReleaseError("Native ZIP member is truncated")
            if checksum != item["sha256"]:
                raise ReleaseError("Public native file bytes differ from owner-signed inventory")
    return inventory


def _inventory(descriptor: dict, archive_path: Path) -> dict[str, dict[str, int | str]]:
    try:
        return _inventory_checked(descriptor, archive_path)
    except (zipfile.BadZipFile, zlib.error, EOFError, NotImplementedError) as error:
        raise ReleaseError("Public native ZIP decoding/integrity verification failed") from error


def published_release(
    root: Path,
    registry: CatalogRegistry,
    version: str,
    directory: Path,
    public: rsa.RSAPublicKey,
) -> dict[str, Endpoint]:
    version = version_value(version)
    if "-" in version:
        raise ReleaseError("Native catalog requires three-part release versions")
    directory.mkdir()
    for name, limit in (("l4tools-release.json", 65535), ("l4tools-release.json.sig", 384)):
        if not registry.download(f"l4tools/{version}/{name}", directory / name, limit):
            raise ReleaseError("Catalog endpoint release is not publicly published")
    data = (directory / "l4tools-release.json").read_bytes()
    verify_bytes(data, (directory / "l4tools-release.json.sig").read_bytes(), public)
    manifest = fields(
        unique_json(data),
        {
            "schema",
            "version",
            "git_sha",
            "dirty",
            "built_at",
            "builder",
            "signed",
            "signature_status",
            "files",
            "payload_sha256",
            "components",
            "component_artifacts",
            "min_os",
            "arch",
            "source_checkpoint",
            "publisher_certificate_sha256",
            "layout_payloads",
            "metadata_signatures",
        },
    )
    if (
        manifest.get("version") != version
        or type(manifest.get("schema")) is not int
        or manifest.get("schema") != 1
        or manifest.get("dirty") is not False
        or manifest.get("signed") is not True
        or manifest.get("signature_status") != "Valid"
        or manifest.get("min_os") != "6.1"
        or not isinstance(manifest.get("arch"), list)
        or len(manifest["arch"]) != 2
        or any(not isinstance(arch, str) for arch in manifest["arch"])
        or sorted(manifest["arch"]) != ["x64", "x86"]
    ):
        raise ReleaseError("Public release differs from native clean signed root contract")
    signatures = fields(
        manifest.get("metadata_signatures"), {"schema", "algorithm", "key_id", "signature"}
    )
    if type(signatures["schema"]) is not int or signatures != {
        "schema": 1,
        "algorithm": "RSA3072-PKCS1v1.5-SHA256",
        "key_id": key_id(public),
        "signature": "l4tools-release.json.sig",
    }:
        raise ReleaseError("Public release metadata signer differs")
    checkpoint = fields(
        manifest.get("source_checkpoint"),
        {"schema", "clean_at_start", "inputs_sha256", "build_options_sha256"},
    )
    if (
        type(checkpoint["schema"]) is not int
        or checkpoint["schema"] != 1
        or checkpoint["clean_at_start"] is not True
    ):
        raise ReleaseError("Public release lacks clean source provenance")
    sha(checkpoint["inputs_sha256"])
    sha(checkpoint["build_options_sha256"])
    expected = {
        "l4setup.exe",
        *(
            f"l4tools-layout-{arch}.{suffix}"
            for arch in ("x86", "x64")
            for suffix in ("json", "json.sig", "zip")
        ),
    }
    artifacts = fields(manifest.get("files"), expected)
    for name in sorted(expected):
        item = fields(artifacts[name], {"size", "sha256"})
        size = integer(
            item["size"],
            65535 if name.endswith(".json") else 384 if name.endswith(".sig") else MAX_ARCHIVE,
        )
        sha(item["sha256"])
        if name.endswith(".sig") and size != 384:
            raise ReleaseError("Public descriptor signature size differs")
        path = directory / name
        if (
            not registry.download(f"l4tools/{version}/{name}", path, size)
            or path.stat().st_size != size
            or file_hash(path) != item["sha256"]
        ):
            raise ReleaseError(
                f"Public release artifact differs from signed root inventory: {version}/{name}"
            )
    if not registry.download(f"l4tools/{version}/SHA256SUMS", directory / "SHA256SUMS", 65535):
        raise ReleaseError("Public release checksum inventory is absent")
    publisher_module(root).verify_artifacts(directory, trusted_public=public, require_metadata=True)
    endpoints = {}
    publisher = sha(manifest.get("publisher_certificate_sha256"))
    for arch in ("x86", "x64"):
        name = f"l4tools-layout-{arch}.json"
        layout_bytes = (directory / name).read_bytes()
        verify_bytes(layout_bytes, (directory / (name + ".sig")).read_bytes(), public)
        descriptor = fields(
            unique_json(layout_bytes),
            {
                "schema",
                "version",
                "arch",
                "publisher_certificate_sha256",
                "archive_sha256",
                "files",
            },
        )
        if (
            type(descriptor["schema"]) is not int
            or descriptor["schema"] != 1
            or descriptor["version"] != version
            or descriptor["arch"] != arch
            or descriptor["publisher_certificate_sha256"] != publisher
            or descriptor["archive_sha256"] != artifacts[f"l4tools-layout-{arch}.zip"]["sha256"]
        ):
            raise ReleaseError("Public native descriptor identity differs")
        inventory = _inventory(descriptor, directory / f"l4tools-layout-{arch}.zip")
        endpoints[arch] = Endpoint(
            version,
            digest(data),
            digest(layout_bytes),
            {
                service: inventory[path]
                for service, path in zip(SERVICES, SERVICE_PATHS, strict=True)
            },
            artifacts["l4setup.exe"],
        )
    return endpoints
