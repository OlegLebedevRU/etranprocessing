# Remote console: MenuBuilder → app1 → MQTT → l4con

## L4Update extension — принят 2026-10-06, не реализован

[RPC703x architecture](../../docs/term_arch-l4update-flow.md) сохраняет транспорт,
вводит typed plan/start/status/cancel подготовки; start даёт accepted и независимую
операцию. Во время применения обычные работы дренируются/получают отказ, update
RPC/events остаются. IoT registration/validation/polling whitelist ещё проверять;
существующая capability не означает поддержку703x. REQ завершённой задачи даёт
NOP с zero UUID; отдельный свежий EVT/EVA необходим для подтверждения барьера.

## 2026-10-05 contract correction

DeployedIoT8c2be80/native suite1.11.0 use canonical payload.dt[] with method-aware
cardinality. Parameterless7002 executes onTSK only when payload_required=false;
REQ/RSP/RES still complete without reexecution. Addressedcancel/exec/renew waitRSP.
7004/7005 are registered but this agent replies501; activepoll whitelist is
7001/7002/7003/7011. Protectedrenew nextcommand busy409, noFIFO/cancelreplacement;
ordinaryexec cancellation bounded5s. Real7003 returns200/pong; actual7011 returned200/exit0 and currentserial discovery
confirmed use. Final ingress change confined to renew location.
[Actual matrix](../../docs/term_arch-rpc7011-flow-matrix.md).

## Назначение
Контракт неинтерактивного cmd/powershell исполнения и вывода; не remote desktop ctl.

## Границы ответственности
MenuBuilder — authorized UI/BFF; app1 — task/session orchestration; l4con — process/cleanup/output.
Схема и migrations app1 здесь не установлены; не добавлять общие DB модели по предположению.

## Внешние контракты
| Direction | Transport | Endpoint/topic | Main payload | Guarantees |
|---|---|---|---|---|
| app1 → l4con | MQTT | srv/{SN}/tsk → srv/{SN}/rsp | task id, method_code, session_id, ttl_sec | анонс → запрос → тело |
| l4con → app1 | MQTT | dev/{SN}/req → dev/{SN}/res | correlationData, result/exit_code | корреляция задачи |
| l4con → app1/UI | MQTT | dev/{SN}/out | session_id, seq, data, eof | volatile output, no retain |
| l4con → server | MQTT | dev/{SN}/svc | svc_online/offline | retained presence/LWT |

RPC: 7001 exec, 7002 cancel, 7003 ping, 7004 session keepalive; 7000/7005 описаны в docs,
поддержку конкретной версии подтверждать кодом. Полные QoS — [MQTT matrix](mqtt-topic-matrix.md).

## Инварианты
- Никаких произвольных cmd/ctrl/debug topics и retained tasks/output.
- Неинтерактивное выполнение с TTL/output cap; нет бесконечной shell-сессии через probe.
- Device identity не получает права server publisher; lease bypass запрещён.
- Поля console не заменять command_id/lease_id от ctl; корреляция по schema метода.
- `l4desk_owner` может открыть console только в своём tenant с явным console lease;
  диагностика сверяет SN, tenant, владельца, browser session и роль аренды.
  Неявная console lease доступна только `superuser`.

## State machine
task announced → parameters requested → running/output(seq) → eof + final result;
cancel/lease timeout → cleanup процесса → итог. Retry/duplicate и потеря out требуют
проверки конечного res, не заявления exactly-once.

## Ключевые исходники
- [DeviceConsoleTab.tsx](../../MenuBuilder/frontend/src/routes/devices/DeviceConsoleTab.tsx) — UI flow.
- [l4con](../../tools/l4con) — terminal consumer (открывать только при разрешённом tools scope).
- [iot_client.py](../../MenuBuilder/backend/app/services/iot_client.py) — BFF/app1 boundary.

## Проверка
Read-only preset/ping сначала; exec → seq/eof/res, duplicate task, stale session,
max output, TTL kill, cancel/reconnect и lost consumer. Process kill только test-owned PID.

## Известные риски и незавершённые вопросы

2026-10-01: l4mcp добавляет console_send_event через тот же lease/executor;
correlation_id возвращается с console_session_id, raw command не дополняется.
Только фиксированный безопасно сгенерированный вызов имеет cap4096 (IoT API);
raw console_run сохраняет cap2048. История900–999 читается через MenuBuilder,
без lease и event offset; UUID448 не является правом доступа. Пустая история
773 после IoT migration объясняется неизвестным tenant старых строк; новые
строки получают tenant. Проверки и состояние:
[план](../../docs/menu_arch-l4mcp-user-events-plan.md).
E2E выявил native truncation1023 bytes; l4con1.9.4 согласован с4096 Unicode
characters и strict error/EOF126, x86/x64 проверены. Установлен на773;
full1024-byte payload, UUID, history, tenant refusal и storm прошли.
Остальные l4con/app1 handlers не исследованы этой карточкой; matrix — утверждённый документ,
не runtime evidence. Вывод shell может содержать секреты — редактировать evidence до сохранения.

## Источники и актуальность
- Authoritative docs: [console protocol](../../docs/ops_run-remote-console-diagnostics.md), [AGENTS](../../AGENTS.md).
- Code references: entry points выше, не полный аудит.
- Проверено: 2026-09-11, HEAD `63ce6a7`, документальные контракты.
- 2026-09-26: правило роли 5 подтверждено пользователем; код app1 и локальные
  тесты обновлены, runtime owner console ещё не проверен.
- Обновить при: RPC method/schema, session lease, output framing/limits, cancel или shell policy.
