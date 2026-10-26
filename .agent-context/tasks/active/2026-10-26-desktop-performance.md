# Desktop performance and visible receiver telemetry

Measurement complete and patch ready for review. Canonical result:
[desktop performance handoff](../completed/2026-10-26-desktop-performance.md).
Production deployment and native timing optimization are not claimed.

## Task intake
- Goal: distinguish RTP loss, decode and presentation on terminal773; compare DISPLAY1 and DISPLAY2, then diagnostic A/B/A90s and10minute soak.
- Scope: MenuBuilder frontend receiver observation; read-only native source analysis and owner-visible Chrome experiments. No MQTT/native/lease policy changes.
- Owners: l4capture produces capture/encoding; Janus transports; MenuBuilder receives/displays. No migration.
- Producer → transport → consumer: desktop capture → RTP/TLS/Janus → WebRTC decode → browser compositor.
- Invariants: existing tenant/access/ownership/session epoch/TTL and STOP remain; only initiator can change quality. Diagnostics never adapt quality automatically.
- Risks: source/receiver share CPU/GPU; changing workload invalidates comparisons; rVFC observes submitted presentation, not physical display or native compositor interval.
- Sources: menubuilder card, video-streaming contract, frontend remote-control safety and performance risk skills; janusClient, VideoPlayerScreen; separate l4tools quality57/56 docs.
- Validation: interval/reset/missing counter tests, frontend suite/build, browser checks; actual native trace + getStats/rVFC, version/source/CPU/GPU recording, warmup exclusions, both displays. No backend checks for frontend-only changes.

## Progress
- Base origin/main83b0124, isolated branch feat/desktop-video-observability. Earlier recovery application is already deployed; this is a new measurement task.
- User approved90s A/B/A plus10minute soak and both displays. OnBeginFrameThrottleVideo flag is diagnostic only; no500ms jitter or camera15FPS preset.
- Product overlay implemented; verification and live experiments pending. Raw private experiment files remain ignored.

## Performance risks
| Component | Risk/trigger | Impact | Mitigation | Evidence |
| --- | --- | --- | --- | --- |
| Receiver polling | overlapping getStats under load | measurement overhead | single in-flight poll,1Hz, cleanup/epoch fencing | tests/build/live pending |
| Presentation | decoded frames mistaken for chosen frames | incorrect optimization | separate RTP/decode/rVFC/element counters; native trace | pending |
| Shared host | builds/desktop changes during timed run | invalid A/B | finish builds before runs, record CPU/GPU | pending |
| Lease/control | diagnostics changing source without ownership | critical | observation-only overlay; normal UI START/STOP | code; runtime pending |
