# Second viewer preserves the shared video mountpoint

## Task intake
- Goal: correct the second-tab freeze, test, push and deploy the backend.
- Scope: MenuBuilder backend video session endpoint and regression tests only.
- Owner: media ingress owns the shared media session/mountpoint; MenuBuilder verifies tenant, terminal access and active lease ownership before returning watch credentials.
- Flow: browser POST video session -> BFF -> ingress lifecycle/health -> existing Janus streaming mountpoint.
- Invariants: no replacement of live mountpoints on watch; no bypass of orchestrator conflicts/errors; preserve permission and lease checks. No schema changes.
- Evidence: production BFF at 14:59:26 UTC returned session_busy for terminal 773, then logged different/unknown PIN and recreated mountpoint. Capture process and ingress connection epoch stayed unchanged.
- Risk: route-before-start uses lease ID, whereas subsequent calls use stream instance ID; process-local PIN cache is not authoritative.
- Validation: before/after stream start, cold cache, conflict/unavailability without fallback, direct route unknown PIN without destroy; existing video/control/permission tests, Ruff/Pyright and release checks.

## Change
- Probe current stream and lease session IDs, reuse an active session only after checking SN, device ID and mountpoint ID.
- Return its authoritative PIN without creating/updating the mountpoint or RTP route.
- Propagate orchestrator errors; remove the direct setup fallback when orchestration is enabled.
- Direct setup returns 409 instead of destroying an occupied mountpoint whose PIN is unknown or different.
- Legacy direct-route tests explicitly select that mode; lifecycle tests mock the new health query.

## Validation / release
- Local video/control/permission regression set: 70 passed; Ruff check/format and Pyright app: passed, zero errors. Secret scan of touched files: no matches; diff whitespace check passed.
- Linux builder: 32 release-flow tests; full backend 718 passed / 22 PostgreSQL integration tests skipped; compileall, Ruff check/format and Pyright passed. These checks do not prove live two-browser playback.
- Code pushed to main: `6fc83a5aaa70e12f7e6d8ff37e0457bedd2b7c45`.
- First publication stalled after registry HTTP 500; canceled only its buildx client after 303 seconds. Normal launcher retry published a new unique tag. First production pull stalled; canceled only its pull client, then launcher reused the immutable artifact without rebuilding. Both failures occurred before service replacement.
- Released digest: `sha256:b54de81591b303ae14f2f2746ef93f76ad1762ba15aa2b1c3246e83d64d449e7`; tag `6fc83a5aaa70e12f7e6d8ff37e0457bedd2b7c45-20261010T152031102901Z`.
- Production deployment journal: 2026-10-10 15:24:05 UTC. Backend container `a189bde52e9f`, running, restart count 0; image digest and revision independently matched. API readiness passed after normal startup retry.
- All neighboring container IDs unchanged, including Janus `1cd35cea4177`, ingress `072bc9c575dd`, app1 `33f41d169e5c`, processing `c9cd9e3c944e`, media nginx `8927aebcc8cc`, portal nginx `f57699a9f1ca`, l4mcp `17481e60bc24`.
- No production JWT minted or copied; no authenticated browser E2E performed. A previously recreated mountpoint can require a normal stop/start to restore agreement with the orchestrator; deployment alone does not resurrect the first viewer's destroyed Janus handle. Reopen terminal 773 and connect a second tab to confirm both continue decoding.
