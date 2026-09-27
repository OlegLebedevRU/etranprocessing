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
- Video/console usage: `L4DeskRemoteSession.last_cursor` — последняя учтённая целая UTC-секунда. Подтверждённые периоды по 60 с, финальный хвост и курсор фиксируются атомарно в PostgreSQL. Для истёкшего provider epoch BFF закрывает старую сессию с waiver недоказанного хвоста; новая эпоха получает отдельную сессию. Округление к целым секундам выполняется один раз для сессии в пользу потребителя.

## State machine
Для remote control: idle → acquire → active → stop/error/release;
для схемы — переходы по доменной задаче, не единая state machine всего портала.
Видеопуть и keepalive: [remote control](../contracts/remote-control.md).

## Ключевые исходники
- [video_control.py](../../MenuBuilder/backend/app/routers/video_control.py) — DTO, auth и BFF control.
- [iot_client.py](../../MenuBuilder/backend/app/services/iot_client.py) — app1 client/error mapping.
- [remote_session_metering.py](../../MenuBuilder/backend/app/services/remote_session_metering.py) — порции, курсор, audit и waiver.
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

17E corrective (2026-09-26, код + локальные тесты): video/console session
create/close фиксируются транзакцией по `terminal.id`; закрытие измеряет
интервал в `FinUsageDaily`. IoT consumer применяет monthly charge только для
tenant из `IOT_CONSUMER_FINANCE_TENANT_IDS` при выключенном shadow mode и
подтверждённом `device_online`. Server/browser E2E остаётся необходимым.

17E free quota candidate (2026-09-27, локальный код + тесты): entitlement
читает локальную дату из `L4DeskTenantProfile.timezone`, как metering. Для
изолированного E2E доступен адресный override 600 секунд, выключенный по
умолчанию; production admission остаётся permissive до отдельного rollout.

17E runtime correction (2026-09-27): регистрация выделила `org_id=4`,
проверив свободный ID только в MenuBuilder; в IoT этот ID уже существовал.
Для теста владелец перенесён в свободный в обоих контурах tenant 1000.
Исправление 17E резервирует ID в IoT через защищённый internal API до
создания tenant и повторно использует резерв по ID регистрации. Commit
`224daaf` развёрнут на 87.242.100.34 2026-09-27; API из backend получил
`409 org_id_already_in_use` для занятого IoT ID 4. В изолированном 17E
backend test05 зарегистрирован с `org_id=10000`, ролью владельца 5 и
одной owner membership; повтор ссылки не создал дубликатов. Gate коллизии
`org_id` закрыт, глобальный production registration flag остаётся выключен.
Периодические сбросы новых asyncpg SSL
соединений к PostgreSQL дают 500; `DatabaseUserStore` при этом может
вернуть ложный 401, скрыв сбой БД. Причина и исправление ещё не проверены.
17E DB resilience candidate (2026-09-27, локальный код + 500 тестов):
новый connect в MenuBuilder повторяется до пяти раз только при
транспортном сбое, тайм-ауте или `CannotConnectNow`; `pool_pre_ping` остаётся.
Серверный smoke 2026-09-27: первый connect на старте получил reset,
повтор прошёл, schema revision 027 подтверждена, status GET ответил 200.
Это не восстанавливает операцию при обрыве внутри транзакции и не закрывает
осиротевшую коммерческую сессию.
Изолированный E2E tenant 1000 с квотой 600 секунд прошёл video metering:
после штатных 635 секунд видео плюс 8 секунд консоли policy seam вернул
`free_quota_exceeded` при 643/600 и нулевом балансе. HTTP admission в
изолированном backend ещё не проверен из-за смены тестового пароля.

## Источники и актуальность
- Authoritative docs: [ownership](../../docs/etran_data-database-ownership.md),
  [E2E](../../docs/etran_arch-video-remote-desktop-e2e.md), [AGENTS](../../AGENTS.md).
- Code references: BFF keepalive/IoT client просмотрены; UI-точки — навигация, не полный аудит.
- Watch сверено 2026-09-26 по provider contract `H-L4D-17C-VIDEO-WATCH-IOT-01-v1` и локальному коду app1; BFF/UI проверены локальными тестами и сборкой, browser E2E не выполнялся.
- Обновить при: auth, routes/DTO, UI timers/state, billing или ownership.
