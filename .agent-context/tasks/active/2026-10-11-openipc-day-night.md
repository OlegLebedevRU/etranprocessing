# OpenIPC day/night release — 2026-10-11 UTC

The server media lease recovery is deployed and accepted separately in
2026-10-11-media-lease-expiry.md. This release adds strict validation for the
next approved camera priority; it does not add another session owner or timer.

IoT master d9afac921d8899b6067a55771d3581b07d646c82 (PR112) defines RPC162
Auto/Day/Night and adds schema-gated boolean nightMode.lightMonitor to RPC161.
Each operation completes with RES plus correlated gauge84/tag450, entirely
siplite ↔ OpenIPC. Unknown endpoints, toggles and GPIO writes are forbidden.

App1 was built using its registry_app1.py from a clean fetched origin/master
checkout on176. Local tests:621 passed,7 skipped; builder tests passed too.
Production immutable digest:
dev-leo4-ru.cr.cloud.ru/etran/app1@sha256:6a65f94af2fec9f1d847eea55aaf32ea83a9558e6089b640439c4c279272fe97.
Only app1 was recreated (34b3cd1b52ac). Existing official deployer checked
revision label, image ID and /docs health; the initial connection-refused probe
was a startup retry and final health passed. No migrations/model changes
relative to the previously deployed IoT source de67b943; rollback remains
compatible. Private runtime configuration was backed up without disclosure.

The12 neighboring production containers retained their IDs: MenuBuilder
172e369c0648, ingress af757cb20e67, Janus1cd35cea4177, media-nginx8927aebcc8cc,
processingc9cd9e3c944e, MCP17481e60bc24, JWT6d168e27b79a,
RabbitMQ41777886db72, Redisf7f10d65139d, gatewayb90fb67cbfe7,
nginx-mutual224ac66f457b and default-nginxf57699a9f1ca.

Siplite master2e8b500148a3d02a1fb1e660fb199513b2533267 (PR36) implements
the command through the existing worker, revision checks, result cache and
exclusive RTP lifecycle guard. No new lifecycle architecture. It verifies
night_enabled after fixed /night/on or /night/off; unsupported commands restore
the original monitor with revision/readback when possible. Failed restoration,
missing telemetry and uncertain writes return explicit non-success outcomes.
Software mode does not prove physical IR-filter or lamp operation.

Full ccache build0016: binary1225056 bytes, ELF13114472 bytes, SHA256
1f8ae71e850b3378e9ab6a0706d40bcb911cc32cd6a1bc0ea088b228b9c7c331.
Actual-function host tests cover successful transitions, dry-run/no-op, busy,
unsupported command rollback, failed rollback and missing post-command state.
Both devices were updated through public RPC19 and existing HTTP80 webroot;
temporary source/public binaries were removed after event proof:
4619 OTA_OK1358907 → HELLO1358909;
4623 OTA_OK1358908 → HELLO1358910, both l4-00.02.0016.

Live4619: Night, Day, Auto and unchanged Auto match config plus correlated
stored gauge84; stale revision and missing disruptive consent are rejected,
dry-run reports validated with no applied_fields. Original Day/monitor=false
was restored with fresh revision and gauge confirmation. video0 remains
15fps/1024bitrate/VBR/1280×720/GOP"1.0". On4623 RPC162 explicitly returns501
camera_network_unavailable plus gauge. RPC160/161 regression passed both.

Evidence is stored in ignored D:/esp32-workplace/sip/build/acceptance:
camera-day-night-0016.json, camera-day-night-0016.log,
camera-day-night-busy-0016.log, ip-camera-regression-0016.json,
ota-events-0016.json, build-0016.log, iot-day-night-release.log and
iot-day-night-deploy.log.

Live owned-stream exclusion passed: ingress has fresh RTP, RPC162 returns409
active_stream with no applied_fields, keepalive retains fresh RTP, then the
test stops/releases its own lease.

Final deployed video UI0016 check passed90 seconds at1280×720:620 callbacks,
max frame gap283.32ms, zero stalls/freeze/loss/drop/NACK. Stop/restart/new frame
and route-unmount release passed. The initial picker attempt timed out before
starting a lease because its online-only filter hid4619; the acceptance client
selects All before searching. Failed selector evidence is retained separately.
Final proof: video-browser-0016.json, video-ui-live-0016.png. Configured15fps
is distinct from the measured callback cadence; no15fps delivery claim.
