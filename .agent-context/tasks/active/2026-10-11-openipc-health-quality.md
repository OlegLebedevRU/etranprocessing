# OpenIPC health/identity and quality release

User requested health, presets and physical day/night verification in one
firmware release, with explicit Есть/Нет video confirmation instead of a
five-minute run. IR illumination is unconnected and excluded. Physical filter
verification must be coordinated with the operator before sending a command.

Canonical IoT contract: PR113, master
f75e3c104471d4ba78f5450a890687f532974f40. RPC163 applies medium/hd presets with
the existing revision, schema, disruptive consent and relay guard. Legacy
writes are sequential and read back individually, never claimed atomic.
Optional gauge84 identity and health probes retain only approved bounded fields.
No migrations, STM32 register changes, GPIO guessing or new network routing.

Builder176 published app1
dev-leo4-ru.cr.cloud.ru/etran/app1@sha256:e25fefc9f598fdb04b21e2c7e1fdae18c9dad0f19afe86e172656867e4e378d3.
Registry build record: 20261011T183036808133Z. Linux tests603 passed33 skipped;
Windows tests629 passed7 skipped. Production deployment used the existing
serialized `.github/ci/deploy.py app1` path, verified SHA/digest and health.
Only app1 changed container: 34b3cd1b52ac -> 23cf41dcbb78. All twelve
neighbors retain their IDs, including ingress, Janus, MQTT, nginx and backend.

Siplite source/build/OTA acceptance lives in
`docs/camera-health-quality-acceptance.md`. Release0017 must prove OTA_OK then
HELLO on4619 and4623. Runtime RES must correlate with gauge84 snapshot_id and
operation_id. Configuration readback alone does not establish decoded video
or physical filter movement; record the user's observations separately.

Preserve4619 isolated camera networking and4623 switch mode. Do not clear
another owner's lease. Before presets/filter writes, confirm no active stream;
busy rejection is deliberate. No timed browser run is authorized for this pass.

## External database blocker after staging

Siplite PR38 merged master13221f79cd65bf0fa1e45fd74915634f968793d3;
0017 BIN1230288/ELF13141628 bytes, SHA256
961765a06168dd2b75dbd575208f57d3b0471447d46e54df11b7527c8975ce21.
Host C tests/full ccache build passed; staged HTTP download matched the hash.
No OTA task was submitted: the initial event API read timed out before POST.
Subsequent event reads returned500. App1's startup HTTP check passed, but live
RPC/database acceptance has not passed. Managed PostgreSQL10.0.0.7:5432
responded to TCP/SSL; SQL sessions timed out/reset, with app1
ConnectionResetError/ProtocolViolationError. The operator confirmed concurrent
ProcessingBackend work and probable DB faults; causation is unproven.

Resume after database recovery with the existing0017 artifact, prove both
OTA_OK→HELLO, then RPC163/gauge84 and explicit operator video/filter acceptance.
All physical/runtime results remain pending. Do not mark the firmware deployed
or restart shared infrastructure merely because `/docs` is healthy.

## Live acceptance after recovery (supersedes the historical blocker)

Database-backed task/event reads recovered after a controlled app1 restart.
Both devices now run the same0017 binary; no second firmware was introduced.

| Device | OTA task | OTA_OK →0017 HELLO event IDs |
|---|---|---|
| 4619 | `8c4570f8-4e14-47cd-9a98-c7d2ca87aef8` | 1358920 →1358921 |
| 4623 | `561ceca7-bc7d-4ab0-81b6-b38726e8e708` | 1358922 →1358923 |

The ambiguous4619 submission was recovered without resending OTA. Failed4623
submission storage was reconciled before creating its successful task. Temporary
HTTP binary copies were removed after both OTA_OK/HELLO pairs were confirmed.
Firmware source is siplite PR38; subsequent PR39/40 changed documentation and
OTA recovery tooling only. Contract and app1 image remain those recorded above.

RPC160 on4619 returned200 and matching gauge84 with health readings, memory,
temperature and camera-clock uptime. Missing FPS/frame samples stay unknown;
the camera's absolute clock is outdated, so uptime is not NTP acceptance.
Identity returned invalid_response: OpenIPC/Majestic versions remain unavailable
for this installed status-page format. Do not represent this probe as healthy.
RPC160 on4623 explicitly returned501 camera_network_unavailable, with gauge84;
no camera is attached. Ethernet isolation and switch mode were preserved.

RPC163 medium and HD writes/readback succeeded, with separate operator Есть
confirmation of displayed video for both profiles. Original HD settings were
restored. Dry-run made no changes; stale revision returned409; unsupported size
and numeric GOP returned400 with matching gauge84 and unchanged configuration.

After explicit operator readiness, Night RPC162 returned200 and matching84;
night/ircut software metrics changed to1. Operator confirmed black-and-white
video but could not determine filter sound. After another readiness confirmation,
Day RPC162 returned200 and matching84, with night/ircut metrics restored to0.
The operator confirmed restored color video in Day and no audible filter sound.
Software/image switching passed; mechanical filter movement remains unconfirmed.
No timed run or IR lamp test was
performed. Initial HD + Day configuration is restored.

Only documentation is changed by this handoff update. Concurrent
ProcessingBackend work is outside this release and was not modified or reverted.
