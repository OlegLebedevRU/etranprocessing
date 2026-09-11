# l4desk

## Назначение
Windows-агент remote desktop input и жизненного цикла FFmpeg.

## Границы ответственности
Парсит ctl, проверяет локальное состояние, управляет вводом/процессом/таймером,
публикует ACK/NACK и stream_event. Не владеет серверной lease, JWT или media ingress.
Общих DB-моделей и Alembic у агента нет.

## Внешние контракты
| Direction | Transport | Endpoint/topic | Main payload | Guarantees |
|---|---|---|---|---|
| app1 → agent | MQTT | `srv/{SN}/ctl` | inventory_get, stream_start/stop, lease_renew, input | QoS 1, no retain, dedup command_id |
| agent → app1 | MQTT | `dev/{SN}/ctl` | ack/nack, stream_event | no retain, timestamp/correlation |
| agent → app1 | MQTT | `dev/{SN}/ctl` | presence l4desk online/offline | retained, LWT; не dev/{SN}/svc |
| supervisor → FFmpeg | Win32 process | локальный процесс | mode/source/profile | ownership, cleanup и watchdog |

Input: pointer_move, mouse_click (e2e — left), key_event по whitelist/политике.
Наличие локальных right/middle не означает разрешение расширять серверный API.
Для pointer_move нет поштучного ACK; это исключение, не шаблон silent ignore для renew.

## Инварианты
- [lease_expired ≠ unexpected_exit](../contracts/lease-lifecycle.md).
- Только утверждённые ctl-топики; не занимать retained svc/app l4con/main_app.
- Ввод разрешён только для desktop, нужной lease/эпохи и интерактивной Windows session.
- Не менять packager/installer/distribution без прямой задачи. При конфликте общих
  правил packaging с узким scope запросить согласование, не расширять diff.

## State machine
running → restarting/unexpected_exit → running/recovered либо failed/restart_limit;
running/restarting → stopped/lease_expired без restart;
agent restart → reconcile → stopped/agent_restart_reconcile.
Состояния source_unavailable/session_unavailable требуют отдельного отказа, не бесконечного recovery.

## Ключевые исходники
- [ctl_protocol.c](../../tools/l4desk/src/ctl_protocol.c) — parse, validation, dedup, response.
- [ffmpeg_supervisor.c](../../tools/l4desk/src/ffmpeg_supervisor.c) — process ownership, expiry/recovery/reconcile.
- [mqtt_client.c](../../tools/l4desk/src/mqtt_client.c) — подключение/publish.
- [input_inject.c](../../tools/l4desk/src/input_inject.c) — whitelist и release_all.
- [build.cmd](../../tools/l4desk/build.cmd) — штатная MSVC /MT сборка.

## Проверка
- После изменений C: из tools\l4desk выполнить `cmd /c build.cmd all`.
- Проверить свежие `bin\x86\l4desk.exe`, `bin\x64\l4desk.exe`, `bin\l4desk.exe`;
  default копируется из x86, x86 — совместимость Windows 7 SP1+/WOW64.
- [Матрица](../operations/validation-matrix.md): renew/NACK/dedup/expiry/recovery/reconcile,
  x86 и x64; сборка не заменяет runtime/e2e. Для doc-only сборка не нужна.

## Известные риски и незавершённые вопросы
Renew ACK без expiry и границы grace/dedup — [известные gaps](../contracts/lease-lifecycle.md).
app1 и UI propagation не проверялись runtime. MQTT client type требует уточнения
перед изменением клиента; эта карточка лишь описывает существующий svc_desk.

## Источники и актуальность
- Authoritative docs: [E2E](../../docs/etran_arch-video-remote-desktop-e2e.md),
  [remote input](../../docs/etran_arch-remote-input-control.md), [AGENTS](../../AGENTS.md).
- Code references: ctl_protocol, supervisor и build.cmd просмотрены; остальные — точки входа.
- Проверено: 2026-09-11, HEAD `63ce6a7`, код/документы, без build/runtime.
- Обновить при: ctl, input policy, FFmpeg lifecycle, build/артефактах.