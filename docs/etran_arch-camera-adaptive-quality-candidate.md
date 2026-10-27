# Camera adaptive quality — runtime1.0 release candidate

Status: runtime candidate under validation. Deployment and visible browser
acceptance are authorized; their exact results belong to the release handoff.

Canonical quality extension belongs to IoT:
`contracts/runtime-quality-v1/schema.json`, revision
`l4-quality/1.0.0`. The exact mirror is
`MenuBuilder/backend/app/contracts/runtime-quality-v1.schema.json`, SHA256
`80b2866c985b48141011b054bb1094c4b2bed581e832561d0f9b8a2227066d85`.
The existing common media3 descriptor and frozen camera bundle remain unchanged.

Upstream candidate: [IoT PR157](https://github.com/OlegLebedevRU/iot-rpc-rest-app/pull/157),
source `70025e7febb85461d2a663c8b07a314128752734`. This is a draft proposal,
not an accepted deployment dependency.

Prepared BFF parser retains the existing lease_id wrapper and canonical
stream_instance_id/writer_key. It validates the explicit revision and target,
then separates lease_id for the provider URL. Parsing is not authorization.
The quality POST route selects the explicit1.0 envelope separately from retained
desktop v1/v2 requests. IoT and firmware independently enforce original-writer,
lease/stream/boot/issuer authority. Unknown source axes return unsupported.

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

Implemented: the camera START carries the original initiating writer key; IoT
stores its digest once. Quality GET/POST and receiver policy reuse the desktop
flow with source-declared bitrate bounds. Other observers remain readers.
The compact player overlay samples local WebRTC/video counters each second,
showing codec, actual raster, RTP rate/loss, decode/presentation FPS and separate
source target/applied values when available. It creates no HTTP or lease timer.
Keep initiator target, encoder readback, actual SPS/raster, traffic
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
Firmware reports this persistence explicitly and coalesces sustained pressure /
recovery changes. Current capability implements bitrate; active raster/FPS and
codec switches are not advertised as implemented by the broader schema.

Validation includes backend request/authority regression and frontend test/build;
browser/hardware acceptance and coordinated rollout are recorded separately.
Desktop native EXEs, MEDIA and frozen common descriptors remain unchanged.

Camera START is read-only. After capability discovery the original initiator
submits the current native target through quality SET, activating source queue
protection without changing Auto. The camera-only receiver adaptation switch
pauses receiver feedback; it does not change desktop controls or transfer ownership.
