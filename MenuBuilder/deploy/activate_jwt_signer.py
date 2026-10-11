"""First activation of the private signer using an already published image.

Run as root on the production host. Secrets stay in protected host files.
The existing base Compose is persisted so ordinary releases retain the signer.
"""

import argparse
import hashlib
import json
import os
import re
import secrets
import shutil
import stat
import subprocess
import time
from pathlib import Path
from urllib.parse import urlsplit


def command(*args):
    result = subprocess.run(args, capture_output=True, check=False)
    if result.returncode:
        # Compose may include expanded credentials in diagnostics.
        raise RuntimeError(f"Command failed: {args[0]} (exit {result.returncode})")
    return result.stdout


def docker(*args):
    return command("docker", *args)


def write_private(path, data):
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(descriptor, "wb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    path.chmod(0o600)


def json_bytes(value):
    return (json.dumps(value, indent=2) + "\n").encode()


def replace_public_key(config, public):
    pattern = r"-----BEGIN PUBLIC KEY-----.*?-----END PUBLIC KEY-----"
    if len(re.findall(pattern, config, re.DOTALL)) != 1:
        raise ValueError("Expected one JWT public key in nginx configuration")
    return re.sub(pattern, public.strip(), config, flags=re.DOTALL)


def wait_health(service, compose):
    endpoint = "health" if service == "jwt-issuer-signer" else "openapi.json"
    for _ in range(30):
        container = docker(*compose, "ps", "-q", service).decode().strip()
        if container:
            try:
                docker(
                    "exec",
                    container,
                    "python",
                    "-c",
                    "import urllib.request; "
                    f"r=urllib.request.urlopen('http://127.0.0.1:8000/{endpoint}', timeout=3); "
                    "assert r.status == 200",
                )
                return container
            except RuntimeError:
                pass
        time.sleep(2)
    raise RuntimeError(f"Health failed: {service}")


def activate(args):
    if os.geteuid() != 0:
        raise RuntimeError("Run this activation as root")
    if command("hostname").decode().strip() != "etranprocessing":
        raise RuntimeError("Unexpected production host")
    memory = next(
        int(line.split()[1])
        for line in Path("/proc/meminfo").read_text().splitlines()
        if line.startswith("MemAvailable:")
    )
    disk = shutil.disk_usage("/")
    if memory <= 300 * 1024 or disk.used / disk.total >= 0.9 or os.getloadavg()[0] >= 2:
        raise RuntimeError("Production resource thresholds exceeded")
    url = urlsplit(args.internal_url)
    if (
        url.scheme != "http"
        or url.hostname != "jwt-issuer-signer"
        or url.port != 8000
        or url.path != "/etranprocessing/jwt"
        or url.query
        or url.fragment
        or url.username
    ):
        raise ValueError("Unexpected private signer URL")
    if not re.fullmatch(r"[0-9a-f]{40}", args.revision):
        raise ValueError("Full revision required")
    if not re.fullmatch(r"[a-z0-9./:-]+@sha256:[0-9a-f]{64}", args.image):
        raise ValueError("Immutable image required")
    key = args.key_file
    metadata = key.stat()
    directory = key.parent.stat()
    if (metadata.st_uid, metadata.st_gid, stat.S_IMODE(metadata.st_mode)) != (10001, 10001, 0o400):
        raise RuntimeError("Private key must be 10001:10001 mode0400")
    if (directory.st_uid, directory.st_gid, stat.S_IMODE(directory.st_mode)) != (0, 0, 0o700):
        raise RuntimeError("Secret directory must be root:root mode0700")
    public = args.public_key.read_text().strip() + "\n"
    derived = command(
        "openssl", "pkey", "-in", str(key), "-passin", "pass:", "-pubout", "-outform", "DER"
    )
    expected = command("openssl", "pkey", "-pubin", "-in", str(args.public_key), "-outform", "DER")
    if derived != expected:
        raise RuntimeError("Signing and verification keys do not match")
    docker("pull", args.image)
    label = (
        docker(
            "image",
            "inspect",
            "--format",
            '{{index .Config.Labels "org.opencontainers.image.revision"}}',
            args.image,
        )
        .decode()
        .strip()
    )
    if label != args.revision:
        raise RuntimeError("Published image revision mismatch")
    base = args.root / "compose.yaml"
    compose = ["compose", "--project-name", "user1", "-f", str(base)]
    original = base.read_bytes()
    config = json.loads(
        docker(*compose, "config", "--no-env-resolution", "--no-interpolate", "--format", "json")
    )
    if "jwt-issuer-signer" in config["services"]:
        raise RuntimeError("Signer already installed; use the ordinary release launcher")
    backend = config["services"]["menubuilder-backend"]
    context = json.loads(
        docker("inspect", "--format", "{{json .Config.Env}}", "menubuilder-backend")
    )
    environment = dict(item.split("=", 1) for item in context)
    issuer = {
        name: environment.get(name, default)
        for name, default in {
            "JWT_ISSUER_CLIENT_ID": "menubuilder-backend",
            "JWT_ISSUER_AUD": "menubuilder",
            "JWT_ISSUER_ISS": "external-jwt-issuer",
            "JWT_ISSUER_KID": "menubuilder-rs256-key-1",
            "JWT_EXPIRE_MINUTES": "60",
            "JWT_REFRESH_EXPIRE_DAYS": "30",
        }.items()
    }
    if any("\n" in value or "\r" in value for value in issuer.values()):
        raise ValueError("Invalid issuer environment")
    signer_env = key.parent / "signer.env"
    backend_env = key.parent / "backend.env"
    if signer_env.exists() or backend_env.exists():
        raise RuntimeError("Activation env files already exist; review before retry")
    secret = secrets.token_urlsafe(48)
    env_line = f"JWT_ISSUER_INTERNAL_SECRET={secret}\n".encode()
    nginx = args.root / "nginx-configs" / "port_3000.conf"
    nginx_original = nginx.read_bytes()
    nginx_updated = replace_public_key(nginx_original.decode(), public).encode()
    state = args.root / ".etran-ci"
    override = state / "user1-images.json"
    override_original = override.read_bytes() if override.exists() else None
    backup = state / ("jwt-activation-" + args.revision)
    backup.mkdir(mode=0o700)
    write_private(backup / "compose.before", original)
    write_private(backup / "nginx.before", nginx_original)
    if override_original:
        write_private(backup / "images.before", override_original)
    try:
        write_private(
            signer_env,
            env_line + "".join(f"{name}={value}\n" for name, value in issuer.items()).encode(),
        )
        native_secret = secrets.token_urlsafe(48)
        write_private(backend_env, env_line + f"CAPTCHA_NATIVE_SECRET={native_secret}\n".encode())
        backend["image"] = args.image
        backend.setdefault("env_file", []).append({"path": str(backend_env), "format": "raw"})
        backend.setdefault("environment", {}).update(
            {
                "JWT_ISSUER_INTERNAL_URL": args.internal_url,
                "JWT_ISSUER_FALLBACK_ENABLED": "false",
                "JWT_ISSUER_MOCK_ENABLED": "false",
                "CAPTCHA_PROVIDER": "native",
                "JWT_PUBLIC_KEY": public,
            }
        )
        backend.setdefault("networks", {})["jwt_signer"] = None
        backend.setdefault("depends_on", {})["jwt-issuer-signer"] = {"condition": "service_healthy"}
        config.setdefault("networks", {})["jwt_signer"] = {"internal": True}
        config["services"]["jwt-issuer-signer"] = {
            "image": args.image,
            "user": "10001:10001",
            "command": [
                "uvicorn",
                "app.jwt_signer:app",
                "--host",
                "0.0.0.0",
                "--port",
                "8000",
                "--workers",
                "1",
                "--no-access-log",
            ],
            "restart": "unless-stopped",
            "read_only": True,
            "cap_drop": ["ALL"],
            "security_opt": ["no-new-privileges:true"],
            "environment": {
                "JWT_SIGNER_PRIVATE_KEY_FILE": "/run/secrets/jwt_signer_private.pem",
                "PYTHONDONTWRITEBYTECODE": "1",
                "JWT_PUBLIC_KEY": public,
            },
            "env_file": [{"path": str(signer_env), "format": "raw"}],
            "volumes": [
                {
                    "type": "bind",
                    "source": str(key),
                    "target": "/run/secrets/jwt_signer_private.pem",
                    "read_only": True,
                    "bind": {"create_host_path": False},
                }
            ],
            "networks": {"jwt_signer": None},
            "healthcheck": {
                "test": [
                    "CMD",
                    "python",
                    "-c",
                    "import urllib.request; "
                    "urllib.request.urlopen('http://127.0.0.1:8000/health', timeout=2)",
                ],
                "interval": "10s",
                "timeout": "3s",
                "retries": 3,
            },
        }
        candidate = backup / "compose.candidate.json"
        write_private(candidate, json_bytes(config))
        docker(
            "compose",
            "--project-name",
            "user1",
            "--project-directory",
            str(args.root),
            "-f",
            str(candidate),
            "config",
            "--quiet",
        )
        # Write in place: mounted nginx files must keep their inode.
        base.write_bytes(candidate.read_bytes())
        docker(
            *compose, "up", "-d", "--no-deps", "--no-build", "--pull", "never", "jwt-issuer-signer"
        )
        signer_container = wait_health("jwt-issuer-signer", compose)
        docker(
            *compose,
            "up",
            "-d",
            "--no-deps",
            "--no-build",
            "--pull",
            "never",
            "menubuilder-backend",
        )
        backend_container = wait_health("menubuilder-backend", compose)
        image_id = docker("image", "inspect", "--format", "{{.Id}}", args.image).strip()
        for container in (signer_container, backend_container):
            if docker("inspect", "--format", "{{.Image}}", container).strip() != image_id:
                raise RuntimeError("Running image mismatch")
        nginx.write_bytes(nginx_updated)
        docker("exec", "nginx-default", "nginx", "-t")
        docker("exec", "nginx-default", "nginx", "-s", "reload")
        images = json.loads(override_original) if override_original else {"services": {}}
        for service in ("menubuilder-backend", "jwt-issuer-signer"):
            images.setdefault("services", {})[service] = {"image": args.image}
        write_private(override, json_bytes(images))
        owner = state.stat()
        os.chown(override, owner.st_uid, owner.st_gid)
        write_private(
            state / "menubuilder-backend-release.json",
            json_bytes({"image": args.image, "revision": args.revision}),
        )
        os.chown(state / "menubuilder-backend-release.json", owner.st_uid, owner.st_gid)
        print(
            f"Activated signer and backend revision={args.revision}; "
            f"public_sha256={hashlib.sha256(expected).hexdigest()}"
        )
    except Exception:
        base.write_bytes(original)
        nginx.write_bytes(nginx_original)
        docker(
            *compose,
            "up",
            "-d",
            "--no-deps",
            "--no-build",
            "--pull",
            "never",
            "menubuilder-backend",
        )
        docker("exec", "nginx-default", "nginx", "-t")
        docker("exec", "nginx-default", "nginx", "-s", "reload")
        # Remove only the new task service; other containers remain untouched.
        container = (
            docker(
                "ps",
                "-aq",
                "--filter",
                "label=com.docker.compose.service=jwt-issuer-signer",
                "--filter",
                "label=com.docker.compose.project=user1",
            )
            .decode()
            .strip()
        )
        if container:
            docker("rm", "-f", container)
        for path in (signer_env, backend_env):
            path.unlink(missing_ok=True)
        if override_original is not None:
            write_private(override, override_original)
            owner = state.stat()
            os.chown(override, owner.st_uid, owner.st_gid)
        else:
            override.unlink(missing_ok=True)
        raise


if __name__ == "__main__":
    import fcntl

    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--image", required=True)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--key-file", type=Path, required=True)
    parser.add_argument("--public-key", type=Path, required=True)
    parser.add_argument("--internal-url", required=True)
    args = parser.parse_args()
    with (args.root / ".etran-ci" / "deploy.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        activate(args)
