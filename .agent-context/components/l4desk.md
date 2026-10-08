# l4desk

## Windows layout — source/local gates 2026-10-06

EXE-derived installed binary root; config/l4desk policy, state/l4desk media state,
logs/l4desk and logs/ffmpeg use ProgramData. Supervisor reads the same media state
path. Installed CLI/environment redirects refused. [Packet](../tasks/active/2026-10-06-l4layout-stage2.md):
x86/x64 builds/path gates and full x64 native tests PASS using temporary roots/fake
FFmpeg. Installed writable ACL/live gate absent; svc_desk presence/ctl unchanged.

## 2026-10-02 stabilization1.9.3
Bounded NO_PROXY SN discovery; ctl/presence/input contracts unchanged. x86/x64 build passed; signed1.9.4 suite installed773, local l4desk/capture probes passed. Remote injection E2E was not repeated. [Handoff](../tasks/completed/2026-10-02-l4tools-stabilization-ui-release.md).

Suite1.9.6 retains l4desk1.9.3. Supervisor status now discovers the live process
by exact EXE path/console session; setup waits up to15s for asynchronous launch.
Clean signed install773 passed local probes; reboot and remote input/video E2E
remain untested. [Decisions](../../docs/term_arch-l4tools-stabilization-decisions.md),
[release handoff](../tasks/completed/2026-10-02-l4superv-service-dependencies.md).


## Назначение
Windows-агент remote desktop input и жизненного цикла видеопроцесса
(`l4capture` при доступности, FFmpeg как fallback).

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
| supervisor → l4capture/FFmpeg | Win32 process | локальный процесс | mode/source/profile | ownership, cleanup и watchdog |

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
- После изменений C: из tools\l4desk выполнить `cmd /c build.cmd all` (сборка /MT x86 и x64).
- Проверить свежие `bin\x86\l4desk.exe`, `bin\x64\l4desk.exe`, `bin\l4desk.exe` (v1.5.0);
  default копируется из x86, x86 — совместимость Windows 7 SP1+/WOW64.
- Запуск тестов: `cmd /c tools\l4desk\tests\run_tests.cmd` (модульные тесты C) и
  `python tools\l4desk\tests\integration_test.py` (сквозная интеграция с Mosquitto).
- [Матрица](../operations/validation-matrix.md): renew/NACK/dedup/expiry/recovery/reconcile,
  x86 и x64; сборка не заменяет runtime/e2e. Для doc-only сборка не нужна.

## Актуальный статус реализации (ветка release/l4tools-1.8.2-beta-1)
- Строгая проверка эпохи `stream_instance_id` (NACK `stream_mismatch`).
- Строгая валидация дедлайна `expires_at_ms > now_ms` (NACK `invalid_payload`).
- Сессионный мьютекс изолирован по SN: `Local\L4Desk_SingleInstance_<SN>`.
- Подтверждена обработка канонического `command_id` UUID из `app1`.
- На терминале 773 проверен x64 l4capture через l4desk, два экрана целиком,
  быстрый старт, приемлемое качество и отсутствие заметной задержки. Серверный
  media route renew и cleanup stop подтверждены в том же E2E. Подробности,
  точные бинарные хеши и ограничения — в [handoff](../tasks/l4tools-1.8.2-beta-1-handoff.md).
- Тип MQTT-клиента: `svc_desk`; retained presence только `dev/{SN}/ctl`.

## Источники и актуальность
- Authoritative docs: [E2E](../../docs/etran_arch-video-remote-desktop-e2e.md),
  [remote input](../../docs/etran_arch-remote-input-control.md), [AGENTS](../../AGENTS.md),
  [README](../../tools/l4desk/README.md), [CHANGELOG](../../tools/l4desk/CHANGELOG.md).
- Code references: ctl_protocol, supervisor и build.cmd просмотрены; остальные — точки входа.
- Актуализировано: 2026-09-26, объединение L4D и L4C для 1.8.2-beta-1.
- Обновить при: ctl, input policy, FFmpeg lifecycle, build/артефактах.
