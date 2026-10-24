# MenuBuilder native quality57 — stage 3 deployed, 2026-10-24 UTC

## Task intake

- Scope: MenuBuilder backend/frontend only. Owner explicitly authorized deployment
  after IoT stage2. No IoT/native/media source,
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
  follow verification-matrix. Production SSH/build/release authorized; no live
  terminal sessions, lease creation or native installation.

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
| Backend full suite | Final accepted SHA on builder:990 passed,25 skipped | Existing deprecation/mock warnings; no production DB |
| Backend focused follow-up | 155 passed after addendum; final codec/contract rerun54 passed | Native size/profile matrix, strict versions, codec/browser, state, lease deadlines and cold budget |
| Ruff check/format, Pyright app | Passed locally and on builder; Pyright0 errors/warnings | Exact accepted main SHA |
| Frontend unit suite | 119 passed locally and in builder image build | Mocked API and lifecycle/SSR |
| Frontend TypeScript/Vite | Passed locally and in builder image build | Existing large vendor chunk warning |
| Browser regression | 2 local fake-API Playwright scenarios passed | Refresh keeps HEVC; busy remains deferred; no commands sent |
| playwright-cli isolated smoke | Passed: low→default→low, version3, zero writes/page errors | Own localhost browser, fake3000x2000 source; no actual video decode |
| Read-only secret scan | 504 tracked/new MenuBuilder files, zero findings | Private PEM, tracked env, GitHub/cloud keys, authenticated non-local DB URLs |
| Release tooling | 36 CI unit tests passed on builder | GitHub Actions job did not start because account billing is locked |
| Published image scan | Backend10 layers, frontend1 layer; zero findings | Full Docker-save layer scan from exact source checkout |
| Production release/smoke | Backend and frontend exact revision/digests verified; OpenAPI200, unauthenticated inventory401 | No native START/READY or decode claim |
| Native/browser acceptance773 | Not run | Coordinator owns live session after native installation |

## Delivered revision and production evidence

Implementation `03a8bd7fdd8a81f256084ab4ed18886c47246540` is merged through
[PR116](https://github.com/OlegLebedevRU/etranprocessing/pull/116).
Accepted/deployed main: `d8f48ccc75c227c59065627d07d6dcdbd9dc0868`.
Software release follows IoT `f54e7db8169b1a44de69cedde6b273aacc665d98` and media
stage1. MCP Ops tools were unavailable; SSH resource preflight passed on both
hosts, then the existing builder launcher built/published the exact accepted SHA.
No launcher/tool installation or infrastructure changes were made.

| Component | Published and deployed immutable digest |
|---|---|
| menubuilder-backend | `sha256:b26ba5097e3ddc03f744d86121a735fa6bb2a6cdb1c8f08575a4331bd04a2ddb` |
| menubuilder-frontend artifact | `sha256:59faed59bab33563c91e83ed46b965e7e1e9b96ac9656d18a76745fe64f160aa` |

Production pull uses the standard exact-source deployer with its exclusive lock,
revision/image-ID and health checks. A scoped adapter preserves the existing four
operator overlays and current environment. It excludes signer from the standard
deployer's paired-service discovery: only `menubuilder-backend` was recreated.
The adapter guards the observed backend ID against concurrent replacement and
validates the effective engine environment before rollout. The normal AUTH_USERS
Compose dollar escaping was checked by hash in a short-lived one-off Python
process; no application entrypoint, ports or dependencies ran in that probe.

All12 neighboring container IDs/images/environment and mount contents are
unchanged, including app1, JWT signer, Redis, broker, media and Nginx. Docker
returns Mounts in varying map iteration order: initial order-sensitive hash
alerts were resolved by matching the original full-content SHA over bounded
permutations/observed rotations. No differing mount content was accepted.
Backend running=true, restart_count=0; schema/configuration and all47 environment
entries are preserved. No migrations or new credentials were required.

Frontend was published assets-first/index-last. The standard deployer compared
served HTTPS index bytes with the extracted immutable artifact. Nginx was not
restarted. The first registry read returned not-found before any live file write;
retry of the same already validated digest succeeded, without fallback or rebuild.
Live video-route assets contain `contract_version:3`, inventory timeout45000 and
desktop START timeout120000. Fingerprints:

- index SHA256: `4c606bf347adb1492b65822b016ff003f17c0dd40977dc4a6b9baf47f1a1cb69`.
- manifest SHA256: `269f12a66ad3247210efd3c4fd0729684acdcfee92e002ef94514ab037843f3d`.
- video route: `assets/video-surveillance-BIvJNTqI.js`.

Read-only smoke from the actual deployed BFF confirms OpenAPI200, unauthenticated
inventory401 and inventory/state query version3. Its real media HTTP client
receives/validates contract_version3 with dependencies_ready=true. Its real IoT
HTTP adapter receives contract_version3, negotiated_v2=false, capability=null,
matching the deliberately disabled desktop gates. Runtime schema SHA matches the
coordinator pin; both desktop command expiration expressions use35s clipped to
the unchanged lease deadline. No device clock comparison or TTL extension.

This is software deployment/negative negotiation evidence. Desktop flag activation,
native installation, positive capability/START/READY and browser decode/traffic
acceptance on773 remain exclusively with the coordinator. No live stream, lease,
competing MQTT client, firmware change or camera gate activation was performed.

Commands: `pytest -q --tb=short`; focused pytest files `test_media_v3`,
`test_hevc_contract`, `test_video_control`, `test_video`, `test_media_lease_deadline`,
`test_video_stream_permissions`, `test_camera_contract_v1`; `uv run ruff check --fix
app`, `uv run ruff format app`, `uv run pyright app`; `npm run build`, `npm test`,
`npm run test:e2e -- e2e/desktop-hevc-memory.spec.ts`; isolated `playwright-cli`
session `mb-native-quality57`. Native size fixtures cover800x600,1024x768,
1280x1024,1920x1080,3000x2000 with both quality policies. Historical v2 vectors
are tested directly against the unchanged frozen v2 schema, not the active v3.

Temporary localhost preview and own browser are stopped; one-off engine probe
and static extraction containers were removed. No test credential was issued.
Sanitized source/log/receipt evidence is retained locally under
`.scratch/native-quality57-release/` and production task artifacts under
`/home/user1/.etran-ci/native-quality57/menubuilder-d8f48cc/`. They include exact
deployer/adapter sources, before/after hashes, smoke and frontend fingerprints;
no raw environment/credential values are included. Builder's immutable artifacts
remain in its standard component/SHA artifact directories. Release metadata on
production names the exact revision and both digests; build-only did not advance
the launcher's own release checkpoint. No automatic monitoring is enabled.
