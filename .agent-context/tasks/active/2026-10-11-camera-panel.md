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
Completed: implementation and follow-up merged to main, runtime released and accepted.

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

## Merge and runtime
- PR73 merged to main:74253e4339447e3cde4c8abd606152f4ac34ecfe.
- Installed beta launcher built/tested/published/pulled this exact revision.
- Backend digest:sha256:477f4be2d7acd2f361948254ca59596997b9456212388c53612ee93364c2ee07;
  MenuBuilder and its standard coupled JWT signer healthy on this image.
- Initial frontend digest:sha256:41382b31449640333ce81b276a6016d00757cfe078b6d3a0f6747be1cd0b830c;
  live artifact verified; nginx-default not restarted.
- app1, ProcessingBackend, ingress, media-nginx, Janus, RabbitMQ, Redis and
  l4mcp container IDs remained unchanged.

## Live acceptance
- Fresh approved cookie session, org1: camera4619 state200, terminal online,
  OpenIPC ready, power on. Camera4623 API404 (no camera tag).
- Live Edge browser: no POST on selection, no page errors. Refresh generated
  one RPC160 POST and confirmed result plus gauge84:
  task97a94760-5cf4-4724-bec6-68f5fb95774d,
  snapshot58046967-d744-4ad6-80f9-96db34099fc9.
- Operator answered «Есть» for stream4619 in the new Cameras section.
- With their active lease, unchanged-HD write rejected409/camera_busy before
  task creation; no settings altered and no session takeover.
- Medium/HD, Day/Night and physical power behavior already accepted on0018;
  this stage did not switch the filter or power, create production terminals,
  change licenses/certificates/DB schema, or perform a five-minute video soak.
- Visual inspection found a desktop source-selection hint in the camera player;
  follow-up uses the actual «Начать просмотр» action; original video page retained.
- Evidence kept locally in sip/build/acceptance/camera-panel-*.json/log/png;
  credentials remain only in the user's ignored .env, never copied into Git.

## Final follow-up release
- PR74 merged:6dc51929ddcf16ccd20360466801f27518797581; base Compose now pins
  the verified backend digest above. Only the frontend was released again.
- Final frontend digest:sha256:72e28fab504766b3355519eb6e87b47d5c21d00d923237ae82f01c28053fa21d.
- Builder repeated93 unit tests and production build; local final9 browser
  tests passed. Read-only live Edge check confirmed the correct viewing hint,
  zero automatic POSTs, no page errors, and disabled HD during the active lease.
- nginx-default and all neighboring services retained their IDs; MB and its
  coupled signer retained their accepted backend image and healthy state.
- No further firmware/OTA required for this MenuBuilder-only stage.
