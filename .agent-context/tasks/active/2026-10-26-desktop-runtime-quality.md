# Desktop low latency / original initiator quality channel

## Intake

- Owner: reduce accumulated latency, overlap stages, replace stale raw work,
  temporarily reduce picture quality on congestion and recover. Exactly one
  legitimate rate writer: original session initiator. No vendor assumptions.
- Boundaries: l4capture encoder/raw ownership, leo4proxy media queue, existing
  l4desk svc_desk connection, app1 publisher state, MenuBuilder BFF/UI. No DB,
  camera-specific contracts or legacy START admission changes.
- Producer/consumer: initiating browser → tenant BFF → app1 original owner/key/
  lease/epoch checks → existing MQTT → native IPC → encoder completion reports.
  Proxy and viewers supply measurements only, never autonomous rate commands.
- Preserve lease expiry, STOP, exact selected raster/codec, profile ceiling,
  legacy compatibility, source sample lifetime and final send safety gate.
- Native release signing/publication deferred by owner; browser tests authorized.

## Implementation and evidence

See [contract](../../../docs/etran_arch-desktop-runtime-quality.md). The browser
coordinator keeps a per-START random writer key; the backend stores only its
digest. Another tab with the same login cannot acquire that channel. Normal
GET/status/join and repeated START do not reveal or replace it. No takeover or
SU override. Missing optional support leaves video functional. Product UI has
only a short reduced-quality notice, no diagnostic overlay.

Native branch perf/l4capture-desktop-pacing (separate l4tools worktree) contains
owned latest-frame encoding, optional runtime MF bitrate actuator and local
transport measurements/obsolete-AU handling. Capture/desk/proxy x86/x64/default
x86 builds prepared unsigned. Live-focused tests10 pass on both architectures;
new desk binary-pair and proxy feedback isolation tests also pass both.

Private32sec DISPLAY2 actuator A/B/A:321frames, zero RTP gaps/discarded AUs,
10FPS at2000→1000→2000target, encoded1.710→0.877→1.808Mbps. Application completes
about200ms. Installed binaries still previous versions; visible Chrome773 was
restored (100freshframes/10sec). This is actuator proof on one PC, not weak-link
latency acceptance or a universal hardware claim.

App1 branch feat/desktop-low-latency-quality based on f6ea377:15 focused cases
and170 existing video cases passed. New module/control/bridge/tests Pyright clean;
unchanged HTTP router diagnostics remain outside the patch. MenuBuilder36
quality/HEVC backend cases, selected Ruff/format/Pyright, frontend policy/lifecycle
tests and production build pass. No tests of unrelated native suite components.

## Deployment/test status

[MCP Ops Readiness: UNAVAILABLE] no configured Ops tools; SSH fallback used.
Production preflight:load0.39, available1822MiB, root41%. Builder:load0.11,
available2606MiB, root84%/about5GB free. No mutations yet in this feature rollout.
Fresh remote bases:etranprocessing83b0124, app1f6ea377. Keep current Compose
override sets, environment and mounts; roll only app1, MenuBuilder backend and
frontend. No adjacent container/service restart or secret changes.

Owner started root capture-latency-control.ps1; status request
32aab9df053c4b518cbdb7996539d653 succeeded at19:21:32UTC, controllerPID6164.
Fixed operations only, refuses active media; graceful desk restart and
Leo4Proxy service only. Native switch has not been requested. Pinned candidate
manifest is private .scratch/capture-pacing/runtime-candidate.json in native
worktree. Retain best candidate between runs; no routine rollback.

Remaining: commit/review/release apps through standard builder/registry/pull,
preserving overrides; normal STOP773, fixed native switch, fresh original browser
START, actual quality reports/second-tab refusal; displays1/2; constrained peer
bandwidth A/B/A and10min run. Chrome's Network emulation exposes WebRTC/P2P
conditions; validate actual getStats effects rather than assuming HTTP throttling
means WebRTC impairment. Upstream producer path must be assessed separately.
