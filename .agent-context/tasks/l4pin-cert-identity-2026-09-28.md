# Handoff: replacement of terminal certificate and identity event

## Контекст задачи
- Windows tools: branch \`fix/l4pin-cert-cleanup\`, commit \`a4556c8\`; IoT app1: branch \`feat/cert-activation-event\`, commit \`320ceac\`.
- Test terminal previously switched from 1000009 to 1000011 only after manual deletion of old certificates and restart of L4Superv. New CA certificate NotBefore is issuance minus one day.
- \`l4con\` MQTT client type chosen by user: \`extra_service\`. Presence remains \`dev/{SN}/svc\`.

## Выполнено
- \`l4pin\` uses a fresh CNG key container per issuance; validates and installs the new certificate before removing all previous Leo4 terminal certificates from the target MY store. On failure, prior certificates are retained or restored; failures are explicit.
- Restored and compacted \`l4pin\` GUI: visible PIN, larger Force checkbox.
- \`l4superv\` detects a stale Leo4Proxy identity and retries the service transition, including Mosquitto and l4con restart on SN change.
- \`l4con\` publishes event 75 after CONNACK and matching local/proxy SN and thumbprint. It retries while connected if proxy identity is temporarily unavailable.
- app1 accepts event metadata from MQTT 3.1.1 JSON when MQTT 5 headers are absent and does not bill event 75.

## Затронутые контракты
| Contract | Producer | Consumer | Compatibility |
|---|---|---|---|
| \`dev/{SN}/evt\`, event 75, tags 324/440–443 | \`l4con\` \`a4556c8\` | app1 \`320ceac\` | Requires both revisions; JSON \`101/102/200/correlationData\` fallback for MQTT 3.1.1 |
| \`dev/{SN}/svc\` presence | \`l4con\` \`a4556c8\` | existing server | Unchanged \`extra_service\` retained presence |

## Проверено
- [x] l4-hmi source and upstream draft: event 70 is used; tags 430–439 are reserved for NVS watermark. Event 75 and tags 440–443 do not conflict with inspected source/docs.
- [x] Read-only production \`tb_dev_events\` code/tag audit: event 75 and tags 440–443 absent at audit time.
- [x] \`l4pin\`, \`l4con\`, \`l4superv\`: x86/x64 MSVC /MT builds passed; \`l4pin\` in-memory tests 8/8 per architecture.
- [x] app1: \`uv run pytest app-service/tests -q\`: 434 passed; changed files passed Ruff and Black.
- [ ] Terminal live reissue, GUI visual check and event 75 in production: not yet deployed or tested.

## Риски и следующие действия
- Deploy app1 revision \`320ceac\` through the approved image flow before enabling the new terminal binary. Check event 75 persistence and absence of billing increment.
- On a test terminal with services stopped by the user, install the new Windows binaries and verify failed enrollment retains the old certificate, successful reissue leaves exactly one Leo4 certificate, services reconnect under the new SN, and event 75 appears once.
- Keep accepted \`l4tools-1.8.2-beta-1\` unchanged; package this fix as a separate candidate after runtime checks.
- No test sessions, certificates or server files were modified during this handoff preparation.

## Context distillates
- \`../components/l4con.md\` and \`../../docs/ops_run-remote-console-diagnostics.md\` updated with event 75.
