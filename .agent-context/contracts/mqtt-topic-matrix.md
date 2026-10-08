# MQTT: remote control, console и presence

## L4Update — принятая архитектура 2026-10-06, не реализовано

[Update flow](../../docs/term_arch-l4update-flow.md): штатные RPC и EVT/EVA без
новых топиков/MQTT-клиента. Один код события, весь update объект в одном числовом
теге; номера пока не выделены. Барьер REQ/RSP+свежий EVT/EVA до/после переключений.
Идентичные MQTT client ID одновременно запрещены, в том числе при временной пробе.
Обновление путей/consumer новых703x требует отдельной реализации и verification.

## L4FM v2 — выпущенный контракт 2026-10-05

[FM](file-manager.md): start/renew через RPC7023, transfer через7021;
list/stop — отдельные srv/{SN}/fmc и dev/{SN}/fmr, envelope v=2.
7020/7022/7023-stop отклоняются, v1 fallback отсутствует. PB обслуживает
readiness/session/transfer metadata, но не list. File bytes только S3 через
Leo4Proxy на терминале. app1 binding dev.*.fmr → fm_result_v1, shared Redis
correlation; queue name не версия wire protocol. Mosquitto contract3 имеет
14 явных собственных routes, без wildcard. Live базовый FM flow подтверждён
на1000007/suite1.13.0, установка1.13.1 и fresh registration проверены отдельно.

## 2026-09-30 addition

`l4con` (`extra_service`) publishes event 75 to `dev/{SN}/evt` with QoS 1
and retain false. The payload and tags are documented in
[event 75 inventory](../../docs/term_tool-event75-inventory.md). This does
not change `dev/{SN}/svc` presence or console RPC topics.

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
| app1 → l4con | MQTT `srv/{SN}/fmc` | v2 list/stop: command_id, lease_id, expiry; path/offset только list | 1 (bridge) | false | UUID + SN + lease; 7с bounded exchange |
| l4con → app1 | MQTT `dev/{SN}/fmr` | v2 completed/failed; до64 entries/24КиБ; correlationData | 1 | false | pending до publish, first valid reply wins; stop после worker exit |
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
- Runtime evidence ограничено указанными flow/версиями; полная ACL/fault matrix не закрыта.

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

## Explicit Mosquitto routes — 2026-10-05 implementation

Shared generator/migration now emits topic contract 3, without wildcard or overlapping bridge routes:
- outbound `dev/{SN}/`: `app`, `svc`, `evt`, `req`, `res`, `out`, `ctl`, `fmr` (QoS 1);
- inbound `srv/{SN}/`: `tsk`, `rsp`, `eva`, `cmt`, `ctl`, `fmc` (QoS 1).

Sources checked: l4con MQTT publishers/subscriptions, l4desk ctl, app1
core/config.py and core/services/device_task_processing.py (eva/cmt).
Presence app/svc retained payloads are unchanged. Bridge routes do not change retain semantics.
Known old own-SN wildcards are migration input only; emitted config has exactly 14 distinct routes.
Foreign-SN, unknown routes and remapping are rejected without replacing the existing config.
The active-config validator requires the exact route set, direction, QoS and SN; comments cannot
satisfy routes, duplicates/wildcards invalidate the configuration. Setup uses supervisor's
prepare-mosquitto path and therefore the same migration. Local builds/tests are not live broker evidence.


## L4Con readiness barrier — 2026-10-06

L4Con now subscribes QoS1 srv/{SN}/eva alongside existing tsk/rsp/fmc, tracking
successful tsk/rsp/eva SUBACKs for local readiness. Its private native health IPC
uses the existing production MQTT connection to send ordinary zero-UUID REQ, await
valid RSP/NOP, force fresh existing event75, and validate EVA success by UUID,
event_type_code=75 and dev_event_id. IoT producer checked in app-service/core/services/
device_task_processing.py and device_events_collect.py: NOP method_code0/zeroUUID,
EVA status success/error plus matching metadata. Presence remains extra_service;
no duplicate CONNECT/client ID or backend change. Native packet/IPC evidence only;
actual IoT/terminal gate remains pending. No new update event code/tag allocated here.
