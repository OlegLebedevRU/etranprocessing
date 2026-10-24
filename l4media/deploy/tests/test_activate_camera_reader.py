from __future__ import annotations

import importlib.util
import json
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest

SPEC = importlib.util.spec_from_file_location(
    "activate_camera_reader", Path(__file__).parents[1] / "activate_camera_reader.py"
)
assert SPEC is not None and SPEC.loader is not None
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


def test_camera_candidate_preserves_other_services_and_does_not_copy_secrets():
    previous = {
        "services": {
            "ingress": {"image": "old", "environment": {"EXISTING_FLAG": "kept"}},
            "janus": {"image": "frozen-janus"},
        }
    }
    image = release.REGISTRY + "sha256:" + "a" * 64
    flags = {"L4MEDIA_HEVC_ENABLED": "1", "L4MEDIA_SERVICE_TOKEN": "synthetic-not-copyable"}
    desired = release.candidate_config(previous, image, "b" * 40, flags, True)
    assert previous["services"]["ingress"]["image"] == "old"
    assert desired["services"]["janus"] == previous["services"]["janus"]
    environment = desired["services"]["ingress"]["environment"]
    assert environment == {
        "EXISTING_FLAG": "kept",
        "L4MEDIA_HEVC_ENABLED": "1",
        "L4MEDIA_CAMERA_CONTRACT_ENABLED": "1",
        "L4MEDIA_SOURCE_REVISION": "b" * 40,
    }


def test_compose_preserves_flag_overlays_and_candidate_wins(tmp_path):
    home = tmp_path
    media = home / "l4media"
    state = home / ".etran-ci"
    media.mkdir()
    state.mkdir()
    base = media / "compose.yaml"
    flags = state / "hevc-flags.json"
    candidate = state / "camera-reader-candidate.json"
    command = release.compose_command([str(base), str(flags), str(candidate)], candidate, home)
    assert command[-2:] == ["-f", str(candidate)]
    assert command.index(str(flags)) < command.index(str(candidate))
    assert command.count(str(candidate)) == 1
    with pytest.raises(ValueError):
        release.compose_command([str(base), str(home.parent / "outside.json")], candidate, home)
    with pytest.raises(ValueError):
        release.compose_command([str(flags), str(base)], candidate, home)


@pytest.mark.parametrize(
    "image,revision",
    [
        ("untrusted/ingress@sha256:" + "a" * 64, "b" * 40),
        (release.REGISTRY + "latest", "b" * 40),
        (release.REGISTRY + "sha256:" + "a" * 64, "short"),
    ],
)
def test_candidate_rejects_unpinned_or_wrong_owner(image, revision):
    with pytest.raises(ValueError):
        release.candidate_config({}, image, revision, {}, True)


def test_activation_rolls_back_image_and_flags_without_touching_neighbours(tmp_path, monkeypatch):
    state = tmp_path / ".etran-ci"
    state.mkdir()
    (tmp_path / "l4media").mkdir()
    original = {"services": {"ingress": {"image": "old-image"}, "janus": {"image": "frozen"}}}
    override = state / "l4media-images.json"
    override.write_text(json.dumps(original))
    files = [str(tmp_path / "l4media/compose.yaml"), str(override), str(state / "hevc-flags.json")]
    before = {
        "Id": "old-container",
        "Image": "old-image-id",
        "Config": {
            "Image": "old-image",
            "Env": [
                "L4MEDIA_HEVC_ENABLED=1",
                "L4MEDIA_SOURCE_REVISION=" + "c" * 40,
                "L4MEDIA_SERVICE_TOKEN=synthetic-do-not-copy",
            ],
            "Labels": {"com.docker.compose.project.config_files": ",".join(files)},
        },
    }
    monkeypatch.setitem(sys.modules, "fcntl", SimpleNamespace(flock=lambda *_: None, LOCK_EX=2, LOCK_NB=4))
    monkeypatch.setattr(release.Path, "home", lambda: tmp_path)
    monkeypatch.setattr(release, "inspect", lambda: before)
    monkeypatch.setattr(release, "get", lambda *_: {"status": "ok", "active_media_sessions": 0})
    monkeypatch.setattr(release, "neighbours", lambda: {"l4media-janus": "same-janus-id"})
    revision = "b" * 40

    def docker(*args):
        if args[0] == "pull":
            return ""
        assert args[:2] == ("image", "inspect")
        return json.dumps(
            [{"Id": "new-image-id", "Config": {"Labels": {"org.opencontainers.image.revision": revision}}}]
        )

    monkeypatch.setattr(release, "docker", docker)
    commands = []
    monkeypatch.setattr(release, "run", lambda *args: commands.append(args) or "")

    def readiness(image, _enabled):
        if image == "new-image-id":
            raise ValueError("synthetic readiness failure")
        assert image == "old-image-id"
        return {}

    monkeypatch.setattr(release, "wait_ready", readiness)
    with pytest.raises(ValueError, match="synthetic readiness failure"):
        release.activate(release.REGISTRY + "sha256:" + "a" * 64, revision, True)
    assert json.loads(override.read_text()) == original
    rollback = json.loads((state / "camera-reader-candidate.json").read_text())
    assert rollback["services"]["ingress"]["image"] == "old-image"
    assert rollback["services"]["ingress"]["environment"] == {
        "L4MEDIA_HEVC_ENABLED": "1",
        "L4MEDIA_SOURCE_REVISION": "c" * 40,
    }
    assert "L4MEDIA_SERVICE_TOKEN" not in json.dumps(rollback)
    up = [command for command in commands if "up" in command]
    assert len(up) == 2 and all(command[-1] == "ingress" and "--no-deps" in command for command in up)


def test_activation_waits_for_active_streams_before_pull(tmp_path, monkeypatch):
    monkeypatch.setitem(sys.modules, "fcntl", SimpleNamespace(flock=lambda *_: None, LOCK_EX=2, LOCK_NB=4))
    monkeypatch.setattr(release.Path, "home", lambda: tmp_path)
    monkeypatch.setattr(release, "inspect", lambda: {})
    monkeypatch.setattr(release, "get", lambda *_: {"active_media_sessions": 1})
    monkeypatch.setattr(release, "docker", lambda *_: pytest.fail("Docker mutation before drain"))
    with pytest.raises(ValueError, match="Active streams"):
        release.activate(release.REGISTRY + "sha256:" + "a" * 64, "b" * 40, True)


def test_v3_activation_allows_authorized_interruption_and_keeps_overlays(tmp_path, monkeypatch):
    monkeypatch.setitem(sys.modules, "fcntl", SimpleNamespace(flock=lambda *_: None, LOCK_EX=2, LOCK_NB=4))
    monkeypatch.setattr(release.Path, "home", lambda: tmp_path)
    state = tmp_path / ".etran-ci"
    state.mkdir()
    files = [str(tmp_path / "l4media/compose.yaml"), str(state / "hevc-flags.json")]
    before = {
        "Image": "old-id",
        "Config": {
            "Image": "old-image",
            "Env": ["L4MEDIA_HEVC_ENABLED=1", "L4MEDIA_CAMERA_CONTRACT_ENABLED=1"],
            "Labels": {"com.docker.compose.project.config_files": ",".join(files)},
        },
    }
    monkeypatch.setattr(release, "inspect", lambda: before)
    monkeypatch.setattr(release, "get", lambda *_: pytest.fail("Authorized release must not wait for idle"))
    monkeypatch.setattr(release, "neighbours", lambda: {"janus": "same"})
    revision = "b" * 40
    monkeypatch.setattr(
        release,
        "docker",
        lambda *args: (
            ""
            if args[0] == "pull"
            else json.dumps([{"Id": "new-id", "Config": {"Labels": {"org.opencontainers.image.revision": revision}}}])
        ),
    )
    commands = []
    monkeypatch.setattr(release, "run", lambda *args: commands.append(args) or "")
    monkeypatch.setattr(release, "wait_ready_v3", lambda image, rev: {"contract_version": 3})
    receipt = release.activate(
        release.REGISTRY + "sha256:" + "a" * 64, revision, True, media_v3=True, allow_interruptions=True
    )
    assert receipt["media_contract"] == "l4-media/3.0.0"
    assert receipt["neighbours_unchanged"]
    up = [command for command in commands if "up" in command]
    assert len(up) == 1 and up[0][-1] == "ingress" and "--no-deps" in up[0]
    assert str(state / "hevc-flags.json") in up[0]
    desired = json.loads((state / "l4media-images.json").read_text())
    assert desired["services"]["ingress"]["environment"]["L4MEDIA_CAMERA_CONTRACT_ENABLED"] == "1"


def test_v3_readiness_requires_exact_revision_and_verified_hevc(monkeypatch):
    container = {"Image": "new-id", "Config": {"Env": ["L4MEDIA_HEVC_ENABLED=1"]}}
    monkeypatch.setattr(release, "inspect", lambda: container)
    monkeypatch.setattr(release.time, "monotonic", lambda: 0)
    caps = {
        "contract_version": 3,
        "media_contract_versions": [3],
        "server_revision": "b" * 40,
        "dependencies_ready": True,
        "hevc_runtime_verified": True,
        "codecs": ["h265"],
        "formats": [{"codec": "h265"}],
    }
    monkeypatch.setattr(release, "get", lambda _, path, *selection: {"status": "ok"} if path == "/health" else caps)
    assert release.wait_ready_v3("new-id", "b" * 40) == caps
    # Expire the retry deadline immediately for deterministic rejection.
    times = iter([0, 46])
    monkeypatch.setattr(release.time, "monotonic", lambda: next(times))
    caps["server_revision"] = "c" * 40
    with pytest.raises(ValueError, match="revision/dependencies"):
        release.wait_ready_v3("new-id", "b" * 40)
