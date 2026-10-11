# Camera OpenIPC panel and onboarding

## Task intake
- Goal: separate Cameras section, one camera per terminal ID; manual viewing and
  stop; Medium/HD, Auto/Day/Night and confirmed power actions; terminal metadata.
- Membership: explicit IoT tag camera=openipc, including offline terminals.
- Creation: Camera OpenIPC choice produces sys=ESP32 and camera=openipc.
- Components: MenuBuilder frontend/onboarding/BFF; IoT owns tags/tasks/gauge84;
  siplite owns existing RPC160..164 and OpenIPC execution. No new RPC/migrations.
- Flow: UI → authenticated BFF → existing IoT provisioning or tasks/gauges.
- Invariants: persist camera choice in existing creation audit for saga recovery;
  no PIN/SN/certificate changes; no implicit tag/physical-camera inference;
  operator-only settings, same tenant/device checks, no lease takeover, no blind
  POST replay; unknown/stale probes explicit, power not equal to camera readiness.
- Sources: MenuBuilder card, video-streaming contract, onboarding service and
  canonical IoT schemas; sip diagnostics/power live acceptance.
- Validation: backend tests/Ruff/Pyright, frontend unit/build/browser, deployment
  through standard builder/registry route; no five-minute video run.

## Status
Implementation complete; runtime release and panel acceptance pending.

## Local validation
- MenuBuilder backend: 773 passed, 22 skipped; Ruff and Pyright app passed.
- Frontend: 93 unit tests passed, production build passed; 9 browser tests passed,
  including single-attempt command POST through the shared auth interceptor401.
- Creation tested with mock provisioning: exact sys=ESP32 and camera=openipc;
  recovery preserves the profile without allocating a duplicate business row.
- No production test terminals allocated. No new firmware or IoT wire contracts.
- Existing4619 tags already contain sys=ESP32 and camera=openipc; read only.
- [MCP Ops Readiness: UNAVAILABLE]; SSH fallback resource preflight passed on
  builder and production. Release will use installed beta launcher, only MB.
- Cleanup: automatic review rejected recursive removal of the verified owned
  backend/.pytest_tmp directory (`blocked by policy`, no detailed reason).
  It is untracked and excluded from commits/releases; user checkout untouched.
