# Camera adaptive quality — preparation 0.1

Status: candidate, not registered in live routes. No deployment, browser START,
writer/gate activation or new firmware installation in this patch.

Canonical quality extension belongs to IoT:
`contracts/runtime-quality-proposal/schema.json`, revision
`l4-quality/0.1.0-proposed`. The exact mirror is
`MenuBuilder/backend/app/contracts/runtime-quality-proposal.schema.json`, SHA256
`4c80fcc16f5e74ab4ebd954e0598e162d23e3ce5bead29a49c81fc6dca66e4c0`.
The existing common media3 descriptor and frozen camera bundle remain unchanged.

Upstream candidate: [IoT PR157](https://github.com/OlegLebedevRU/iot-rpc-rest-app/pull/157),
source `70025e7febb85461d2a663c8b07a314128752734`. This is a draft proposal,
not an accepted deployment dependency.

Prepared BFF parser retains the existing lease_id wrapper and canonical
stream_instance_id/writer_key. It validates the explicit revision and target,
then separates lease_id for the provider URL. Parsing is not authorization.
The current route union deliberately does not select this candidate until the
source adapter and complete negotiated camera extension are implemented.

Reuse desktop original-initiator binding and the existing quality GET/POST
paths, command generation, report sample_seq, accepted/applied distinction and
abort/stale-session handling. Do not reuse its100..4000 bitrate or half-raster
limits for camera. Multiple observers read quality; only the original initiating
observer supplies external targets. A source can locally reduce and restore
bitrate under the same accepted generation, reporting actual separately.

The candidate expresses bitrate, size, FPS, codec preference, presets, rate
control and GOP seconds. Auto is native0/empty, preset names are not a whitelist,
and fractional FPS is expressible even if the current camera actuator cannot
apply it. Implementation support must be advertised from actual capability.

Before live integration: carry the initiator key through the camera prepare/start
path and store only its digest once at original START; implement source reports
and camera-specific issuer/boot/lease admission; connect the BFF adapter and
receiver policy. Keep initiator target, encoder readback, actual SPS/raster, traffic
window and browser decoded/presented FPS distinct. Other observers cannot take
over through joining, repeating START, another tab or an administrator override.

MEDIA owns private Janus codec/PT/fmtp/ports and MID/media layout. Do not rewrite
this binding when live coded raster changes. Full actual descriptors reuse the
existing common schema, independently of immutable START history. Continuous
camera resize needs decoder-envelope checks: local5MP→3MP→2MP changed SPS level.
Runtime codec switch, mountpoint reconstruction and mass client reconnection are
outside this implementation stage, not universal restrictions of the contract.

Hardware qualification also showed that working camera bitrate POST persists
configuration while `/api/v1/live` refused bitrate. An automatic loop must account
for write persistence and recovery; it must not advertise a flash-free actuator.
The current patch performs no camera writes and enables no feedback loop.

Validation: offline request boundary checks and existing backend regression.
Browser/hardware acceptance and coordinated rollout remain separate. Desktop
runtime sources, frontend, MEDIA and all existing routes remain unchanged here.
