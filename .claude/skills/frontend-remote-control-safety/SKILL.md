---
name: frontend-remote-control-safety
description: Безопасные UI изменения Remote Desktop/video, pointer/canvas/wheel, keepalive, permissions и отображения stream state в MenuBuilder.
---

# Frontend remote-control safety

1. Раздели UI state, server lease и фактический stream агента.
   [Remote control](../../../.agent-context/contracts/remote-control.md) — стартовый контракт.
2. Запрещай pointer/mouse/key вне активного desktop control; camera/view-only
   не дают права ввода. Сервер проверяет permissions независимо от UI.
3. Keepalive стартует только после подтверждённой lease нужного scope и останавливается
   при stop/error/unmount/потере прав. Различай stream REST keepalive и control WS keepalive;
   проверь, что retry/reconnect не создаёт второй таймер или renew старой lease.
4. Проверяй passive wheel listeners, preventDefault, pointer capture/release,
   throttling, background-tab timers и stale closures.
5. Terminal stream_event должен корректировать UI; локальный running-флаг или
   успешный start HTTP не доказывают кадры. Badge stopped/error показывает reason.
6. Проверь acquire → active → stop, denial, late event, offline/reconnect и unmount;
   route/mountpoint должен существовать до stream_start, cleanup — при сбое.
7. После frontend code changes выполни `npm --prefix MenuBuilder\frontend run build`
   из корня, существующие релевантные UI-тесты и browser/runtime сценарии.

## Результат
UI/API state transitions, evidence отсутствия лишних команд и таймеров,
сборка и browser-проверки отдельно, незакрытые звенья E2E явно в handoff.