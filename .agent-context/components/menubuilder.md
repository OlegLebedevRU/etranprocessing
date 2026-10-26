# MenuBuilder

## Desktop receiver telemetry — diagnostics only, 2026-10-26

Owner clarified: overlay belongs only to tests. The final PR restores production
src exactly to main83b0124; a manual diagnostic harness injects its own overlay.
It separates RTP loss, inbound/video-element drops, decode/rVFC FPS, Mbps, freeze
count and decode/jitter residence. Both773
displays completed90s A/B/A in Chrome155 and DISPLAY1 completed600s. Diagnostic
OnBeginFrameThrottleVideo flag showed no repeatable gain; source timing is a
follow-up hypothesis, not an accepted native fix. No production rollout is needed
for these diagnostic scripts. Workload/CPU/trace limitations are explicit in the
[measurement handoff](../tasks/completed/2026-10-26-desktop-performance.md).

## Desktop compatibility — deployed, 2026-10-26

Application main3b59799 deployed with accepted helpera449ec0; BFF ports5004/5005
and app1 desktop flags restored while preserving app1 image/camera cohort and12
neighbor container IDs. Owner-visible Chrome773 passed H264/H265/Auto at both
profiles; HEVC decoded3000x2000, fresh frames and renewals beyond60s TTL verified.
Six leases released, final stopped/no active slot. Edge lacking H265 correctly
requires H264. Builder1155passed/25skipped, UI130unit/build and scoped checks pass.
Old agent checked by code/contracts only, as agreed. [Release and actual live
evidence](../tasks/completed/2026-10-26-desktop-stream-recovery.md).

## Native nominal cohort1.4 — preparation only

New START/admission selects observation1.4, cadence0.3 declared nominal SDP,
enrollment0.5 and unchanged media3/admission0.2. BFF does not enforce achieved
FPS tolerances/windows or GOP105%. Server-owned camera14 markers select this
cohort; existing camera13 markers retain exact old headers for lifecycle reads,
STOP and clock/keepalive compensation. Mixed states/old preparation tickets fail
closed. No DB or frozen common schema change; deployment remains coordinated.

## Camera lifecycle context — preparation only, 2026-10-25

Native START uses historical CTLv1 usb-camera presence, so generic readers lost
the camera bundle and triggered422/cleanup after initial decoded video. Candidate
persists URL-safe camera13 context in the existing server-owned operation marker
after validated native START, then uses exact provider identity for state/STOP/
mountpoint/keepalive and precommit compensation. No schema/locking/frontend
change; desktop USB does not select camera from presence hints. Root owns live
acceptance and release authorization.

## Camera UUID4 attempt boundary — preparation only, 2026-10-25

Actual START reached the camera CTLv1 boundary but failed because BFF generated
UUID5 operation/stream IDs while frozen admission0.2 requires UUID4. Candidate
allocates random IDs once in the encrypted prepare ticket and retains them
through readiness/duplicates. MEDIA session UUID5 and desktop identity stay
unchanged. Cross-contract regression validates actual BFF request IDs against
the byte-exact frozen camera CTL schema. No config writers or UI receipt change.

## Camera cadence0.2 / observation1.3 — preparation only, 2026-10-25

PR122 proposes exact camera1.3/cadence0.2/enrollment0.4 dispatch and observation
validation, preserving commonmedia3/admission0.2 and deployed1.2 frozen bytes.
Tickets bind the entire selected bundle, rejecting older or mixed tickets before
provider IO. Camera nominal FPS no longer inherits the old20/30 geometry budget;
producer cadence proof/resource checks remain separate. No new observer/hint
fields, UI freshness bypass, config writes, desktop change or deployment.
Canonical packet: contracts/siplite-camera-v1.3/. Prior1.2 was deployed from
fd4e6a6; this1.3 candidate must not be advertised until coordinated owner rollout.

## Camera common media-v3 — preparation only, 2026-10-25

Candidate integrates common media3 descriptor/track/envelopes through camera
observation/HTTP adapter1.2, full-offer enrollment0.3 and admission0.2/cadence0.1.
Camera RPC/config remains1.0; native quality low/default never invokes RPC163.
Media receives only its common header, IoT receives the exact five-header
camera bundle. Gates stay false and ticket key stays unprovisioned. This is
source/offline preparation, not deployment, firmware installation or live
browser decode acceptance. [Contract and rollout boundaries](../../docs/etran_arch-camera-media-v3-adapter.md).


## Native quality57 stage 3 — deployed, 2026-10-24

Common desktop media v3 retains CTL2 and `low/default` quality policies while
forwarding concrete source tracks without resizing. Mixed versions fail closed;
camera-specific admission remains separate. Cold desktop START allows 35 s, clipped
to the existing lease deadline; HTTP/UI waits accommodate native activation.
Main `d8f48cc` delivered backend and static frontend through builder/registry/pull;
990 backend/119 frontend tests and quality checks passed. Live HTTP validates
media/IoT version3; capabilities remain empty with IoT desktop flags disabled.
All12 neighbors including signer are unchanged; no live773 acceptance is claimed.
[Implementation and verification](../tasks/active/2026-10-24-menubuilder-native-quality57.md).

## Siplite camera — deployed TASK40, 2026-10-24

[Implementation](../../docs/etran_arch-siplite-camera-menubuilder.md): source
b60088f, PR100. Native main_app START, H265-first pre-START Auto, settings drafts,
port2 discovery/cancel and encrypted credential orchestration. Builder953passed/
25skipped, frontend105unit/16browser fixtures passed. Ordinary browser4623 reads200,
vault/recipient ready; old firmware controls are disabled. No mutations/decode.
[Release](../tasks/completed/2026-10-24-siplite-task40-menubuilder.md).

## Camera vault services, 2026-10-23 — deployed482e099/schema034

Standalone camera_credential_vault/recipient helpers store encrypted active/pending
intent and durable receipts, wrap DEKs under a stable runtime KEK, and recover
credentials internally for a new manual operation after certificate rotation.
TASK35 helpers and TASK40 router/UI integration are deployed. The private stable server KEK and backup must survive deployments
and TLS renewals. [Helper and rollout contract](../../docs/etran_arch-camera-credential-storage.md).

## Certificate chooser operation state, 2026-10-13 — released

mainc3e464e delivered through builder for frontend. Existing renewal/PIN reads run
on drawer open and are shared across chooser/detail. Pending or unconfirmed renewal
shows Check and blocks a new start; recovery remains distinct. Active manual PIN
is displayed below its option, blocking manual entry and competing renewal. Failed
reads disable unsafe choices; accepted operations fence earlier responses. A matched
verified PIN read resolves the ambiguous UUID without another POST. Classic keeps
its modal. [Evidence and limits](../tasks/completed/2026-10-13-certificate-chooser-state.md).

## Manual PIN administrative permission, 2026-10-13 — released

main5301d23 forwards normalized authenticated SU admin_override through the internal
setup PIN contract, matching remote renewal. Provider payment/policy refusals stay
402/403 etc., no longer false503; raw provider bodies are not exposed. Tenant/SN
binding, UUID replay and purpose isolation remain. [Evidence and limits](../tasks/completed/2026-10-13-manual-pin-773.md).

## L4Desk device workflows, 2026-10-13 — released

main41157ff delivered through builder for MB backend/frontend. SU can edit device
descriptions and read/issue PIN within any active tenant; profile settings remain
read-only for SU. User activity cannot override administrative disable, including
forged administrative re-enable. Polling is silent like Classic monitoring and
preserves rows/forms. Status distinguishes admin/user/IoT restrictions. Certificate
drawer requires deliberate remote/manual choice; shared renewal supports inline
confirmation while Classic keeps default modal. Manual retries retain UUID and
PIN plaintext stays out of persistent browser storage.
[Release and verification limits](../tasks/completed/2026-10-13-l4desk-device-workflows.md).

## Devices visibility correction, 2026-10-13

Plan12 enrollment-only list restriction is removed: L4Desk devices retain the
organization visibility query regardless of enrollment. Finance/admission stays
separate; count/status filters precede pagination. Section label is Устройства.
Terminal screen selects exact sys=windows after existing visibility checks;
IP-camera tags and Windows USB sources remain separate. A successful presence batch
with unknown/unprovisioned rows does not set the global provider-outage warning;
provider exceptions do. Release evidence:
[handoff](../tasks/completed/2026-10-13-devices-list-correction.md).

## Certificate installation recovery, 2026-10-12 — released

main b152e35 delivered via builder for PB/MB/frontend. Used-PIN command failure
is shown separately from issuance; five minutes without verified current identity
escalates. New orders are blocked while the current issued renewal is unconfirmed.
Explicit retry uses its original unexpired PIN and saved native PKCS7/key through
unchanged RPC7011; no additional payment, issuance, native release or RabbitMQ
configuration change. Local PB311/MB779 tests, quality,93frontend unit and6fakeREST
browser tests passed. Deployed read-only selectors correctly choose773 PIN327
despite newer unused328. Owner-triggered install/hot rotation/RabbitMQ not yet
verified. [Delivery and limits](../tasks/active/2026-10-12-renewal-install-recovery.md).

## JWT signer и native CAPTCHA, 2026-10-11 — выпущено

Приватный RS256 signer, public key lk-key-000003, internal HMAC отдельно от
выключенного serverless резерва; provider native/SmartCaptcha/off. Local backend
748passed/22skipped, frontend build/88unit/4browser; production45e69a7,
paired signer health и новый public fingerprint подтверждены. Сайты прошли HTTPS
и negative CAPTCHA acceptance после DNS correction03b9768/30f973b.
Login/refresh/tenant switch с approved credentials и реальная email доставка не проверены.
[Контракт и проверки](../tasks/active/2026-10-11-jwt-signer-native-captcha.md).

## FM start correction, 2026-10-05

Deployed main920e544: active-admission guard NO KEY UPDATE preserves terminal-change serialization
and permits PB FM FK inserts; eliminates BFF/PB row-lock cycle. Failed-start message and safe drain
countdown prevent confusing immediate retries. MB706passed/22skipped, quality passed; UI78unit,
6browser/build passed. [Release and live acceptance](../tasks/active/2026-10-05-fm-start-lock.md).

## UI follow-up 2026-10-05: renew в L4Desk terminals

Frontend68298fc выпущен: row action открывает общую CertificateRenewal дляSU/owner; offline queue доступна, server eligibility сохраняется. Local78unit +6fakeREST browser scenarios/buildpassed, deployedchunkпроверен. Backend/PIN/RPCконтрактне менялся. [Release evidence](../tasks/active/2026-10-05-licensing-implementation.md#follow-up-действие-renew-в-l4desk-terminals).

## Production 2026-10-05: разделение применено

Schema031;52Classic/4L4Desk, explicit enrollment11(включая только773 вorg1), split ON обоих consumers, YooKassa OFF. License/identity fingerprints неизменны. PB234/MB700 tests, quality/build passed. [Фактический release и ограничения E2E](../tasks/active/2026-10-05-licensing-implementation.md#k10--production-завершён-2026-10-05-1224-utc). Предыдущие разделы ниже — исторические срезы.

## Актуальное состояние 2026-10-05

Локальные K2–K10 завершены: explicit product/enrollment, единая Classic certificate tariff, SU-only simulation, tenant-safe subscriptions, Classic disable/console, immutable product edits. Полный suite697passed/22skipped, Ruff/Pyrightpassed; UI build/78unit/2fake-REST Playwright passed. Bridge030/031 уже production b6772ba. Final images/cutover ещё не выпущены. Ниже — исторические срезы; актуальный контракт в [handoff](../tasks/active/2026-10-05-licensing-implementation.md).

Внедрение2026-10-05, локальный K2: Classic provider больше не даёт общий mock.
YooKassa под прежним флагом; flagOFF — административная simulation только SU,
нулевые серверные заказы отдельно free. Order lock/audit и monotonic expiry;
MB suite658passed/20skipped, qualitypassed. Runtime не выпущен. Новый G8:
settings/onboarding PIN обходят Classic tariff, PB setup issuance погашает
pending обоих purposes; зависимый этап остановлен до решения владельца.
[Фактические K0/K1/K2 и G8](../tasks/active/2026-10-05-licensing-implementation.md).

Редакция 5: [уточнение Classic и сертификатов](../../docs/etran_arch-licensing-variants-audit-2026-10-05.md#16-уточнение-охвата-classic-и-сертификатного-биллинга).
Все подтверждённые Classic tenants — только Classic; общий remote без отдельного
billing, технический учёт сохраняется. RPC7011 не проверяет Classic cert tariff
для роли 3 — C21. G6 решён: связать RPC7011 с оплатой; эмуляция только для
суперпользователя с серверной проверкой и audit. Контракт ещё не реализован.

Редакция 4 аудита: [пошаговый план](../../docs/etran_arch-licensing-variants-audit-2026-10-05.md#15-детальный-план-внедрения-обособленные-продукты-общий-механизм-решений)
разделяет новые Classic/L4Desk и сохраняет архивные модели. Реализация не начата;
исторические both и охват org block требуют решения до переключения. Общий
механизм решений не означает общую коммерческую формулу двух продуктов.

## 2026-10-05: аудит вариантов лицензирования

Read-only production/code audit: у tenants 1/339 оба license flags включены,
но L4DeskTenantProfile отсутствует, поэтому subscriptions возвращает403.
Technical L4DeskTerminal не равен явному коммерческому enrollment; создание
profile переключает admission всего tenant и требует отдельного решения.
Classic UI существует на /billing, подписки — /licenses. В текущем backend
Classic get_payment_provider возвращает MockPaymentProvider; денежные операции
в аудите не выполнялись. У10000 архивный entitlement=active не разрешает новую
additional subscription. Решение пока предложено, код/данные не менялись.
[Анализ и матрицы](../../docs/etran_arch-licensing-variants-audit-2026-10-05.md),
[handoff](../tasks/completed/2026-10-05-licensing-architecture-audit.md).

Редакция 2: владелец направляет Classic-пользователей только в Classic,
L4Desk-разделы/API должны быть закрыты по product scope; мастер — нейтральный.
773 (`Terminal.id=1`) после пользовательского DELETE и admin enable:
08:53 UTC `is_active=true`, L4Desk `deleted_at` заполнен; settings-фильтр
исключает его, admin Terminal list — нет. DELETE и enable используют разные
lifecycle-переходы; штатный undelete не найден. Данные не восстанавливались.

## 2026-10-05: RPC7011 authenticated renewal

BFF and form deployed with schema030/PB/IoT; operator installed signed tools1.11.0.
Action «Заказать удалённое продление» issues hidden purpose=renew PIN and queues
real IoTtask synchronously; offline execution/reporting is asynchronous. Three
manual repeats reusePIN, then3min cooldown; no outbox/new operation UUID.
PIN masked in command history; fresh PB current-serial discovery confirms use.
UI77tests/isolated browser and real7003 passed; actual7011 rotation/store/hotrotation
and current-serial discovery passed. MB632tests includes variable-width serial admission.
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

## siplite video compatibility (2026-10-11, deployed UI + live browser)
- Video route ignores presence state `stopped` as a source mode, cleans pending startup leases, and stops media before coordinated release even if stop fails. Frozen API contracts are unchanged.
- Frontend build and callback/lifecycle tests pass. Live 4619 on firmware 0010 passes five-minute 1280x720 browser acceptance with zero freezes/RTP loss; stop and route unmount release owned leases without HTTP/JS errors. Sustained capture has 3014 RTP packets and zero gaps. Device fixes include PSRAM camera RX/TLS allocation, complete-frame DMA buffers and RX scheduling; upstream contracts and UI release remain unchanged. Details: [handoff](../tasks/active/2026-10-11-siplite-video-control.md).

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
