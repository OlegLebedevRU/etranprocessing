# Desktop live-UTC renewal interop

`desktop-renew-vectors.json` is a byte-for-byte copy of the MenuBuilder agent's
actual keepalive -> `MediaOrchestratorClient.video_v2` -> httpx capture, produced
by `test_actual_desktop_v3_renew_uses_live_provider_utc_after_clock_jumps` in
`MenuBuilder/backend/tests/test_media_lease_deadline.py`. The production helper
is byte-identical to PR121 source
`be41d887c7e6bad1abfed3ab59708f08ebf92afa`; the capturing test is added in its
follow-up. It is test data, not a frozen schema.

SHA256: `b3787a1627d30c0a994cd295babc1dc0e17b96d944340d5ffc5b388b33b3be25`.

The shared evidence is under
`D:/.codex/siplite-hevc/evidence/task75-menubuilder-v3/desktop-renew-vectors.json`.
The C test submits both captured request bodies unchanged to the production
renewal handler. At monotonic receipt7000, live UTC receipt moves forward or
backward3600 seconds and provider expiry remains receipt+60. Both must yield
deadline67000. Only the common media-v3 selector is sent; camera-specific RPC
metadata is not requested by this desktop path. HTTP routing/header guards have
their existing independent tests; this fixture exercises the lifecycle boundary.
