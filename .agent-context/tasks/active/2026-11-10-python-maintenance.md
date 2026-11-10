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
  full-source Black check273files passes. Broad IoT Ruff audit reports findings in
  unchanged source; no unrelated mass import/style cleanup is included.
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
  Advisory IDs: GHSA-3qf3-8w2g-rqmx and GHSA-wj6h-64fc-37mp. The same chain also
  reports the rsa dependency archived; include it in that deferred migration.
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
- Production and builder rollout completed; runtime evidence follows below.

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

## Final release and runtime verification

- Accepted application source etranprocessing/main7737011b09b1c2f6cd9158383ffd831d00672d87,
  PR150; IoT/master4c512cbe338cf4519c49fc52e16390b8b2afe014, PR162.
- Builder installer repair accepted main dac6b6f45ed5a10db14c760aefcf19b49bcbf632,
  PR151. Installed only through repository package. Ownership repair succeeded;
  standard IoT publication retry completed. Previous tool binary remains available.
- Published/runtime image digests:
  - PB: sha256:c568862762a6881dde24285cd3d7e4a40632342ed7068e0f2e3b120cc3d04d82.
  - MB/signer: sha256:4bc568b03237d9b2443af6bf8c7664b8e69c5b717b391bab8cb7b4807c514832.
  - MCP: sha256:fc00936b595b36188a7733090432cbdbed030d612fa1f1118d98a900421828a1.
  - app1/runtime: sha256:51fc538ba3798459814494e81d25ab06a1063d6cbe38c33d9a946652cd00bb7f.
- All six Python containers rolled sequentially. Image IDs/OCI revisions, Python
  3.14.10, uv0.13.7 and selected package versions verified inside each container.
  Source remains immutable; no pip hotpatching or OS Python replacement.
- Preserved each service's complete original Compose chain, effective application
  environment, command, mounts, ports, resource limits and json-file20m/3 logging.
  Old/new official Python base also differs by LANG=C.UTF-8 and build metadata;
  preflight initially stopped before replacement until that default was identified.
- MCP's private compatibility overlay was copied to this release with only inherited
  Python build metadata removed, letting the new image supply correct defaults;
  application variables/unset keys and original rollback overlay are preserved.
- New PB/MB/app1 candidate containers each performed SELECT1 against the real
  managed DB before replacement. All reported Python3.14.10/SQLAlchemy2.1.8/
  asyncpg0.32.0. Standard PB Alembic gate passed; existing schema034 unchanged.
- Builder opt-in PostgreSQL18 regression:5passed with new driver; tests include
  actual serialization/rollback/timeout/disconnect recovery. Disposable loopback
  container, tmpfs data, private env file and generated credentials all removed.
- Active camera4623 stream was observed before rollout. User explicitly authorized
  interrupting anything needed for deployment. No fake operator identity, manual
  lease deletion, camera password/power/binding change or new stream START used.
- Post-rollout4623: generation35, queued0, active=false/enrolled=false, advancing
  last_tick and owner deadline. Fresh preparation/enrollment is needed for next START.
  First aggregate diagnostic compared uninitialized unrelated rows and failed on
  None; corrected query scopes to4623 and confirms worker progress.
- PB, MB, MCP and IoT health/API200. Signer health passed. Public MB page/captcha
  API200 with normal TLS/SNI validation and workstation API check200.
- RabbitMQ39queues, ready0/unacked0,53consumers. Bounded logs of all six services:
  no ERROR/CRITICAL/traceback in inspected window. All neighbor container IDs
  unchanged; frontend, brokers and gateways were not recreated.
- Final host resources: production RAM2050MiB available/disk52%/load0.36;
  builder RAM2581MiB available/disk74%/load0.65. No temporary probe containers remain.
- Release/rollback metadata and host-only orchestration artifact retained privately
  under production .etran-ci/runs/python-maintenance/<application source SHA>/.
- Local precise .python-version pins now select3.14.10, including shared/media via
  ancestor root pin. Fresh User PATH verified: Python3.14.10/uv0.13.7/Ruff0.17.4.
  Root source Ruff/format/Pyright and uv pip check also pass on cryptography50.
- Follow-up pins/documentation do not alter deployed application code or image
  contents and need no additional service restart. No new authenticated browser
  login/payment writes or camera decode E2E claimed. JWT migration remains deferred.

## Operator acceptance

User subsequently confirmed that everything visually works after deployment.
This is operator confirmation, not an additional automated browser/decode test.
No further runtime changes were needed. JWT migration remains a separate task.
