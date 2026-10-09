"""Real bounded ZIP/hash and fixed transport fixtures; no network or GAR writes."""

from __future__ import annotations

import io
import urllib.error
import zipfile
from pathlib import Path

import pytest

from l4release.catalog_artifacts import _inventory, _path
from l4release.catalog_registry import CatalogRegistry, _NoRedirect, digest
from l4release.common import ReleaseError
from l4release.layout_payload import EXECUTABLES, TEMPLATES


def archive(tmp_path: Path):
    contents = {name: name.encode() for name in set(EXECUTABLES) | set(TEMPLATES.values())}
    path = tmp_path / "payload.zip"
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as target:
        for name, data in contents.items():
            target.writestr(name, data)
    descriptor = {
        "files": [
            {"path": name, "size": len(data), "sha256": digest(data)}
            for name, data in contents.items()
        ]
    }
    return path, descriptor


def test_real_full_inventory_stream_hash(tmp_path):
    path, descriptor = archive(tmp_path)
    assert set(_inventory(descriptor, path)) == set(EXECUTABLES) | set(TEMPLATES.values())


@pytest.mark.parametrize(
    "fault", ["hash", "size", "alias", "extra", "missing", "symlink", "reserved", "traversal"]
)
def test_real_inventory_refuses_bad_bytes_and_paths(tmp_path, fault):
    path, descriptor = archive(tmp_path)
    first = descriptor["files"][0]
    if fault == "hash":
        first["sha256"] = "a" * 64
    elif fault == "size":
        first["size"] += 1
    elif fault == "alias":
        descriptor["files"].append({**first, "path": first["path"].upper()})
    elif fault == "extra":
        with zipfile.ZipFile(path, "a") as target:
            target.writestr("extra.bin", b"x")
    elif fault == "missing":
        descriptor["files"].pop()
    elif fault == "symlink":
        with zipfile.ZipFile(path) as source:
            contents = {member.filename: source.read(member) for member in source.infolist()}
        with zipfile.ZipFile(path, "w") as target:
            for index, (name, data) in enumerate(contents.items()):
                member = zipfile.ZipInfo(name)
                member.external_attr = (0o120777 if index == 0 else 0o100644) << 16
                target.writestr(member, data)
    elif fault == "reserved":
        first["path"] = "NUL.bin"
    elif fault == "traversal":
        first["path"] = "../escape"
    with pytest.raises(ReleaseError):
        _inventory(descriptor, path)


@pytest.mark.parametrize(
    "value",
    [
        "../a",
        "l4tools/../catalog.json",
        "https://other/x",
        "l4tools/x:ads",
        "l4tools//a",
        "l4tools/./a",
    ],
)
def test_fixed_registry_path_rejects_override(value):
    with pytest.raises(ReleaseError):
        CatalogRegistry._relative(value)


@pytest.mark.parametrize(
    "value", ["CON.txt", "a/../b", "a//b", "a/COM1.exe", "a/b.", "a:b", "/absolute"]
)
def test_windows_native_path_rejects_alias(value):
    with pytest.raises(ReleaseError):
        _path(value)


class Response(io.BytesIO):
    status = 200

    def __init__(self, data, length=None):
        super().__init__(data)
        self.headers = {"Content-Length": str(len(data) if length is None else length)}


class Opener:
    def __init__(self, response):
        self.response, self.requests = response, []

    def open(self, request, timeout):
        self.requests.append(request)
        if isinstance(self.response, Exception):
            raise self.response
        return Response(self.response)


def registry(response):
    client = CatalogRegistry.__new__(CatalogRegistry)
    client.base = "https://registry.invalid"
    client.env = {"AR_GENERIC_KEY_ID": "fixture-id", "AR_GENERIC_KEY_SECRET": "fixture-only"}
    client.opener = Opener(response)
    return client


def test_public_get_no_authorization_and_bound(tmp_path):
    client = registry(b"123")
    assert client.download("l4tools/metadata/catalog.json", tmp_path / "get", 3)
    assert not client.opener.requests[0].has_header("Authorization")
    client = registry(b"1234")
    with pytest.raises(ReleaseError, match="bound"):
        client.download("l4tools/metadata/catalog.json", tmp_path / "large", 3)


def test_only_actual404_absent(tmp_path):
    client = registry(urllib.error.HTTPError("https://fixture.invalid", 404, "", {}, None))
    assert not client.download("l4tools/metadata/catalog.json", tmp_path / "absent", 3)
    client = registry(urllib.error.HTTPError("https://fixture.invalid", 403, "", {}, None))
    with pytest.raises(ReleaseError, match="403"):
        client.download("l4tools/metadata/catalog.json", tmp_path / "denied", 3)


def test_short_http_body_restarts_only_owned_output(tmp_path):
    client = registry(b"")
    responses = iter([Response(b"12345", 6), Response(b"abc", 3)])
    client.opener.open = lambda request, timeout: next(responses)
    target = tmp_path / "owned"
    assert client.download("l4tools/metadata/catalog.json", target, 6)
    assert target.read_bytes() == b"abc"


def test_short_http_body_refuses_after_three_attempts(tmp_path):
    client = registry(b"")
    calls = []

    def short(request, timeout):
        calls.append(request)
        return Response(b"12", 3)

    client.opener.open = short
    with pytest.raises(ReleaseError, match="truncated after3 attempts"):
        client.download("l4tools/metadata/catalog.json", tmp_path / "short", 3)
    assert len(calls) == 3
    assert all(not request.has_header("Authorization") for request in calls)


def test_existing_output_is_never_truncated(tmp_path):
    client = registry(b"new")
    target = tmp_path / "existing"
    target.write_bytes(b"operator-owned")
    with pytest.raises(FileExistsError):
        client.download("l4tools/metadata/catalog.json", target, 3)
    assert target.read_bytes() == b"operator-owned"


def test_explicit_incomplete_read_restarts(tmp_path):
    import http.client

    class Broken(Response):
        def read(self, size=-1):
            raise http.client.IncompleteRead(b"ab", 1)

    client = registry(b"")
    responses = iter([Broken(b"", 3), Response(b"xyz")])
    client.opener.open = lambda request, timeout: next(responses)
    target = tmp_path / "explicit"
    assert client.download("l4tools/metadata/catalog.json", target, 3)
    assert target.read_bytes() == b"xyz"


def test_excess_body_does_not_retry(tmp_path):
    client = registry(b"")
    calls = []

    def excess(request, timeout):
        calls.append(request)
        return Response(b"abc", 2)

    client.opener.open = excess
    with pytest.raises(ReleaseError, match="exceeds Content-Length"):
        client.download("l4tools/metadata/catalog.json", tmp_path / "excess", 3)
    assert len(calls) == 1


def test_upload_exact_existing_multipart_directory_api():
    client = registry(b"")
    client.upload("l4tools/metadata/catalog.json", b"payload")
    request = client.opener.requests[0]
    assert request.get_method() == "PUT"
    assert request.full_url == "https://registry.invalid/upload/l4tools/metadata/"
    assert b'name="file"; filename="catalog.json"' in request.data
    assert b"payload" in request.data
    assert request.has_header("Authorization")


def test_registry409_no_retry():
    client = registry(urllib.error.HTTPError("https://fixture.invalid", 409, "", {}, None))
    with pytest.raises(ReleaseError, match="409; no retry"):
        client.upload("l4tools/metadata/catalog.json", b"payload")
    assert len(client.opener.requests) == 1


def test_redirect_denied():
    with pytest.raises(ReleaseError, match="redirect"):
        _NoRedirect().redirect_request(None, None, 302, "", {}, "https://other.invalid")


def test_corrupt_zip_refused_as_release_error(tmp_path):
    path, descriptor = archive(tmp_path)
    path.write_bytes(b"corrupt ZIP")
    with pytest.raises(ReleaseError, match="integrity"):
        _inventory(descriptor, path)
