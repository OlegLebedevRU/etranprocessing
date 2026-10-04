# MenuBuilder

## 2026-10-05: RPC7011 authenticated renewal

BFF and form deployed with schema030/PB/IoT; operator installed signed tools1.11.0.
Action «Заказать удалённое продление» issues hidden purpose=renew PIN and queues
real IoTtask synchronously; offline execution/reporting is asynchronous. Three
manual repeats reusePIN, then3min cooldown; no outbox/new operation UUID.
PIN masked in command history; fresh PB current-serial discovery confirms use.
UI77tests/isolated browser and real7003 passed; actual7011 rotation remains open.
[Release evidence](../tasks/active/2026-10-04-rpc7011-implementation.md).

## 2026-10-03: public auth CAPTCHA and subscription numbers

Production backend c5db1c2 / frontend b066e59: runtime Yandex SmartCaptcha config,
server verification before login/register/resend; same key pair as landing via env.
Anonymous register/confirm stay public after refresh 401. Subscription rows expose
device_id; UI «Номер терминала» uses it, purchase identity stays terminal_id.
No schema changes. [Rules](../../docs/menu_auth-smartcaptcha.md),
[release evidence](../tasks/completed/2026-10-03-l4desk-auth-captcha.md).

## 2026-10-03: terminal subscriptions, production 8bb0359

Schema029; первый неудалённый терминал бесплатен, дополнительные — по paid_until
и трём календарным дням grace. YooKassa OFF блокирует их платные возможности;
один согласованный флаг открывает коммерческий режим. Старые money workers/API
заморожены; session/duration остаются техническим ядром без posting. UI показывает
состояние, следующий шаг, корзину, историю и отдельное время использования.
[Правила](../../docs/menu_bill-terminal-subscription.md),
[production evidence](../tasks/completed/2026-10-03-l4desk-terminal-subscriptions-release.md).

## 2026-10-02: media auth acceptance records

Final historical18D-AUTH report/candidate restored from97e39dd into main;
producer04560ca was already reachable. Report hash matches its candidate;
original immutable SHAs/verdicts preserved. No code, env, rotation or deployment.
See [consolidation handoff](../tasks/completed/2026-10-01-server-stack-consolidation.md).

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

2026-10-01: добавлен read-only MCP GET /api/mcp/events/{device_id} для истории
900–999 через internal IoT search. Token/current tenant проверяются на каждой
странице; даже superuser не выходит за active tenant. Offline чтение без lease,
проверяются SN/device binding; API не пишет consumer offset. План и состояние:
[user events](../../docs/menu_arch-l4mcp-user-events-plan.md),
[handoff](../tasks/completed/2026-10-01-menubuilder-user-events.md).
Positive E2E переносится на последующий этап l4mcp по решению пользователя.

Post-18E пакет `093f985` (2026-09-29, развёрнут и принят контроллером): tenant policy для
Classic/L4Desk и страниц лицензий читается из `orgs` через `/auth/me`; schema
`028` должна быть установлена до нового MenuBuilder image. Это ограничение
страниц, а не изменение финансовых API. Общая форма редактирования терминала
используется в Classic и L4Desk. Production read-only browser smoke выполнен;
startup guard обновлён в `bacea78`. Сценарий изменения tenant policy остаётся
проверенным на тестовом tenant 10000 с возвратом исходных значений. Corrective
`d823d56` установлен: `/auth/me` возвращает 503 при недоступной политике вместо
разрешающих defaults; независимое решение в
`MenuBuilder/docs/l4desk/handoffs/L4D-18E-MB-POSTFIX-01-controller.md`.
см. `MenuBuilder/docs/l4desk/handoffs/L4D-18E-MB-POSTFIX-01-report.md`.

17F payment recovery (2026-09-27): успешная оплата и повторный poll теперь
обновляют cached entitlement в транзакции платежа. На isolated image
`4184ee9` повтор mock payment 5 восстановил `active` в Hub tenant 10000,
сохранив баланс 1000 коп., projection version 3 и transaction 9. Production
source ещё `fb2273c`; полный rollout впереди. Regression проверяет полную
и частичную оплату без переноса anchor; 529 backend tests passed.
[Отчёт](../../MenuBuilder/docs/l4desk/handoffs/L4D-17F-MB-PAYMENT-RECOVERY-01-report.md).

17F grace test path (2026-09-27, локальные тесты + runtime E2E):
изолированный backend поддерживает allowlisted tenant clock offset до 7 дней
и адресный worker tick для короткой проверки grace/block/stop. Production
clock и commercial flags не менялись. Stop outbox теперь сверяет текущий
stream epoch в app1, использует lease/stream stop вместо remote-session stop
для video, освобождает lease и закрывает локальную сессию через metering.
Локально 524 теста прошли. В test-backend на tenant 10000 подтверждены
один monthly charge и идемпотентный повтор события, grace → blocked,
принудительная остановка живого video stream, отказ нового lease и
восстановление через mock-платёж. Первый сеанс прервала сеть до stop:
недоказанный хвост прощён; stop доказан повторным сеансом. Платёж,
email и полный deployment gate вне этого evidence. Test clock возвращён
к offset 0, production image не менялся. [Отчёт MenuBuilder](../../MenuBuilder/docs/l4desk/handoffs/L4D-17F-MB-GRACE-TESTPATH-01-report.md),
[детальная хронология](../../l4desk-service/docs/handoffs/L4D-17F-MB-GRACE-TESTPATH-01-plan.md).

17F Hub correction (2026-09-27, runtime E2E): на основном nginx
`/api/v1/admin/hub/` маршрутизируется в MenuBuilder с JWT, вместо общего
`/api/v1/` в app1. `HubService.get_finance_overview` обрабатывает
`Decimal` из PostgreSQL. После развёртывания browser Hub registrations и
finance overview вернули 200; роль 5 получила 403, video start/stop
терминала 1000007 прошли без 500. Отдельно остаются вопросы повторного
reconciliation operation ID, correlation filters и пустого archive manifest.

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

17E consumer-contract audit (2026-09-27): принятый IoT fixture stop/feed
`1.1.0 / 2026-09-25-v2` сохранён отдельной неизменяемой копией в backend
tests; старый `1.0.0 / 2026-09-17-v1` оставлен. Payload всех девяти событий
и примеров feed/reconciliation совпадает; локальный consumer валидирует обе
версии. Это локальная совместимость fixture, не новый runtime feed E2E.

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

## Video page step 1 (2026-09-29, local code and mocked browser)
- MenuBuilder frontend labels `low` as Medium and `default` as HD. The wire values and terminal rate policy are unchanged; neither label proves a WAN limit.
- The video page collapses navigation on entry and frees viewport space after device selection. The player uses decoded `videoWidth`/`videoHeight` for aspect ratio and offers browser-only Fit / Native size. Native size is unavailable while remote input is active; the overlay refreshes its coordinate geometry when control activates.
- Local TypeScript and Vite build passed; browser layout was checked with mocked API responses at desktop and mobile sizes. Live video and control E2E remain open. Details: [step 1 handoff](../tasks/active/video-page-medium-hd-frontend-step1.md).

## L4Desk UI (2026-10-03, code + release log + browser repro)
- Main includes the UI in `d538b74` and the profile/locking correction in `e9091fe`. Role 5 uses only L4Desk, cannot access Classic routes or monitoring, and defaults to terminals. Role 3 defaults to Classic for both-site tenants and may explicitly switch; tenant site restrictions remain enforced.
- Terminal activity PATCH is tenant-scoped, sets a target flag and rejects disabling with unfinished remote sessions. Video/console admission rejects inactive terminals; cleanup remains available. Certificate identity and shared models are unchanged.
- Terminal filters operate on the complete authorized settings list before UI pagination; Video/Console selectors intersect provider snapshots with active portal-visible terminals.
- Video uses an Online-first Drawer and a single toolbar; the live badge requires fresh decoded browser frames. Native size remains unavailable during active input.
- The user's release log confirms backend/frontend `5b4a880`, runtime image verification, health, HTTP static delivery and unchanged neighboring containers. Live Playwright reproduced stale Classic navigation and 500 on control/lease; `e9091fe` corrects both, with PostgreSQL locks scoped to terminals. Corrective deployment and live media/input E2E remain unconfirmed.
- Corrective checks: 48 backend tests including schema compatibility, 70 frontend tests, TypeScript/Vite, Ruff/Pyright and mocked browser role scenarios passed. Shared models/schema/data are unchanged; no migrations are required. [Handoff](../tasks/active/2026-10-02-menubuilder-l4desk-ui.md).
