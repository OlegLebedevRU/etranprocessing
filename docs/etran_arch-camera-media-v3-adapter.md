## Native nominal candidate1.4 (2026-10-25; preparation only)

Current candidate selects camera1.4/cadence0.3/enrollment0.5 and unchanged media3/
admission0.2. Exact observation packet contracts/siplite-camera-v1.4 retains the
common track shape and old frozen packets. FPS is declared nominal from the
authenticated selected camera SDP, not an achieved-rate limit. No2%/window/
unique-F/GOP105% checks are added by BFF. IoT/firmware own full bitstream and
nominal authority proof; one complete GOP/two verified IDR+PS boundaries and
bounded30s IO acquisition do not grant a new readiness or lease budget.

New validated START stores camera14 marker. Existing server-owned camera13
markers retain their exact old five-header selection for state, STOP, lease
clock and keepalive compensation; no silent renegotiation. Mixed state revisions
or old tickets attempting new admission fail closed. Desktop context unchanged.
No DB, common schema or camera configuration change.

The preceding sections describe the previously deployed1.3 baseline; they are
retained for review context, not fallback authority for a new1.4 attempt.

# Camera adapter for common media-v3

Status: implementation candidate for review, not deployed or hardware accepted.
The existing main_app camera control adapter is distinct from desktop CTL2.
No Windows binding, new pipeline, transcoding or STM32 change is introduced.

## Exact boundary

Camera RPC/config/local API remains siplite-camera/1.0.0. Camera measured
observation and admission HTTP adapter select the exact new bundle:

| Header | Value |
| --- | --- |
| X-L4-Media-Contract | l4-media/3.0.0 |
| X-L4-Camera-Contract | siplite-camera/1.3.0 |
| X-L4-Camera-Cadence | camera-cadence/0.2.0-proposed |
| X-L4-Camera-Enrollment | camera-enrollment/0.4.0-proposed |
| X-L4-Camera-Admission | camera-ctl-monotonic/0.2.0-proposed |

This bundle belongs to BFF→IoT inventory, prepare/enrollment/readiness and
stream lifecycle requests. BFF→MEDIA sends ONLY the common media header plus
existing service authentication. Existing /api/v2/media paths carry envelopes3.
No old camera selector is accepted as a v3 fallback. Frozen camera1.0/1.1/1.2 and common schema files remain unchanged. New observation1.3 retains the1.2 shape and selects the new measured nominal cadence0.2/enrollment0.4 policy; old1.2 constants cannot select those revisions.

Canonical observation packet: contracts/siplite-camera-v1.3/. It externally
references the exact common track and quality_options definition; its manifest
pins common schema SHA b86ca6f066fcdecb09cf7145a7fb23d87b77311a53c7ea04f0fb6e8a905b2818.
No camera track or descriptor fork is maintained. Authority markers stay outside
the strict common descriptor and outside media service requests.

## Admission and quality

After validated native START, the existing internal video-session operation_id
stores `op-stream-v2-camera13-<provider stream UUID>`. The URL-safe marker is
shorter than the128-character column and preserves the existing op-stream-v2
consumer prefix; no suffix UUID parser or DB migration is involved. It is never
inferred from request mode, one source_type value or usb-camera presence alone.
Desktop and ordinary USB sessions retain their existing operation identifiers.
Generic remote-session requests reserve the `op-stream-v2-camera*` namespace
with normal request validation before provider IO, so a client-supplied generic
operation identifier cannot manufacture the internal camera routing marker.

Generic state, mountpoint, lease-less/explicit STOP and keepalive readers use
that server-owned marker and exact provider ID to select the full camera bundle.
The returned state must match SN, lease and captured stream; stale context cannot
target a replacement stream. STOP captures context before closing its local row.
Post-START persistence compensation uses the already validated native result,
since no stored marker may exist yet. IoT shared lease keepalive keeps its normal
owner/generation/ACK path; its subsequent full-state/clock lookup and MEDIA renew
must both complete. MEDIA receives only its common header, never camera headers.
Old pre-marker camera sessions require a fresh explicit attempt, not source-hint
fallback. Existing actor/readiness/lease and expected-provider cleanup fences
remain mandatory. This marker is routing context, not a new authority grant.

Before acquiring a stream lease the BFF checks fresh main_app status, verified
authenticated physical port2/MAC/discovery, independently validated camera1.3
observation/capabilities, actual device geometry and media/browser codec support.
It constructs one concrete descriptor3 and prepares its exact binding in IoT.
A fresh gauge alone is not readiness or proof of post-START producer activity.

The encrypted BFF ticket is scoped to tenant, terminal, user and session, and binds all five exact revision headers. Pre1.3 tickets without that binding or with any old/mixed selector fail closed before provider IO. One
original30s preparation/enrollment wait includes provider I/O and polling; polls
do not extend it. Each wire enrollment exchange retains its independent5s
budget. The readiness ticket uses an original5s deadline anchored before issuing
the query. Device R/I and issuer monotonic admission, durable floor and replay
state remain owned by IoT/firmware. The BFF never gives a delayed START a new TTL.

Camera preparation allocates independent random UUID4 stream_instance_id and
operation_id once, storing them inside that existing encrypted attempt ticket.
Readiness resealing and repeated/concurrent START retain those identifiers; a
new explicit preparation receives new ones. Missing, noncanonical, non-v4 or
colliding identifiers reject before provider IO. An existing running record must
match the ticket's operation/command/stream identity as well as its descriptor.
Existing authoritative lease/owner checks precede route creation, and the IoT
record/readiness fence still prevents crossing lease or admission attempts.

IoT copies operation_id into camera CTLv1 command_id and publisher stream ID into
stream_instance_id unchanged: frozen admission0.2 requires UUID4 for both.
Common MEDIA session_id remains the existing UUID5 derived from stream ID so
health/renew/STOP reconstruct the same identity; it is not on the camera CTL
wire. Desktop UUID behavior and common/frozen schemas remain unchanged. No DB,
worker-local cache, UUID bit conversion or new attempt on retry is introduced.

Medium selects profile low, HD selects default. These are immutable quality
policies, not RPC163 presets. The selected profile must be supported by BOTH
producer observation and projected capabilities. Both may use one native track.
Neither choice writes size/FPS/bitrate, and Native Auto sentinels remain config.
The CTLv1 profile token remains default under frozen admission0.2; the chosen
media quality is bound through the full common descriptor hash, not that token.

Auto prefers H265. Unknown compatibility is a refusal, not evidence to fallback.
Confirmed incompatibility can select an already observed compatible H264 format
before START. An unobserved codec requires separately authorized camera config
or probing; viewing does not grant camera-write authority. Explicit H265 never
becomes H264, and no post-START failure creates a new codec/attempt automatically.

Camera START prepares the media route first, then consumes the exact IoT readiness
nonce. Stable nonce/lease-derived stream identity prevents duplicate allocation.
Cancellation, stale selection, failed/ambiguous START compensate only the same
identity. State keeps command_ack distinct from actual_descriptor, which remains
null until independently proven. RTP freshness and browser decoding are separate.

## Stable camera media clock

The camera-only IoT video-lease response adds media_lease_expires_at (integer
seconds in one fixed attempt coordinate), media_remaining_ms (authoritative
Redis PTTL residual1..60000) and media_clock_id (UUID4). Generic desktop lease
UTC semantics are unchanged. BFF camera START validates the same owner/session,
SN and stream scope and subtracts its complete local monotonic HTTP roundtrip
from the residual availability check; it sends the existing fixed expiry value
unchanged in the common media request. It never maps it using current UTC or
starts a new budget from receipt.

Camera state captures media_clock_id at START. BFF requires the returned state
to retain the originally observed ID. During keepalive it loads that immutable
state ID, re-reads the authoritative camera export and compares IDs before media
renew. Missing/changed clock authority retires/compensates only the exact old
stream/lease/descriptor identity and cannot create another START. Diagnostic
last_rtp_at_ms and the generic keepalive expires_at are not camera lease clocks.
Cross-stack-oriented local tests cover7s/15s residuals, forward/backward UTC
values, duplicate exports, roundtrip equality expiry and lost clock anchors.
They do not claim an actual distributed server clock-jump acceptance.

## UI and rollout

The camera toolbar exposes the same Medium/HD policy choices as the desktop.
Physical camera settings and legacy RPC163 buttons remain separate, gated by
advertised writer capabilities and explicit configuration permission. Preparation
shows its stage, is cancellable before a lease exists, and ignores late replies.
Selection/profile changes cancel that preparation. Authentication middleware must
not replay ambiguous lease or START mutations.

The original30s overall deadline belongs only to camera preparation/start.
Desktop retains the same cancellation and stale-response guard without that
camera deadline: lease acquisition, HTTP START (120s), and Janus signaling keep
their own bounded waits. Time spent acquiring a lease cannot consume the desktop
START allowance. A late response after cancellation still triggers scoped cleanup.
Desktop renew forwards live provider UTC expiry; fixed camera-clock metadata is
never requested for a desktop descriptor.

Default gates camera_monotonic_runtime_enabled and camera_native_auto_enabled
remain false; camera_admission_ticket_key remains empty until owner provisioning.
This source change does not provision them. Exact IoT/MEDIA revisions and packet
hashes must agree before a separate owner-approved MEDIA→IoT→MenuBuilder deploy.
Firmware installation, writers and allowlist activation require the coordinator's
separate stage. Offline fixtures are not live acceptance on terminal4623 or773.

## Cadence0.2 representation

The producer selects a unique nominal integer FPS1..120 from validated original RTP using the external cadence0.2 policy. Common track FPS is not an actual rounded mean. The frozen ceil(max acquired GOP) interval is a nominal acquisition reference, not instantaneous cadence or a guaranteed maximum. Earlier fully validated IDR+PS intervals are allowed under the external asymmetric policy, while upper105%/5s, fullAU, repeatedPS, noB, source/geometry/path and continuous revocation checks remain mandatory. No optional telemetry/hint fields or common schema fork is introduced. Camera20/30 geometry FPS budgets are removed only on this new revision; schema bounds do not promise terminal resource support.

This1.3 implementation remains a review candidate. Existing1.2 release flags/secrets are not changed by this patch; source preparation does not advertise1.3 runtime support or authorize deployment/activation. UI trusted-receipt metadata work remains separate and cannot substitute for producer proof.
