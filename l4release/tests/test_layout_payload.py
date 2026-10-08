import hashlib
import json
import zipfile
from pathlib import Path

import pytest

from l4release.common import ReleaseError
from l4release.layout_payload import EXECUTABLES, _destination, build_layout_payload


def make_stage(stage: Path) -> None:
    for path in EXECUTABLES:
        file = stage / path
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_bytes(b"MZfixture")
    for path, content in {
        "l4superv.json": '{"base_path":"C:\\\\l4tools","watchdog_enabled":true}',
        "mosquitto/acl.conf": "user svc\n",
        "l4capture/bin/idle_refresh.ini": "interval=1\n",
        "l4con/l4con_install.cmd": "obsolete fixture",
        "l4superv/package-components.json": "obsolete inventory fixture",
        "term_tool-user-guide.md": "fixture guide",
        "l4capture/SBOM.json": "{}",
    }.items():
        file = stage / path
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(content, encoding="utf-8")


def test_complete_deterministic_zip_and_separate_templates(tmp_path):
    stage, output = tmp_path / "stage", tmp_path / "output"
    make_stage(stage)
    item = build_layout_payload(stage, output, "1.13.3", "x86", "11" * 32)
    descriptor = json.loads((output / item["manifest"]).read_text())
    with zipfile.ZipFile(output / item["archive"]) as archive:
        assert set(archive.namelist()) == {f["path"] for f in descriptor["files"]}
        for file in descriptor["files"]:
            content = archive.read(file["path"])
            assert len(content) == file["size"]
            assert hashlib.sha256(content).hexdigest() == file["sha256"]
        assert "base_path" not in json.loads(archive.read("templates/l4superv.json"))
        assert not any(p.endswith(".cmd") or "/log/" in p for p in archive.namelist())
    assert build_layout_payload(stage, output, "1.13.3", "x86", "11" * 32) == item
    assert not list(output.glob("*.pending"))


@pytest.mark.parametrize(
    "path",
    [
        "foreign/file.txt",
        "l4con/settings.json",
        "l4con/log/x.txt",
        "l4con/secret.pfx",
        "l4con/other.exe",
    ],
)
def test_unclassified_data_refuses(tmp_path, path):
    stage = tmp_path / "stage"
    make_stage(stage)
    file = stage / path
    file.parent.mkdir(parents=True, exist_ok=True)
    file.write_text("fixture", encoding="utf-8")
    with pytest.raises(ReleaseError):
        build_layout_payload(stage, tmp_path / "output", "1.13.3", "x86", None)


def test_missing_native_component_refuses(tmp_path):
    stage = tmp_path / "stage"
    make_stage(stage)
    (stage / "l4launch/l4launch.exe").unlink()
    with pytest.raises(ReleaseError, match="Incomplete"):
        build_layout_payload(stage, tmp_path / "output", "1.13.3", "x64", None)


def test_windows_device_alias_refuses():
    with pytest.raises(ReleaseError, match="Windows alias"):
        _destination("l4con/CON.txt")


def test_publisher_includes_all_layout_artifacts_and_detects_corruption(tmp_path):
    from deploy.publish_l4tools import artifact_upload_order, verify_artifacts

    stage, output = tmp_path / "stage", tmp_path / "output"
    make_stage(stage)
    output.mkdir()
    (output / "l4setup.exe").write_bytes(b"unsigned fixture")
    manifest = {
        "version": "1.13.3",
        "publisher_certificate_sha256": "11" * 32,
        "files": {},
        "layout_payloads": {},
    }
    for arch in ("x86", "x64"):
        manifest["layout_payloads"][arch] = build_layout_payload(
            stage, output, "1.13.3", arch, "11" * 32
        )
    for file in output.iterdir():
        content = file.read_bytes()
        manifest["files"][file.name] = {
            "sha256": hashlib.sha256(content).hexdigest(),
            "size": len(content),
        }
    (output / "l4tools-release.json").write_text(json.dumps(manifest), encoding="utf-8")
    (output / "SHA256SUMS").write_text(
        "".join(
            f"{hashlib.sha256(file.read_bytes()).hexdigest()}  {file.name}\n"
            for file in output.iterdir()
        ),
        encoding="utf-8",
    )
    _, digests, _ = verify_artifacts(output)
    assert len(artifact_upload_order(digests)) == 7
    assert artifact_upload_order(digests)[-1] == "l4tools-release.json"
    (output / "l4tools-layout-x64.zip").write_bytes(b"damaged")
    with pytest.raises(ValueError, match="Checksum"):
        verify_artifacts(output)


def test_bounded_inventory_and_hardlink_refuse(tmp_path):
    stage = tmp_path / "stage"
    make_stage(stage)
    for i in range(50):
        (stage / "l4con" / f"document{i}.md").write_text("fixture", encoding="utf-8")
    with pytest.raises(ReleaseError, match="64-file"):
        build_layout_payload(stage, tmp_path / "output", "1.13.3", "x86", None)
    for i in range(50):
        (stage / "l4con" / f"document{i}.md").unlink()
    (stage / "l4con/linked.md").hardlink_to(stage / "l4con/l4con.exe")
    with pytest.raises(ReleaseError, match="Hardlinked"):
        build_layout_payload(stage, tmp_path / "output", "1.13.3", "x86", None)
