# TASK-40 real camera source

`rtsp_test_source.py` is an acceptance adapter for one explicitly authorized test
terminal. It is not the native desktop agent or evidence of hardware encoder
readiness. It uses client type `svc_desk`, localhost Mosquitto, client ID
`svc_desk`, retained QoS1 presence/LWT and non-retained CTL replies. Quiesce the
installed l4desk before starting it; preserve the existing proxy and broker.

The private camera JSON has `url`, `username`, `password` keys. Keep it outside
Git. Do not put the authenticated URL in command arguments or reports. The
adapter constructs the FFmpeg input internally and suppresses its stderr.

```powershell
uv run --script l4media/testing/rtsp_test_source.py --sn $env:TEST_SN `
  --camera-config $env:CAMERA_PRIVATE_CONFIG --state-dir $env:TEST_STATE_DIR `
  --fail-file $env:TEST_FAILURE_FILE --probe-only
```

Remove `--probe-only` to connect. The installed leo4proxy owns RTP/RTCP ports
5004/5005 by default. Control and media follow the normal local broker → proxy
→ production TLS/L4RTP ingress path. No direct connection to the remote MQTT
broker or Janus is used by this source.

Each live inventory nonce triggers a new encoded camera sequence probe. The
accepted test profile is 1920×1080@20, Main8bit420, no reordered frames, repeated
VPS/SPS/PPS and a one-second keyframe interval. The probe validates the output
against the frozen G0 schema. Capability says `backend=camera`, `hardware=false`,
and identifies `test-rtsp-camera`; it grants no desktop or input capabilities.

The camera advertises Level4.1. `hevc_metadata=level=4` normalizes VPS/SPS to the
contract's Level4.0; `-c:v copy` preserves the encoded pictures. TASK-40 evidence
compares VCL bytes before/after and proves they are identical. The negotiated
descriptor describes the normalized output, including actual Level4.0.
This fixed test adapter must be reprobed if the camera settings change.

READY follows actual matching SPS and an IDR access unit forwarded as RTP.
The adapter logs command type/version and remaining TTL without payloads. If a
command arrives at most two seconds before the local clock reaches its issue
time, it waits for that time without extending the original expiry window.
Each manual start creates a new FFmpeg process/SSRC and live timestamps; there
is no packet-loop replay. STOP and lease expiry terminate the owned producer.
G0 has no normal STOP ACK/stopped envelope: physical exit is recorded locally,
without inventing a wire event. Failures use the frozen producer error envelope
and never restart or switch codec. Old relay callbacks are bound to their epoch.

Create the exact `--fail-file` to trigger a controlled producer failure. Create
`REPLAY_LAST_FAILURE` in `--state-dir` after a later manual start to resend the
previous epoch's schema-valid error through the same MQTT connection. Verify
that the current stream keeps decoding and no producer restart occurs. Create
`STOP` inside `--state-dir` for orderly adapter shutdown and offline presence.
Then restore the installed supervisor/l4desk and verify the legacy H.264 flow.
The source only supports the negotiated test HEVC path; use the real installed
legacy agent for H.264/Auto regression.

Server policy requires empty-by-default BFF HEVC/test-camera SN allowlists and
the IoT test-camera allowlist to contain this authorized certificate SN. Keep
the global hardware HEVC chain flag false until its separate acceptance gate.
Use the authorized owner's ordinary MenuBuilder session; do not forge tokens or
bypass lease admission. Three subscriptions under that session prove fan-out,
not admission of independent users.
