# siplite camera contract implementation intake and handoff

## Task intake

- Scope: TASK-00 of the user-authorized siplite camera implementation. This
  increment changes contracts/docs/offline oracles only. No product runtime,
  migrations, camera writes, GPIO changes or server deployment in this increment.
- Owners: IoT RPC/gauge; OpenIPC firmware/board API; siplite device/local API;
  l4media service admission; MenuBuilder BFF/UI. No DB migration planned here.
- Flow: camera RTP/RTSP → siplite → TLS L4RTP/1 → l4media/Janus → browser;
  BFF → existing IoT RPC → camera controller; local tool → authenticated same controller.
- Invariants: port2 only; no transcoding; one publisher/track; immutable epoch;
  explicit codec honored; only Auto before START can resolve H.265→H.264;
  /res+gauge84 with no new event types/STM32 commands; secret write-only input.
- User explicitly confirmed main_app MQTT type and approved phased push/deploy.
- Minimal context: AGENTS, repo-intake-and-routing, cross-stack-contract-audit,
  l4media card, video-streaming contract, supplied camera/server contract,
  frozen hevc-v2 and the current IoT camera schemas/registries.
- Validation: offline machine schemas/fixtures, old vectors, desktop definitions,
  unknown-field mutations, Auto policy, encryption roundtrip/tamper, pins/links.
  No MenuBuilder/ProcessingBackend code changed, so their tests/builds are N/A
  for this increment under the documentation/contract scope rule.

## Delivered behavior and compatibility

`contracts/siplite-camera-v1` defines negotiated camera1.0.0 independently of
frozen2.0.0. Honest native2592×1944/Main8/420/level153 is representable; runtime
admission remains gated by live format verification. Desktop tracks and all old
fixtures are unchanged. OpenAPI is a proposed extension projection, not a
replacement for the currently deployed ingress document.

RPC165/166/167 are registered upstream for discovery/connection/illumination.
Stable local v1 API exposes the same controller and remains useful offline.
Cloud credentials are sealed to the trusted device RSA certificate before task
storage; cryptographic and local schemas are mirrored byte-identically in IoT.

## Evidence

Local evidence: D:/.codex/siplite-hevc/evidence/task00/. Runtime probes performed
against the authorized camera: eight read-only HTTP routes returned200;
schema118774 bytes,415 descriptors; authenticated RTSP/TCP delivered30 RTP packets.
Current sources reports1920×1080@20 HEVC; archived native SPS is2592×1944 level153.
No POST, GPIO drive, /night action, power-cycle or firmware update was performed.
RTP packet receipt is not browser decode or siplite readiness.

Package verification output and source/package hashes are recorded with the
published increment. No private key or plaintext camera credential is stored in
this handoff or its contract fixtures; synthetic values are identified as such.

## Next gates

TASK-10 must verify board mapping and native/companion API. White/global-enable
writer and PWM cannot be claimed from schema names alone. TASK-20 implements and
deploys negotiated l4media readers; then IoT/BFF before firmware writers. The
fresh camera schema exceeds32KiB; PSRAM/parser/deadline work remains siplite TASK-60.
Real siplite4623/browser/GPIO acceptance remains NOT RUN.

The original user worktrees were preserved. Isolated worktrees and temporary
offline tools remain under D:/.codex/siplite-hevc; read-only RTSP socket closed.
The l4media context card and docs index were updated for this contract.
