"""Prepare a local ignored bootstrap key; print only its public half."""

import os
import subprocess
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

ROOT = Path(__file__).resolve().parents[2]


def main():
    directory = ROOT / ".beta-ci-secrets"
    directory.mkdir(mode=0o700, exist_ok=True)
    if os.name == "nt":
        account = subprocess.check_output(["whoami"], text=True).strip()
        subprocess.run(
            [
                "icacls",
                str(directory),
                "/inheritance:r",
                "/grant:r",
                account + ":(OI)(CI)F",
            ],
            check=True,
        )
    path = directory / "deploy"
    if path.exists():
        print((directory / "deploy.pub").read_text().strip())
        return
    key = Ed25519PrivateKey.generate()
    path.write_bytes(
        key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.OpenSSH,
            serialization.NoEncryption(),
        )
    )
    if os.name != "nt":
        path.chmod(0o600)
    public = (
        key.public_key()
        .public_bytes(
            serialization.Encoding.OpenSSH, serialization.PublicFormat.OpenSSH
        )
        .decode()
    )
    public += " etran-beta-deploy"
    (directory / "deploy.pub").write_text(public + "\n")
    known = subprocess.check_output(["ssh-keygen", "-F", "87.242.100.34"], text=True)
    entries = [
        line
        for line in known.splitlines()
        if not line.startswith("#") and " ssh-ed25519 " in line
    ]
    if not entries:
        raise RuntimeError("No previously trusted ED25519 production host key")
    (directory / "known_hosts").write_text("\n".join(entries) + "\n")
    print("restrict " + public)


if __name__ == "__main__":
    main()
