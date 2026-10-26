# Desktop773: receiver telemetry and performance study

Status: frontend patch verified and ready for review/deployment; measurements
complete. No product performance gain or native optimization release is claimed.

## Task intake and scope

- Owner: MenuBuilder frontend owns receiver observation; l4capture owns capture,
  conversion, encoding and pacing. Janus transports encoded media.
- Base: origin/main83b0124ff8fb125c6cc51bc863a738ca8e23c433, still current at final
  remote check. Branch feat/desktop-video-observability, isolated worktree.
- Authorized: normal browser773 START/STOP, both displays,90s A/B/A after warmup,
 10minute long run, visible overlay, read-only source/server access.
- Applied: repo-intake-and-routing, frontend-remote-control-safety,
  performance-and-resilience-risk-analysis; menubuilder/video contract cards.
- Unchanged: tenant/permissions/ownership/lease TTL/epoch, media contracts,
  source quality defaults, jitterBufferTarget, native binaries and MQTT clients.
  Observation never sends input or changes quality. Adaptation remains an
  initiator-owned operation; no automatic adaptation was added.

## Patch

`janusClient.ts` now exposes optional raw video counters and codec metadata.
`videoTelemetry.ts` computes interval rates and separates RTP loss, inbound-video
drops and video-element drops. Identity/counter/clock resets and hidden tabs
rebaseline; missing values stay missing. Loss estimates can decrease, so negative
loss deltas are preserved. A failed/hung stats request cannot interrupt playback.

`VideoTelemetryOverlay.tsx` displays source/raster/codec, decoded and rVFC FPS,
Mbps, loss/drop/freeze deltas, mean decode time and jitter residence. It works for
operator and viewer, can collapse, and stops polling/callbacks with the player.
Native trace and rVFC are diagnostic collection, not production per-frame logging.

Interpretation follows [WebRTC Stats](https://www.w3.org/TR/webrtc-stats/):
rates use counter deltas divided by elapsed time; mean decode is
ΔtotalDecodeTime/ΔframesDecoded, mean jitter residence is
ΔjitterBufferDelay/ΔjitterBufferEmittedCount. Inbound framesDropped and video
element droppedVideoFrames are separate observations, neither proves RTP loss.
[rVFC](https://wicg.github.io/video-rvfc/) observes frames submitted for
presentation; it does not prove physical display or unique desktop updates.

## Environment and method

Window UTC12:22–12:47:36 on2026-10-26; final smoke followed. Chrome155.0.8059.85,
Intel Iris Xe driver30.0.100.9864, Windows10.0.19045, i7-1165G7/8 logical CPUs.
Same separate persistent Chrome profile and window, manual normal authentication.
Only B used `--disable-features=OnBeginFrameThrottleVideo`; A2 removed it and the
process command line was independently checked. Normal user Chrome stayed open.

Each display: A1→B→A2,15s warmup plus90s measured. Measurement used1Hz getStats,
bounded rVFC metadata,20s native trace per run, CPU/process/GPU samples at roughly
6s actual intervals. Long: DISPLAY1,600s, default Chrome, plus another20s trace
around minute5. Trace windows do not cover every long-run freeze.

Both sources used negotiated v3 H265 Main/Main Tier/8-bit420/no B frames, HD
(`default`), nominal15FPS, native raster. Source log confirmed target/peak
4000/6000kbps, DXGI/MF backends. DISPLAY1=3000×2000/level150;
DISPLAY2=1920×1080/level120. Source targets are not achieved-rate promises.
Installed capture implementation SHA256:
`922e6e044acc0b38fb8735b0ba0655dd87b983f60581d6af6ca19175d5bec3cd`.
Read native tree was cac38a37f2a48d9a616390b9c479ec32839df016; code correspondence
to an installed binary is not inferred from a file version or release-directory name.

Important workload limitation: receiver window at screenX/Y10/10,
1037×826 CSS pixels/DPR2 was captured by DISPLAY1, creating visual feedback.
DISPLAY2 had different desktop content. Therefore these are within-display
diagnostic flag comparisons, not a controlled raster/scene benchmark. No build or
unit/browser test ran during the timed comparisons or600s run. Ambient CPU still
varied; no statistically established flag speedup is claimed.

## Results

FPS columns are decode/presentation. Loss is the signed RTP estimate delta;
video drop is the video-element delta. CPU is sampled whole-host mean.

| Display / phase | FPS decode / presentation | Mbps | RTP lost | Video drop | Freeze Δ | CPU % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 A1 | 8.51 / 8.47 | 1.72 | 26 | 2 | 1 | 31.6 |
| 1 B | 7.47 / 7.44 | 1.65 | 14 | 0 | 2 | 42.7 |
| 1 A2 | 7.59 / 7.56 | 1.67 | 0 | 4 | 2 | 43.2 |
| 2 A1 | 12.50 / 12.50 | 1.88 | 0 | 0 | 0 | 29.7 |
| 2 B | 12.44 / 12.36 | 1.82 | 2 | 7 | 1 | 28.7 |
| 2 A2 | 12.47 / 12.47 | 1.84 | 0 | 0 | 0 | 27.7 |
| 1 long600s | 7.25 / 7.18 | 1.69 | 13 | 42 | 32 | 40.1 |

Inbound-video framesDropped delta was0 in every run. In the long run4350 decoded
frames, mean decode1.173ms, mean jitter residence83.27ms; presentation interval
p95283.32ms and max1466.53ms. The packetsLost estimate was0 at intermediate
checks, then increased to13; intermediate zeroes are not the final result.

Long CPU p9586%/max100%; capture process mean6.90% and all Chrome processes1.99%,
normalized across8 logical CPUs. Maximum individual GPU engine utilization mean:
VideoDecode13.18%,3D13.99%; these are not total GPU utilization. VideoEncode engine
was absent from counters and remains unavailable, not0%. Hardware receive decode
was independently observed through D3DVideoDecoder native trace events.

## Frame correlation and conclusions

Native joins use rVFC RTP timestamp→RTCVideoDecoderAdapter::Decode and media
timestamp→WebMediaPlayerMSCompositor::EnqueueFrame→SetCurrentFrame. User timing
marks align browser receiveTime with the trace clock for newer runs.

- Median VideoFrameSubmitter::OnBeginFrame was16.662–16.670ms on both displays,
  with/without the flag. There was no steady50ms cadence in this desktop test.
  Isolated scheduling stalls reached roughly133ms; medians do not hide them.
- Long initial trace matched178 complete decode/enqueue/selection chains:
  receive→decode mean100.04ms, decode→enqueue1.312ms, enqueue→selection9.48ms.
  The later trace matched172 chains with16.668ms median BeginFrame and no
  unselected interior queued frames. Those windows had no evidence of a sustained
  compositor throughput limit; they do not explain all32 long-run freezes.
- B/DISPLAY2 contained4 interior queued frames never selected. Their source PTS
  gaps were88/91/81/80ms, but queue gaps were317.165/0.263/0.349/1.284ms. Enqueue
  was299.872/209.135/128.484/49.768ms after ideal render time: late frames arrived
  in a burst and were not all chosen. A2/DISPLAY1 had1 unselected late frame.
  This proves a presentation omission independently of network loss counters;
  it does not identify a single cause for every burst.
- Flag B did not produce a repeatable benefit. Leave the normal browser launch
  unchanged. No500ms jitter target or camera15FPS preset was transferred.
- Source-side timing is a next investigation target. Native SOAK samples during
  the long run showed encode p95 between47 and172ms, source3–10FPS in those
  sampled seconds and raw/encoder/transport drop counters0. Decoding around1ms
  cannot account for those slow producer intervals.
- Static code uses GetTickCount64 for pacing/PTS and Sleep(5) in the capture loop.
  Source timestamp gaps frequently cluster around15–16ms multiples. Microsoft
  documents approximately10–16ms resolution for
  [GetTickCount64](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-gettickcount64)
  and timer-resolution dependence of
  [Sleep](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleep).
  Coarse timing is a hypothesis, not a measured speedup or justification for
  changing shared lease/watchdog clocks. A native candidate must isolate media
  pacing/PTS from deadline clocks and measure capture/conversion/encode/wait on
  reproducible scenes before installation or preset changes.

## Verification and cleanup

- [x] Final build passed;133/133 unit tests;7/7 relevant browser tests including
  overlay missing values/collapse and slow/cancelled START/refusal flows.
- [x] Both displays real H265 decode;6 A/B/A runs;600s long; final built-source
  DISPLAY2 smoke with visible overlay. No native executable was changed.
- [x] Long and final smoke: STOP200 and lease DELETE204 confirmed. Two idempotent
  STOP responses were observed during each final cleanup; this existing lifecycle
  behavior was recorded rather than changed by the telemetry patch.
- [x] Profile logged out normally, site storage/cookies cleared, study browser
  closed and temporary profile removed after validating its absolute workspace
  path. Normal user Chrome left intact; diagnostic flag is not installed.
- [x] Sampler data covers the entire long run. It later exited on a file-sharing
  conflict during post-run reading; persistent shared writer fixed and smoke-tested.
- [x] Read-only changed-file secret scan had no findings; final scan is repeated
  after report/context edits. Python/backend checks N/A: no backend/Python changes.
- [ ] Production deployment of this frontend patch: intentionally pending the
  user's requested deployment step. Browser used the local production build with
  real portal API; this is not evidence the server already serves the overlay.
- [ ] Native timing optimization, independent source workload benchmark and
  causal attribution of every long-run freeze remain unverified.

Private evidence is ignored under frontend/.pytest_tmp/desktop-performance:
raw samples/traces, late trace, host.jsonl, source-soak-final.txt, binary hashes,
summary.json and overlay screenshot. Published report contains aggregates only;
no PIN/JWT/credentials/private input. The temporary study profile was removed.

## Contract and risk handoff

| Contract / component | Producer → consumer | Compatibility / risk |
| --- | --- | --- |
| Local receive stats | Janus client → UI overlay | optional new fields; old framesDecoded API retained |
| Lease/media contracts | BFF/native/Janus → existing UI | unchanged; viewer observation sends no commands |
| Sampling | receiver counters/rVFC → overlay |1Hz, single in-flight read, cleanup/reset/stale handling |
| Trace inference | Chrome native events → study summary | bounded windows; no physical-display guarantee |
| Native timing | l4capture clock/loop → encoded source | hypothesis only; do not change deadline basis casually |

Next owner: frontend reviewer/deployer for the overlay; native capture owner for
stage-level profiling and a separately verified pacing candidate. Existing
quality/raster/codec policy and initiator ownership remain authoritative.
