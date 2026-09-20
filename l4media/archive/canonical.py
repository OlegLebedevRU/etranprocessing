"""Canonical data structures, serialization, and JSON schema validators for l4media archives."""

from __future__ import annotations

import gzip
import hashlib
import json
import re
import uuid
from dataclasses import asdict, dataclass, field
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

MANIFEST_VERSION = "1.0.0"
SCHEMA_VERSION = "1.0.0"
OWNER_PROJECT = "l4media"
RECORD_TYPE_SAMPLES = "media_stream_samples"
RECORD_TYPE_QUALITY = "media_quality_events"

BATCH_ID_PATTERN = re.compile(r"^arch-[a-z0-9_-]+$")
SOURCE_MONTH_PATTERN = re.compile(r"^[0-9]{4}-(0[1-9]|1[0-2])$")
SHA256_HEX_PATTERN = re.compile(r"^[0-9a-f]{64}$")
STORAGE_PATH_PATTERN = re.compile(r"^[0-9]{4}/(0[1-9]|1[0-2])/[a-zA-Z0-9_-]+/[a-z0-9_-]+$")


def now_utc_iso() -> str:
    """Return current UTC timestamp in ISO 8601 format with Z suffix."""
    return datetime.now(UTC).strftime("%Y-%m-%dT%H:%M:%SZ")


def canonical_json_dumps(data: Any) -> str:
    """Serialize data into deterministic compact JSON string."""
    return json.dumps(data, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def compute_file_sha256(file_path: Path | str) -> str:
    """Compute hex SHA-256 digest of a file in streaming chunks."""
    h = hashlib.sha256()
    with open(file_path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest().lower()


def generate_batch_id(source_month: str, seed: str | None = None) -> str:
    """Generate a valid archive_batch_id conforming to ^arch-[a-z0-9_-]+$."""
    if not SOURCE_MONTH_PATTERN.match(source_month):
        raise ValueError(f"Invalid source_month format: {source_month}")
    suffix = (seed or uuid.uuid4().hex)[:8].lower()
    return f"arch-media-{source_month}-{suffix}"


@dataclass
class FileEntry:
    path: str
    size_bytes: int
    sha256: str
    record_count: int
    compression: str = "gzip"

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class VerificationResult:
    verified_at_utc: str
    verifier: str = "l4media-archive-worker"
    reread_records_count: int = 0
    reread_checksum_sha256: str = ""
    restore_sample_status: str = "passed"
    cursor_guard_passed: bool = True

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class PurgeResult:
    purged_at_utc: str | None
    purged_records_count: int
    purge_status: str = "completed"  # "completed" | "pending" | "failed" | "skipped"

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class ArchiveError:
    code: str
    message: str
    occurred_at_utc: str

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class RetentionPolicy:
    retain_until_utc: str
    retention_years: int = 3
    backup_required: bool = True

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class StorageLayout:
    volume_root: str
    relative_path: str

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class RecordEnvelope:
    record_type: str
    record_id: str | int
    occurred_at_utc: str
    source_project: str = OWNER_PROJECT
    cursor: int | None = None
    tenant_id: int | None = None
    terminal_id: int | None = None
    sn: str | None = None
    session_id: str | None = None
    payload: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        d: dict[str, Any] = {
            "record_type": self.record_type,
            "record_id": self.record_id,
            "occurred_at_utc": self.occurred_at_utc,
            "source_project": self.source_project,
            "cursor": self.cursor,
            "tenant_id": self.tenant_id,
            "terminal_id": self.terminal_id,
            "sn": self.sn,
            "session_id": self.session_id,
            "payload": self.payload,
        }
        return d


@dataclass
class ArchiveManifest:
    archive_manifest_version: str = MANIFEST_VERSION
    archive_batch_id: str = ""
    owner_project: str = OWNER_PROJECT
    schema_version: str = SCHEMA_VERSION
    created_at_utc: str = ""
    source_month: str = ""
    time_range: dict[str, str] = field(default_factory=dict)
    cursor_bounds: dict[str, Any] | None = None
    record_types: list[str] = field(default_factory=list)
    record_counts: dict[str, int] = field(default_factory=dict)
    files: list[FileEntry] = field(default_factory=list)
    compression: str = "gzip"
    state: str = "prepared"  # "prepared" | "verified" | "purged" | "failed"
    verification: VerificationResult | None = None
    purge: PurgeResult | None = None
    error: ArchiveError | None = None
    retention: RetentionPolicy = field(default_factory=lambda: RetentionPolicy(retain_until_utc=""))
    storage_layout: StorageLayout = field(default_factory=lambda: StorageLayout(volume_root="", relative_path=""))

    def to_dict(self) -> dict[str, Any]:
        d: dict[str, Any] = {
            "archive_manifest_version": self.archive_manifest_version,
            "archive_batch_id": self.archive_batch_id,
            "owner_project": self.owner_project,
            "schema_version": self.schema_version,
            "created_at_utc": self.created_at_utc,
            "source_month": self.source_month,
            "time_range": self.time_range,
            "cursor_bounds": self.cursor_bounds,
            "record_types": self.record_types,
            "record_counts": self.record_counts,
            "files": [f.to_dict() for f in self.files],
            "compression": self.compression,
            "state": self.state,
            "verification": self.verification.to_dict() if self.verification else None,
            "purge": self.purge.to_dict() if self.purge else None,
            "error": self.error.to_dict() if self.error else None,
            "retention": self.retention.to_dict(),
            "storage_layout": self.storage_layout.to_dict(),
        }
        return d


def write_deterministic_jsonl_gz(file_path: Path | str, records: list[dict[str, Any]]) -> tuple[int, int, str]:
    """Write list of records as deterministic gzip JSONL file (mtime=0.0).

    Returns:
        tuple of (record_count, file_size_bytes, sha256_hex)
    """
    path = Path(file_path)
    path.parent.mkdir(parents=True, exist_ok=True)

    with open(path, "wb") as raw_f:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw_f, mtime=0.0) as gz_f:
            for rec in records:
                line = canonical_json_dumps(rec) + "\n"
                gz_f.write(line.encode("utf-8"))
        raw_f.flush()

    size_bytes = path.stat().st_size
    sha256 = compute_file_sha256(path)
    return len(records), size_bytes, sha256


def validate_manifest_schema(manifest_dict: dict[str, Any], schema_path: Path | None = None) -> list[str]:
    """Validate manifest dictionary against JSON Schema Draft 2020-12 if available,

    and against strict internal rules.
    """
    errors: list[str] = []

    # 1. Internal validation rules
    required_top = [
        "archive_manifest_version",
        "archive_batch_id",
        "owner_project",
        "schema_version",
        "created_at_utc",
        "source_month",
        "time_range",
        "cursor_bounds",
        "record_types",
        "record_counts",
        "files",
        "compression",
        "state",
        "retention",
        "storage_layout",
    ]
    for key in required_top:
        if key not in manifest_dict:
            errors.append(f"Missing required property: '{key}'")

    if manifest_dict.get("archive_manifest_version") != "1.0.0":
        errors.append(
            f"archive_manifest_version must be '1.0.0', got '{manifest_dict.get('archive_manifest_version')}'"
        )

    if manifest_dict.get("owner_project") != "l4media":
        errors.append(f"owner_project must be 'l4media', got '{manifest_dict.get('owner_project')}'")

    batch_id = manifest_dict.get("archive_batch_id", "")
    if not BATCH_ID_PATTERN.match(str(batch_id)):
        errors.append(f"archive_batch_id '{batch_id}' does not match pattern")

    source_month = manifest_dict.get("source_month", "")
    if not SOURCE_MONTH_PATTERN.match(str(source_month)):
        errors.append(f"source_month '{source_month}' does not match pattern YYYY-MM")

    state = manifest_dict.get("state")
    if state not in ("prepared", "verified", "purged", "failed"):
        errors.append(f"Invalid state: '{state}'")

    files = manifest_dict.get("files", [])
    if not isinstance(files, list) or len(files) == 0:
        errors.append("files must be a non-empty array")
    else:
        for idx, f in enumerate(files):
            for f_prop in ["path", "size_bytes", "sha256", "record_count", "compression"]:
                if f_prop not in f:
                    errors.append(f"files[{idx}] missing '{f_prop}'")
            sha = f.get("sha256", "")
            if not SHA256_HEX_PATTERN.match(str(sha)):
                errors.append(f"files[{idx}].sha256 '{sha}' is not a valid 64-char hex string")

    # State specific requirements
    if state == "verified":
        v = manifest_dict.get("verification")
        if not v:
            errors.append("state 'verified' requires non-null verification block")
        elif (
            not v.get("verified_at_utc")
            or v.get("restore_sample_status") != "passed"
            or not v.get("cursor_guard_passed")
        ):
            errors.append("state 'verified' verification block invalid")
    elif state == "purged":
        v = manifest_dict.get("verification")
        p = manifest_dict.get("purge")
        if not v or not p:
            errors.append("state 'purged' requires verification and purge blocks")
        elif p.get("purge_status") != "completed" or not p.get("purged_at_utc"):
            errors.append("state 'purged' purge block invalid")
    elif state == "failed":
        err = manifest_dict.get("error")
        if not err or not err.get("code") or not err.get("message") or not err.get("occurred_at_utc"):
            errors.append("state 'failed' requires valid error block")

    retention = manifest_dict.get("retention", {})
    if retention.get("retention_years", 0) < 3:
        errors.append("retention.retention_years must be >= 3")
    if not retention.get("backup_required"):
        errors.append("retention.backup_required must be true")

    storage = manifest_dict.get("storage_layout", {})
    rel_path = storage.get("relative_path", "")
    if not STORAGE_PATH_PATTERN.match(str(rel_path)):
        errors.append(f"storage_layout.relative_path '{rel_path}' does not match pattern")

    # 2. JSON Schema Draft 2020-12 validation if jsonschema is available
    if schema_path and schema_path.is_file():
        try:
            from jsonschema import Draft202012Validator, FormatChecker

            with open(schema_path, encoding="utf-8") as sf:
                schema_json = json.load(sf)
            validator = Draft202012Validator(schema_json, format_checker=FormatChecker())
            for schema_err in validator.iter_errors(manifest_dict):
                errors.append(f"JSONSchema: {schema_err.message} at {list(schema_err.path)}")
        except ImportError:
            pass

    return errors


def validate_envelope_dict(record_dict: dict[str, Any], schemas_bundle_path: Path | None = None) -> list[str]:
    """Validate RecordEnvelope dictionary against schemas.json and strict internal rules."""
    errors: list[str] = []
    required_fields = ["record_type", "record_id", "occurred_at_utc", "source_project", "payload"]
    for field_name in required_fields:
        if field_name not in record_dict:
            errors.append(f"Missing required envelope field: '{field_name}'")

    if record_dict.get("source_project") not in ("iot-rpc-rest-app", "l4media", "MenuBuilder"):
        errors.append(f"Invalid source_project: '{record_dict.get('source_project')}'")

    if not isinstance(record_dict.get("payload"), dict):
        errors.append("payload must be an object (dict)")

    if schemas_bundle_path and schemas_bundle_path.is_file():
        try:
            from jsonschema import Draft202012Validator, FormatChecker

            with open(schemas_bundle_path, encoding="utf-8") as bf:
                bundle = json.load(bf)
            envelope_schema = bundle.get("$defs", {}).get("RecordEnvelope")
            if envelope_schema:
                val = Draft202012Validator(envelope_schema, format_checker=FormatChecker())
                for err in val.iter_errors(record_dict):
                    errors.append(f"JSONSchema Envelope: {err.message} at {list(err.path)}")
        except ImportError:
            pass

    return errors
