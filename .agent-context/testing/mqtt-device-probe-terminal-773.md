# MQTT Device Probe: terminal 773

## Назначение
Утверждённый пользователем mqttx test probe существующих MQTT-контрактов:
сначала passive observe, затем ограниченная эмуляция device messages.

## Идентичность
- Terminal SN: `a4b0000773c82116d210826`; terminal ID: `773`.
- Default local broker: `127.0.0.1:1883`; mqttx указан пользователем как локальный клиент.
- Наличие mqttx, broker и binding identity в сервисах в этой задаче не проверялось.
- TLS/ключи/пароли/пути к ключам/connection profile не хранятся в репозитории.

## Границы ответственности
Probe — наблюдатель либо device emulator, не server publisher и не реальный l4desk/FFmpeg.
Owner тестового контура подтверждает isolation, identity, client_id и разрешённые ACL.
Миграции/новые протоколы не входят в probe. Loopback с bridge не считается изолированным.

## Внешние контракты
| Direction | Transport | Endpoint/topic | Main payload | Guarantees |
|---|---|---|---|---|
| observer ← approved flow | MQTT subscribe | только точные разрешённые topics SN | raw contract events, redacted evidence | без publish/LWT/status overwrite |
| probe → server consumer | MQTT publish | разрешённые dev/{SN} из matrix | device event schema текущей версии | test-owned session; не server ACL |
| UI/API → app1 → l4desk | REST/WS → MQTT | srv/{SN}/ctl | настоящие команды | probe лишь наблюдает, не обходит lease |

Точные topics/QoS/retain/types: [MQTT matrix](../contracts/mqtt-topic-matrix.md).
Для observer нужен уникальный согласованный client_id: повтор действующего ID способен
отключить реального клиента и вызвать его Will. Если безопасное подключение невозможно — не подключаться.

## Инварианты
- Не публиковать в ad-hoc topics или в сторону, не разрешённую device ACL.
- Не использовать retained commands/ACK/NACK/diagnostic payload/output.
  Исключение — утверждённый presence flow, только с sole-owner согласованием.
- Не подменять production-терминал, не обходить lease validation, не выдавать
  MQTT publish за действие ОС. Не выполнять destructive actions без test scope/cleanup.
- Для создания/изменения клиента обязательное уточнение main_app/extra_service;
  existing svc_desk не мигрировать автоматически. Наблюдение не создаёт новый presence контракт.
- Test ID обязателен в evidence; wire fields только по schema: ctl command_id,
  lease_id и stream_instance_id когда применимы, version/timestamp; для console —
  correlationData/session_id/seq. Не добавлять новые поля ради теста.

## State machine
scope/identity/ACL confirmed → passive observe → отдельно approved publish/negative case
→ collect evidence → stop/release/disconnect/cleanup. Без подтверждения scope — stop.

## Ключевые исходники
- [ctl_protocol.c](../../tools/l4desk/src/ctl_protocol.c) — реальная terminal schema.
- [ffmpeg_supervisor.c](../../tools/l4desk/src/ffmpeg_supervisor.c) — recovery/watchdog.
- [iot_client.py](../../MenuBuilder/backend/app/services/iot_client.py) — штатный API command path.

## Проверка
| Сценарий | Ожидаемое / граница доказательства |
|---|---|
| Passive observe | lease_renew → ACK и stream_event, correlation/UTC/retain flags |
| Presence / inventory_get | утверждённый status/inventory consumer; presence — state-changing |
| Contract publisher event | schema parsing/server state; не работа FFmpeg |
| Duplicate ID / repeated event | idempotency/исходный ответ в разрешённом TTL, нет второго side effect |
| Wrong lease_id | NACK lease_mismatch на применимом ctl path, состояние неизменно |
| Expired timestamp / unknown type/version | отказ или fallback строго по versioned contract |
| Invalid JSON | parse error без crash/reconnect-loop; только изолированный negative case |
| Lease lifecycle | фиксировать stream_instance_id/lease_id/command_id; keepalive по UI-периоду, expiry вырос, FFmpeg жив дольше исходного окна |
| Stop keepalive | stopped/lease_expired; FFmpeg не перезапускается |
| Unexpected exit | при valid lease единственный test-owned PID завершён по разрешению; restarting → bounded retry → running/recovered |
| Agent restart | собственный orphan завершён, stopped/agent_restart_reconcile, UI обновлён |

Recovery не имитировать через mqttx. Проверять PID, creation time/ownership и test session
перед разрешённым завершением; массовое завершение процессов по имени запрещено.
В агентской среде управлять только PID, разрешёнными доступными средствами/правилами процесса.
Cleanup: stop/release через API, disconnect observer, убрать только свои test artifacts;
не очищать retained topics чужого клиента. Результат — [шаблон](mqtt-device-probe-result-template.md).

## Известные риски и незавершённые вопросы
Production binding/ACL/bridge isolation неизвестны; без подтверждения запуск запрещён.
Known renew/expiry/dedup gaps — [lease card](../contracts/lease-lifecycle.md).
Новый diagnostic topic не создаётся: требует отдельного protocol proposal с schema/version,
ACL, no retain, short TTL, correlation, read-only whitelist, audit, production-off flag,
документацией и тестами; не замена ctl/tsk/rsp/req/res/out и не shell bypass.

## Источники и актуальность
- Authoritative docs: [AGENTS](../../AGENTS.md), [console](../../docs/ops_run-remote-console-diagnostics.md),
  [remote-input](../../docs/etran_arch-remote-input-control.md), [MQTT](../contracts/mqtt-topic-matrix.md),
  [lease](../contracts/lease-lifecycle.md); identity/default local endpoint — запрос пользователя.
- Code references: ctl/supervisor просмотрены, probe не запускался.
- Проверено: 2026-09-11, HEAD `63ce6a7`, документальная инструкция, runtime **not run**.
- Обновить при: test identity/scope, protocol version, ACL/bridge, tool mode или cleanup policy.