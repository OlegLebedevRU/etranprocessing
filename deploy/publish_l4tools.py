#!/usr/bin/env python3
"""Publish l4tools release artifacts directly to Generic Artifact Registry.

Usage:
  python deploy/publish_l4tools.py verify <artifacts_dir> [--allow-dirty]
  python deploy/publish_l4tools.py check <version>
  python deploy/publish_l4tools.py publish <artifacts_dir> [--dry-run] [--allow-dirty]
  python deploy/publish_l4tools.py record <artifacts_dir>
      [--record-dir <dir>] [--releases-file <path>]
"""

import argparse
import base64
import hashlib
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from datetime import UTC, datetime
from pathlib import Path

DEFAULT_REGISTRY = "https://l4tools-generic.ar.cloud.ru"
REQUIRED_FILES = ("l4setup.exe", "SHA256SUMS", "l4tools-release.json")
UPLOAD_ORDER = ("l4setup.exe", "SHA256SUMS", "l4tools-release.json")
LAYOUT_FILES = tuple(
    f"l4tools-layout-{arch}.{suffix}" for arch in ("x86", "x64") for suffix in ("zip", "json")
)
SIGNATURE_FILES = tuple(f"l4tools-layout-{arch}.json.sig" for arch in ("x86", "x64")) + (
    "l4tools-release.json.sig",
)


def artifact_upload_order(digests: dict[str, str]) -> tuple[str, ...]:
    """Only fixed local artifact names; release manifest is published last."""
    extra = tuple(name for name in LAYOUT_FILES if name in digests)
    if extra and len(extra) != len(LAYOUT_FILES):
        raise ValueError("Incomplete layout payload set")
    signatures = tuple(name for name in SIGNATURE_FILES if name in digests)
    if signatures and (len(signatures) != 3 or not extra):
        raise ValueError("Incomplete metadata signature set")
    return ("l4setup.exe", *extra, *signatures, "SHA256SUMS", "l4tools-release.json")


def configured_metadata_public(env: dict[str, str], env_path: Path | None) -> object | None:
    """Explicit local trust source. Never download a key or read the private key."""
    value = env.get("L4TOOLS_METADATA_KEY_PATH")
    if not value:
        return None
    from cryptography.hazmat.primitives import serialization

    path = Path(value)
    if not path.is_absolute():
        if env_path is None:
            raise ValueError("Relative metadata key requires an explicit env file")
        path = env_path.resolve().parent / path
    with path.with_suffix(".public.pem").open("rb") as stream:
        data = stream.read(16385)
    if len(data) > 16384:
        raise ValueError("Metadata public key exceeds its bound")
    return serialization.load_pem_public_key(data)


def verify_metadata_signatures(artifacts_dir: Path, manifest: dict, trusted_public: object) -> None:
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import padding, rsa

    if not isinstance(trusted_public, rsa.RSAPublicKey):
        raise ValueError("A locally trusted RSA metadata public key is required")
    if trusted_public.key_size != 3072 or trusted_public.public_numbers().e != 65537:
        raise ValueError("Metadata public key must be RSA3072/e65537")
    identity = hashlib.sha256(
        trusted_public.public_bytes(
            serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo
        )
    ).hexdigest()
    expected = {
        "schema": 1,
        "algorithm": "RSA3072-PKCS1v1.5-SHA256",
        "key_id": identity,
        "signature": "l4tools-release.json.sig",
    }
    if manifest.get("metadata_signatures") != expected:
        raise ValueError("Metadata signature identity/format mismatch")
    for name in SIGNATURE_FILES:
        with (artifacts_dir / name.removesuffix(".sig")).open("rb") as stream:
            document = stream.read(65536)
        with (artifacts_dir / name).open("rb") as stream:
            signature = stream.read(385)
        if not 0 < len(document) <= 65535 or len(signature) != 384:
            raise ValueError("Metadata document/signature size mismatch")
        try:
            trusted_public.verify(signature, document, padding.PKCS1v15(), hashes.SHA256())
        except InvalidSignature as error:
            raise ValueError("Metadata signature verification failed") from error


DEFAULT_RECORD_DIR = Path("artifacts/l4tools")
DEFAULT_RELEASES_FILE = Path("releases.jsonl")


def load_env_file(env_path: Path | None = None) -> dict[str, str]:
    """Load key-value pairs from .env file if it exists."""
    candidates = []
    if env_path:
        candidates.append(env_path)
    else:
        script_dir = Path(__file__).resolve().parent
        repo_root = script_dir.parent
        candidates.extend(
            [
                Path(".env"),
                repo_root / ".env",
                Path("deploy/.env"),
                script_dir / ".env",
            ]
        )

    loaded: dict[str, str] = {}
    for candidate in candidates:
        if candidate.is_file():
            try:
                for line in candidate.read_text(encoding="utf-8").splitlines():
                    line = line.strip()
                    if not line or line.startswith("#") or "=" not in line:
                        continue
                    key, val = line.split("=", 1)
                    key = key.strip()
                    val = val.strip().strip("'\"")
                    if key:
                        loaded[key] = val
                break
            except Exception as ex:
                print(f"[WARN] Failed to read env file {candidate}: {ex}", file=sys.stderr)
    return loaded


def get_credentials(
    env_vars: dict[str, str] | None = None,
) -> tuple[str | None, str | None]:
    """Retrieve AR_GENERIC_KEY_ID and AR_GENERIC_KEY_SECRET from env or .env file."""
    env = env_vars if env_vars is not None else load_env_file()
    key_id = os.environ.get("AR_GENERIC_KEY_ID") or env.get("AR_GENERIC_KEY_ID")
    key_secret = os.environ.get("AR_GENERIC_KEY_SECRET") or env.get("AR_GENERIC_KEY_SECRET")
    return key_id, key_secret


def mask_secret(text: str, secret: str | None, key_id: str | None = None) -> str:
    """Mask credentials and sensitive strings in log outputs."""
    masked = text
    if secret:
        masked = masked.replace(secret, "***")
    if key_id and len(key_id) > 6:
        masked = masked.replace(key_id, f"{key_id[:4]}***")
    # Also mask any basic auth URL pattern: https://user:pass@host -> https://***:***@host
    masked = re.sub(r"://([^:]+):([^@]+)@", r"://***:***@", masked)
    return masked


def compute_sha256(path: Path) -> str:
    """Calculate SHA256 hex digest for a file."""
    hasher = hashlib.sha256()
    with path.open("rb") as f:
        while chunk := f.read(65536):
            hasher.update(chunk)
    return hasher.hexdigest().lower()


def parse_sha256sums(path: Path) -> dict[str, str]:
    """Parse SHA256SUMS file into {filename: sha256_hex}."""
    result: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        # Format: "<hash>  <filename>" or "<hash> *<filename>"
        parts = line.split(maxsplit=1)
        if len(parts) == 2:
            sha, filename = parts[0].strip().lower(), parts[1].strip().lstrip("*")
            result[Path(filename).name] = sha
    return result


def parse_rfc3230_digest(headers: dict[str, str]) -> str | None:
    """Extract sha-256 digest hex from RFC 3230 Digest header."""
    for key, value in headers.items():
        if key.lower() == "digest":
            match = re.search(r"sha-256=([0-9a-fA-F]+)", value, re.IGNORECASE)
            if match:
                return match.group(1).lower()
    return None


def verify_artifacts(
    artifacts_dir: Path,
    allow_dirty: bool = False,
    *,
    trusted_public: object | None = None,
    require_metadata: bool = False,
) -> tuple[dict, dict[str, str], dict[str, int]]:
    """Verify presence, integrity and manifest validity of release artifacts.

    Returns:
        tuple of (manifest_data, sha256_map, size_map)
    """
    if not artifacts_dir.is_dir():
        raise FileNotFoundError(f"Artifacts directory not found: {artifacts_dir}")

    manifest_file = artifacts_dir / "l4tools-release.json"
    manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
    layout = manifest.get("layout_payloads")
    signed_metadata = manifest.get("metadata_signatures") is not None
    if require_metadata and not signed_metadata:
        raise ValueError("Signed metadata is required")
    if signed_metadata:
        if layout is None or trusted_public is None:
            raise ValueError("Signed metadata requires layout and a locally trusted public key")
        verify_metadata_signatures(artifacts_dir, manifest, trusted_public)
    if layout is not None and (not isinstance(layout, dict) or set(layout) != {"x86", "x64"}):
        raise ValueError("Incomplete layout payload metadata")
    required_files = (
        *REQUIRED_FILES,
        *(LAYOUT_FILES if layout is not None else ()),
        *(SIGNATURE_FILES if signed_metadata else ()),
    )
    # 1. Check required files
    for filename in required_files:
        filepath = artifacts_dir / filename
        if not filepath.is_file():
            raise FileNotFoundError(f"Missing required artifact: {filepath}")

    # 2. Check SHA256SUMS
    sums_file = artifacts_dir / "SHA256SUMS"
    expected_sums = parse_sha256sums(sums_file)
    computed_sums: dict[str, str] = {}
    sizes: dict[str, int] = {}

    for filename in required_files:
        filepath = artifacts_dir / filename
        computed = compute_sha256(filepath)
        computed_sums[filename] = computed
        sizes[filename] = filepath.stat().st_size

        if filename != "SHA256SUMS":
            if filename not in expected_sums:
                raise ValueError(f"File {filename} is missing from SHA256SUMS")
            if computed != expected_sums[filename]:
                raise ValueError(
                    f"Checksum mismatch for {filename}: computed "
                    f"{computed} != expected {expected_sums[filename]}"
                )

    # 3. Check manifest
    manifest_file = artifacts_dir / "l4tools-release.json"
    try:
        manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
    except json.JSONDecodeError as ex:
        raise ValueError(f"Invalid JSON in manifest {manifest_file}: {ex}") from ex

    version = manifest.get("version")
    if not version:
        raise ValueError("Manifest is missing 'version' field")

    files_info = manifest.get("files", {})
    l4setup_info = files_info.get("l4setup.exe", {})
    expected_exe_sha = l4setup_info.get("sha256", "").lower()
    if not expected_exe_sha:
        raise ValueError("Manifest is missing 'files.l4setup.exe.sha256'")
    if computed_sums["l4setup.exe"] != expected_exe_sha:
        raise ValueError(
            f"Manifest sha256 mismatch for l4setup.exe: "
            f"manifest={expected_exe_sha}, computed={computed_sums['l4setup.exe']}"
        )
    if layout is not None:
        if signed_metadata:
            for name in SIGNATURE_FILES[:2]:
                if files_info.get(name) != {"sha256": computed_sums[name], "size": 384}:
                    raise ValueError("Metadata signature inventory mismatch")
        for arch in ("x86", "x64"):
            entry = layout[arch]
            for key, suffix in (("archive", "zip"), ("manifest", "json")):
                name = f"l4tools-layout-{arch}.{suffix}"
                if entry.get(key) != name or entry.get(key + "_sha256") != computed_sums[name]:
                    raise ValueError("Layout artifact metadata mismatch")
                if (
                    files_info.get(name, {}).get("sha256") != computed_sums[name]
                    or files_info.get(name, {}).get("size") != sizes[name]
                ):
                    raise ValueError("Layout artifact file inventory mismatch")
        for arch in ("x86", "x64"):
            descriptor = json.loads(
                (artifacts_dir / f"l4tools-layout-{arch}.json").read_text(encoding="utf-8")
            )
            if (
                descriptor.get("version") != version
                or descriptor.get("arch") != arch
                or descriptor.get("archive_sha256") != computed_sums[f"l4tools-layout-{arch}.zip"]
                or descriptor.get("publisher_certificate_sha256")
                != manifest.get("publisher_certificate_sha256")
            ):
                raise ValueError("Layout descriptor identity mismatch")

    is_dirty = manifest.get("dirty", False)
    if is_dirty and not allow_dirty:
        raise ValueError(
            f"Manifest marks release {version} as dirty. "
            f"Rebuild cleanly or use --allow-dirty for beta."
        )

    return manifest, computed_sums, sizes


def check_remote_version(
    version: str,
    registry_base: str = DEFAULT_REGISTRY,
    timeout: int = 15,
    key_id: str | None = None,
    key_secret: str | None = None,
) -> int:
    """Check if version is already published in registry.

    Returns:
        0: not published (404)
        2: already published (200)
        1: error (other status or network failure)
    """
    check_url = f"{registry_base.rstrip('/')}/l4tools/{version}/l4setup.exe"
    if not key_id or not key_secret:
        key_id, key_secret = get_credentials()
    headers = {}
    if key_id and key_secret:
        auth_str = f"{key_id}:{key_secret}"
        headers["Authorization"] = "Basic " + base64.b64encode(auth_str.encode("utf-8")).decode(
            "ascii"
        )
    req = urllib.request.Request(check_url, headers=headers, method="HEAD")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as response:
            if response.status == 200:
                print(f"[CHECK] Version {version} is ALREADY published in registry: {check_url}")
                return 2
    except urllib.error.HTTPError as ex:
        if ex.code == 404:
            print(f"[CHECK] Version {version} is not yet published in registry (404 OK)")
            return 0
        print(f"[ERROR] HTTP error during check for {check_url}: {ex.code} {ex.reason}")
        return 1
    except urllib.error.URLError as ex:
        print(f"[ERROR] Connection error during check for {check_url}: {ex.reason}")
        return 1
    except Exception as ex:
        print(f"[ERROR] Unexpected error during check for {check_url}: {ex}")
        return 1

    return 0


def upload_file_with_retry(
    url: str,
    file_bytes: bytes,
    key_id: str,
    key_secret: str,
    filename: str | None = None,
    max_retries: int = 3,
    timeout: int = 300,
) -> int:
    """Upload file via HTTP PUT using multipart/form-data with Basic Auth and exponential backoff.

    Returns:
        HTTP status code (200, 201, 204, etc.)
    Raises:
        RuntimeError on failure.
    """
    auth_str = f"{key_id}:{key_secret}"
    auth_header = "Basic " + base64.b64encode(auth_str.encode("utf-8")).decode("ascii")

    # If url ends with filename, strip it to get the target folder URL
    if filename:
        if url.rstrip("/").endswith(filename):
            target_dir_url = url[: url.rfind(filename)]
            if not target_dir_url.endswith("/"):
                target_dir_url += "/"
        else:
            target_dir_url = url if url.endswith("/") else url + "/"
    else:
        parts = url.rstrip("/").rsplit("/", 1)
        if len(parts) == 2:
            target_dir_url = parts[0] + "/"
            filename = parts[1]
        else:
            target_dir_url = url + "/"
            filename = "file"

    masked_url = mask_secret(target_dir_url + filename, key_secret, key_id)

    if filename.endswith(".json"):
        content_type = "application/json"
    elif filename.endswith(".exe"):
        content_type = "application/octet-stream"
    else:
        content_type = "text/plain"

    boundary = f"----WebKitFormBoundary{uuid.uuid4().hex}"
    body_header = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="file"; filename="{filename}"\r\n'
        f"Content-Type: {content_type}\r\n\r\n"
    ).encode()
    body_footer = f"\r\n--{boundary}--\r\n".encode()
    body = body_header + file_bytes + body_footer

    for attempt in range(1, max_retries + 1):
        req = urllib.request.Request(
            target_dir_url,
            data=body,
            headers={
                "Authorization": auth_header,
                "Content-Length": str(len(body)),
                "Content-Type": f"multipart/form-data; boundary={boundary}",
            },
            method="PUT",
        )
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                status = resp.status
                print(f"[UPLOAD] PUT {masked_url} -> HTTP {status}")
                return status
        except urllib.error.HTTPError as ex:
            # Client errors (401/403/409) must not be retried.
            if 400 <= ex.code < 500:
                raise RuntimeError(
                    f"HTTP {ex.code} {ex.reason} uploading "
                    f"{masked_url} (client error, not retrying)"
                ) from ex
            # 5xx errors can be retried
            if attempt == max_retries:
                raise RuntimeError(
                    f"HTTP {ex.code} {ex.reason} uploading "
                    f"{masked_url} after {max_retries} attempts"
                ) from ex
            backoff = 2**attempt
            print(
                f"[WARN] HTTP {ex.code} uploading {masked_url}. Retrying "
                f"in {backoff}s (attempt {attempt}/{max_retries})..."
            )
            time.sleep(backoff)
        except urllib.error.URLError as ex:
            if attempt == max_retries:
                raise RuntimeError(
                    f"Network error uploading {masked_url} "
                    f"after {max_retries} attempts: {ex.reason}"
                ) from ex
            backoff = 2**attempt
            print(
                f"[WARN] Network error uploading {masked_url}: {ex.reason}. "
                f"Retrying in {backoff}s (attempt {attempt}/{max_retries})..."
            )
            time.sleep(backoff)

    raise RuntimeError(f"Failed to upload {masked_url}")


def verify_uploaded_digest(
    check_url: str,
    expected_sha256: str,
    timeout: int = 15,
    key_id: str | None = None,
    key_secret: str | None = None,
) -> bool:
    """Perform HEAD request and compare RFC 3230 digest with expected sha256."""
    if not key_id or not key_secret:
        key_id, key_secret = get_credentials()
    headers = {}
    if key_id and key_secret:
        auth_str = f"{key_id}:{key_secret}"
        headers["Authorization"] = "Basic " + base64.b64encode(auth_str.encode("utf-8")).decode(
            "ascii"
        )
    req = urllib.request.Request(check_url, headers=headers, method="HEAD")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            headers = dict(resp.headers.items())
            remote_digest = parse_rfc3230_digest(headers)
            if not remote_digest:
                # If digest header not present, warn and fallback to checking ETag if hex-like
                etag = headers.get("ETag", "").strip('"')
                if len(etag) == 64 and re.fullmatch(r"[0-9a-fA-F]+", etag):
                    remote_digest = etag.lower()

            if not remote_digest:
                print(
                    f"[WARN] Registry did not return 'digest' header "
                    f"for {check_url}. HEAD HTTP {resp.status} verified."
                )
                return True

            if remote_digest == expected_sha256.lower():
                print(f"[VERIFY] Digest match for {check_url}: {remote_digest}")
                return True
            print(
                f"[ERROR] Digest mismatch for {check_url}: "
                f"remote={remote_digest} != local={expected_sha256.lower()}"
            )
            return False
    except Exception as ex:
        print(f"[ERROR] Failed HEAD verification for {check_url}: {ex}")
        return False


def verify_downloaded_artifacts(
    version: str,
    sha256_map: dict[str, str],
    size_map: dict[str, int],
    registry_base: str = DEFAULT_REGISTRY,
    timeout: int = 120,
    key_id: str | None = None,
    key_secret: str | None = None,
) -> bool:
    """Perform HTTPS GET for each artifact in UPLOAD_ORDER, compute sha256 and verify matching."""
    registry_base = registry_base.rstrip("/")
    if not key_id or not key_secret:
        key_id, key_secret = get_credentials()
    base_headers = {"User-Agent": "publish_l4tools-verify/1.0"}
    if key_id and key_secret:
        auth_str = f"{key_id}:{key_secret}"
        base_headers["Authorization"] = "Basic " + base64.b64encode(
            auth_str.encode("utf-8")
        ).decode("ascii")

    for filename in artifact_upload_order(sha256_map):
        url = f"{registry_base}/l4tools/{version}/{filename}"
        expected_sha = sha256_map.get(filename, "").lower()
        expected_size = size_map.get(filename)
        req = urllib.request.Request(url, headers=base_headers)
        hasher = hashlib.sha256()
        total_bytes = 0
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                if resp.status != 200:
                    print(f"[ERROR] GET {url} returned HTTP {resp.status}")
                    return False
                while True:
                    chunk = resp.read(65536)
                    if not chunk:
                        break
                    hasher.update(chunk)
                    total_bytes += len(chunk)
        except Exception as ex:
            print(f"[ERROR] Failed HTTPS GET {url}: {ex}")
            return False

        computed_sha = hasher.hexdigest().lower()
        if expected_size is not None and total_bytes != expected_size:
            print(
                f"[ERROR] Size mismatch for {url}: downloaded "
                f"{total_bytes} != expected {expected_size}"
            )
            return False

        if computed_sha != expected_sha:
            print(
                f"[ERROR] SHA-256 mismatch for {url}: downloaded "
                f"{computed_sha} != expected {expected_sha}"
            )
            return False

        print(f"[VERIFY-GET] OK: {filename} ({total_bytes} bytes, sha256={computed_sha})")

    return True


def record_release(
    version: str,
    manifest: dict,
    sha256_map: dict[str, str],
    size_map: dict[str, int],
    registry_base: str = DEFAULT_REGISTRY,
    record_dir: Path = DEFAULT_RECORD_DIR,
    releases_file: Path | None = DEFAULT_RELEASES_FILE,
) -> Path:
    """Record publication metadata in artifacts/l4tools/<version>.json and releases.jsonl."""
    record_dir.mkdir(parents=True, exist_ok=True)
    record_path = record_dir / f"{version}.json"

    published_at = datetime.now(UTC).isoformat()
    record_data = {
        "schema": 1,
        "component": "l4tools",
        "version": version,
        "git_sha": manifest.get("git_sha", ""),
        "published_at": published_at,
        "publisher": "publish_l4tools-cli",
        "registry_url": registry_base,
        "urls": {
            f: f"{registry_base.rstrip('/')}/l4tools/{version}/{f}"
            for f in artifact_upload_order(sha256_map)
        },
        "sha256": sha256_map,
        "size": size_map,
        "registry_digest_verified": True,
        "signed": manifest.get("signed", False),
        "dirty": manifest.get("dirty", False),
    }

    record_path.write_text(json.dumps(record_data, indent=2), encoding="utf-8")
    print(f"[RECORD] Saved publication record: {record_path}")

    # Append to releases.jsonl if specified or exists
    if releases_file:
        try:
            releases_file.parent.mkdir(parents=True, exist_ok=True)
            already_recorded = False
            if releases_file.is_file():
                for line in releases_file.read_text(encoding="utf-8").splitlines():
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        entry = json.loads(line)
                        if entry.get("component") == "l4tools" and entry.get("version") == version:
                            already_recorded = True
                            break
                    except Exception:
                        pass
            if already_recorded:
                print(
                    f"[RECORD] Entry for {version} already exists "
                    f"in {releases_file}; skipping duplicate append."
                )
            else:
                journal_entry = {
                    "component": "l4tools",
                    "version": version,
                    "revision": manifest.get("git_sha", ""),
                    "flow_revision": manifest.get("git_sha", ""),
                    "tag": f"tools/v{version}",
                    "digest": sha256_map.get("l4setup.exe", ""),
                    "built_at": manifest.get("built_at", ""),
                    "published_at": published_at,
                }
                with releases_file.open("a", encoding="utf-8") as f:
                    f.write(json.dumps(journal_entry) + "\n")
                print(f"[RECORD] Appended release to {releases_file}")
        except Exception as ex:
            print(f"[WARN] Failed to write to {releases_file}: {ex}", file=sys.stderr)

    return record_path


def publish_release(
    artifacts_dir: Path,
    dry_run: bool = False,
    allow_dirty: bool = False,
    skip_check: bool = False,
    registry_base: str = DEFAULT_REGISTRY,
    record_dir: Path = DEFAULT_RECORD_DIR,
    releases_file: Path | None = DEFAULT_RELEASES_FILE,
    env_file: Path | None = None,
    expected_version: str | None = None,
) -> int:
    """Execute complete release publication flow.

    Returns:
        0: success
        1: error (validation, network, auth)
        2: already published (check detected existing version)
        3: digest mismatch (remote verification failed)
    """
    # 1. Verify
    print(f"=== 1. Verifying artifacts in {artifacts_dir} ===")
    try:
        env_data = load_env_file(env_file)
        public = configured_metadata_public(env_data, env_file)
        manifest, sha256_map, size_map = verify_artifacts(
            artifacts_dir, allow_dirty=allow_dirty, trusted_public=public
        )
        if manifest.get("layout_payloads") is not None and not manifest.get("metadata_signatures"):
            raise ValueError("New layout publication requires signed metadata")
    except Exception as ex:
        print(f"[ERROR] Verification failed: {ex}")
        return 1

    version = manifest["version"]
    if expected_version and version != expected_version:
        print(
            (
                f"[ERROR] Version in manifest ({version}) does "
                f"not match expected version ({expected_version})."
            ),
            file=sys.stderr,
        )
        return 1
    print(f"[OK] Artifacts verified for version {version}:")
    for f in artifact_upload_order(sha256_map):
        print(f"  - {f}: {size_map[f]} bytes, sha256={sha256_map[f]}")

    # 2. Check
    if not skip_check:
        print(f"\n=== 2. Checking remote registry for version {version} ===")
        check_code = check_remote_version(version, registry_base=registry_base)
        if check_code == 2:
            print(
                f"[CHECK] Version {version} already exists in registry. "
                f"Verifying if all published artifacts are identical..."
            )
            if verify_downloaded_artifacts(
                version=version,
                sha256_map=sha256_map,
                size_map=size_map,
                registry_base=registry_base,
            ):
                print(
                    f"[INFO] Version {version} is already "
                    f"published with identical SHA-256 checksums."
                )
                print(
                    "[INFO] Idempotent publication confirmed; "
                    "skipping upload, recording audit if missing."
                )
                if not dry_run:
                    record_release(
                        version=version,
                        manifest=manifest,
                        sha256_map=sha256_map,
                        size_map=size_map,
                        registry_base=registry_base,
                        record_dir=record_dir,
                        releases_file=releases_file,
                    )
                return 0
            else:
                print(
                    f"[HALT] Version {version} already exists in registry with "
                    f"differing artifacts or partial publication. Overwrite is prohibited."
                )
                return 2
        if check_code != 0:
            print(f"[ERROR] Remote check failed (code {check_code}).")
            return 1

    # 3. Credentials
    key_id, key_secret = get_credentials(env_data)

    if dry_run:
        print("\n=== [DRY-RUN] Simulating publication ===")
        key_id = key_id or "DRY_RUN_KEY_ID"
        key_secret = key_secret or "DRY_RUN_KEY_SECRET"
    else:
        if not key_id or not key_secret:
            print(
                (
                    "[ERROR] Registry credentials missing! Set AR_GENERIC_KEY_ID "
                    "and AR_GENERIC_KEY_SECRET in env or .env file."
                ),
                file=sys.stderr,
            )
            return 1

    # 4. Upload & Verify Digest
    print(f"\n=== 3. Uploading artifacts ({'DRY-RUN' if dry_run else 'LIVE'}) ===")
    registry_base = registry_base.rstrip("/")

    for filename in artifact_upload_order(sha256_map):
        filepath = artifacts_dir / filename
        upload_url = f"{registry_base}/upload/l4tools/{version}/"
        check_url = f"{registry_base}/l4tools/{version}/{filename}"
        expected_sha = sha256_map[filename]

        if dry_run:
            masked_url = mask_secret(f"{upload_url}{filename}", key_secret, key_id)
            print(f"[DRY-RUN] PUT {masked_url} ({size_map[filename]} bytes)")
            print(f"[DRY-RUN] HEAD {check_url} -> verify digest == {expected_sha}")
            continue

        file_bytes = filepath.read_bytes()
        try:
            upload_file_with_retry(
                upload_url,
                file_bytes,
                key_id,
                key_secret,
                filename=filename,
            )
        except Exception as ex:
            print(f"[ERROR] {ex}")
            return 1

        # Immediately verify digest
        if not verify_uploaded_digest(check_url, expected_sha):
            print(
                f"[ERROR] Partial upload detected for version "
                f"{version}! File {filename} digest mismatch."
            )
            return 3

    # 5. Full HTTPS GET byte verification
    print(
        f"\n=== 4. Verifying downloaded bytes via HTTPS "
        f"GET ({'DRY-RUN' if dry_run else 'LIVE'}) ==="
    )
    if dry_run:
        for filename in artifact_upload_order(sha256_map):
            print(
                f"[DRY-RUN] GET {registry_base}/l4tools/{version}/{filename} "
                f"-> compute sha256 == {sha256_map[filename]}"
            )
    else:
        if not verify_downloaded_artifacts(
            version=version,
            sha256_map=sha256_map,
            size_map=size_map,
            registry_base=registry_base,
        ):
            print(
                f"[ERROR] HTTPS GET byte verification failed for version "
                f"{version}! Publication incomplete; aborting record."
            )
            return 3

    # 6. Record
    print("\n=== 5. Recording publication metadata ===")
    if dry_run:
        print(f"[DRY-RUN] Would write record to {record_dir}/{version}.json")
        if releases_file:
            print(f"[DRY-RUN] Would append entry to {releases_file}")
    else:
        record_release(
            version=version,
            manifest=manifest,
            sha256_map=sha256_map,
            size_map=size_map,
            registry_base=registry_base,
            record_dir=record_dir,
            releases_file=releases_file,
        )

    print(
        f"\n[SUCCESS] Version {version} published "
        f"successfully to {registry_base}/l4tools/{version}/"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Local publishing tool for l4tools releases to Generic Artifact Registry"
    )
    parser.add_argument("--registry", default=DEFAULT_REGISTRY, help="Generic registry base URL")
    parser.add_argument(
        "--env-file", type=Path, default=None, help="Path to .env file with credentials"
    )
    parser.add_argument(
        "--dist", type=Path, default=None, help="Artifacts directory (e.g. tools/dist)"
    )
    parser.add_argument("--version", default=None, help="Release version (e.g. 1.7.2)")
    parser.add_argument(
        "--dry-run", action="store_true", help="Simulate upload without network changes"
    )
    parser.add_argument(
        "--allow-dirty",
        action="store_true",
        help="Allow release built from dirty git tree",
    )
    parser.add_argument("--skip-check", action="store_true", help="Skip remote pre-check")
    parser.add_argument(
        "--record-dir",
        type=Path,
        default=DEFAULT_RECORD_DIR,
        help="Directory for release JSON",
    )
    parser.add_argument(
        "--releases-file",
        type=Path,
        default=DEFAULT_RELEASES_FILE,
        help="Path to releases.jsonl",
    )

    subparsers = parser.add_subparsers(dest="command", required=False)

    # verify
    sub_verify = subparsers.add_parser("verify", help="Verify release artifacts directory")
    sub_verify.add_argument("dir", type=Path, help="Artifacts directory (e.g. tools/dist)")
    sub_verify.add_argument(
        "--allow-dirty",
        action="store_true",
        help="Allow release built from dirty git tree",
    )

    # check
    sub_check = subparsers.add_parser(
        "check", help="Check if version is already published in registry"
    )
    sub_check.add_argument("version", help="Release version (e.g. 1.6.0)")

    # publish
    sub_publish = subparsers.add_parser("publish", help="Publish release artifacts to registry")
    sub_publish.add_argument("dir", type=Path, help="Artifacts directory (e.g. tools/dist)")
    sub_publish.add_argument(
        "--dry-run", action="store_true", help="Simulate upload without network changes"
    )
    sub_publish.add_argument(
        "--allow-dirty",
        action="store_true",
        help="Allow release built from dirty git tree",
    )
    sub_publish.add_argument("--skip-check", action="store_true", help="Skip remote pre-check")
    sub_publish.add_argument(
        "--record-dir",
        type=Path,
        default=DEFAULT_RECORD_DIR,
        help="Directory for release JSON",
    )
    sub_publish.add_argument(
        "--releases-file",
        type=Path,
        default=DEFAULT_RELEASES_FILE,
        help="Path to releases.jsonl",
    )

    # record
    sub_record = subparsers.add_parser("record", help="Record release metadata for local artifacts")
    sub_record.add_argument("dir", type=Path, help="Artifacts directory (e.g. tools/dist)")
    sub_record.add_argument(
        "--allow-dirty",
        action="store_true",
        help="Allow release built from dirty git tree",
    )
    sub_record.add_argument(
        "--record-dir",
        type=Path,
        default=DEFAULT_RECORD_DIR,
        help="Directory for release JSON",
    )
    sub_record.add_argument(
        "--releases-file",
        type=Path,
        default=DEFAULT_RELEASES_FILE,
        help="Path to releases.jsonl",
    )

    args = parser.parse_args()

    if args.command is None:
        if args.dist:
            args.command = "publish"
            args.dir = args.dist
            if not getattr(args, "allow_dirty", False):
                args.allow_dirty = True
        else:
            parser.print_help()
            return 1

    if args.command == "verify":
        try:
            manifest, sha256_map, size_map = verify_artifacts(
                args.dir,
                allow_dirty=args.allow_dirty,
                trusted_public=configured_metadata_public(
                    load_env_file(args.env_file), args.env_file
                ),
            )
            print(f"[OK] Artifacts verified for version {manifest['version']}:")
            for f in artifact_upload_order(sha256_map):
                print(f"  {f}: {size_map[f]} bytes, sha256={sha256_map[f]}")
            return 0
        except Exception as ex:
            print(f"[ERROR] {ex}", file=sys.stderr)
            return 1

    if args.command == "check":
        return check_remote_version(args.version, registry_base=args.registry)

    if args.command == "publish":
        return publish_release(
            artifacts_dir=args.dir,
            dry_run=args.dry_run,
            allow_dirty=args.allow_dirty,
            skip_check=args.skip_check,
            registry_base=args.registry,
            record_dir=args.record_dir,
            releases_file=args.releases_file,
            env_file=args.env_file,
            expected_version=getattr(args, "version", None),
        )

    if args.command == "record":
        try:
            manifest, sha256_map, size_map = verify_artifacts(
                args.dir,
                allow_dirty=args.allow_dirty,
                trusted_public=configured_metadata_public(
                    load_env_file(args.env_file), args.env_file
                ),
            )
            record_release(
                version=manifest["version"],
                manifest=manifest,
                sha256_map=sha256_map,
                size_map=size_map,
                registry_base=args.registry,
                record_dir=args.record_dir,
                releases_file=args.releases_file,
            )
            return 0
        except Exception as ex:
            print(f"[ERROR] {ex}", file=sys.stderr)
            return 1

    return 1


if __name__ == "__main__":
    sys.exit(main())
