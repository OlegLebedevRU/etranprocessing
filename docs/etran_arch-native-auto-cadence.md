# Native Auto FPS observation — proposed extension

Revision `camera-cadence/0.1.0-proposed`; parser/firmware review required. No runtime
writer or reader advertises this revision yet. Frozen HEVC2.0 and
siplite-camera/1.0.0 bytes and integer limits remain unchanged.

Native configuration is size="", fps=0. These are configuration sentinels, never
the actual descriptor. Prefer H.265 before START; never rewrite original RTP
timestamps or configure fixed camera FPS merely to satisfy this observation rule.

## Proposed integer-rate meaning

Count only complete, validated VCL access units in decoding/presentation order,
with no B frames, matching Main8/420 SPS/PPS/VPS, and original90kHz RTP timestamps.
Packet arrival time, SDP timing, markers without full AU validation and config FPS
are not evidence. Extend32bit RTP timestamp wrap using consecutive positive deltas
strictly below2^31. A reset, ambiguous wrap, missing/corrupt AU or nonmonotonic time
invalidates the observation rather than synthesizing a frame/timestamp.

For integer candidate F and AU index i starting at0, let D_i be the extended
timestamp minus the first timestamp. Require, for every AU:

`abs(F * D_i - 90000 * i) <= 45000 + F`.

This is an explicit proposed half-frame cumulative phase envelope plus one90kHz
tick for timestamp quantization. Half a frame makes a frame closer to its assigned
integer slot than to an adjacent slot; the extra tick handles integer timestamp
quantization. It admits bounded cadence jitter but not arbitrary rounded means.
It is a policy choice for review, not a claim that RTP specifies this tolerance.
Phase is anchored to the first validated AU; it is not reset on each GOP or window.
Consequently sustained rate drift eventually fails even if each short window
looks close. No finite startup window proves infinite future constancy.

Candidates are F=1..30, capped at20 if width>1920 OR height>1080. Existing raster,
level/profile, no-B and other negotiated constraints still apply. Admission needs
at least4 RTP seconds, two complete validated GOPs, repeated parameter sets at
their IDR boundaries, and exactly one surviving integer candidate. The two complete
GOPs need a third boundary to prove completion. Maximum acquisition window10 RTP
seconds and a10second local monotonic acquisition budget: otherwise unavailable.
Startup may wait for a complete IDR/PS boundary; pre-boundary AUs never enter the
observation. Time spent waiting counts toward the acquisition budget.

Continue checking every AU against the original anchor after START. Geometry,
parameter sets, codec, cadence or GOP/PS changes revoke the immutable descriptor
and end that session. No in-place FPS correction or codec fallback. The existing
keyframe interval limit still applies independently; two GOPs do not waive it.

Use checked64bit integer arithmetic. For32bit RTP unwrap, compare every delta;
do not subtract unsigned timestamps without validating wrap. Before a multiply,
index increment or timestamp accumulation would exceed signed64bit, fail closed
and require a new observation/session. Maintain only31 candidate flags, AU index,
extended timestamp/anchor and the existing bounded GOP/PS parser state. No unbounded
timestamp history is required at runtime.

## Negotiation and conformance

This adds meaning not established by the existing schema. Proposed negotiation:
camera source/relay capabilities explicitly list `camera-cadence/0.1.0-proposed`;
the eventual accepted revision must be pinned in a separately negotiated camera
descriptor extension and agreed by IoT, MB and media before writers enable it.
Do not insert extra properties into frozen strict schemas. Without agreement keep
the current constant-lattice parser and report observation unavailable.

`contracts/camera-cadence-proposal/verify.py` produces timestamp-only synthetic
vectors for exact20, bounded20 cadence, short prefix, out-of-envelope drift,
geometry-exceeding25 and duplicate timestamps. This is a cadence oracle, not a
VCL/GOP parser or live admission proof. Original diagnostic prefix has222 intervals,
998999 ticks, mean20.000020 and phase[-0.334,0] frames. That prefix fits the proposed
phase envelope but contains header/marker evidence only; observed_track must stay
null until full validated AU/GOP/PS evidence and negotiated support exist.

Parser handoff: implement the accepted rule as a separate negotiated mode, retain
original-RTP and no-camera-write behavior, test continuous revocation/overflow,
then independently inspect original live RTP. PC observation does not establish
terminal port2 behavior. No deployment or firmware acceptance follows from this
proposal's publication.
