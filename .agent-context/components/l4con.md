# l4con

## FM correction candidate 1.11.1, 2026-10-05

Automatic loopback API discovery from ready matching-SN Leo4Proxy with fm_transport, default
root C:\l4tools\fm provisioned by setup. L4FM_API_URL removed. PB HTTP only loopback, S3 HTTPS
only via named local proxy/CONNECT; no direct network fallback or terminal certificate on S3.
fs.proxy compatibility gate requires the new transport. Existing extra_service presence unchanged.
[Handoff](../tasks/active/2026-10-05-fm-proxy-installation.md).

## 2026-10-05: RPC7xxx /7011, l4con1.10.0

Suite1.11.0 signed/published; operator installed773 successfully. Local services
Running, proxy1.8.2 ready; l4con1.10.0 and l4pin1.8.0 PE versions verified.
Canonical payload.dt, status/result_uid, cancel addressing/dedup, protected
renewal and bounded network/Job timers are implemented and tested x86/x64.
Presence remains extra_service. Actual7011 on773 returnedRES200/exit0, installed/hotrotated and was
confirmed by fresh current-serial PB mTLS discovery. Fault/reboot/Win7 acceptance
remains separate. [Matrix](../../docs/term_arch-rpc7011-flow-matrix.md).
## 2026-10-02: stabilization cascade (1.9.5)

Source ae36bd86f5f968847998699aea421df63ebe4754: bounded NO_PROXY proxy discovery,
strict ready/certificate JSON and SN validation. MQTT extra_service presence,
4096-character commands and user-event IPC/Job/rate/payload contracts unchanged.
x86/x64 builds, MQTT5, discovery and user-event fixtures passed. x64 installed
in C:\l4tools\l4con after operator stopped services; operator restarted, PID219560.
Production event E2E was not repeated in this cascade. See cascade report/runtime handoff.

## 2026-10-01: command buffers (1.9.4)

The structured MCP E2E exposed silent truncation of command_line to1023
UTF-8 bytes in1.9.3. User approved a minimal native correction to1.9.4.
The consumer now supports4096 Unicode code points (IoT contract), decodes
JSON Unicode escapes, validates UTF-8 and rejects invalid/oversized commands
with error/EOF and exit126 before launching. Invalid command_line never
falls back to command_id. Shell/account/Job/TTL/presence stay the same.
x86/x64/default builds and MQTT protocol tests passed; isolated native tests
cover4096 ASCII, Cyrillic and supplementary characters, overflow, IPC/Job.
x64 SHA256867e7eda78bc8305ba05024dfcd734bd44227bc789f17ff54c2bf72f62f5caa6
installed in C:\\l4tools\\l4con with previous EXE backup. User restarted services;
full1024-byte payload (Unicode and all quotes), oversize/-2, invalid int32/-1,
optional UUID/invalid UUID2, storm0/5 and local caller denial3 passed via MCP
and persisted IoT history. L4Con PID188684 remained unchanged during tests.
No suite package built. See the l4mcp event handoff for subsequent runtime results.

## 2026-10-01: user events (1.9.3 working tree)

`--send-event` is a quiet native IPC client, not another MQTT client.
Only processes belonging to the current remotely launched command's Job may
publish events 900–999 through the service's existing connection. Tags 446
(raw text <=1024 UTF-8 bytes), 447 (int32), 448 (optional external UUID)
are described in [user events](../../docs/term_tool-l4con-user-events.md).
Missing448 is omitted; supplied invalid448 rejects the call. A global
1000ms limiter has no burst/queue/retry/refusal logs. Cancel/TTL/end revoke
the grant; descendants share the TTL even when the root exits earlier.
Source/resource version1.9.3; x86/x64 builds and isolated native IPC/job tests
passed. Final x64 installed/retained on773; real MCP console calls and IoT API
confirmed string/int32/UUID/UTF8/overflow/default fields and one server event
for five rapid calls (CLI0,5,5,5,5). Local callers remain denied3.
PowerShell launch uses EncodedCommand to preserve the original quotes.
[Handoff](../tasks/completed/2026-10-01-l4con-user-events-handoff.md) records
checks and Windows7 runtime limitation. Configured MCP principal denied773;
runtime used separate temporary credentials explicitly supplied by the user.

## 2026-09-30 update: event 75

The release 1.9.1 source in this worktree publishes event 75 to
`dev/{SN}/evt` (QoS 1, no retain) after verifying the active proxy and local
certificate identities. Tags 444 and 445 carry the installed EXE inventory
and the package version from `state.json`. It scans every 60 seconds and
publishes changes. See [event 75 inventory](../../docs/term_tool-event75-inventory.md).
App1's event collector stores the JSON payload without tag-specific schema;
this was checked in `app-service/core/services/device_events_collect.py` in
the external app1 repository. MQTT runtime delivery was not tested on a
terminal. The original remote input drag and wheel regression remains open.

## Назначение
Native Windows consumer удалённых console/diagnostics задач и потокового вывода.

## Границы ответственности
Запуск cmd/powershell, process ownership, TTL/cancel cleanup и output chunks.
Не server authorization/lease owner и не desktop input/FFmpeg supervisor.
Общих ORM-моделей/миграций нет.

## Внешние контракты
| Direction | Transport | Endpoint/topic | Main payload | Guarantees |
|---|---|---|---|---|
| app1 → l4con | MQTT | srv/{SN}/tsk,rsp,cmt | method/task/session | отдельный RPC flow |
| l4con → app1 | MQTT | dev/{SN}/req,res,out | request, result, seq/eof | no retain |
| l4con → server | MQTT | dev/{SN}/svc | svc_online/offline | extra_service retained LWT |

## Инварианты
- svc topic не разделять с l4desk; не менять тип клиента без обязательного уточнения.
- Process/output limits и cleanup обязательны; не превращать диагностику в bypass shell.
- Packaging/installer не менять без прямой задачи; static Win32 /MT и x86/x64/default.

## State machine
См. [console contract](../contracts/remote-console.md): task → running/output → final/cancel/timeout.
Не переносить FFmpeg recovery правила на произвольную shell-команду.

## Ключевые исходники
- [tools/l4con](../../tools/l4con) — разрешённая утилита; выбрать consumer/executor по задаче.
- [build.cmd](../../tools/l4con/build.cmd) — штатная сборка конкретной утилиты.
- [DeviceConsoleTab.tsx](../../MenuBuilder/frontend/src/routes/devices/DeviceConsoleTab.tsx) — UI consumer.

## Проверка
Штатный build x86/x64/default при code changes; TTL/cancel, exit_code/eof, duplicate,
output cap и reconnect. Сначала безопасный read-only preset в изолированном контуре.

## Известные риски и незавершённые вопросы
Карточка основана на protocol docs; наличие всех описанных методов требует source/runtime проверки.

## Источники и актуальность
- Authoritative docs: [console](../../docs/ops_run-remote-console-diagnostics.md), [AGENTS](../../AGENTS.md).
- Code references: entry points выше; код l4con не аудитировался.
- Проверено: 2026-09-11, HEAD `63ce6a7`, документальная карта.
- Обновить при: MQTT type, RPC/exec/cleanup, output или сборке.
