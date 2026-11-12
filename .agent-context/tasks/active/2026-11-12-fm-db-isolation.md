# FM database isolation and demand driven readiness

## Task intake

- Goal: protect PB payment resources from unused FM polling, deploy the compatible
  PB mitigation, then define demand driven readiness and a cross-stack agent task.
- Scope: PB implementation/release, repository deployment helper and FM docs.
  Native client/MQTT implementation, MB/app1 changes are a subsequent task.
- Owner: PB owns terminal auth/discovery/fm_agents and its pool; IoT owns the shared
  lease and dispatch; MB owns tenant/user admission. PB owns migrations (none here).
- Producer → transport → consumer: existing agent → Leo4Proxy/mTLS HTTPS → PB;
  existing MB → authenticated internal HTTPS → PB. Wire protocol unchanged.
- Invariants: current certificate SN/serial, active terminal, instance UUID,
  45-second heartbeat validity, shared lease and S3-only bytes remain unchanged.
- Risks: production DB/network root cause remains unproven. Dedicated pool bounds
  FM concurrency, not database-wide CPU/lock contention. Legacy polling continues.
- Sources: processing-backend card, file-manager contract, authoritative FM docs,
  auth/discovery/routers/database code. No excluded native/legacy folders read.
- Completion: quality + full PB suite, bounded-pool/auth/failure regressions,
  release revision/digest/health/environment/mount/neighbors checks, future contract.

## Implemented

- Independent FM pool: default size2/overflow0, acquisition1 s/connect3 s/command5 s.
  Per-worker additional budget is2; main pool retains its existing configuration.
- FM DB failures produce503/Retry-After5, no internal retry. Acquisition and total
  latency logged through uvicorn, payloads and exception contents omitted.
- Successful identical hello discovery sampled300 s, bounded4096 entries/process.
  Restart/expiry/change samples again. Failed observations invalidate matching
  samples; failures and other endpoints are not suppressed. Cache is not auth.
- Every successful hello still persists fm_agents freshness; readiness remains45 s.
- Scoped release helper preserves live Compose file order, environment, mounts,
  network, neighbors and restores previous image on verification failure.

## Verification

- Windows/Python3.14.10: Ruff app/new tests passed, Pyright app 0 errors.
- Full PB pytest:381 passed/2 skipped,35.61 s. Existing suite warnings include
  AsyncMock/deprecated TestClient warnings; no warning suppression added.
- New18 cases cover pool saturation independent of payment, pre/within-request
  failures503/cleanup/no retry, sampling expiry/changes/failure/capacity, real auth
  hello ready/disabled/serial mismatch/missing headers/instance mismatch.
- Production preflight SSH: MemAvailable1814MiB, disk53%, load0.57. MCP Ops tools
  unavailable, SSH fallback used. DB current=head034. Current PB code vs main
  differs only in .python-version before this change.
- Linux builder:36 CI tests and381 PB passed/2skipped; Ruff/format/Pyright passed.
- Deploy helper:5 preservation tests passed. Read-only secret pattern scan across
  permitted tracked files:21 file/rule matches reviewed (fixtures, placeholder
  URLs and PEM header validators); no real credentials found by this scan.

## Production release — 2026-11-12, verified13:56 UTC

- PR156 merged; source `61a14fc1f91cadd346b466ff3b32683d783b7bcf`.
- Builder launcher build-only → registry digest
  `sha256:f6d9f36cc28ee96e5be4d296c88359a10aef8857e3a7d561acda0d097fb86972`.
- Production pulled the exact digest. Scoped repository helper delivered via scp
  preserves all actual Compose overlays, including existing operator overrides.
  Container `3166779ae4f0ff512a4c19b2e5bdf44a48f2bae7121560d7c4504ee9e0733fb7`.
- Health200; natural agent hello200. First observed acquisition43.9ms/total155.9ms;
  subsequent acquisition5.1–6.2ms and total20.1–41.7ms. Short observation only,
  not a performance benchmark or proof of the previous incident root cause.
- Helper verified environment, mounts, network and neighbor container IDs unchanged.
  No migrations; schema034. First health probe during startup refused connection,
  standard bounded retry succeeded; no rollback was required.
- Follow-up through approximately14:03 UTC:154 natural hello HTTP200,
  0 fm_database_unavailable log events,16 payment endpoint HTTP200. These are
  transport observations, not verification of payment business outcomes.
  MemAvailable1837MiB, disk54%, load0.24.
- Server evidence: `.etran-ci/runs/fm-isolation/<release-SHA>/verified.json`, durable
  forward/rollback overlays in the same directory. Previous image digest
  `sha256:c568862762a6881dde24285cd3d7e4a40632342ed7068e0f2e3b120cc3d04d82`.
- Builder was used build-only so its automatic deployment checkpoint was not
  advanced. This release is recorded by production verified.json and this handoff;
  future releases must preserve live overlays again, not use a default base-only
  rollout. No direct builder source/tool/state edits were performed.
- Native transfer/lease hardware E2E, disabled-terminal live scenario and injected
  production DB failure were not run; auth/failure regressions are local tests.

## Remaining

- [Demand driven readiness contract](../../../docs/etran_arch-file-manager-on-demand-readiness.md)
  and [agent assignment](2026-11-12-fm-on-demand-agent-task.md) are prepared;
  their cross-stack implementation remains a subsequent task.
- No production credentials created; no scratch scripts copied into containers.
- Temporary scanner/PR-body files removed. Runtime release records/overlays retained
  because Compose references them. User's original dirty checkout was untouched.
- Documentation link check:9 changed/new Markdown files,176 local links checked,
  no broken links; excluded native/legacy paths not inspected. New docs sensitive
  pattern scan and git diff --check passed. No extra code tests for doc-only step.
