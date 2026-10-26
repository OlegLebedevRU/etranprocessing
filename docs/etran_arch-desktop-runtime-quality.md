# Desktop runtime quality: one publisher channel

This optional extension leaves existing START/media descriptors and agent
admission unchanged. Old agents keep ordinary streaming. The implementation
currently supports the overlapping NV12/HEVC path; AVC and unsupported drivers
remain usable without runtime adaptation. No migration is required.

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
`lease_id`, `stream_instance_id`, `writer_key`, `target_bitrate_kbps` only from an
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

## Policy and measurements

Only the original publisher runs the browser policy, once per second with no
overlapping requests. Transport queue pressure, over 200 ms of accumulated track
delivery delay or over 2% RTP packet loss in a fresh sample cuts the current target
by 25%. Track delay compares getSynchronizationSources() delivery timestamps with
the source's 90 kHz RTP progression, relative to the best observed baseline. It
handles RTP wraps and SSRC changes. It is separate from packet loss; video element
mediaTime is unsuitable because Chrome can rebase it while video remains queued.
After five clear seconds with fresh
transport measurements and decoded frames, recovery adds 10% of the selected
profile ceiling. The minimum actuator request is 100 kbit/s; the maximum is the
original profile ceiling (low 2000/default 4000). A driver refusal disables this
optional adaptation instead of stopping the stream. These policy coefficients
still require constrained-link acceptance; they are not hardware capabilities.

Packet loss uses deltas of `packetsLost`/`packetsReceived`. Decoder or compositor
drops do not count as network loss. Delivery kbit/s measures completed tunnel
plaintext, not spare channel capacity; a static desktop's low traffic must not
lower its target. Missing/stale measurements do not authorize recovery. The UI
shows a short notice when a fresh report confirms a temporarily reduced target.
The diagnostic statistics overlay is not included in the product.

## Native work and latency limits

One worker owns encoder calls, while capture/conversion/control continue on the
main thread. There are two owned NV12 slots: processing and replaceable latest
pending. Pending older than one source period is discarded; a needed IDR request
survives replacement/expiry. There is one borrowed completed AU and no growing
encoded-frame queue. Main retains RTP, IPC and the final lease/session send gate.
Allocation/thread failure falls back to the existing synchronous path.

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
