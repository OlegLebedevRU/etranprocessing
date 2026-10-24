# MenuBuilder camera runtime141 handoff

## Task intake

- Scope: MenuBuilder BFF and camera UI, implementation PR; no deploy or external writer changes.
- Base: origin/main `6af7ec7c09cbf38d72a201640e0406e8d1dcd114`, including PR109.
- Owner: MenuBuilder owns browser authorization/orchestration; IoT owns enrollment,
  durable admission/producer fences and migration0013. Media owns route/codec support;
  firmware owns genuine hello/proof/commit and physical producer release.
- Pinned IoT141: `5f4129a279b25cd49b5fe6c54d80477975940885`.
  Read pinned export plus immutable remote_input/runtime_state implementations.
- Invariants: no browser-selected descriptor/boot/epoch/nonce/tenant/service key;
  no lease before readiness, no automatic replay of ambiguous mutations, no codec
  fallback after START, no inference of physical release or decoded video from ACK.
- Validation: local mocks and browser fixtures only. Firmware/broker/production
  acceptance is outside this implementation evidence.

## Implemented contract

The camera page captures a single verified physical MAC, discovery generation,
source and codec choice. BFF independently checks fresh online inventory, verified
port2 and auth, actual_track and media capabilities, then creates the immutable
descriptor using the existing trusted native-media descriptor construction path.
Configuration FPS/dimension sentinels never become a descriptor. A codec change
requiring a camera write is refused; users must complete an independent authorized
settings operation first. No probing lease or hidden camera configuration write.

BFF `/api/cameras/{device}/admission/prepare`, `/enrollment`, `/readiness` take
strict selection or opaque tickets. Each repeats active-terminal/operator access
checks. Tickets are encrypted/authenticated, scoped to tenant/SN/user/session and
short original deadlines; they contain no camera/service credentials. They work
across BFF workers sharing the environment key, without a local authority cache.
IoT's durable nonce consumption, lease and producer fences remain authoritative.
No claim of a new BFF distributed lock or durable cancellation registry is made.

Internal calls use exactly camera1.0 plus enrollment0.1/admission0.2 headers.
Prepare sends only descriptor and physical_camera_mac; enrollment is GET; readiness
is a bodyless POST. BFF validates readiness boot/epoch/source/MAC/path/hash/codec
against its preparation and current enrollment. Path is exactly
`port2/<discovery.generation>/<descriptor.geometry_generation>`.

Only then the UI acquires a60s stream lease and calls existing START with an opaque
ticket. BFF forwards the nonce solely as `X-L4-Camera-Readiness`; the camera1.0 body
is unchanged. Stable stream/operation IDs keep repeated delivery of the same
nonce/lease from allocating a different attempt. Existing state/STOP/RENEW routes
and PR109 keepalive budget remain; IoT selects negotiated commands from stored state.

UI stages: checking camera, connecting terminal, checking readiness, starting video,
waiting for frames, live, stopping/error. Cancel is available during preparation.
Generation/AbortSignal checks discard late completions; a late acquired lease is
cleaned by its captured device/lease IDs. No START/prepare/readiness/lease auth replay.
The live badge still requires fresh decoded frames. Watch snapshots cannot erase
pre-lease UI state. The browser never publishes MQTT.

## Gates and rollout

- `CAMERA_MONOTONIC_RUNTIME_ENABLED=false` by default.
- `CAMERA_ADMISSION_TICKET_KEY` is empty by default; enabling requires a separately
  provisioned Fernet key shared across BFF workers. Keep it in environment/secret
  storage. Rotation invalidates outstanding short-lived tickets. No key is committed.
- New camera UI always explicitly requests admission. Disabled/unsupported server
  fails visibly, without silently falling back. Legacy API clients retain the old
  path while the gate is off; when on, a camera START requires admission.
- Existing video/session gates, permissions, firmware writer gates and SN allowlists
  stay independent. No deploy, broker access, firmware modification or migration here.
- Deploy reviewed IoT141 + its migration with gate off, then compatible BFF/UI with
  gate off. Coordinator owns firmware4623 and deliberate joint enablement/acceptance.

## Native Auto cross-owner dependency

IoT141 has no third-revision negotiation for `camera-cadence/0.1.0-proposed`.
Current media capabilities and strict camera descriptor schemas also lack an
accepted cadence extension. Therefore Native configuration (`fps=0` or `size=""`)
is explicitly refused by this new path even when an experimental observation exists.
OpenIPC PR2 experiment is not product negotiation evidence.

Required owner agreement before activation:

1. Firmware/media owners publish a separately versioned capability and descriptor
   extension describing validated original-RTP cadence and continuous revocation.
2. IoT owner defines the exact independently negotiated HTTP header/capability and
   includes the accepted extension in canonical descriptor/binding hashes, readiness
   and START equality checks. Do not change frozen strict camera1.0 schemas in place.
3. MenuBuilder consumes that accepted descriptor through its backend preparation
   path, verifies current port2/actual-track facts and includes the same extension
   in its immutable ticket. Only then remove the Native Auto refusal.
4. Joint tests cover disagreement, absent/partial negotiation, cadence revocation,
   stale geometry and original real RTP on4623 before enabling writers.

## Verification

- Full backend pytest:972 passed,25 skipped,53 warnings (existing deprecations and
  AsyncMock warnings); subsequent identity changes get targeted regression checks.
- Frontend unit suite:115 passed, including8 new admission tests and PR109 lifecycle.
- Ruff check/format and Pyright: passed after correcting a conditional import.
- Final targeted backend:26 passed, including stable duplicate identity and lost
  START response cleanup. Final Ruff/Pyright pass with zero errors.
- Final frontend TypeScript/Vite build passed. Camera browser fixtures:20 passed;
  existing desktop HEVC browser regressions:2 passed (isolated port5196).
- `playwright-cli` headless Edge verified visible disabled-runtime error against
  mock API. Initial CLI navigation before installing mocks produced expected local
  proxy errors; no production browser session or real camera was used.
- Scoped read-only secret scan of changed/new review files found only the existing
  `private-fixture-password` test assertion; reviewed as synthetic. No real credential
  introduced. `git diff --check` passed.
- No real firmware/MQTT, decoded-camera video or physical STOP/release acceptance.
- Standard browser port5182 was occupied; use isolated `MB_PLAYWRIGHT_PORT=5194`.

Commands from component directories:
`uv run pytest -q`; `uv run ruff check --fix app tests/test_camera_admission.py`;
`uv run ruff format app tests/test_camera_admission.py`; `uv run pyright app`;
`uv run pytest tests/test_camera_admission.py tests/test_camera_video.py -q`;
`npm test`; `npm run build`; `npm run test:e2e -- --workers=1 e2e/cameras.spec.ts`.

Known environment observations: npm ci reports7 dependency advisories on the pinned
lockfile; no dependency upgrade was included. Vite reports the existing large vendor
chunk. Local worktree and evidence are retained for review; no temporary remote
resources or test credentials were created.

The temporary CLI mock script, preview process and browser session were removed.
Multiple verified discovery candidates fail closed rather than guessing a physical
selection; the current UI needs a unique verified candidate for this single-source
camera path. Real producer release and IoT141 durable-consumption concurrency remain
upstream/runtime acceptance obligations, not claims made by BFF mocks.
