"""Read every saved image layer; reject private key material or private env files."""

import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
from typing import BinaryIO

PRIVATE_MARKERS = (
    b"-----BEGIN PRIVATE KEY-----",
    b"-----BEGIN RSA PRIVATE KEY-----",
    b"-----BEGIN EC PRIVATE KEY-----",
    b"-----BEGIN OPENSSH PRIVATE KEY-----",
)


def scan_layer(layer: tarfile.TarFile) -> list[str]:
    findings: list[str] = []
    for entry in layer:
        if not entry.isfile():
            continue
        path = entry.name
        if path.rsplit("/", 1)[-1] == ".env" or ".ssh" in path.split("/"):
            findings.append(path)
        content = layer.extractfile(entry)
        if content is None:
            raise ValueError("Unreadable layer entry")
        with content:
            # Full PEM header lines, not source constants naming a key format.
            for line in content:
                if line.strip() in PRIVATE_MARKERS:
                    findings.append(path)
                    break
    return findings


def metadata_blob(archive: tarfile.TarFile, member: tarfile.TarInfo) -> bool:
    if member.size > 8 * 1024 * 1024:
        return False
    stream = archive.extractfile(member)
    if stream is None:
        return False
    try:
        with stream:
            value = json.load(stream)
    except (ValueError, UnicodeError):
        return False
    return isinstance(value, dict) and (
        "architecture" in value
        and "rootfs" in value
        or value.get("schemaVersion") == 2
        and ("layers" in value or "manifests" in value)
        or isinstance(value.get("container_config"), dict)
        and value.get("os") in {"linux", "windows"}
        and isinstance(value.get("id"), str)
        and re.fullmatch(r"(?:sha256:)?[0-9a-f]{64}", value["id"]) is not None
    )


def scan_saved_image(saved: BinaryIO) -> tuple[int, list[str]]:
    """Read both legacy layer.tar and containerd Docker-save blob representations.

    Docker's compatibility manifest identifies required layers. Every additional
    tar/gzip blob is scanned too, including compressed/uncompressed duplicates.
    Unknown blobs, missing layers and unfamiliar exports still fail closed.
    """
    layers = 0
    findings: list[str] = []
    with tarfile.open(fileobj=saved, mode="r:*") as archive:
        files = [member for member in archive.getmembers() if member.isfile()]
        members = {member.name: member for member in files}
        if len(members) != len(files):
            raise ValueError("Duplicate image archive member")
        required = {name for name in members if name.endswith("/layer.tar")}
        if "manifest.json" in members:
            manifest_file = members["manifest.json"]
            if manifest_file.size > 8 * 1024 * 1024:
                raise ValueError("Image manifest exceeds budget")
            stream = archive.extractfile(manifest_file)
            if stream is None:
                raise ValueError("Unreadable image manifest")
            with stream:
                manifest = json.load(stream)
            if not isinstance(manifest, list) or not manifest:
                raise ValueError("Unknown Docker image manifest")
            for image in manifest:
                names = image.get("Layers") if isinstance(image, dict) else None
                if not isinstance(names, list) or not names or not all(isinstance(name, str) for name in names):
                    raise ValueError("Invalid Docker layer references")
                required.update(names)
        if not required or not required.issubset(members):
            raise ValueError("No recognized Docker layers or missing referenced layer")
        candidates = required | {name for name in members if re.fullmatch(r"blobs/sha256/[0-9a-f]{64}", name)}
        for name in sorted(candidates):
            stream = archive.extractfile(members[name])
            if stream is None:
                raise ValueError("Unreadable image layer")
            try:
                with stream, tarfile.open(fileobj=stream, mode="r:*") as layer:
                    findings.extend(scan_layer(layer))
                    layers += 1
            except tarfile.ReadError as error:
                if name in required or not metadata_blob(archive, members[name]):
                    raise ValueError("Unrecognized or corrupt image blob: " + name) from error
    if not layers:
        raise ValueError("No Docker image layers recognized; scan was not performed")
    return layers, findings


def main() -> None:
    with subprocess.Popen(
        shlex.split(os.environ.get("DOCKER_COMMAND", "sudo docker")) + ["image", "save", sys.argv[1]],
        stdout=subprocess.PIPE,
    ) as process:
        assert process.stdout is not None
        # Compatibility manifests may follow their layers. Spool to disk rather
        # than buffering the entire image in RAM or guessing from file names.
        with tempfile.TemporaryFile() as saved:
            shutil.copyfileobj(process.stdout, saved)
            if process.wait() != 0:
                raise SystemExit("Image export failed")
            saved.seek(0)
            try:
                layers, findings = scan_saved_image(saved)
            except (ValueError, tarfile.TarError, UnicodeError) as error:
                raise SystemExit(str(error)) from error
    if findings:
        for path in sorted(set(findings)):
            print("Review required: " + path)
        raise SystemExit(1)
    print(f"Layer scan passed ({layers} saved layers): no private PEM key or private env/SSH files")


if __name__ == "__main__":
    main()
