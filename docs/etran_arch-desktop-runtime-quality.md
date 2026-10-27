# Desktop runtime quality: one publisher channel

The deployed API supports NV12/HEVC, I420/OpenH264 and optional coded-raster
changes through the same publisher channel. Native and browser policy follow-up
changes still need combined browser acceptance. Existing START schemas
and initial descriptors remain unchanged. Old agents keep ordinary streaming
without mandatory adaptation. No migration is required.

## Authority and lifecycle

Only the original desktop START initiator may request a bitrate change. The
browser generates a random UUID writer key for that START and keeps it in the
publisher coordinator's memory. BFF forwards it only to the app1 producer START
as `X-Video-Quality-Writer`; it never enters media-v3 or MQTT START envelopes.
app1 stores only its SHA-256 digest in the original publisher record. Duplicate
START/read/join cannot replace it. Neither status nor observers receive it.

Runtime POST requires this key plus the exact original tenant, user, login
session, active publisher lease and stream instance. This distinguishes tabs
sharing one login. There is no superuser override or automatic takeover by an
observer. Losing the original browser context leaves the current rate installed;
a new legitimate START establishes a new writer. Old STARTs without a writer key
do not gain a quality channel retroactively.

`GET /api/v1/video/devices/{device_id}/stream/quality?stream_instance_id=...`
returns measurements to authorized viewers. POST to the same path accepts
`lease_id`, `stream_instance_id`, `writer_key`, `target_bitrate_kbps` and optional
`raster_step` only from an
operator; app1 performs the authoritative ownership checks. BFF uses the existing
trusted caller headers and does not replace their session identity.

app1 reserves a monotonic generation in the existing Redis video transaction,
watching the active lease too. MQTT `stream_quality_set` goes through the existing
publisher to `srv/{SN}/ctl`, QoS1/no retain, with command UUID, lease/stream IDs,
generation, requested bitrate and a five-second expiry clipped to the lease.
The writer key never goes to the terminal. Native code uses the active issuer
clock/lease anchor, exact epoch, generation and private capture IPC. A quality
command does not renew a lease. Existing STOP/revoke/expiry checks remain.

The agent sends `stream_quality_report` on `dev/{SN}/ctl`, QoS1/no retain, using
its existing connection. Reports bind SN/lease/stream/agent boot and increasing
sample sequence. A future command generation or another boot/epoch is rejected.
An MQTT ACK means accepted, not applied. Only a native completion report confirms
the last applied bitrate; driver refusal and missing confirmation remain explicit.

## Optional raster extension (candidate)

MQTT envelopes remain v1. A source opts in with `quality_version:2` and the complete
report extension: `coded_width`, `coded_height`, `raster_step`, `ceiling_kbps` and
`last_idr_bytes`. POST may then include `raster_step` with the bitrate in one command.
Without this report, raster commands are rejected without affecting ordinary video.
Old bitrate commands omit the new property entirely.

Steps halve both initial coded dimensions, rounding down only to even4:2:0 sizes.
Thus3000×2000→1500×1000→750×500;1920×1080→960×540→480×270. Every sample covers the
whole original source; area filtering removes aliasing. Integer2x/4x reductions
avoid fractional per-pixel weights. Capture/input geometry, source, codec, profile,
level, lease and stream epoch remain fixed. Initial START/READY descriptors stay
immutable snapshots; the separate quality report describes the current coded size.

Rebuild disposes borrowed/pending samples on an asynchronous owner, then creates
the new encoder. It does not assume two concurrent hardware encoders. Failure
recreates the previous configuration. Main continues STOP/renew and keeps the final
send gate; the existing bounded cold-activation budget applies until a new IDR.
Only a complete locally sent new SPS/PPS(+VPS)/IDR confirms the new geometry.
Already delivered receiver backlog is not erased by this source confirmation.

Software H264 readiness requires a real GDI/OpenH264 sequence and actual SPS
profile/level, independently of GPU/MF readiness. The helper component is bound;
GPU metadata is not a software admission requirement. Component hashing is cached
against file identity, size and write/creation times. Pre-HEVC agents need not
understand the optional quality extension.

## Policy and measurements

Only the original publisher runs the browser policy, once per second with no
overlapping requests. Transport queue pressure or over 200 ms of accumulated track
delivery delay halves the current target. RTP loss alone does not prove congestion
and does not issue rate commands or block recovery. Track delay compares
[getSynchronizationSources()](https://www.w3.org/TR/webrtc/#dom-rtcrtpreceiver-getsynchronizationsources)
delivery timestamps with
the source's 90 kHz RTP progression, relative to the best observed baseline. It
handles RTP wraps and SSRC changes. It is separate from packet loss; video element
mediaTime is unsuitable because Chrome can rebase it while video remains queued.
After confirmed bitrate application, the queue-growth reference is rebased: a
fixed residual browser playout offset must not pin quality at its minimum after
the channel clears. At the actuator floor, an unsuccessful further cut also
rebases this reference; continuing growth still prevents recovery.
After five clear seconds with fresh
transport measurements and decoded frames, recovery adds 10% of the selected
profile ceiling. The minimum actuator request is 100 kbit/s; the maximum is the
original profile ceiling (low 2000/default 4000). A parameter refusal disables this
optional adaptation instead of stopping the stream. These policy coefficients
still require constrained-link acceptance; they are not hardware capabilities.
Actual encoder/device failure retains the existing terminal stream failure path.

The candidate uses the source's effective ceiling when lower than the selected
profile. At a quarter bitrate budget per tier, it can halve both dimensions while
keeping comparable bits/pixel. A measured oversized IDR allows the first half-size
step earlier: its serialization at the reduced target would exceed the same200ms
queue budget. This is a nominal-rate estimate after measured pressure, not a
measurement of available link capacity. The existing100kbit/s floor bounds tiers.

Recovery restores the bitrate budget of the next larger raster first:
`effective_ceiling / 4^(step-1)`, bounded by the original profile. It then probes
that raster at unchanged bitrate. For example,750×500@125 first recovers to500
before1500×1000; the next step needs2000 before3000×2000. Increasing pixels fourfold
while retaining the old quarter-rate budget starved the tested encoder during
recovery. No vendor-specific minimum bitrate is inferred from that observation.
Failed sharper probes double the retry interval up to the operator-approved 30-second cooldown; clear delivery never reduces
bitrate merely because a probe is deferred. These choices need A/B/A and long-run
evidence. A resolution confirmation cannot erase a large frame already in transit.

Optional [framesReceived](https://www.w3.org/TR/webrtc-stats/#dom-rtcinboundrtpstreamstats-framesreceived)
counts complete frames before decoding. Continued RTP without complete frames and
without new losses exposes assembly delay before first decode. Missing counters,
idle sources, decoding stalls or element drops alone do not create that signal.

Packet loss uses deltas of `packetsLost`/`packetsReceived`. Decoder or compositor
drops do not count as network loss. Delivery kbit/s measures completed tunnel
plaintext, not spare channel capacity; a static desktop's low traffic must not
lower its target. Missing/stale measurements do not authorize recovery. The UI
shows a short notice when a fresh report confirms a temporarily reduced target.
The diagnostic statistics overlay is not included in the product.

## Native work and latency limits

One worker owns encoder calls, while capture/conversion/control continue on the
main thread. There are two owned NV12/I420 slots: processing and replaceable latest
pending. Pending older than one source period is discarded; a needed IDR request
survives replacement/expiry. There is one borrowed completed AU and no growing
encoded-frame queue. Main retains RTP, IPC and the final lease/session send gate.
Allocation/thread failure falls back to the existing synchronous path.

Async MF NeedInput and HaveOutput are independent. Pending output must not block
new input requested by the transform; MF retains its own copied sample while the
worker releases its raw slot for fresh work. Ready output takes priority and its
own sample PTS is preserved across buffering/skips. HEVC still validates timestamp
range/order and negotiated format. Event credits are counted without assuming a
two-event hardware queue. A temporarily busy rate setter remains pending, with
newer generation/target taking precedence; it is not a confirmed refusal.
[Microsoft's event model](https://learn.microsoft.com/en-us/windows/win32/medfound/asynchronous-mfts)
permits input requests before output. Existing pipeline STOP and watchdogs remain
authoritative; genuine HRESULT failures are not hidden by increasing deadlines.

The optional local RTCP APP extension carries producer rate announcements and
proxy measurements only. It is bound to the current loopback producer/SSRC and
is not accepted from downstream observers. The proxy never selects bitrate.
Obsolete complete AUs are discarded with dependency repair. Pressure alone does
not request another IDR. A fresh IDR already being serialized may finish. A rate
change preserves in-flight dependencies and does not force an IDR. After a slow
driver rate change, the worker selects the newest waiting raw frame instead of
encoding the sample that aged during the change. The existing TCP send window is
retained; shrinking it to16KB manufactured pressure on a healthy WAN. TLS
send failure discards the failed connection's unsent bytes rather than resuming
a partial record as another frame. The local queue budget is 200 ms.

An optional loopback RTP send-clock extension measures actual local transport
age independently of capture/encode time. Proxy strips it before WAN forwarding;
legacy producers retain the previous source-clock path. This avoids calling a
cold encoder's slow first frame a transport backlog. H264 STAP-A is recognized as
keyframe/parameter repair because the existing AVC packetizer emits that format.

This is not an absolute 200 ms capture-to-display guarantee. Relative RTP queue
age cannot prove compositor latency, and large keyframes, the driver and TCP
serialization still impose delay. A weak-link browser run must measure these
separately. No jitterBufferTarget=500 ms, fixed camera cadence or Chrome feature
flag is shipped as a desktop preset.

## Evidence and remaining gates

On the development PC, a private 32-second desktop DISPLAY2 A/B/A changed target
2000→1000→2000 without encoder restart. All windows delivered 10 FPS; measured
encoded throughput was 1.710→0.877→1.808 Mbit/s. The receiver accepted 321 frames,
zero RTP gaps and zero discarded encoded AUs. Native rate completions arrived
about 200 ms after requests. This proves a working actuator on this machine,
not portability or weak-link latency acceptance. The installed browser stream
was restored afterward (100 fresh frames/10 seconds).

Remaining: coordinated app1/BFF/native integration, rejection from a second
authenticated tab, displays 1 and 2, constrained-link reduction/recovery and the
agreed long run. Debug binaries are unsigned; final signing/publication is
deferred until improvement or status quo is confirmed with the owner.

### Software processing follow-up (candidate, 2026-10-27)

Modern H.264 full-field reductions use a reusable separable area workspace. Horizontal overlap spans are cached per geometry; only one horizontal row and one accumulator row are retained. Common weight factors are cancelled without changing rounding. Scalar and optional CPU-checked SSE2 paths match the existing 2D area reference byte for byte; allocation failure uses that reference. No new hardware admission condition is introduced. Legacy profile scaling remains unchanged.

The control thread polls IPC and completed encoded output between capture and scaling, in addition to the normal loop poll. Every send still passes the existing session/STOP/deadline gate; the capture frame is released before an observed STOP exits processing. There is no additional frame queue or autonomous native quality controller.

OpenH264 runtime maximum bitrate updates both the global setting and spatial-layer0. Updating `SPATIAL_LAYER_ALL` alone leaves the initialized `iMaxSpatialBitrate` unchanged and can reject recovery above that value. The focused actual-encoder regression initializes750/1000kbps, drops/recoveries through1003kbps and reaches1500kbps without reinitialization. [OpenH264 option implementation](https://github.com/cisco/openh264/blob/master/codec/encoder/plus/src/welsEncoderExt.cpp).
### Desktop compositor diagnostic (Chrome155.0.8059.85, 2026-10-27)

The separate20-second `raster15-trace` diagnostic observed187 decoder callbacks and187 compositor enqueues,184 selected frames, and184 matching rVFC callbacks linked by both media PTS and decoder-buffer RTP timestamp. Three interior enqueued frames were not selected, with zero RTP-loss and decoder-drop deltas. Decoded-frame selection is therefore distinct from network loss here as well as in the earlier camera investigation.

The video compositor `OnBeginFrame` interval was median16.658ms/p9517.669ms;1112 of1135 intervals were within2ms of16.667ms, none within2ms of50ms. Decode-start to callback was median4.056ms/p957.334ms (ordered trace pairing); callback to enqueue0.289/0.652ms; enqueue to selection8.197/16.437ms. Matched rVFC receive-to-expected-display was135.3/215.1ms. These are separate measurements, not an additive absolute capture-to-display estimate. Individual transport trace events lack frame IDs, so packet-to-decode duration is not claimed from those events alone. rVFC receiveTime supplies the frame-level reception boundary.

Tracing and trace-file transfer can disturb playback. The401 callbacks captured across the whole diagnostic include time outside the20-second native trace; use only the184 timestamp-matched frames for correlation. A4.8-second callback gap occurred outside the native trace window during diagnostic transfer/collection; its cause is not isolated, and it is excluded from performance conclusions. Performance acceptance runs disable tracing, visual-clock screen reads, and the Chrome experimental BeginFrame flag.