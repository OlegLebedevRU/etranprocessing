# Handoff: replacement of terminal certificate and identity event

## Контекст задачи
- Windows tools: branch `fix/l4pin-cert-cleanup`; IoT reference docs only: branch `docs/cert-identity-event`, commit `bd27ab0`. Existing app1 remains at production revision `35f1fce`. The experimental app1 branch `feat/cert-activation-event` was withdrawn from remote after the user prohibited app1 changes.
- Test terminal previously switched from 1000009 to 1000011 only after manual deletion of old certificates and restart of L4Superv. New CA certificate NotBefore is issuance minus one day.
- `l4con` MQTT client type chosen by user: `extra_service`. Presence remains `dev/{SN}/svc`.

## Выполнено
- `l4pin` uses a fresh CNG key container per issuance; validates and installs the new certificate before removing all previous Leo4 terminal certificates from the target MY store. On failure, prior certificates are retained or restored; failures are explicit.
- Restored and compacted `l4pin` GUI: visible PIN, larger Force checkbox.
- `l4superv` detects a stale Leo4Proxy identity and retries the service transition, including Mosquitto and l4con restart on SN change.
- `l4con` publishes event 75 after CONNACK and matching local/proxy SN and thumbprint. It retries while connected if proxy identity is temporarily unavailable.
- `l4con` uses MQTT 5 end to end at the local broker connection and sends the event metadata as User Properties; app1 is unchanged.

## Затронутые контракты
| Contract | Producer | Consumer | Compatibility |
|---|---|---|---|
| `dev/{SN}/evt`, event 75, tags 324/440–443 | `l4con` on this branch | existing app1 `35f1fce` | MQTT 5 User Properties `event_type_code/dev_event_id/dev_timestamp/correlationData`; no server changes |
| `dev/{SN}/svc` presence | `l4con` `a4556c8` | existing server | Unchanged `extra_service` retained presence |

## Проверено
- [x] l4-hmi source and upstream draft: event 70 is used; tags 430–439 are reserved for NVS watermark. Event 75 and tags 440–443 do not conflict with inspected source/docs.
- [x] Read-only production `tb_dev_events` code/tag audit: event 75 and tags 440–443 absent at audit time.
- [x] `l4pin`, `l4con`, `l4superv`: x86/x64 MSVC /MT builds passed; `l4pin` in-memory tests 8/8 per architecture.
- [x] `l4con` MQTT 5 packet tests passed on x86/x64. App1 prototype tests passed (434), but that branch was withdrawn and is not part of the delivery.
- [x] Local Mosquitto integration: MQTT 5 CONNECT, retained `svc_online`, RPC 7003 `pong` all passed using an isolated test EXE mutex. Temporary EXE and broker process were removed.
- [x] On terminal 773, user stopped L4Superv/L4Con; three x64 EXEs copied to `C:\l4tools` and SHA-256 verified. User restarted L4Superv. Existing app1 persisted `tb_dev_events.id=1356471`, `device_id=773`, `event_type_code=75`, correct SN/source, UTC timestamp, correlation and inventory tags 441–443.
- [x] User visually accepted the updated l4pin GUI on 773 without entering a PIN.
- [x] On separate test agent 1000011, user installed the same three x64 EXEs and restarted L4Superv. Existing app1 persisted `tb_dev_events.id=1356473`, type 75, for 1000011.
- [x] User obtained a new PIN for existing 1000009 and installed it with Force while L4Superv ran. IoT showed 1000009 online, 1000011 offline, and new type 75 event `id=1356474` with expected SN, source, 40-character thumbprint and correlation. User's local `l4pin --status` showed exactly one Leo4 certificate, OU=1000009.
- [ ] Early CHECK refusal, failed reissue after CHECK/CSR, and long-running reconnection were not exercised. The user no longer has the consumed one-time PIN, so no refusal test was attempted.

## Риски и следующие действия
- Do not deploy or modify app1. Its existing `evt` path counts event 75 as one event message; the September counter for org 1 was 7 after this test, but no before snapshot exists to attribute an exact delta.
- For a later refusal test, use a controlled invalid or expired PIN and verify preservation of the sole 1000009 certificate. A failure after CHECK/CSR would need a controlled CA/network fault test. Do not consume a new valid PIN solely to reproduce the early refusal.
- Keep accepted `l4tools-1.8.2-beta-1` unchanged; package this fix as a separate candidate after runtime checks.
- The certificate on the separate test agent was replaced with the existing terminal 1000009 identity. No server file was modified. No temporary broker process or test EXE remains.

## Context distillates
- `../components/l4con.md` and `../../docs/ops_run-remote-console-diagnostics.md` updated with event 75.
