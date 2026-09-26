# Remote control: UI → BFF → app1 → l4desk

## Назначение
Карта границ аренды/ввода; не смешивать media viewing с правом управлять desktop.

## Границы ответственности
MenuBuilder — browser auth/device access, app1 — lease/command owner,
l4desk — terminal validation/action; l4media не владеет lease. Общие миграции не требуются
без изменения данных; при изменении схемы владелец определяется отдельно.

## Внешние контракты
Публичные пути ниже указаны относительно router, префикс проверять в регистрации приложения.

| Direction | Transport | Endpoint/topic | Main payload | Guarantees |
|---|---|---|---|---|
| UI → BFF | REST | /devices/{device_id}/control/keepalive | lease_id | device access + video-view permission |
| BFF → app1 | REST | /api/internal/v1/remote-input/lease/{lease_id}/keepalive | доверенные org/user headers | feature flag/timeout/error mapping |
| UI → BFF → app1 | WS / REST fallback | video_control routes | pointer_move, mouse_click, key/key_event | lease/scope/source/epoch validation |
| app1 → l4desk | MQTT | srv/{SN}/ctl | ctl v1 | command TTL, no retain |
| l4desk → app1 → UI | MQTT → WS/status | dev/{SN}/ctl | stream_event, ack/nack | reason/correlation, не локальный UI guess |

## Инварианты
- SN ≠ device_id; lease_id ≠ stream_instance_id; client_ref ≠ command_id.
- Trusted headers формирует backend, не browser. Key/PIN/JWT не логировать.
- Ввод только при активном desktop control; camera/view-only — без pointer/key.
- Keepalive только подтверждённой сессии; stop/error/unmount/lost rights выключают таймеры.
- Terminal event имеет приоритет над оптимистичным UI running; stopped/error показывает reason.

## State machine
idle → acquiring → active lease → keepalive → release/error/expired;
локальный stream параллелен: [lease lifecycle](lease-lifecycle.md).
UI нельзя оставлять active после stopped/reconcile или нового stream_instance_id.

## Ключевые исходники
- [video_control.py](../../MenuBuilder/backend/app/routers/video_control.py) — routes/DTO/security.
- [iot_client.py](../../MenuBuilder/backend/app/services/iot_client.py) — upstream API.
- [useRemoteControl.ts](../../MenuBuilder/frontend/src/hooks/useRemoteControl.ts) — WS heartbeat (5 с в коде).
- [RemoteControlOverlay.tsx](../../MenuBuilder/frontend/src/components/RemoteControlOverlay.tsx) — input.

## Проверка
REST и WS keepalive раздельно, wrong owner/tenant/lease, camera input denied, late event,
no pointer before lease, stop/unmount/reconnect без второго таймера и без stuck input.
Проверить passive wheel/pointer capture/background throttling в браузере.

## Известные риски и незавершённые вопросы
Исторический BFF fallback running не доказывает terminal ACK. 5-секундный UI timer
не гарантируется фоновой вкладкой. Серверный bind lease↔device↔owner требует app1 проверки.

## 17E corrective, 2026-09-26
`l4desk_owner` (роль 5) получает view/stream/input и console lease только для
своего tenant; console WS требует явный lease с совпадающими SN, tenant,
owner_user_id и browser session. Источник — изменения MenuBuilder и app1 по
17E; до развёртывания и browser E2E это уровень «код + локальные тесты».
Публичный BFF принимает `session_id` при acquire только если он совпадает с
каноническим session ID в JWT; release использует тот же JWT context.

## Источники и актуальность
- Authoritative docs: [remote-input](../../docs/etran_arch-remote-input-control.md),
  [E2E](../../docs/etran_arch-video-remote-desktop-e2e.md).
- Code references: BFF/client keepalive просмотрены, hook heartbeat найден; browser не запускался.
- Проверено: 2026-09-11, HEAD `63ce6a7`, выборочный код + документы.
- Обновить при: routes/DTO, permissions, heartbeat, input types или UI state handling.
