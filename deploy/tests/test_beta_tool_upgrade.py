"""Explicit builder upgrades preserve the existing tool and never follow symlinks."""

import importlib.util
import os
from pathlib import Path

import pytest

spec = importlib.util.spec_from_file_location(
    "beta_install", Path(__file__).parents[1] / "beta/install.py"
)
assert spec is not None and spec.loader is not None
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


def install(path, content, **kwargs):
    installer.install_tool(
        path,
        content,
        getattr(os, "getuid", lambda: 0)(),
        getattr(os, "getgid", lambda: 0)(),
        **kwargs,
    )


def test_unapproved_upgrade_preserves_tool(tmp_path):
    path = tmp_path / "uv"
    path.write_bytes(b"previous fixture")
    with pytest.raises(RuntimeError, match="Refusing"):
        install(path, b"new fixture")
    assert path.read_bytes() == b"previous fixture"


def test_explicit_upgrade_retains_rollback_and_is_idempotent(tmp_path):
    path = tmp_path / "uv"
    path.write_bytes(b"previous fixture")
    install(path, b"new fixture", upgrade=True)
    assert path.read_bytes() == b"new fixture"
    backups = list(tmp_path.glob("uv.previous-*"))
    assert len(backups) == 1 and backups[0].read_bytes() == b"previous fixture"
    install(path, b"new fixture")
    assert len(list(tmp_path.glob("uv.previous-*"))) == 1
    assert not (tmp_path / "uv.installing").exists()


def test_failed_atomic_install_preserves_live_binary(tmp_path):
    path = tmp_path / "uv"
    path.write_bytes(b"previous fixture")
    (tmp_path / "uv.installing").write_bytes(b"unrelated fixture")
    with pytest.raises(FileExistsError):
        install(path, b"new fixture", upgrade=True)
    assert path.read_bytes() == b"previous fixture"
