"""Validate private issuer material with the release image and provision its Compose mapping.

Run on the production host from the reviewed repository distribution. No service
is restarted here; the standard image release performs the single-service rollout.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tarfile
from datetime import UTC, datetime
from pathlib import Path

import yaml

ISSUER_ENV = {
    "CA_SIGNER_BACKEND": "local",
    "CA_ISSUING_KEY_PATH": "/run/secrets/terminal-ca/issuer.key.pem",
    "CA_ISSUING_KEY_PASSWORD_FILE": "/run/secrets/terminal-ca/issuer-password.env",
    "CA_ISSUING_CERT_PATH": "",
}

SECRET_NAMES = frozenset({"issuer.key.pem", "issuer-password.env"})


def read_secret_bundle(stream) -> dict[str, bytes]:
    """Read a bounded, explicit distribution from SSH stdin without a /tmp secret file."""
    material = {}
    with tarfile.open(fileobj=stream, mode="r|*") as archive:
        for member in archive:
            if (
                member.name not in SECRET_NAMES
                or not member.isfile()
                or member.name in material
            ):
                raise ValueError("Unexpected or duplicate issuer bundle member")
            if not 0 < member.size <= 65536:
                raise ValueError("Invalid issuer secret size")
            source = archive.extractfile(member)
            if source is None:
                raise ValueError("Cannot read issuer bundle member")
            material[member.name] = source.read()
    if set(material) != SECRET_NAMES:
        raise ValueError("Incomplete issuer bundle")
    if b"-----BEGIN ENCRYPTED PRIVATE KEY-----" not in material["issuer.key.pem"]:
        raise ValueError("An encrypted PKCS#8 issuing key is required")
    return material


def provision_secrets(directory: Path, material: dict[str, bytes]) -> None:
    os.umask(0o077)
    directory.mkdir(mode=0o700, parents=False, exist_ok=True)
    directory.chmod(0o700)
    for name, content in material.items():
        target = directory / name
        if target.is_symlink():
            raise ValueError("Symlink secret files are not supported")
        if target.exists():
            if target.read_bytes() != content:
                raise ValueError(
                    "Different issuer secrets already exist; explicit rotation required"
                )
        else:
            with target.open("xb") as output:
                output.write(content)
                output.flush()
                os.fsync(output.fileno())
        target.chmod(0o600)


def issuer_service(entry: dict, secret_directory: Path) -> dict:
    environment = entry.setdefault("environment", {})
    if not isinstance(environment, dict):
        raise TypeError("Expected a mapping for processing-backend environment")
    environment.update(ISSUER_ENV)
    volumes = entry.setdefault("volumes", [])
    mount = f"{secret_directory}:/run/secrets/terminal-ca:ro"
    for volume in volumes:
        target = (
            volume.get("target") if isinstance(volume, dict) else volume.split(":")[1]
        )
        if target == "/run/secrets/terminal-ca":
            if volume != mount:
                raise ValueError("An unexpected terminal CA mount already exists")
            break
    else:
        volumes.append(mount)
    return entry


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compose", type=Path, required=True)
    parser.add_argument("--secrets-dir", type=Path, required=True)
    parser.add_argument(
        "--image", required=True, help="Verified release image pinned by OCI digest"
    )
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--bundle-stdin", action="store_true")
    args = parser.parse_args()
    if "@sha256:" not in args.image:
        raise ValueError("A digest-pinned release image is required")
    if args.compose.is_symlink() or args.secrets_dir.is_symlink():
        raise ValueError("Symlink provisioning targets are not supported")
    if args.bundle_stdin:
        material = read_secret_bundle(sys.stdin.buffer)
        if not args.apply:
            print(
                "Issuer bundle validated; dry run does not install secrets or change services"
            )
            return
        provision_secrets(args.secrets_dir, material)
    directory = args.secrets_dir.resolve(strict=True)
    for name in ("issuer.key.pem", "issuer-password.env"):
        path = directory / name
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"Missing regular issuer secret file: {name}")
    if (
        b"-----BEGIN ENCRYPTED PRIVATE KEY-----"
        not in (directory / "issuer.key.pem").read_bytes()
    ):
        raise ValueError("An encrypted PKCS#8 issuing key is required")
    config = yaml.safe_load(args.compose.read_text())
    service = config["services"]["processing-backend"]
    issuer_service(service, directory)
    print(
        "Only processing-backend will receive the local signer environment and a read-only secrets mount"
    )
    if not args.apply:
        print("Dry run: no files or services changed; use --apply for provisioning")
        return
    os.umask(0o077)
    directory.chmod(0o700)
    for name in ("issuer.key.pem", "issuer-password.env"):
        (directory / name).chmod(0o600)
    # Import only the signer: no production DB, migrations, web app or networking.
    command = [
        "sudo",
        "-n",
        "docker",
        "run",
        "--rm",
        "--network",
        "none",
        "--read-only",
        "--user",
        f"{directory.stat().st_uid}:{directory.stat().st_gid}",
        "--cap-drop",
        "ALL",
        "--security-opt",
        "no-new-privileges",
        "-v",
        f"{directory}:/run/secrets/terminal-ca:ro",
        "--entrypoint",
        "python",
    ]
    for key, value in ISSUER_ENV.items():
        command.extend(["-e", f"{key}={value}"])
    command.extend(
        [
            args.image,
            "-c",
            (
                "from app.services.local_ca import issuing_key; "
                "assert issuing_key().key_size >= 3072; "
                "print('Release image validates issuer key, certificate and root')"
            ),
        ]
    )
    subprocess.run(command, check=True)
    original = args.compose.read_text()
    rendered = yaml.safe_dump(config, sort_keys=False)
    if rendered == original:
        print("Issuer Compose mapping already provisioned")
        return
    candidate = args.compose.with_name(args.compose.name + ".issuer-candidate")
    if candidate.exists():
        raise ValueError("Existing issuer candidate must be reviewed before retry")
    try:
        with candidate.open("x") as output:
            output.write(rendered)
            output.flush()
            os.fsync(output.fileno())
        candidate.chmod(0o600)
        subprocess.run(
            [
                "sudo",
                "-n",
                "docker",
                "compose",
                "--project-directory",
                str(args.compose.resolve().parent),
                "-f",
                str(candidate),
                "config",
                "--quiet",
            ],
            check=True,
        )
        backup = args.compose.with_name(
            args.compose.name
            + ".before-issuer-"
            + datetime.now(UTC).strftime("%Y%m%dT%H%M%S%fZ")
        )
        shutil.copy2(args.compose, backup)
        backup.chmod(0o600)
        candidate.replace(args.compose)
    finally:
        candidate.unlink(missing_ok=True)
    print(
        "Issuer mapping provisioned; service recreation belongs to the standard release flow"
    )


if __name__ == "__main__":
    main()
