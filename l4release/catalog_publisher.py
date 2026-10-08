"""Reviewed catalog plans and one build-script writer; no distributed CAS claim."""

from __future__ import annotations

import tempfile
import time
from pathlib import Path

from .acceptance_trial import reserved_revision
from .catalog import catalog_bytes, validate_catalog
from .catalog_artifacts import published_release
from .catalog_evidence import canonical, fields, integer, sha, unique_json, validate_acceptance
from .catalog_registry import CatalogRegistry, digest
from .catalog_state import protect_state, state_directory
from .common import ReleaseError, atomic_bytes, atomic_json, version_value
from .metadata import key_id, load_public_key, load_signing_key, verify_bytes, verify_embedded_key
from .pipeline import git, input_snapshot, read_config, source_status
from .runner import workspace_lock

CURRENT = "l4tools/metadata/catalog.json"
TTL = 7 * 86400


def _file(path: Path, limit: int) -> bytes:
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    if not 0 < len(data) <= limit:
        raise ReleaseError("Catalog input file exceeds its fixed bound")
    return data


def _checkpoint(root: Path) -> dict:
    config = read_config(root)
    return {
        "git_sha": git(root, "rev-parse", "HEAD"),
        "clean": not bool(source_status(root, config)),
        "inputs_sha256": digest(canonical(input_snapshot(root, config))),
    }


class CatalogPublisher:
    def __init__(self, root: Path, env: dict[str, str], env_path: Path):
        self.root, self.env, self.env_path = root.resolve(), env, env_path.resolve()
        self.public = load_public_key(env, env_path)
        verify_embedded_key(root, self.public)
        self.registry = CatalogRegistry(root, env)
        self.state = state_directory(self.root, env, self.env_path)

    def _get(self, relative: str, limit: int, scratch: Path) -> bytes | None:
        path = scratch / f"get-{len(list(scratch.glob('get-*'))):04d}"
        if not self.registry.download(relative, path, limit):
            return None
        return _file(path, limit)

    def _floor(self) -> dict:
        initial = {
            "schema": 1,
            "key_id": key_id(self.public),
            "revision": 0,
            "sha256": None,
            "time": 0,
            "pending": None,
        }
        path = self.state / "floor.json"
        if not path.exists():
            return initial
        protect_state(path)
        value = fields(unique_json(_file(path, 65535)), set(initial))
        if (
            value["schema"] != 1
            or type(value["schema"]) is not int
            or value["key_id"] != key_id(self.public)
        ):
            raise ReleaseError("Publisher floor owner/schema differs")
        if (
            type(value["revision"]) is not int
            or not 0 <= value["revision"] < 2**64
            or type(value["time"]) is not int
            or not 0 <= value["time"] <= 253402300799
        ):
            raise ReleaseError("Publisher floor revision/time is invalid")
        if value["revision"]:
            sha(value["sha256"])
            integer(value["time"], 253402300799)
        elif value["sha256"] is not None or value["time"]:
            raise ReleaseError("Empty publisher floor differs")
        if value["pending"] is not None:
            pending = fields(value["pending"], {"revision", "sha256", "plan_sha256"})
            if integer(pending["revision"]) <= value["revision"]:
                raise ReleaseError("Pending publisher revision does not advance the floor")
            sha(pending["sha256"])
            sha(pending["plan_sha256"])
        return value

    def _catalog(self, pair: tuple[bytes | None, bytes | None], now: int) -> dict | None:
        data, signature = pair
        if data is None and signature is None:
            return None
        if data is None or signature is None:
            raise ReleaseError("Catalog fixed pair is incomplete; no new plan/publication")
        verify_bytes(data, signature, self.public)
        value = unique_json(data)
        issued = integer(value.get("issued_at"), 253402300799)
        if issued > now:
            raise ReleaseError("Publication base catalog is future-dated")
        # Expired catalog is authenticated publication ancestry ONLY. Never pass
        # this exception to native readers, routing or terminal admission.
        return validate_catalog(value, expected_key_id=key_id(self.public), now=issued)

    def _base(
        self, floor: dict, now: int, scratch: Path
    ) -> tuple[dict | None, tuple[bytes | None, bytes | None]]:
        if floor["pending"] is not None:
            raise ReleaseError(
                "An exact pending catalog plan must be resumed before creating another"
            )
        pair = (self._get(CURRENT, 65535, scratch), self._get(CURRENT + ".sig", 384, scratch))
        catalog = self._catalog(pair, now)
        if now < floor["time"]:
            raise ReleaseError("Publisher clock moved below the protected floor")
        if catalog is None:
            if floor["revision"]:
                raise ReleaseError("Catalog404 contradicts the protected publisher floor")
        else:
            if pair[0] is None:
                raise ReleaseError("Authenticated catalog bytes are absent")
            revision = catalog["revision"]
            if revision < floor["revision"] or (
                revision == floor["revision"] and digest(pair[0]) != floor["sha256"]
            ):
                raise ReleaseError("Public catalog is rolled back or changed at the same revision")
        return catalog, pair

    def _build(
        self,
        evidence: list[Path],
        base: dict | None,
        pair: tuple[bytes | None, bytes | None],
        issued: int,
        now: int,
        scratch: Path,
        *,
        promote: bool = False,
    ) -> dict:
        if not 1 <= len(evidence) <= 2:
            raise ReleaseError(
                "Supply one pipeline acceptance report per architecture, at most two"
            )
        endpoints = {}
        reports, transitions = [], []
        release_pair = None
        seen = set()
        for path in evidence:
            data, signature = _file(path, 65535), _file(Path(str(path) + ".sig"), 384)
            verify_bytes(data, signature, self.public)
            report = unique_json(data)
            source = fields(report.get("from"), {"version", "root_sha256", "inventory_sha256"})
            target = fields(report.get("to"), {"version", "root_sha256", "inventory_sha256"})
            if any(not isinstance(endpoint["version"], str) for endpoint in (source, target)):
                raise ReleaseError("Pipeline endpoint versions must be strings")
            versions = (version_value(source["version"]), version_value(target["version"]))
            if release_pair is not None and release_pair != versions:
                raise ReleaseError("One reviewed catalog plan admits exactly one release pair")
            release_pair = versions
            for version in versions:
                if version not in endpoints:
                    endpoints[version] = published_release(
                        self.root, self.registry, version, scratch / version, self.public
                    )
            arch = report.get("arch")
            if not isinstance(arch, str) or arch not in {"x86", "x64"}:
                raise ReleaseError("Pipeline architecture differs")
            transition = validate_acceptance(
                data,
                signature,
                self.public,
                endpoints[versions[0]][arch],
                endpoints[versions[1]][arch],
                now=now,
            )
            identity = (transition["arch"], transition["profile"])
            if identity in seen:
                raise ReleaseError("Duplicate architecture/profile compatibility report")
            seen.add(identity)
            checksum = digest(data)
            transitions.append({**transition, "evidence_sha256": checksum})
            reports.append(
                {
                    "path": str(path.resolve()),
                    "sha256": checksum,
                    "signature_sha256": digest(signature),
                }
            )
        releases = {r["version"]: dict(r) for r in base["releases"]} if base else {}
        for version, value in endpoints.items():
            checksum = value["x86"].root_sha256
            if version in releases and releases[version]["manifest_sha256"] != checksum:
                raise ReleaseError("An immutable published version changed root identity")
            releases.setdefault(
                version, {"version": version, "manifest_sha256": checksum, "revoked": False}
            )
        edges = list(base["transitions"]) if base else []
        for transition in transitions:
            if releases[transition["to"]]["revoked"]:
                raise ReleaseError("A revoked target cannot be admitted by catalog publication")
            existing = [
                edge
                for edge in edges
                if all(edge[name] == transition[name] for name in ("from", "to", "arch", "profile"))
            ]
            if existing:
                if not promote or existing != [transition]:
                    raise ReleaseError(
                        "Transition is already admitted; retain its immutable evidence"
                    )
            elif promote:
                raise ReleaseError("Promotion requires the exact already admitted acceptance edge")
            else:
                edges.append(transition)
        if promote and (base is None or release_pair is None):
            raise ReleaseError("Promotion requires an authenticated public catalog")
        stable = release_pair[1] if promote and release_pair else (base["stable"] if base else None)
        if promote and base and stable == base["stable"]:
            raise ReleaseError("Accepted target is already stable")
        revision = (
            max(base["revision"] if base else 0, reserved_revision(self.state, self.public)) + 1
        )
        document = {
            "schema": 1,
            "key_id": key_id(self.public),
            "revision": revision,
            "issued_at": issued,
            "expires_at": issued + TTL,
            "stable": stable,
            "releases": sorted(releases.values(), key=lambda r: r["version"]),
            "transitions": sorted(
                edges, key=lambda e: (e["from"], e["to"], e["arch"], e["profile"])
            ),
        }
        validate_catalog(document, expected_key_id=key_id(self.public), now=now)
        return {
            "schema": 1,
            "kind": "l4tools-catalog-promotion" if promote else "l4tools-catalog-publication",
            "key_id": key_id(self.public),
            "base_revision": base["revision"] if base else 0,
            "base_sha256": digest(pair[0]) if pair[0] is not None else None,
            "base_signature_sha256": digest(pair[1]) if pair[1] is not None else None,
            "reports": reports,
            "catalog": document,
            "source_checkpoint": _checkpoint(self.root),
        }

    def plan(self, evidence: list[Path], *, now: int | None = None, promote: bool = False) -> dict:
        now = int(time.time()) if now is None else now
        with tempfile.TemporaryDirectory(prefix="l4catalog-plan-") as temporary:
            scratch = Path(temporary)
            base, pair = self._base(self._floor(), now, scratch)
            return self._build(evidence, base, pair, now, now, scratch, promote=promote)

    def _immutable(self, relative: str, data: bytes, scratch: Path) -> None:
        previous = self._get(relative, len(data), scratch)
        if previous is None:
            self.registry.upload(relative, data)
        elif previous != data:
            raise ReleaseError(
                "Immutable catalog/evidence archive already contains different bytes"
            )
        if self._get(relative, len(data), scratch) != data:
            raise ReleaseError("Public immutable metadata GET verification failed")

    def publish(self, plan_path: Path, *, now: int | None = None) -> dict:
        now = int(time.time()) if now is None else now
        reviewed = unique_json(_file(plan_path, 65535))
        expected_fields = {
            "schema",
            "kind",
            "key_id",
            "base_revision",
            "base_sha256",
            "base_signature_sha256",
            "reports",
            "catalog",
            "source_checkpoint",
        }
        fields(reviewed, expected_fields)
        if (
            reviewed["schema"] != 1
            or type(reviewed["schema"]) is not int
            or reviewed["kind"] not in {"l4tools-catalog-publication", "l4tools-catalog-promotion"}
            or reviewed["key_id"] != key_id(self.public)
        ):
            raise ReleaseError("Reviewed catalog plan identity differs")
        reports = reviewed["reports"]
        if not isinstance(reports, list) or not reports:
            raise ReleaseError("Reviewed catalog plan has no native pipeline acceptance")
        evidence = []
        for item in reports:
            item = fields(item, {"path", "sha256", "signature_sha256"})
            if not isinstance(item["path"], str) or not item["path"]:
                raise ReleaseError("Reviewed pipeline acceptance report path is invalid")
            sha(item["sha256"])
            sha(item["signature_sha256"])
            evidence.append(Path(item["path"]))
        if not isinstance(reviewed["catalog"], dict):
            raise ReleaseError("Reviewed catalog must be an object")
        issued = integer(reviewed["catalog"].get("issued_at"), 253402300799)
        if issued > now:
            raise ReleaseError("Reviewed catalog plan is future-dated")
        config = read_config(self.root)
        with workspace_lock(self.root / config["dist"] / ".release" / "workspace.lock"):
            if not self.state.exists():
                self.state.mkdir(mode=0o700)
            protect_state(self.state)
            with (
                workspace_lock(self.state / "publisher.lock"),
                tempfile.TemporaryDirectory(prefix="l4catalog-publish-") as temporary,
            ):
                scratch = Path(temporary)
                floor = self._floor()
                if now < floor["time"]:
                    raise ReleaseError("Publisher clock moved below protected floor")
                checksum = digest(canonical(reviewed))
                pending = floor["pending"]
                if pending:
                    if (
                        pending["plan_sha256"] != checksum
                        or pending["revision"] != reviewed["catalog"]["revision"]
                    ):
                        raise ReleaseError("Another exact catalog publication is pending")
                    saved = self.state / f"revision-{pending['revision']}"
                    protect_state(saved)
                    for name in ("catalog.json", "catalog.json.sig"):
                        protect_state(saved / name)
                    if reviewed["base_revision"]:
                        protect_state(saved / "base.json")
                        protect_state(saved / "base.json.sig")
                    base_bytes = (
                        _file(saved / "base.json", 65535) if reviewed["base_revision"] else None
                    )
                    base_sig = (
                        _file(saved / "base.json.sig", 384) if reviewed["base_revision"] else None
                    )
                    pair = (base_bytes, base_sig)
                    base = self._catalog(pair, now)
                else:
                    base, pair = self._base(floor, now, scratch)
                actual = self._build(
                    evidence,
                    base,
                    pair,
                    issued,
                    now,
                    scratch,
                    promote=reviewed["kind"] == "l4tools-catalog-promotion",
                )
                if (
                    canonical(actual) != canonical(reviewed)
                    or not actual["source_checkpoint"]["clean"]
                ):
                    raise ReleaseError(
                        "Reviewed catalog plan/source/evidence/public artifacts changed"
                    )
                key = load_signing_key(self.root, self.env, self.env_path)
                verify_embedded_key(self.root, key.public_key())
                data, signature = catalog_bytes(actual["catalog"], key, now=now)
                current = (
                    self._get(CURRENT, 65535, scratch),
                    self._get(CURRENT + ".sig", 384, scratch),
                )
                if pending:
                    if digest(data) != pending["sha256"] or any(
                        value not in {old, new}
                        for value, old, new in zip(current, pair, (data, signature), strict=True)
                    ):
                        raise ReleaseError(
                            "Pending fixed catalog pair contains unknown bytes/drift"
                        )
                elif current != pair:
                    raise ReleaseError("Fixed catalog pair changed before publication")
                revision = actual["catalog"]["revision"]
                archive = f"l4tools/metadata/archive/{revision}"
                for item in actual["reports"]:
                    report_data = _file(Path(item["path"]), 65535)
                    report_sig = _file(Path(item["path"] + ".sig"), 384)
                    if (
                        digest(report_data) != item["sha256"]
                        or digest(report_sig) != item["signature_sha256"]
                    ):
                        raise ReleaseError("Pipeline acceptance changed after plan verification")
                    self._immutable(
                        f"l4tools/metadata/evidence/{item['sha256']}.json", report_data, scratch
                    )
                    self._immutable(
                        f"l4tools/metadata/evidence/{item['sha256']}.json.sig", report_sig, scratch
                    )
                self._immutable(archive + "/catalog.json", data, scratch)
                self._immutable(archive + "/catalog.json.sig", signature, scratch)
                saved = self.state / f"revision-{revision}"
                if not pending:
                    saved.mkdir(exist_ok=True)
                    protect_state(saved)
                    for name, value in (
                        ("catalog.json", data),
                        ("catalog.json.sig", signature),
                        ("base.json", pair[0]),
                        ("base.json.sig", pair[1]),
                    ):
                        if value is not None:
                            destination = saved / name
                            if destination.exists():
                                protect_state(destination)
                                if _file(destination, len(value)) != value:
                                    raise ReleaseError("Protected staged catalog bytes differ")
                            else:
                                atomic_bytes(destination, value)
                                protect_state(destination)
                    floor = {
                        "schema": 1,
                        "key_id": key_id(self.public),
                        "revision": actual["base_revision"],
                        "sha256": actual["base_sha256"],
                        "time": now if actual["base_revision"] else 0,
                        "pending": {
                            "revision": revision,
                            "sha256": digest(data),
                            "plan_sha256": checksum,
                        },
                    }
                    atomic_json(self.state / "floor.json", floor)
                    protect_state(self.state / "floor.json")
                elif (
                    _file(saved / "catalog.json", 65535) != data
                    or _file(saved / "catalog.json.sig", 384) != signature
                ):
                    raise ReleaseError("Protected pending catalog bytes changed")
                # Recheck immediately before mutable PUT; no remote CAS is claimed.
                observed = (
                    self._get(CURRENT, 65535, scratch),
                    self._get(CURRENT + ".sig", 384, scratch),
                )
                if any(
                    value not in {old, new}
                    for value, old, new in zip(observed, pair, (data, signature), strict=True)
                ):
                    raise ReleaseError(
                        "Fixed catalog pair drifted after immutable archive publication"
                    )
                if _checkpoint(self.root) != actual["source_checkpoint"]:
                    raise ReleaseError(
                        "Source checkpoint changed before mutable catalog publication"
                    )
                for relative, wanted, existing in (
                    (CURRENT, data, observed[0]),
                    (CURRENT + ".sig", signature, observed[1]),
                ):
                    if existing != wanted:
                        self.registry.upload(relative, wanted)
                if (
                    self._get(CURRENT, 65535, scratch) != data
                    or self._get(CURRENT + ".sig", 384, scratch) != signature
                ):
                    raise ReleaseError(
                        "Fixed catalog pair public GET verification failed; exact plan retained"
                    )
                validate_catalog(unique_json(data), expected_key_id=key_id(self.public), now=now)
                floor = {
                    "schema": 1,
                    "key_id": key_id(self.public),
                    "revision": revision,
                    "sha256": digest(data),
                    "time": now,
                    "pending": None,
                }
                atomic_json(self.state / "floor.json", floor)
                protect_state(self.state / "floor.json")
                return {
                    "revision": revision,
                    "catalog_sha256": digest(data),
                    "stable": actual["catalog"]["stable"],
                    "public_pair_verified": True,
                }
