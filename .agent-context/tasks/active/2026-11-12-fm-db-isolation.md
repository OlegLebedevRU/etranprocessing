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
- Deployment and live behavior: pending release; do not infer E2E from unit tests.

## Remaining

- Record release revision/digest and actual live observations after rollout.
- Demand driven readiness contract and agent assignment follow PB deployment.
- No production credentials created; no scratch scripts copied into containers.
