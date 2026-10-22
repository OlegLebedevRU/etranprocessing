# siplite media API v2

Contract: `hevc-contract/2.0.0`; canonical package is `contracts/hevc-v2/`.
`spec/` is a byte-identical service copy, checked by `make contract-check`.
OpenAPI 3.1: `GET /api/v2/openapi.json`; v1 OpenAPI and H264 API remain available.

All `/api/v2/media/` endpoints require the existing service token (Bearer or
`X-Media-Service-Token`). Never expose these management endpoints to browsers.
Browser watch admission remains the BFF's responsibility; Janus PIN is per session.

1. Read authenticated `GET /api/v2/media/capabilities`. HEVC is disabled by
   default (`L4MEDIA_HEVC_ENABLED=0`). Enabling it permits a disposable
   H265 create/info/delete test against the running pinned Janus. An enabled
   flag alone never advertises HEVC. This test proves mountpoint support,
   not browser decode, encoder readiness or a complete media path.
2. Resolve Auto/H264/H265 and probe the producer before preparing a route.
   POST the frozen `media_start` shape to `/api/v2/media/sessions/start`.
   Replace fixture UUIDs/deadline with real lease and operation identifiers.
   The descriptor is immutable for this stream; the generated PIN is returned.
3. Start the producer only after route creation, passing the same descriptor.
   Ready, fresh RTP and browser decoded frames are separate confirmations.
4. Renew with `media_renew` only after the authoritative control lease renews.
   Stop with `media_stop`; every call includes lease, stream and descriptor epoch.
   A changed descriptor requires a new explicit stream/operation/session UUID.
5. GET `/api/v2/media/sessions/{session_id}` with `sn`, `lease_id`,
   `stream_instance_id`, `descriptor_revision` query parameters. `transport`
   separates connection, packet count and fresh RTP. `active` starts on forwarded
   RTP; it does not prove valid HEVC, producer READY or decoded video.

Supported: one video track, no audio/transcode; H264 baseline RFC6184 or HEVC
Main 8-bit 4:2:0 RFC7798, DON=0, no B frames. Profiles `low` (15 fps maximum)
and `default` (25 fps maximum), raster up to 1920x1080, exactly as probed and
validated by frozen schema. Schema rejects extra tracks/audio/unknown properties,
duplicate JSON keys and bodies above 32768 bytes. Fmtp is generated from the
descriptor, including payload type and profile. Live Janus mismatches return
409 without destructive recreation. Missing Janus on a repeated start returns
502, never a false active response.

L4RTP/1 is unchanged: `4c345254 01 00 <SN UTF-8 byte length: uint16 big endian>
<SN>`, followed by `<type: uint8><reserved: 0><length: uint16 big endian><packet>`.
Type1 is RTP, type2 RTCP. Use the existing mTLS/SN boundary in production;
plain ingress is private. Example RTP headers (synthetic, not decodable samples):
H264 `8060000100000001000000016580`; HEVC `806000010000000100000001260180`.
PT96 is in the second byte; the producer must use its descriptor's PT and send
VPS/SPS/PPS plus IDR periodically for late viewers. Do not use these header
examples as evidence of decoded media. Replay actual encoded sample packets
with their original timing through the existing terminal transport, without
re-encoding or substituting a software HEVC encoder.

Reverse RTCP remains bounded by the existing per-connection queue. V2 PLI/FIR
coalesces to at most one keyframe request per 500 ms per publisher; other valid
compound RTCP is retained. No server restart/fallback loop is introduced.

Redis stores format_version2 plus the complete request/descriptor, identity,
ports, route generation and lease times. Legacy records remain H264. A new
record missing its descriptor is ignored, never interpreted as legacy.
Crash reload restores live records as starting; stopped/expired epochs never
become active again. A rollback to the previous binary requires draining v2
sessions first, then disabling HEVC and rolling back the image. Old code cannot
consume JSON v2 Redis records. No database migration is needed.

Validation: `make test all` and `tests/builder_smoke.py` (isolated Docker network
on the builder). The latter checks runtime Janus, Redis crash reload, three
codec-specific SDP watch offers and synthetic L4RTP counters; it explicitly does
not claim decoded sample or authorized multi-user admission. Real H264/HEVC
sample + WebRTC decoded frames and target deployment belong to TASK-40.
