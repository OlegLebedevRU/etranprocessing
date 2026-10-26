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

## Live follow-up,2026-10-27 local time

The preceding rollout status is historical. app1PR154/e446f6b and MenuBuilder
PR132/6ef662a were deployed, health/env/mounts/ports/neighbors verified. app1PR155
fixes overlapping initial/selected display probes within the original deadline;
194 scoped/1088 builder cases pass, and c86cd558 is now deployed with digest
sha256:5e51bc3abced14d8e83da45e2dc9dbb957872fc1ccf4077da8bc0068e8cfeeff.
MenuBuilder backend and frontend remain6ef662a; do not rebuild unchanged backend.

Native local commit34182bb, unsigned installed capture15E94BB61792864DAC4586F50F513447AD55C0DA39A8138AB7C0D5FADA662FD2,
proxyC04915DFA80ADEFA2CDA142B150807CD1817EF4DD4B1B8E1D16FF1CDEC75528E,
desk0BE3801B0410636CADE93B293187CCF324E5E01F700577FEB23A544213C9F24F.
11 focused tests pass x86/x64. Live fixes preserve the encoded dependency chain
on bitrate changes, reselect fresh raw pixels after a slow driver update, retain
the original256KB TCP window. ControllerPID6164 can accept new fixed-path hash
manifests without restarting its loop. No signing/native publication authorized
until acceptance; no routine rollback. One local UDP send10035 was observed on
each display, but no further RTP transport retry/buffer changes were made.

P2P emulation must enable Network before creating the peer and retain one global
unlimited rule in clear phases. A first ineffective run is excluded. At1Mbps,
old policy receives0.959Mbps with zero RTP loss but32sec playout drift. Chrome
mediaTime rebases and hides it; compare rVFC.now with90kHz RTP progression.
New writer-only feedback uses getSynchronizationSources track delivery/RTP clocks.
Quarter cuts peaked4.1sec; half cuts DISPLAY2 peaked1.88sec,median0.39sec,9.71FPS;
quality restored to2000kbps after about45sec. These are single-PC trial results.

DISPLAY1/3000x2000 exposed a rejected controller result: local RTP loss and a
retained playout offset pinned100kbps even after clearing the artificial limit.
PR133 is still open at3336bfd. Current UNCOMMITTED follow-up removes the invented
2% loss-only rate criterion and permits floor recovery when queue growth stops;
loss remains a separate metric. At the floor, a no-op cut rebases delay reference
instead of retaining an old offset forever. UI notice no longer asserts every
cause is network congestion.13 policy+2 lifecycle cases pass; local production
build session95871 is running. Validate this version before merging/releasing.

Only the original Chrome773 tab is now reused. Three task-created study=latency
tabs were closed after normal STOP; all original tabs remain. Browser9335,
Chrome155.0.8059.85. Source fixture owned target307E8C761CBB3D75966B7CA58DFC7687
currently fullscreen DISPLAY1,viewer DISPLAY2. Dedicated private harness in
desktop-video-observability/MenuBuilder/frontend/.pytest_tmp/desktop-performance-native:
latency-study.mjs DISPLAY1|DISPLAY2 90 1000 <label>. LATENCY_LOCAL_DIST points to
this worktree's frontend/dist for pre-release UI trials. It requires fresh START
and a new stream UUID; uses real deployed APIs. Normal STOP via stop-owned-desktop.mjs.
latency-source.mjs moves fixture/viewer between monitors. Do not run old study-medium
harness: it routes an obsolete frontend dist. Weak=0 and seconds600 runs the soak.

Remaining: validate queue-only/floor fix on both displays; commit/push it to133,
update PR description, merge and build ONLY menubuilder-frontend on standard
builder launcher --component menubuilder-frontend --build-only. Deploy immutable
artifact with existing task wrapper, preserving static permissions and all
container IDs; app1 is already current. Then10min actual-production-assets soak,
fresh original publisher, wrong-writer403 gate, trace/CPU/GPU analysis and cleanup.
Host metrics sampler97612 started around00:03 for22min; may need extending for soak.
Native private host-latency.jsonl records global CPU/GPU and capture resources.
Private wrapper remote tools directory must be removed after use; retain candidate
JSON manifests referenced by Compose labels. Exit controller retains installed EXEs.
