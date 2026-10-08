"""Owner-authorized offline fresh installation kit; no registry publication."""

from __future__ import annotations

import json
import shutil
import time
from pathlib import Path

from .bootstrap import kit_bootstrap
from .catalog import catalog_bytes, read_catalog
from .common import ReleaseError, file_hash, version_value
from .metadata import key_id, load_signing_key, verify_embedded_key
from .pipeline import publisher_module


def create_install_kit(
    root: Path,
    dist: Path,
    version: str,
    destination: Path,
    env: dict[str, str],
    env_path: Path,
    *,
    now: int | None = None,
) -> Path:
    version = version_value(version)
    key = load_signing_key(root, env, env_path)
    verify_embedded_key(root, key.public_key())
    manifest, _, _ = publisher_module(root).verify_artifacts(
        dist, trusted_public=key.public_key(), require_metadata=True
    )
    if manifest["version"] != version:
        raise ReleaseError("Installation kit version differs from verified release")
    if destination.exists():
        raise ReleaseError("Installation kit destination already exists; never overwrite")
    bootstrap, bootstrap_signature, helpers = kit_bootstrap(
        root, env, env_path, manifest, file_hash(dist / "l4tools-release.json"), key
    )
    now = int(time.time()) if now is None else now
    document = {
        "schema": 1,
        "key_id": key_id(key.public_key()),
        "revision": 1,
        "issued_at": now,
        "expires_at": now + 7 * 86400,
        "stable": None,
        "releases": [
            {
                "version": version,
                "manifest_sha256": file_hash(dist / "l4tools-release.json"),
                "revoked": False,
            }
        ],
        "transitions": [],
    }
    data, signature = catalog_bytes(document, key, now=now)
    read_catalog(data, signature, key.public_key(), now=now)
    names = ["l4setup.exe", "l4tools-release.json", "l4tools-release.json.sig", "SHA256SUMS"]
    for arch in ("x86", "x64"):
        names.extend(f"l4tools-layout-{arch}.{suffix}" for suffix in ("json", "json.sig", "zip"))
    destination.mkdir(parents=True, exist_ok=False)
    # A crash leaves an incomplete directory, which cannot be reused or silently overwritten.
    for name in names:
        shutil.copyfile(dist / name, destination / name)
        if file_hash(destination / name) != file_hash(dist / name):
            raise ReleaseError("Installation kit copy failed integrity verification")
    (destination / "setup-catalog.json").write_bytes(data)
    (destination / "setup-catalog.json.sig").write_bytes(signature)
    expected_helpers = {item["name"]: item for item in json.loads(bootstrap)["helpers"].values()}
    for name, source in helpers.items():
        shutil.copyfile(source, destination / name)
        expected = expected_helpers[name]
        if (destination / name).stat().st_size != expected["size"] or file_hash(
            destination / name
        ) != expected["sha256"]:
            raise ReleaseError("Bootstrap kit copy failed integrity verification")
    (destination / "l4tools-bootstrap.json").write_bytes(bootstrap)
    (destination / "l4tools-bootstrap.json.sig").write_bytes(bootstrap_signature)
    publisher_module(root).verify_artifacts(
        destination, trusted_public=key.public_key(), require_metadata=True
    )
    (destination / "INSTALL.txt").write_text(
        f"Fresh installation authorization expires at UTC epoch {document['expires_at']}.\n"
        "Use an elevated interactive Windows console.\n"
        "Existing four suite services must be absent before install.\n"
        f'l4setup.exe --fresh-verify --bundle "{destination}" '
        f"--fresh-version {version} --arch x86\n"
        f'l4setup.exe --fresh-install --bundle "{destination}" '
        f"--fresh-version {version} --arch x86\n"
        "Save the printed operation_id.\n"
        "Status/recovery use that original id and original version.\n"
        "This kit authorizes a fresh installation.\n"
        "It does not promote stable or admit an update compatibility edge.\n",
        encoding="utf-8",
    )
    return destination
