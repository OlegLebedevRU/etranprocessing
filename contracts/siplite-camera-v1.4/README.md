# Camera observation / media-v3 adapter 1.4.0 proposal

Owner packet only: no runtime advertisement, deployment or hardware acceptance
is authorized by this packet. Camera RPC/config/local remain1.0.0.

The observation and capabilities shape is identical to frozen1.3. Only identity,
title and exact camera/cadence/enrollment constants change. Common media3 track
and quality options remain external references. Frozen1.3 and older packets,
common media3 and CTLv1 admission0.2 bytes are retained unchanged.

Exact camera HTTP selection (all five required, mixed selections fail closed):

- X-L4-Media-Contract: l4-media/3.0.0
- X-L4-Camera-Contract: siplite-camera/1.4.0
- X-L4-Camera-Cadence: camera-cadence/0.3.0-proposed
- X-L4-Camera-Enrollment: camera-enrollment/0.5.0-proposed
- X-L4-Camera-Admission: camera-ctl-monotonic/0.2.0-proposed

BFF to MEDIA sends only the common media header and service authentication.
Camera authority revisions remain outside the common descriptor. Revision
changes require fresh full enrollment/readiness, never reinterpret old offers.
Boot/issuer/floor, physical binding, R/I and lease deadlines remain unchanged.

Normative cadence0.3 and enrollment0.5 are owned by IoT in its corresponding
versioned packets. Cadence0.3 uses original RTP timestamps and unique integer
nominal F1..120. Every overlapping4s window, and every overlapping32s window
once available, satisfies the checked-integer count rule
`100*abs(90000*N-F*D) <= 2*F*D + 9000000`, where D is the actual RTP span and N
the AU interval count. This is2% plus one AU count quantum, not a larger FPS
percentage or a rounded mean. Latest predecessor at/before T-span defines each
window. First32s checking has no new grace and does not delay READY.

Acquisition retains original10s deadline, at least4s and two complete GOPs,
unique-only candidate intersection and first eligible complete-GOP boundary.
Eliminated candidates never return. F and maximum completed acquisition GOP G0
freeze at READY. Common keyframe_interval_ms is ceil(G0/90), an acquisition
reference rather than exact repeated duration or guaranteed maximum. Earlier
positive fully validated IDR+PS entries remain valid; later spans remain bounded
by105% G0 and five seconds. Full AU/no-B/repeated PS and AU interval checks stay
mandatory. Bounded4096-timestamp history uses32KiB PSRAM; missing required
history or overflow fails closed. Canonical owner oracle defines edge arithmetic.

Native Auto size="", fps=0 and bitrate=0 remain camera configuration. Common
track.fps is confirmed nominal policy output, not measured rational FPS. RTP
timestamps and camera settings must not be rewritten to pass admission. No
desktop20/30FPS geometry budgets are camera limits. No telemetry/hint fields
are added. Medium/HD remain low/default policy choices; both may use one
confirmed native format. ACK, fresh RTP and browser decode are separate proofs.

Gauge84 media_observations carries this object; legacy camera1.0 actual_track
may remain null. manifest.json pins raw schema/example bytes and common3 SHA.
`python verify.py` performs offline representation/negative-vector checks only.
