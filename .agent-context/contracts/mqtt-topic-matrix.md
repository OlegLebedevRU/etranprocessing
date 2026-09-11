# MQTT: remote control, console и presence

## Назначение
Сжатая матрица утверждённых топиков этих flow. Не разрешает остальные топики платформы.

## Границы ответственности
app1 — серверные команды/lease; l4desk — ctl/input/FFmpeg; l4con — console RPC.
Брокер отвечает за transport/ACL, не за результат исполнения. Миграций здесь нет.

## Внешние контракты
| Producer → consumer | Transport / topic | Main payload / допустимые типы | QoS | Retain | Correlation / guarantees |
|---|---|---|---|---|---|
| app1 → l4desk | MQTT `srv/{SN}/ctl` | ctl v1: inventory_get, stream_start/stop, lease_renew; pointer_move, mouse_click, key_event | 1 | false | command_id, lease_id, stream_instance_id при наличии; command TTL |
| l4desk → app1 | MQTT `dev/{SN}/ctl` | ack, nack, stream_event | 1 | false | command_id для ответа; stream_instance_id для event; timestamp |
| l4desk → app1 | MQTT `dev/{SN}/ctl` | presence JSON, agent=l4desk, status=online/offline | 1 | true | timestamp, stale evaluation; не ACK команды |
| app1 → l4con | MQTT `srv/{SN}/tsk` | анонс задачи, method_code | 1 | false | id, correlationData |
| app1 → l4con | MQTT `srv/{SN}/rsp` | RPC task: 7001 exec, 7002 cancel, 7003 ping, 7004 keepalive | 1 | false | id, session_id, ttl_sec; точный schema по методу |
| app1 → l4con | MQTT `srv/{SN}/cmt` | опциональный commit | 1 | false | контракт задачи; не отдельная ctl-команда |
| l4con → app1 | MQTT `dev/{SN}/req` | запрос тела задачи | 0/1 | false | correlationData |
| l4con → app1 | MQTT `dev/{SN}/res` | итог status_code, exit_code, duration_ms | 1 | false | task_id, correlationData |
| l4con → app1/UI | MQTT `dev/{SN}/out` | stdout/stderr, data, seq, eof | 0/1 | false | session_id; volatile, не долговечный журнал |
| extra_service (l4con) → server | MQTT `dev/{SN}/svc` | svc_online / svc_offline | 1 | true | выбранный тип клиента, LWT |
| main_app → server | MQTT `dev/{SN}/app` | app_online / app_offline | 1 | true | выбранный тип клиента, LWT |

ctl envelope команд: `v`, `type`, `command_id`, `sn`, `issued_at_ms`, `expires_at_ms`;
lease_id зависит от команды (inventory допускает отсутствие). Не переименовывать
wire `command_id` в `cmd_id`. Схемы console RPC отличаются: не добавлять `v`, lease_id
или test correlation id туда, где их не допускает контракт. ID теста хранить во внешнем evidence.

## Инварианты
- Запрещены ad-hoc `srv/{SN}/cmd`, `dev/{SN}/ctrl`, debug/test-каналы и retained actions.
- `svc_desk` — существующий отдельный ctl presence; перенос на svc конфликтует с l4con.
- Will до CONNECT; retained online после CONNACK; offline перед DISCONNECT.
- MQTT device identity не даёт права публиковать server-side команды; проверяй ACL направления.
- Политика QoS/retain указана по docs; ACL и доставка runtime в этой задаче не проверены.

## State machine
CONNECT/Will → CONNACK → subscribe/presence → commands/results → offline/DISCONNECT;
аварийный offline — Will. Retained online без свежего last_seen не доказывает доступность.

## Ключевые исходники
- [ctl_protocol.c](../../tools/l4desk/src/ctl_protocol.c) — parse/validate/ACK/NACK/dedup.
- [mqtt_client.c](../../tools/l4desk/src/mqtt_client.c) — transport/publish (точка повторной сверки).
- [iot_client.py](../../MenuBuilder/backend/app/services/iot_client.py) — BFF, не MQTT publisher app1.

## Проверка
Схема/TTL, duplicate и expired, ACL wrong SN/direction, reconnect, retain и late event.
Сначала passive observe по [probe card](../testing/mqtt-device-probe-terminal-773.md).

## Известные риски и незавершённые вопросы
- `stream_renew` распознаётся C как alias lease_renew; серверную генерацию не предполагать.
- В корневых правилах перечислен svc_desk, в пользовательских guidelines — два типа;
  до изменения MQTT-клиента уточнить тип, не выбирать и не мигрировать автоматически.
- Исторически упоминаемый `docs/remote-input-protocol.md` отсутствует; не считать его
  проверенным источником. Пользоваться существующей архитектурой и C, для расхождений — согласование.

## Источники и актуальность
- Authoritative docs: [remote-input](../../docs/etran_arch-remote-input-control.md),
  [E2E §2.4](../../docs/etran_arch-video-remote-desktop-e2e.md),
  [console matrix](../../docs/ops_run-remote-console-diagnostics.md), [AGENTS](../../AGENTS.md).
- Code references: C renew/dedup просмотрены; app1 отсутствует, публикации/ACL не запускались.
- Проверено: 2026-09-11, HEAD `63ce6a7`, документ + выборочный код, не runtime.
- Обновить при: schema/topic/type/QoS/retain/ACL, версии producer или consumer.