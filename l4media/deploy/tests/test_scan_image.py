"""Synthetic saved images exercise the release scanner without Docker or credentials."""

import importlib.util
import io
import subprocess
import sys
import tarfile
from pathlib import Path

import pytest


def make_tar(files):
    stream = io.BytesIO()
    with tarfile.open(fileobj=stream, mode="w") as archive:
        for path, body in files.items():
            member = tarfile.TarInfo(path)
            member.size = len(body)
            archive.addfile(member, io.BytesIO(body))
    return stream.getvalue()


@pytest.mark.parametrize(
    ("files", "error"),
    [
        ({"app/source.py": b'HEADER = b"-----BEGIN PRIVATE KEY-----"\n'}, False),
        ({"app/.env": b"EXAMPLE=placeholder\n"}, True),
        ({".ssh/example": b"synthetic\n"}, True),
        ({"app/key.pem": b"-----BEGIN PRIVATE KEY-----\n"}, True),
    ],
)
def test_layer_scan(monkeypatch, files, error):
    image = make_tar({"example/layer.tar": make_tar(files)})
    run_scan(monkeypatch, image, error)


def test_unknown_image_layout_fails(monkeypatch):
    run_scan(monkeypatch, make_tar({"manifest.json": b"[]"}), True)


def run_scan(monkeypatch, image, error):
    path = Path(__file__).parents[1] / "scan-image.py"
    spec = importlib.util.spec_from_file_location("media_image_scan", path)
    assert spec is not None and spec.loader is not None
    scanner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(scanner)

    class Export:
        stdout = io.BytesIO(image)

        def __enter__(self):
            return self

        def __exit__(self, *_args):
            self.stdout.close()

        def wait(self):
            return 0

    monkeypatch.setattr(subprocess, "Popen", lambda *_args, **_kwargs: Export())
    monkeypatch.setattr(sys, "argv", [str(path), "synthetic-image"])
    if error:
        with pytest.raises(SystemExit):
            scanner.main()
    else:
        scanner.main()
