# Camera media-v3 preparation

## Task intake
- Scope: l4media ingress only; no MenuBuilder, IoT, Windows binaries or firmware.
- Contract owner: etranprocessing common l4-media/3.0.0. Frozen schema unchanged.
- Producer -> transport -> consumer: camera/siplite original RTP/RTCP -> existing
  L4RTP/1 TLS ingress -> Janus/browser. Camera CTL/enrollment remains with IoT.
- Invariants: original TTL, immutable source/stream/lease/descriptor, no stale
  camera connection adoption, unchanged desktop/USB transport reuse.
- Validation: warning-clean Linux C suite, deterministic clock/restore checks,
  real socket framing/generation checks and isolated Redis roundtrip.
- Not authorized: deployment, gates, allowlist, writers or device installation.

## Version selection
BFF-to-MEDIA uses only `X-L4-Media-Contract: l4-media/3.0.0` and existing service
authentication. Camera-specific selectors remain on BFF-to-IoT. Mixed selectors
remain an explicit rejection. Common descriptors/tracks/media envelopes retain
contract_version3. They do not contain camera/cadence/enrollment fields.

The existing source-symmetric reader admits the same concrete confirmed camera
format for low/default; it does not impose desktop FPS/bitrate budgets or rewrite
camera settings, SPS or RTP timestamps. Tests exercise H265Main8/4202592x1944@20,
level153, exact Janus fmtp and both policies with no new media pipeline.

## Active deadlines
V3 START captures the remaining issuer deadline once using one precise
CLOCK_REALTIME receipt sample and the local monotonic clock. Receipt UTC rounds
up to milliseconds; monotonic is sampled before UTC so neither truncated
subseconds nor sampling delay can extend the absolute deadline. Existing-session
START replays never recapture a timer.
IP-camera renewals map newer absolute expiry into the same original issuer
coordinate; unchanged/older expiry cannot lengthen it. Desktop/USB instead keep
their existing live-UTC expiry protocol: each different expiry maps its precise
remaining receipt interval into a new monotonic deadline, including a smaller
absolute expiry after UTC rollback. Exact expiry replay is idempotent for every
source. The old monotonic lease must still be live after processing; expiry
cannot be revived. Queue/network/setup residence consumes the budget, and equality
means expiry. The existing maximum admission interval remains7200 seconds.

The active watchdog, per-packet gate, transport idle timeout and RTP-freshness
calculation use monotonic time for v3. Diagnostic wall timestamps remain UTC.
Clock failures/regression fail closed. Redis stores the absolute monotonic
anchor and Linux boot UUID alongside the private session record. Restore may
reuse that deadline on the same kernel boot and unchanged monotonic clock namespace; absent/foreign/expired anchors
restore a stopped session without an active route. There is no fresh TTL after
restart. V1/v2 deadline semantics are retained.

The IP-camera absolute lease deadline belongs to a fixed issuer coordinate during
one attempt. A changed issuer time origin is an authority/attempt change, not an
implicit request for another full TTL. This does not make arbitrary independent
wall clocks interchangeable at initial media admission.

## Camera transport generation
A new v3 IP-camera route revokes already connected transports for that SN before
route publication. Newly connected transports bind to the prepared camera route
generation at their L4RTP preamble. A per-route connection-epoch floor also
rejects pre-prepare sockets whose SN preamble arrived late, before they can
displace the current publisher. Retargeting cannot transfer a bound/revoked
camera connection to another route. Packet and reverse-feedback paths check the
binding, and the event loop closes revoked connections safely. Desktop/USB
transport reuse keeps its existing behavior.

L4RTP/1 still carries SN rather than stream_instance_id. A new authenticated
same-SN connection after prepare is trusted to the terminal's MQTT/mTLS control
adapter. Generation fencing does not add a wire-level stream identity or claim
protection from a malicious holder of that terminal certificate. Firmware must
stop/quiesce the old sink and create one fresh TLS connection per admitted
attempt; no automatic reconnect inside an attempt.

## Acceptance boundary
Offline C/mocked Janus/isolated Redis results are distinct from real Janus RTP,
terminal hardware or browser decode. Existing deployed common-media capabilities
are a readiness observation, not camera-v3 acceptance. Deployment and scoped
live acceptance require the coordinator's subsequent instruction.
