# L4 Tools 1.9.4 — stabilization, UI and signed release

## Result and provenance
- Owner: native tools; no ProcessingBackend/MenuBuilder code or server containers changed.
- Source/artifact checkpoint b76cf83dbb0e3e3ce7f995993904ca60f356c42f is on origin/main.
- [Uniform stage report](../../../docs/term_dev-l4tools-cascade-report.md),
  [plan](../../../docs/term_dev-l4tools-stabilization-ui-release-plan.md),
  [immutable registry record](../../../artifacts/l4tools/1.9.4.json).
- Package1.9.4: proxy1.7.3, pin1.7.3, con1.9.5, superv/install1.9.3,
  desk1.9.3, sql1.7.6, capture1.0.0; legacy l4install built but not part of modern payload.
- Signed setup SHA59180db4c1d3a11a4b16b5916d39013630a0e17d1b070d6f7d588337371d6a82.
- Registry all3 file bytes verified. Two truncated Python GETs were rejected;
  complete direct curl GETs matched size/SHA/signature, then standard record command ran.

## User decisions and implemented boundaries
- Normal native HTTP uses NO_PROXY; explicit local leo4proxy routes remain.
- Legacy certsrv never activates MQTT/RTP. No valid new CA means local standby.
- After verified replacement, clean previous iot and recognized terminal certsrv
  in Machine MY and all Windows profile MY. Access failure precedes removal;
  deletion failure restores old records but retains the verified replacement.
- Local ready remains independent of external media policy. Polling/HTTPS stay available.
- MQTT types/presence, transparent proxy behavior and l4con user-event contracts unchanged.

## Verification
- Native x86/x64/default builds and appropriate certificate/HTTP/registry/profile,
  mock SCM/process/identity, con MQTT5/IPC/Job/rate/Unicode, policy/lifetime tests passed.
- Real loopback TLS stayed usable after credentials retired; fixture used temporary keys.
- Current Windows10Home19045 x64, terminal773/tenant1: right CA/SN, ready, media connection.
  Server deny/allow applied by10min polling; four service PIDs stayed unchanged, no7034.
  Internal con→mosquitto and HTTPS remained;20 UDP datagrams dropped without upstream.
- Mosquitto denied575s grew3773bytes; final293s grew1415bytes, about290B/min.
  Log text SYSTEM-only; ACL unchanged. Short observation does not establish long-term rotation.
- User accepted setup two-panel UI and final pin UI; real GUI fixture800x600 passed both arch.
  Current200%DPI preview inspected; Terminal0000773/O/OU, large PIN and touch Force shown.
- Actual GUI Upgrade and signed Repair: ready/0,9 installed EXE SHA match signed stage,
  certificates reused, no process kill/7034; capture/local health probes passed.
- All18 payload EXE plus installer Authenticode Valid/RFC3161; embedded2x60files match stage.
- Native components were not rebuilt after signing. Manifest clean/signed; Git secret scans passed.

## Operational state and limits
- Signed1.9.4 remains in C:\l4tools, services running,773 active, original certificate unchanged.
- Warning PendingFileRenameOperations/pending_reboot_detected recorded; no reboot performed.
- Implementation checkout D:\work\etranprocessing-mcp-user-events; original dirty checkout preserved.
- Local ignored tools/dist/.runtime-backup/20261002-cascade retains rollback/evidence.
  OpenH264 API/6libs restored from existing local vendor cache with matching SHA; keep cache.
- Not exercised: actual bad Win10Pro/WPAD/filter case, Win7, live all-profile cleanup,
  active RTP session, actual cancel/rollback, physical mixed-monitor DPI, long-term logs.
- Earlier composite live-cert CLI command rejected by automatic review; it was not retried.
  Service identity/mTLS/HTTPS runtime subsequently verified through authorized service flow.
- Future release: [existing signing flow](../../../tools/release/README.md), operator signs
  Complete-SignedRelease.ps1, then every signature/timestamp/payload/hash is verified.
  Never publish unsigned or use allow-dirty; never overwrite an immutable version.
