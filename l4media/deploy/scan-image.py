"""Read every saved image layer; reject private key material or private env files."""

import io
import os
import shlex
import subprocess
import sys
import tarfile

PRIVATE_MARKERS = (
    b"-----BEGIN PRIVATE KEY-----",
    b"-----BEGIN RSA PRIVATE KEY-----",
    b"-----BEGIN EC PRIVATE KEY-----",
    b"-----BEGIN OPENSSH PRIVATE KEY-----",
)


def main() -> None:
    findings: list[str] = []
    layers = 0
    with subprocess.Popen(
        shlex.split(os.environ.get("DOCKER_COMMAND", "sudo docker")) + ["image", "save", sys.argv[1]],
        stdout=subprocess.PIPE,
    ) as process:
        assert process.stdout is not None
        with tarfile.open(fileobj=process.stdout, mode="r|*") as archive:
            for member in archive:
                if not member.isfile() or not member.name.endswith("/layer.tar"):
                    continue
                layers += 1
                stream = archive.extractfile(member)
                assert stream is not None
                with tarfile.open(fileobj=io.BytesIO(stream.read())) as layer:
                    for entry in layer:
                        if not entry.isfile():
                            continue
                        path = entry.name
                        if path.rsplit("/", 1)[-1] == ".env" or ".ssh" in path.split("/"):
                            findings.append(path)
                        content = layer.extractfile(entry)
                        assert content is not None
                        # Detect complete PEM headers at line starts, avoiding
                        # program constants describing supported key formats.
                        for line in content:
                            if line.strip() in PRIVATE_MARKERS:
                                findings.append(path)
                                break
        if process.wait() != 0:
            raise SystemExit("Image export failed")
    if not layers:
        raise SystemExit("No Docker image layers recognized; scan was not performed")
    if findings:
        for path in sorted(set(findings)):
            print("Review required: " + path)
        raise SystemExit(1)
    print("Layer scan passed: no private PEM key or private env/SSH files")


if __name__ == "__main__":
    main()
