# Python dependency and tooling maintenance

## Task intake

- User authorized dependency/tooling audit and careful local, builder and production
  updates, explicitly choosing all upgrades inside existing dependency ranges.
- Owners: per-project lock files; shared declarative packages retain model/schema
  semantics; IoT app1/camera-runtime remains a separate master release.
- Producer: locked source/development toolchain -> dedicated builder -> immutable
  release images -> production consumers. No in-container package hotpatches.
- Preserve Python3.14 application target, all auth/tenant/session/physical binding
  fences, DB schema, image rollback, complete Compose chains and logging policy.
- Use repository intake and verification-matrix skills. MCP Ops unavailable;
  authorized SSH fallback. No unrelated Windows tools/MCP app upgrades.
- Acceptance: fresh lock resolution/audit, local checks and full affected suites,
  Linux builder gates and OCI digests, serialized rollout with unchanged runtime
  configuration plus application/DB/broker health. Never replace system Ubuntu Python.

## Baseline and proposed versions

- Source etranprocessing/maincecb87c, IoT/master179872f; isolated clean worktrees.
- Windows default python3.14.0; project managed Python3.14.7. uv0.12.5 local/builder.
- Production application Python3.14.7; OS Python3.12.3 on both Ubuntu hosts.
- Official uv latest0.13.7, Python3.14 patch3.14.10. Local uv upgraded with previous
  binary backup; managed3.14.10 installed; Ruff/FastAPI CLI upgraded independently.
- Runtime resolver selected FastAPI0.162.4, SQLAlchemy2.1.8, asyncpg0.32.0,
  Pydantic2.14.3, cryptography50.0.2 and FastMCP4.1.7 stable. Tests/audit pending;
  no production dependency upgrade has occurred yet.
- User separately authorized changing root cryptography range to>=50,<51 after
  three advisories were blocked by the old<49 ceiling. Exact schema pins retained.
- Builder installer default refuses tool replacement; explicit upgrade option
  retains a checksum-named previous binary and uses atomic replacement.

## Local verification

- PB363passed/2skipped; MB1209passed/25skipped; MCP21passed; IoT1215passed/13skipped.
  Root release/deploy240passed; CI36passed/6subtests; shared65passed; access2passed;
  gauge4passed; media archive21passed. No frontend code/dependencies changed.
- Ruff/format/Pyright pass for PB, MB, MCP, shared packages, archive and installer.
  IoT changed runtime file and critical runtime/DB modules pass scoped checks;
  full-source Black check273files passes. Broad IoT Ruff audit has pre-existing
  formatting/import/style findings; no unrelated mass cleanup is included.
- Compatibility adaptations are type-only SQLAlchemy2.1 variadic Row/Select,
  explicit IoT sqlalchemy[asyncio] extra, and a cast of PostgreSQL's non-null clock
  timestamp. No query, transaction, lease, schema or topic behavior changed.
- Updated shared source provenance hash for its changed uv.lock. Tests initially
  caught stale provenance and bootstrap test temp inside repository; corrected
  provenance and used a fresh external task-specific temp. Initial test invocation
  also selected root tests via --project and encountered denied shared Windows
  pytest temp; final per-project runs above use correct directories/private temp.
- Root deploy tests now declare their previously implicit PyYAML dev dependency.
- OSV audit: PB, MCP, shared/access/gauge, media and IoT have no reported advisory;
  root also clean after authorized cryptography50 upgrade. MB retains two findings
  (python-jose/ecdsa) with no fixed release; user explicitly deferred JWT migration.
  Existing JWT algorithm allowlists remain unchanged; do not claim a clean MB audit.
- Local uv0.13.7, managed Python3.14.10, Ruff0.17.4, Pyright1.1.414, pytest9.2.0.
  FastAPI CLI0.162.4. User PATH now prioritizes managed Python for fresh shells;
  previous uv binary/User PATH retained privately for rollback. Existing process
  environments and unrelated MCP applications were not modified.
- Ubuntu Python package3.12.3-1ubuntu0.17 equals installed apt candidate on both
  hosts. Application/managed Python is updated independently of the OS interpreter.
- Builder Python installation uses the same UV_PYTHON_INSTALL_DIR as its launcher.
- IoT GHCR build workflow disabled in GitHub before pushing; its source was already
  manual-only. etranprocessing automatic workflow remains disabled.
- Production and builder rollout: pending; local results alone are not deploy proof.

## Builder publication follow-up

- Builder installer upgraded uv0.13.7 and managed Python3.14.10 successfully.
  PB, MB and MCP Linux gates and immutable image publication completed from7737011.
- IoT Linux suite/build/push completed, but final imagetools verification failed:
  an earlier root metadata query using the runner Docker config had replaced only
  .docker/buildx/current with root:0600. Parent, lock and activity remain runner-owned.
  This was introduced by the investigation, not by application dependencies.
- Repair is delivered via the approved installer: validate that exact selector is
  a regular non-symlink file, restore runner ownership, then retry the standard
  registry publication path. No recursive ownership change or Docker auth edits.
