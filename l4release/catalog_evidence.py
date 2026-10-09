"""Owner-attested pipeline acceptance: one transition and one forced rollback."""

from __future__ import annotations

import json
import re
import uuid
from dataclasses import dataclass

from cryptography.hazmat.primitives.asymmetric import rsa

from .common import ReleaseError, version_value
from .metadata import key_id, verify_bytes

SERVICES = ("Leo4Proxy", "mosquitto", "L4Con", "L4Superv")
FILETIME_EPOCH = 116444736000000000


def fields(value: object, expected: set[str]) -> dict:
    if not isinstance(value, dict) or set(value) != expected:
        raise ReleaseError("Pipeline compatibility fields do not match schema1")
    return value


def integer(value: object, maximum: int = 2**64 - 1) -> int:
    if type(value) is not int or not 0 < value <= maximum:
        raise ReleaseError("Invalid pipeline compatibility integer")
    return value


def sha(value: object) -> str:
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value) or value == "0" * 64:
        raise ReleaseError("Invalid pipeline compatibility digest")
    return value


def unique_json(data: bytes) -> dict:
    def unique(pairs):
        value = {}
        for name, item in pairs:
            if name in value:
                raise ReleaseError("Duplicate pipeline compatibility JSON field")
            value[name] = item
        return value

    value = json.loads(data, object_pairs_hook=unique)
    if not isinstance(value, dict):
        raise ReleaseError("Compatibility document must be an object")
    return value


def canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode("ascii")


@dataclass(frozen=True)
class Endpoint:
    version: str
    root_sha256: str
    inventory_sha256: str
    services: dict[str, dict[str, int | str]]
    producer: dict[str, int | str]

    def identity(self) -> dict:
        return {
            "version": self.version,
            "root_sha256": self.root_sha256,
            "inventory_sha256": self.inventory_sha256,
        }


def _uuid(value: object, seen: set[str]) -> str:
    if not isinstance(value, str):
        raise ReleaseError("Pipeline identity must be a UUID")
    try:
        parsed = uuid.UUID(value)
    except ValueError as error:
        raise ReleaseError("Invalid pipeline UUID") from error
    if not parsed.int or str(parsed) != value or value in seen:
        raise ReleaseError("Duplicate/noncanonical pipeline UUID")
    seen.add(value)
    return value


def profile(value: object) -> str:
    value = fields(value, {"major", "minor", "build", "native_arch", "product_type"})
    major, minor, build = value["major"], value["minor"], value["build"]
    if any(type(n) is not int or not 0 <= n <= 2**32 - 1 for n in (major, minor, build)):
        raise ReleaseError("Invalid native platform facts")
    supported = (major == 6 and 1 <= minor <= 3 and build >= 7601) or (
        major == 10 and minor == 0 and build >= 10240
    )
    if (
        not supported
        or not isinstance(value["native_arch"], str)
        or value["native_arch"] not in {"x86", "x64"}
        or type(value["product_type"]) is not int
        or value["product_type"] != 1
    ):
        raise ReleaseError("Unsupported native platform observation")
    return f"windows-nt-{major}.{minor}.{build}-{value['native_arch']}-client"


def _run(
    value: object,
    installed: Endpoint,
    *,
    restored: bool,
    seen: set[str],
    earliest: int,
    latest: int,
) -> None:
    run = fields(
        value,
        {
            "operation_id",
            "plan_sha256",
            "proof_kind",
            "proof_sha256",
            "outcome_sha256",
            "result",
            "error",
            "installed",
            "finished_utc",
        },
    )
    _uuid(run["operation_id"], seen)
    for name in ("plan_sha256", "proof_sha256", "outcome_sha256"):
        sha(run[name])
    expected_kind, expected_result = (108, "RESTORED") if restored else (102, "SUCCESS")
    if (
        type(run["proof_kind"]) is not int
        or run["proof_kind"] != expected_kind
        or run["result"] != expected_result
        or run["installed"] != installed.identity()
    ):
        raise ReleaseError("Pipeline final typed outcome/proof/installed identity differs")
    error = run["error"]
    if type(error) is not int or not 0 <= error <= 2**32 - 1 or bool(error) != restored:
        raise ReleaseError("Pipeline forward/forced-rollback error differs")
    if not earliest <= integer(run["finished_utc"]) <= latest:
        raise ReleaseError("Pipeline outcome timestamp is outside this acceptance run")


def validate_acceptance(
    data: bytes,
    signature: bytes,
    public: rsa.RSAPublicKey,
    source: Endpoint,
    target: Endpoint,
    *,
    now: int,
) -> dict:
    """Validate sealed pipeline attestation, never accept/seal caller PASS.

    Native producer must verify the protected existing outcome proof authority,
    actual clear, shared backward-compatibility check and observed intermediate
    states from the two executor runs before sealing. Python summaries/hashes
    cannot independently mint that authority.
    """
    verify_bytes(data, signature, public)
    report = fields(
        unique_json(data),
        {
            "schema",
            "kind",
            "key_id",
            "run_id",
            "terminal_id",
            "tenant_id",
            "arch",
            "platform",
            "from",
            "to",
            "producer",
            "started_at",
            "finished_at",
            "backward_compatibility",
            "forward",
            "forced_rollback",
        },
    )
    if (
        type(report["schema"]) is not int
        or report["schema"] != 1
        or report["kind"] != "l4tools-pipeline-compatibility"
    ):
        raise ReleaseError("Unsupported pipeline compatibility acceptance")
    # User-approved owner-declared stand scope; never infer tenant from cert O.
    if (
        report["key_id"] != key_id(public)
        or type(report["terminal_id"]) is not int
        or report["terminal_id"] != 773
        or type(report["tenant_id"]) is not int
        or report["tenant_id"] != 1
    ):
        raise ReleaseError("Pipeline compatibility owner/stand differs")
    native_profile = profile(report["platform"])
    arch = report["arch"]
    if (
        not isinstance(arch, str)
        or arch not in {"x86", "x64"}
        or (arch == "x64" and report["platform"]["native_arch"] != "x64")
    ):
        raise ReleaseError("Pipeline suite/native architecture differs")
    if (
        report["from"] != source.identity()
        or report["to"] != target.identity()
        or source.version == target.version
    ):
        raise ReleaseError("Pipeline signed endpoint/inventory differs")
    if any(version_value(v) != v or "-" in v for v in (source.version, target.version)):
        raise ReleaseError("Native acceptance requires exact three-part release versions")
    producer = fields(report["producer"], {"name", "schema"})
    if (
        producer["name"] != "l4release"
        or type(producer["schema"]) is not int
        or producer["schema"] != 1
    ):
        raise ReleaseError("Pipeline acceptance producer differs")
    started, finished = (
        integer(report[name], 253402300799) for name in ("started_at", "finished_at")
    )
    if (
        not started <= finished <= now
        or finished - started > 24 * 3600
        or now - finished > 14 * 86400
    ):
        raise ReleaseError("Pipeline acceptance is stale/future or exceeds one-day run bound")
    check = fields(report["backward_compatibility"], {"contract", "report_sha256"})
    if check["contract"] != "communication-backwards-v1":
        raise ReleaseError("Required pipeline communication backward-compatibility check differs")
    sha(check["report_sha256"])
    seen: set[str] = set()
    _uuid(report["run_id"], seen)
    bounds = {
        "seen": seen,
        "earliest": FILETIME_EPOCH + started * 10_000_000,
        "latest": FILETIME_EPOCH + (finished + 1) * 10_000_000,
    }
    _run(report["forward"], target, restored=False, **bounds)
    _run(report["forced_rollback"], source, restored=True, **bounds)
    # Native record64 describes the action plan, not the operation UUID. The
    # same signed endpoints can legitimately produce identical plan bytes.
    # _run above requires distinct operation UUIDs; terminal proof/outcome
    # records remain separate evidence for these two attempts.
    for name in ("proof_sha256", "outcome_sha256"):
        if report["forward"][name] == report["forced_rollback"][name]:
            raise ReleaseError("Two executor attempts must retain distinct terminal evidence")
    return {"from": source.version, "to": target.version, "arch": arch, "profile": native_profile}
