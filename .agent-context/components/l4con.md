# l4con

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