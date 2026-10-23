"""Current Docker blob exports retain all-layer checks, including hidden history."""

from __future__ import annotations

import gzip
import hashlib
import importlib.util
import io
import json
import tarfile
from pathlib import Path

import pytest

SPEC = importlib.util.spec_from_file_location("scan_image_formats", Path(__file__).parents[1] / "scan-image.py")
assert SPEC is not None and SPEC.loader is not None
scanner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(scanner)


def tar(files):
    stream = io.BytesIO()
    with tarfile.open(fileobj=stream, mode="w") as archive:
        for name, data in files:
            item = tarfile.TarInfo(name)
            item.size = len(data)
            archive.addfile(item, io.BytesIO(data))
    return stream.getvalue()


def blob(data):
    return "blobs/sha256/" + hashlib.sha256(data).hexdigest()


def exported(layers, extras=()):
    config = b'{"architecture":"amd64","rootfs":{"type":"layers","diff_ids":[]}}'
    files = [(blob(config), config), *[(blob(data), data) for data in layers], *extras]
    # As in real Docker save, the manifest follows the data blobs.
    files.append(
        ("manifest.json", json.dumps([{"Config": blob(config), "Layers": [blob(data) for data in layers]}]).encode())
    )
    files.append(("oci-layout", b'{"imageLayoutVersion":"1.0.0"}'))
    return tar(files)


def test_real_export_layout_with_metadata_and_gzip_duplicates():
    plain = tar([("app/source.c", b'const char* label="-----BEGIN PRIVATE KEY-----";\n')])
    compressed = gzip.compress(plain)
    index = b'{"schemaVersion":2,"manifests":[]}'
    legacy_metadata = json.dumps({"id": "a" * 64, "container_config": {}, "os": "linux"}).encode()
    saved = exported(
        [plain], [(blob(compressed), compressed), (blob(index), index), (blob(legacy_metadata), legacy_metadata)]
    )
    count, findings = scanner.scan_saved_image(io.BytesIO(saved))
    assert count == 2 and findings == []


@pytest.mark.parametrize(
    "name,content",
    [
        ("app/.env", b"SYNTHETIC=not-a-secret\n"),
        ("root/.ssh/config", b"synthetic\n"),
        ("app/key.pem", b"-----BEGIN PRIVATE KEY-----\n"),
        ("app/key.pem", b"-----BEGIN RSA PRIVATE KEY-----\n"),
        ("app/key.pem", b"-----BEGIN OPENSSH PRIVATE KEY-----\n"),
    ],
)
def test_private_material_rejected_in_blob_layers(name, content):
    count, findings = scanner.scan_saved_image(io.BytesIO(exported([tar([(name, content)])])))
    assert count == 1 and name in findings


def test_whiteout_does_not_hide_private_file_in_an_earlier_layer():
    original = tar([("app/.env", b"SYNTHETIC=placeholder\n")])
    removed = tar([("app/.wh..env", b"")])
    count, findings = scanner.scan_saved_image(io.BytesIO(exported([original, removed])))
    assert count == 2 and "app/.env" in findings


def test_extra_saved_layer_is_scanned_even_if_not_named_in_docker_manifest():
    public = tar([("app/public", b"public")])
    private = gzip.compress(tar([("app/private.pem", b"-----BEGIN EC PRIVATE KEY-----\n")]))
    count, findings = scanner.scan_saved_image(io.BytesIO(exported([public], [(blob(private), private)])))
    assert count == 2 and "app/private.pem" in findings


def test_missing_layer_corrupt_blob_and_duplicate_member_fail_closed():
    manifest = json.dumps([{"Layers": ["absent/layer.tar"]}]).encode()
    with pytest.raises(ValueError):
        scanner.scan_saved_image(io.BytesIO(tar([("manifest.json", manifest)])))
    with pytest.raises(ValueError):
        scanner.scan_saved_image(io.BytesIO(exported([b"not a tar layer"])))
    layer = tar([("app/public", b"public")])
    with pytest.raises(ValueError):
        scanner.scan_saved_image(io.BytesIO(exported([layer], [(blob(layer), layer)])))
    unknown = b"opaque non-layer blob"
    with pytest.raises(ValueError):
        scanner.scan_saved_image(io.BytesIO(exported([layer], [(blob(unknown), unknown)])))
    unknown_json = b'{"arbitrary_metadata":true}'
    with pytest.raises(ValueError):
        scanner.scan_saved_image(io.BytesIO(exported([layer], [(blob(unknown_json), unknown_json)])))


def test_empty_or_unknown_layout_does_not_report_a_scan():
    with pytest.raises(ValueError):
        scanner.scan_saved_image(io.BytesIO(tar([("unknown", b"unrecognized")])))
    with pytest.raises(ValueError):
        scanner.scan_saved_image(io.BytesIO(tar([("manifest.json", b"[]")])))
