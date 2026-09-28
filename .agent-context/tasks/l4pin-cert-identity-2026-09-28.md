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
- [ ] Terminal live reissue, GUI visual check and event 75 in production: not yet deployed or tested.

## Риски и следующие действия
- Do not deploy or modify app1. Verify event 75 through existing event API/DB after the new `l4con` is installed. Existing app1 bills normal `evt` messages; quantify this single event in the test.
- On a test terminal with services stopped by the user, install the new Windows binaries and verify failed enrollment retains the old certificate, successful reissue leaves exactly one Leo4 certificate, services reconnect under the new SN, and event 75 appears once.
- Keep accepted `l4tools-1.8.2-beta-1` unchanged; package this fix as a separate candidate after runtime checks.
- No test sessions, certificates or server files were modified during this handoff preparation.

## Context distillates
- `../components/l4con.md` and `../../docs/ops_run-remote-console-diagnostics.md` updated with event 75.
