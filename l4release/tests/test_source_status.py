"""Actual Git status regressions; no native build, signing or deployment."""

from __future__ import annotations

import subprocess

import pytest

from l4release.pipeline import input_snapshot, source_status


@pytest.fixture
def repository(tmp_path):
    config = {
        "source_paths": ["tools", "l4release"],
        "generated_inputs": ["tools/version.txt", "tools/l4setup/src/version.h"],
        "required_assets": ["dependencies/vendor/bin/codec.lib", "tools/mosquitto/mosquitto.exe"],
    }
    names = [
        "tools/version.txt",
        "tools/l4setup/src/version.h",
        "tools/l4setup/bin/x86/l4setup.exe",
        "tools/l4setup/obj/build.log",
        "tools/dist/root.json",
        "l4release/__pycache__/cache.pyc",
        "tools/l4common/remote_status_apply.inc",
        "l4release/with space.py",
        *config["required_assets"],
    ]
    for name in names:
        path = tmp_path / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"original")
    subprocess.run(["git", "init", "-q", str(tmp_path)], check=True)
    subprocess.run(["git", "-C", str(tmp_path), "add", "."], check=True)
    subprocess.run(
        [
            "git",
            "-C",
            str(tmp_path),
            "-c",
            "user.name=Fixture",
            "-c",
            "user.email=fixture@example.invalid",
            "commit",
            "-qm",
            "Fixture",
        ],
        check=True,
    )
    return tmp_path, config


@pytest.mark.parametrize(
    "name",
    [
        "tools/version.txt",
        "tools/l4setup/src/version.h",
        "tools/l4setup/bin/x86/l4setup.exe",
        "tools/l4setup/obj/build.log",
        "tools/dist/root.json",
        "l4release/__pycache__/cache.pyc",
    ],
)
def test_generated_or_output_changes_do_not_dirty_source(repository, name):
    root, config = repository
    (root / name).write_bytes(b"legitimate build output")
    assert not source_status(root, config)
    # Initial release cleanliness remains strict across ALL Git paths.
    assert subprocess.check_output(["git", "-C", str(root), "status", "--porcelain"])


@pytest.mark.parametrize(
    "name",
    [
        "tools/l4common/remote_status_apply.inc",
        "l4release/with space.py",
        "dependencies/vendor/bin/codec.lib",
        "tools/mosquitto/mosquitto.exe",
    ],
)
def test_real_source_and_required_binary_assets_refuse_cleanliness(repository, name):
    root, config = repository
    (root / name).write_bytes(b"actual changed source")
    assert source_status(root, config) == [name]


def test_source_rename_into_generated_directory_remains_dirty(repository):
    root, config = repository
    source = "tools/l4common/remote_status_apply.inc"
    destination = "tools/l4setup/bin/renamed.inc"
    subprocess.run(["git", "-C", str(root), "mv", source, destination], check=True)
    assert source_status(root, config) == [source]


def test_untracked_outputs_ignored_but_new_source_found(repository):
    root, config = repository
    (root / "tools/l4setup/obj/new.log").write_bytes(b"generated")
    (root / "tools/l4common/new.inc").write_bytes(b"source")
    assert source_status(root, config) == ["tools/l4common/new.inc"]


def test_snapshot_uses_same_output_classification_required_assets_override(repository, monkeypatch):
    root, config = repository
    monkeypatch.setattr("l4release.pipeline.dependency_lock", lambda root: {"files": {}})
    snapshot = input_snapshot(root, config)
    assert "tools/l4common/remote_status_apply.inc" in snapshot
    assert "dependencies/vendor/bin/codec.lib" in snapshot
    assert "tools/mosquitto/mosquitto.exe" in snapshot
    assert "tools/l4setup/bin/x86/l4setup.exe" not in snapshot
    assert "tools/l4setup/src/version.h" not in snapshot
