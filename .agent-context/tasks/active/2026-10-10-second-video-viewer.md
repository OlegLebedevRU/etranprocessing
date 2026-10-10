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
Pending final results. A previously recreated mountpoint can require a normal stop/start to restore agreement with the orchestrator; deployment alone does not resurrect the first viewer's destroyed Janus handle.
