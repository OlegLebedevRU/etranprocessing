# MenuBuilder video contract v2 candidate

Requires TASK-00 commit `3e236dc5b3b92afc83d49b022b38d07bccfa82c4` and unchanged
`hevc-contract/2.0.0` package. No ORM/Alembic migration.

Feature gates: `VIDEO_CONTRACT_V2_ENABLED=false` by default;
`L4DESK_SESSION_ORCHESTRATION_ENABLED` must also be enabled for HEVC.
`VIDEO_PROXY_RTP_PORT` / `VIDEO_PROXY_RTCP_PORT` default to0 (unconfigured),
and must match the terminal's existing local transport listener. No guessed ports,
credentials or broker settings are included. Existing service authentication is
used for IoT and l4media; management URLs/PINs are not browser configuration.

New UI requests inventory with `contract_version=2`, selected `source_id` and
`profile_id`; H265 stays disabled with a reason unless hardware capability is fresh
and the media runtime advertises verified H265. Auto resolves before media prepare
and concrete START. Unknown capability permits legacy H264 for Auto only. An
explicit HEVC refusal, ACK without READY or runtime failure never triggers a
second START, codec switch or software fallback.

`POST /api/v1/video/devices/{device_id}/stream/start` accepts optional
`codec_policy: auto|h264|h265` with existing mode/source/profile/lease fields.
Requests omitting it retain their response model. New requests add
`resolved_codec`, optional `descriptor` and `media` watch connection information.
The BFF prepares media before terminal START. It validates frozen payloads and
checks identity/descriptor on READY; cleanup targets its own lease/stream/revision.

`POST /api/v1/video/devices/{device_id}/session?contract_version=2` joins an
existing authorized publisher, returning its real stream ID and descriptor. It
does not acquire/renew a producer lease or reconfigure an active route. A missing
route fails closed. Direct-Janus compatibility is H264 only and requires the
existing cached publisher PIN. No independent viewer admission was added:
existing owner/role checks and the IoT single-active-lease rule still apply.
Role4 requires a view-scoped lease and therefore cannot independently join
another user's stream lease under current admission. A technical three-handle
Janus test cannot establish authorization for three users. Admission redesign is
a separate follow-up.

Watcher lifecycle does not send STOP/release/keepalive/scope changes for producer
ownership. Current-epoch failure closes its subscription; stale-epoch events are
ignored. HEVC failure shows a hardware encoding stopped message plus machine code.
The page's live badge requires actual fresh browser frames; webrtcup only means
waiting for image. Existing getStats sampling supplies decoded frame growth;
no new frequent server poll was added.

Integration discrepancy for TASK-10: TASK-00 froze CTL JSON but did not define
the IoT HTTP-v2 adapter. This candidate explicitly uses these provisional paths:
GET `/api/internal/v2/remote-input/devices/{sn}/inventory` returning the frozen
`capabilities_ack`; POST `/api/internal/v2/remote-input/lease/{lease_id}/stream/start`
and `/stop` with frozen CTL bodies. START must return the frozen READY after agent
confirmation, not an accepted/queued ACK. TASK-10 must confirm these routes and
authoritative status descriptor/agent IDs before enabling the gate. There is no
retry through v1 on a v2 error. This discrepancy is not a change to frozen CTL.

Rollout: build candidate images on the designated builder, confirm the adapter
with TASK-10 and media-v2 with TASK-20, then perform TASK-40 using real encoded
H264/HEVC samples and browser framesDecoded growth, late join, stop and runtime
encoder failure. Current unit/browser mocks do not satisfy G-SERVER-READY.
Rollback: disable HEVC admission, explicitly drain v2 publishers, disable the
v2 gate, then restore previous BFF/frontend images. Retain existing legacy paths.
