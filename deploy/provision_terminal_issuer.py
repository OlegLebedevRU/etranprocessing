"""Deliver the reviewed issuer installer and private material via SSH; no image release here."""

import argparse
import io
import shlex
import subprocess
import sys
import tarfile
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--identity", type=Path, required=True)
    parser.add_argument("--compose", required=True)
    parser.add_argument("--remote-secrets-dir", required=True)
    parser.add_argument("--image", required=True)
    parser.add_argument("--key", type=Path, required=True)
    parser.add_argument("--password-file", type=Path, required=True)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    if args.host.startswith("-") or "@sha256:" not in args.image:
        raise ValueError(
            "A destination host and digest-pinned release image are required"
        )
    # Uses the same certificate, trust and password validation as the application.
    sys.path.insert(0, str(ROOT / "ProcessingBackend" / "backend"))
    from app.config import settings
    from app.services.local_ca import issuing_key

    settings.ca_issuing_key_path = str(args.key.resolve(strict=True))
    settings.ca_issuing_key_password_file = str(args.password_file.resolve(strict=True))
    issuing_key()
    if not args.apply:
        print("Issuer key/certificate/root validated locally; no remote changes")
        return
    bundle = io.BytesIO()
    with tarfile.open(fileobj=bundle, mode="w") as archive:
        for name, source in (
            ("issuer.key.pem", args.key),
            ("issuer-password.env", args.password_file),
        ):
            content = source.read_bytes()
            member = tarfile.TarInfo(name)
            member.size, member.mode = len(content), 0o600
            archive.addfile(member, io.BytesIO(content))
    remote_installer = f"/tmp/terminal-issuer-installer-{uuid.uuid4().hex}.py"
    common = [
        "-o",
        "BatchMode=yes",
        "-o",
        "ConnectTimeout=10",
        "-i",
        str(args.identity),
    ]
    subprocess.run(
        [
            "scp",
            *common,
            str(ROOT / "deploy" / "configure_terminal_issuer.py"),
            f"{args.host}:{remote_installer}",
        ],
        check=True,
    )
    try:
        command = shlex.join(
            [
                "python3",
                remote_installer,
                "--compose",
                args.compose,
                "--secrets-dir",
                args.remote_secrets_dir,
                "--image",
                args.image,
                "--bundle-stdin",
                "--apply",
            ]
        )
        # Explicit stdin transport: password/key never enter command arguments or logs.
        subprocess.run(
            ["ssh", "-T", *common, args.host, command],
            input=bundle.getvalue(),
            check=True,
        )
    finally:
        subprocess.run(
            [
                "ssh",
                "-n",
                *common,
                args.host,
                shlex.join(["rm", "-f", "--", remote_installer]),
            ],
            check=True,
        )
    print(
        "Issuer secrets and Compose mapping provisioned; run the standard single-service image release"
    )


if __name__ == "__main__":
    main()
