# MenuBuilder

## Назначение
Пользовательский портал, tenant/admin flows, billing, BFF управления терминалами.

## Границы ответственности
Владеет пользователями, org/menu/billing и назначениями терминалов по ownership matrix.
Не владеет payment ledger, server lease app1 или процессом FFmpeg.
Общие ORM — shared; физические Alembic — только ProcessingBackend.

## Внешние контракты
| Direction | Transport | Endpoint/topic | Main payload | Guarantees |
|---|---|---|---|---|
| Browser → BFF | HTTPS/WS | video/control, console, portal API | device_id, lease_id, JWT context | tenant/device/permission boundary |
| BFF → app1 | REST/WS | `/api/internal/v1/remote-input` | SN, lease, trusted org/user headers | нельзя передавать browser-supplied identity как trusted |
| BFF → shared DB | async SQLAlchemy | menu, billing, orgs | domain models | запись только владельца |

## Инварианты
- org_id из JWT приводится к int на auth boundary; UI permission не заменяет серверную проверку.
- UI, серверная lease и terminal stream — разные состояния; terminal event исправляет UI.
- `X-Internal-Service-Key` не попадает в browser, логи или карточки.
- Video watch: BFF проверяет `video:view` и tenant по `device_id`, подключается к app1 `/ws/watch/{sn}` с внутренним ключом и передаёт браузеру только `invalidate`. UI после каждого сигнала и reconnect читает REST snapshot; lease keepalive остаётся отдельным, счётчик кадров берётся из WebRTC `getStats()` в браузере.

## State machine
Для remote control: idle → acquire → active → stop/error/release;
для схемы — переходы по доменной задаче, не единая state machine всего портала.
Видеопуть и keepalive: [remote control](../contracts/remote-control.md).

## Ключевые исходники
- [video_control.py](../../MenuBuilder/backend/app/routers/video_control.py) — DTO, auth и BFF control.
- [iot_client.py](../../MenuBuilder/backend/app/services/iot_client.py) — app1 client/error mapping.
- [video.py](../../MenuBuilder/backend/app/routers/video.py) — session, ingress/Janus/PIN.
- [useRemoteControl.ts](../../MenuBuilder/frontend/src/hooks/useRemoteControl.ts) — WS/input/keepalive.
- [RemoteControlPanel.tsx](../../MenuBuilder/frontend/src/components/video/RemoteControlPanel.tsx) — UI control.
- [RemoteControlOverlay.tsx](../../MenuBuilder/frontend/src/components/RemoteControlOverlay.tsx) — pointer/canvas.
- [video-surveillance.tsx](../../MenuBuilder/frontend/src/routes/video-surveillance.tsx) — browser watch, REST resnapshot и локальная статистика WebRTC.

## Проверка
Backend code: uv run pytest + ruff/format/pyright; frontend code: npm run build +
релевантные UI-тесты. Negative: wrong tenant, string org_id, expired lease, lost permissions,
camera/view-only input, stop/unmount timers, late events. См. [матрицу](../operations/validation-matrix.md).

## Известные риски и незавершённые вопросы
Compatibility fallback `running` не доказывает ACK/кадры. REST и WS keepalive
нужно проверять раздельно. IoT watch v1 зависит от `WEB_CONCURRENCY=1`; browser E2E качества изображения ещё требует проверки.

## Источники и актуальность
- Authoritative docs: [ownership](../../docs/etran_data-database-ownership.md),
  [E2E](../../docs/etran_arch-video-remote-desktop-e2e.md), [AGENTS](../../AGENTS.md).
- Code references: BFF keepalive/IoT client просмотрены; UI-точки — навигация, не полный аудит.
- Watch сверено 2026-09-26 по provider contract `H-L4D-17C-VIDEO-WATCH-IOT-01-v1` и локальному коду app1; BFF/UI проверены локальными тестами и сборкой, browser E2E не выполнялся.
- Обновить при: auth, routes/DTO, UI timers/state, billing или ownership.
