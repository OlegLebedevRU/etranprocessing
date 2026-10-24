# MenuBuilder native quality57 — stage 3, awaiting deployment signal

## Task intake

- Scope: MenuBuilder backend/frontend only, implementation now; deployment only
  after the owner's explicit signal following IoT. No IoT/native/media source,
  camera gates/firmware, broker, infrastructure or shared database changes.
- Branch: `feat/menubuilder-native-quality57`, isolated worktree based on
  `origin/main` `6af7ec7c09cbf38d72a201640e0406e8d1dcd114` (includes PR109).
  Parallel camera PR110 is not merged or rewritten by this task.
- Owners: native supplies concrete source profiles; IoT owns authoritative
  capability and lease/control state; MenuBuilder selects quality/format and
  orchestrates; media owns routes/SDP. No migrations.
- Flow: UI → MenuBuilder → IoT capability/state and media capability/health →
  exact descriptor prepare → IoT CTL2 START/READY → existing Janus playback.
- Preserve authorization, owner/session and lease deadline checks, source and
  geometry identity, measured probe memory and fresh cheap applicability check,
  exact descriptors and codecs, camera boot/enrollment protections, input safety.
- Skills: repo-intake-and-routing, frontend-remote-control-safety; local checks
  follow verification-matrix. No production access or live terminal sessions.

## Contract and implementation

- Coordinator schema is copied byte-for-byte to
  `MenuBuilder/backend/app/contracts/media-v3.schema.json`, with LF checkout
  pinned in `.gitattributes`. SHA256:
  `b86ca6f066fcdecb09cf7145a7fb23d87b77311a53c7ea04f0fb6e8a905b2818`.
  The shared coordinator package and frozen historical v2 schema are unchanged.
- Common desktop flow selects `X-L4-Media-Contract: l4-media/3.0.0` on IoT and
  media HTTP, including capability, START, state and health. Existing URL paths,
  function names and CTL `v/version=2` remain. IoT inventory keeps the historical
  field name `negotiated_v2` with a required top-level `contract_version:3`.
- UI uses inventory/state/session query `contract_version=3`. Explicit old/unknown
  query values are rejected. Missing or mixed common descriptor/capability/media
  versions fail; no old snapshot is promoted to v3 or retried through legacy START.
- Medium/HD keep `low/default`. The settings hint explains traffic/detail policies;
  concrete width/height/FPS/profile/level come from the source capability without
  clamping or relabelling. Schema dimensions are parsing bounds, not camera presets.
- Both explicit H264/H265 use the common exact-descriptor path. Auto may select
  an advertised concrete H264 profile after known producer HEVC non-support;
  invalid/unknown/stale data cannot authorize that selection. No software HEVC.
- Selected codec/profile/packetization/clock must match media capabilities.
  Browser codec observation is supplied for desktop START and checked for
  freshness and positive support. Existing Janus SDP/decoder negotiation remains;
  local tests do not prove hardware decode of a particular SPS/level.
- State rejects a changed actual descriptor or resolved codec. Viewer creation
  with an explicitly selected v3 contract requires a real descriptor.
- Existing camera revision remains on its separate HTTP header/validators;
  no simultaneous camera+media selection, enrollment/boot relaxation, camera
  runtime rollout or firmware enablement. Existing fit/native UI is retained.

## Cold-start addendum (MenuBuilder only)

- Desktop START expiration is `min(now+35000, lease deadline)` both before media
  preparation and immediately before publishing START. Other source-mode command
  budgets are unchanged. Lease TTL and schema/SHA are unchanged.
- Common v3 IoT capability/START HTTP waits have a 40-second minimum (35 seconds
  plus transit/validation). Camera-specific waits retain their existing settings.
- UI capability inventory waits 45 seconds; desktop START waits 120 seconds for
  sequential capability discovery, media preparation and READY. Timeouts do not
  trigger another START or another hardware probe. Cheap fresh applicability and
  remembered successful capability knowledge remain intact.
- First source listing and selected-source capability inventory remain separate;
  the first cold capability request receives the expanded HTTP budget. Real
  broker pending/publish timing and native activation belong to IoT/coordinator.

## Verification

| Layer | Result | Limits |
|---|---|---|
| Backend full suite | 986 passed,25 skipped before cold-start addendum | Existing deprecation/mock warnings; no production DB |
| Backend focused follow-up | 155 passed after addendum; final codec/contract rerun54 passed | Native size/profile matrix, strict versions, codec/browser, state, lease deadlines and cold budget |
| Ruff check/format, Pyright app | Passed after final code changes; Pyright0 errors/warnings | Changed MenuBuilder only |
| Frontend unit suite | 119 passed after addendum | Mocked API and lifecycle/SSR |
| Frontend TypeScript/Vite | Passed after addendum | Existing large vendor chunk warning |
| Browser regression | 2 local fake-API Playwright scenarios passed | Refresh keeps HEVC; busy remains deferred; no commands sent |
| playwright-cli isolated smoke | Passed: low→default→low, version3, zero writes/page errors | Own localhost browser, fake3000x2000 source; no actual video decode |
| Read-only secret scan | 504 tracked/new MenuBuilder files, zero findings | Private PEM, tracked env, GitHub/cloud keys, authenticated non-local DB URLs |
| Production release/smoke | Not run | Owner signal required after IoT |
| Native/browser acceptance773 | Not run | Coordinator owns live session after native installation |

## Delivery and remaining work

The PR identifies the final source revision. No deployment, builder invocation,
new container, native installation, broker connection or terminal stream was
started. Deployed SHA/digests are intentionally absent until the owner's signal.
At release, use the existing exact-SHA builder/deployer for MenuBuilder backend
and frontend only; retain operator overrides and verify header/version smoke.
No separate rollback/idle-window work is required by the task packet.

Commands: `pytest -q --tb=short`; focused pytest files `test_media_v3`,
`test_hevc_contract`, `test_video_control`, `test_video`, `test_media_lease_deadline`,
`test_video_stream_permissions`, `test_camera_contract_v1`; `uv run ruff check --fix
app`, `uv run ruff format app`, `uv run pyright app`; `npm run build`, `npm test`,
`npm run test:e2e -- e2e/desktop-hevc-memory.spec.ts`; isolated `playwright-cli`
session `mb-native-quality57`. Native size fixtures cover800x600,1024x768,
1280x1024,1920x1080,3000x2000 with both quality policies. Historical v2 vectors
are tested directly against the unchanged frozen v2 schema, not the active v3.

Temporary preview on localhost5197 and own browser session are stopped. Scratch
driver/scan report are removed after recording their sanitized results here.
Keep isolated Python/npm dependencies, build output, worktree and PR for subsequent
authorized deployment. No production test credential or remote artifact exists.
