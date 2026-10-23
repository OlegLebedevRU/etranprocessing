# Native camera server acceptance source

`native_camera_source.py` exercises the negotiated camera1.0.0 path with the
existing siplite CTLv1 producer contract. It uses the approved `main_app` role,
local Mosquitto and the installed proxy; the proxy retains its existing device
certificate/private key. No direct server MQTT connection or browser camera URL
is used. This adapter is explicitly TEST-only and is not ESP32 firmware or proof
of physical port2 discovery. Quiesce the installed desktop publisher first and
restore it after cleanup. Use an existing authorized test SN, never the live
camera terminal's identity concurrently with its firmware.

The private camera JSON contains `url` (RTSP without userinfo), `username` and
`password`. Keep it outside Git and logs. Authenticated URLs are assembled only
in memory; FFmpeg diagnostics are suppressed. The adapter performs read-only
RTSP probes and never changes camera settings or GPIO.

```powershell
uv run --script l4media/testing/native_camera_source.py --sn $env:TEST_SN `
  --camera-config $env:CAMERA_PRIVATE_CONFIG --state-dir $env:TEST_STATE_DIR `
  --fail-file $env:TEST_FAILURE_FILE --ffmpeg $env:FFMPEG
```

The probe copies 50 encoded frames, validates native VPS/SPS/PPS and separately
decodes to a host null sink to verify picture types. This host check is distinct
from browser decoding. Native raster, level and parameter bytes remain unchanged;
there is no encoder, level normalization or runtime codec fallback. The bounded
15-second probe targets the available test camera, not the full slow-FPS firmware
discovery matrix. Unsupported/unknown inputs remain unavailable.

The source publishes retained QoS1 `app_online`/LWT `app_offline` and existing
gauge84. Gauge contains no writable methods, camera credentials or verified MAC
candidate. Its reason and CTL inventory explicitly label the test source.
The camera contract is separate from frozen desktop HEVC2.0. No new event code,
diagnostic topic or wire field is introduced.

START comes only from the tenant/lease-authorized server. Use the empty-by-default
IoT `APP_CONFIG__CAMERA__TEST_START_SN_ALLOWLIST` for the one test identity; keep
`APP_CONFIG__CAMERA__WRITERS_ENABLED=false`. Record and restore any temporary
camera terminal tag/allowlist. The allowlist never permits configuration,
discovery or credential RPC writes. The source ACK confirms command execution;
native CTLv1 does not return an actual descriptor in ACK. Only WebRTC statistics
prove target browser decode.

Every START owns a new FFmpeg process, UDP sockets and relay generation. RTP uses
PT96 and untouched HEVC payload. The monitor requires matching VPS/SPS/PPS plus
a complete random access unit and rejects changed parameter sets/SSRC. Only
parameter sets use bounded assembly; video frames are not accumulated. RTCP is
forwarded bidirectionally through the learned local FFmpeg endpoint. STOP/expiry
terminate the process and join relay threads before acknowledging cleanup.
There is no automatic restart after source failure or transport disconnect.
Create the chosen fail-file to inject a controlled failure; manual START creates
a new epoch. Create `REPLAY_LAST_FAILURE` in the state directory to resend the
previous schema-valid failure and check stale-epoch isolation. Create `STOP` in
that directory or stop the console normally for offline presence and cleanup.

Offline checks:

```powershell
uv run --no-project --with jsonschema==4.26.0 --with paho-mqtt==2.1.0 python -m unittest discover -s l4media/testing -p test_native_camera*.py
uv run --script l4media/testing/generate_native_ctl_schema.py --iot-root $env:IOT_ROOT --check
```

The validator mirror is generated from the accepted pure IoT DTOs. Its source
hashes detect drift; it is not a replacement source of truth. The small fixture
contains parameter sets only, with synthetic picture/decode observations used
by unit tests. Live captures, credentials, release receipts and browser evidence
remain outside Git. The historical fixed-level `rtsp_test_source.py` is retained
for its original TASK40 evidence and is not used for native camera acceptance.
