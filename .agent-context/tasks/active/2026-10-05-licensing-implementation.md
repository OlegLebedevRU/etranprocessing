# Внедрение разделения Classic/L4Desk — K0–K4, локальная реализация

## Task intake

- Цель: выполнить этапы §15/§16 [утверждённого плана](../../../docs/etran_arch-licensing-variants-audit-2026-10-05.md), последовательно по стекам.
- Scope: PB, MB backend/frontend, shared/Alembic, при доказанной необходимости IoT. Исключённые legacy/SQL-каталоги не исследуются; native client в данном шаге не меняется.
- Владельцы: MB — оплата/пользовательские операции; PB — XML Classic/policy/сертификаты и все shared migrations; IoT — identity/security/lease; shared — только декларативная схема.
- Producer → consumer: сначала K0-факты и утверждённые решения; затем PB legacy baseline, MB подтверждение оплаты, shared схема, MB writers/DTO, PB consumers, IoT проверка, UI, адресная миграция.
- Инварианты: Classic без серверного grace; новые L4Desk обособлены; Classic remote без L4Desk billing; technical accounting сохраняется; модели не удаляются. Платный RPC7011 связан с оплатой; эмуляция только суперпользователю, с серверной проверкой и audit.
- Рабочая ревизия: `2d0bfafed03f79262bf08e9aea3b65079f2447e9`, ветка feat/rpc7011-renewal; предыдущие документальные изменения сохранены.
- Перед первым изменением кода: нужны завершённые K0 решения, фактический контракт предшествующего стека. Обнаруженный фактор изменения формулы останавливает зависимый шаг по прямому требованию владельца.

## Выполнено / runtime evidence

- `[MCP Ops Readiness: UNAVAILABLE]`: Ops-инструменты в сессии отсутствуют. SSH preflight: RAM available 2115 MiB, disk45%, load0.21/0.19/0.18.
- Read-only snapshot `2026-10-05 09:41:51 UTC`: [manifest всех 56 организаций](2026-10-05-licensing-baseline.json). Явно SET TRANSACTION READ ONLY, statement_timeout10s, rollback. Выбраны только product/billing поля, количества и идентификаторы; email/username/секреты/PIN не выгружались.
- 46 организаций имеют Classic-признаки без L4Desk profile; четыре profile tenants: 3/4/1000/10000. Шесть пустых orgs223/466/482/509/511/513 не имеют проверенных данных продукта.
- 3/1000/10000 имеют consumed registration и membership; 4 имеет profile/technical row, но нет consumed registration/membership. Все четыре имеют site_mode=both и оба flags=true; это не доказательство смешанного продукта.
- 10000: подтверждённый registration/membership, шесть terminals, две Classic License rows, ни одного Classic billing order, нет role3 users. Присутствие License противоречит правилу безусловной классификации по такой строке.
- Локальный источник `admin_terminals.py`: PATCH при отсутствии License и наличии is_active создаёт License с default сроком now+365days; set-status enable создаёт License с expires_at=now. Это возможный источник ложного Classic-признака; происхождение двух конкретных строк runtime10000 этим наблюдением не доказано.
- Images running: MB `sha256:6fab3cb0832f58ab370535c25d5d713df16add511c349f90cd376bdd8917fc3a`, PB `sha256:6a17f70ff84c7ea590cea7aedfde52f4ed85da780db76c2905c6b5bacb8146db`, app1 `sha256:0b51e5b174ce37087cd4f6b1a89291ae72361e2b65c0fe9c73052fe700085d74`.

## Контракт K0 — принят владельцем, данные ещё не переключены

Фактический реестр имеет полное покрытие: 46 Classic candidates + 4 L4Desk candidates + 6 empty = 56, группы не пересекаются. Ответ владельца «оба вопроса — да» утвердил **52 Classic / 4 L4Desk (3,4,1000,10000)**. Manifest содержит approved_product; это утверждённая классификация, ещё не применённая к DB/site_mode. Текущие формулы org block сохраняются.

Запрошено:

1. Подтвердить исключения L4Desk3/4/1000/10000 и Classic для остальных, либо разрешить перевод10000 в Classic по наличию License. Отдельно показаны пустые orgs.
2. Сохранить текущие формулы org block в этом внедрении либо уточнить новый охват. Текущий licensebilling проверяет blocked, admin org disable пишет inactive, transport не читает org status.

## K1 — фактический PB legacy baseline

- Функция get_terminal_license_state и её DB queries не изменены; docstring фиксирует Classic-only/no-server-grace, точное равенство срока и существующую семантику org blocked/inactive.
- Добавлен тест actual dependency → XML, без подмены готового state: 2 admin × 4 org states × 4 license states =32. Две ORM-выборки остаются только OrgStatus/License; никаких L4Desk queries. Истёкший срок на микросекунду даёт error, равенство/future при разрешённой активности — ok.
- `pytest` targeted:70passed (matrix/licensebilling/cert serial binding). Ruff check/format и Pyright app+новый тест прошли; форматирование tracked runtime-кода изменило только уточняющий docstring.
- Контракт следующему стеку: XML Result=OK с state=ok/error и сохранённым balance для распознанного терминала; missing identity остаётся401/XML. Нельзя добавлять Classic grace или subscription AND в этот legacy reader.
- PB/runtime не деплоился; результат локальный. Новый тест находится в ProcessingBackend/backend/tests/test_classic_license_state_matrix.py.

## Dev-773 и доступ суперпользователя

- Фактический admin_tenants/switch выдаёт новый tenant JWT с сохранением is_superuser/role; active_org меняется без переноса учётной записи. /auth/me читает site_mode выбранной организации. Домашняя org1 не ограничивает доступ суперпользователя к L4Desk tenants.
- Dev773 остаётся одной Terminal identity/org1, с одним SN и cert serial. L4DeskTerminal.runtime_terminal_id unique, а terminal_id — PK; копировать одну identity в другой tenant как второй terminal нельзя.
- Предложено суперпользовательское исключение для обоих UI и L4Desk shadow calculation без списаний/блокировок. Для живого биллинга на общем MQTT/RTP нужен явный выбор одной terminal policy. Вопрос передан владельцу; dev exception пока не реализован, org1 не переводится в L4Desk profile.

## Этап 2 — платёжный фактор закрыт владельцем

- Реальный Classic payment provider отсутствует: get_payment_provider по умолчанию всегда MockPaymentProvider; create_checkout возвращает локальный confirm URL, verify всегдаTrue. Отдельный существующий YooKassa client используется другими billing readers/writers и пока не даёт Classic подтверждение.
- Владелец подтвердил: YooKassa под существующим флагом, сейчасOFF; при включении платные функции доступны. Сейчас Classic — административная оплата суперпользователем либо прямое продление даты через администрирование, которое сохраняется.
- Dev773: live MQTT/RTP по L4Desk + общий admin flag; это адресное исключение, не org-wide подключение Classic к подпискам. Существующий общий канал получает одну итоговую policy.
- Classic lifecycle закрыт владельцем: отключить/включить, запись остаётся в списке; active-session disable отвергается; адресный restore773 без изменения лицензии.

## K2 — фактический локальный контракт оплаты Classic

- Изменены только MB backend: payment_provider и billing router. Новый `get_payment_provider(order, *, is_superuser, email=None, simulate=False)` заменяет глобальный mock. Все четыре вызова старого контракта в billing переподключены.
- При flagOFF положительная сумма доступна только административной simulation; роль3 получает503 без оплаты. При flagON используется существующий YooKassa client; `simulate=true` на confirm всегда требует superuser. У simulation order роль3 не может применить подтверждение даже после включения флага.
- Сохраняются существующие BillingOrder.provider/provider_order_id/payment_url. Provider значения: yookassa/simulation/free. Нет новой таблицы или schema migration в K2. Free — только серверно рассчитанный нулевой заказ, без имитации денег.
- Подтверждение provider читает remote payment и проверяет id, succeeded/paid, сумму/валюту и metadata purpose=classic_billing/order_id/tenant_id. Реальный checkout использует UUID заказа как idempotence key и email организации для чека. Внешних вызовов/реальной оплаты в проверках не было.
- Verify выполняется после завершения читающей транзакции, до order row lock. Затем FOR UPDATE + populate_existing, повторный pending check и сравнение неизменности суммы/валюты/provider reference. Terminal rows блокируются в порядке terminal_id; поздний заказ не уменьшает expires_at (max), оплата не включает Terminal.is_active.
- Применение добавляет общий technical audit classic.payment_confirmed: actor, tenant/order, provider/reference, amount/currency, outcome=simulated/free/paid. PIN в аудит не включён. Не создаются проводки архивного L4Desk ledger.
- Повтор applied confirm возвращает items_updated=0. Условие exactly-once проверено управляемым расписанием двух запросов с моделью row lock; реальный PostgreSQL concurrency E2E не выполнен. Docker info локально не прошёл; destructive DB fixtures в production не запускались.
- Проверки: target136passed перед добавлением двух zero-price cases; полный MB suite658passed/20skipped,52warnings; skipped — существующие disposable-PG проверки. Ruff check/format и Pyright app прошли. Предупреждения full suite относятся к прежним AsyncMock/deprecation fixtures и не объявляются production-дефектами.
- K2 не решает ещё связь оплаченного cert_pin item с purpose=renew/7011: это следующий MB producer шаг4 и PB consumer шаг6. Free renewal обход пока остаётся в существующем route; не деплоить K2 как завершённое разделение продуктов.

## Новый G8 — Classic PIN через settings/onboarding

- При подготовке схемы сверены оба конца H-L4D-06A-PB-v1. TerminalPinService и TerminalOnboardingService вызывают `/api/certificates/pins/issue` без Classic cert tariff/self-service проверки; PB endpoint создаёт payment_required=False после service auth/tenant/SN проверки.
- PB setup issuance обновляет в expired **все** pending CertificatePin данного terminal_id без фильтра purpose. Это затрагивает ожидающий renew PIN RPC7011 и ранее оплаченный setup PIN.
- Это новый фактор коммерческого допуска и взаимодействия purpose flows. Владелец утвердил единую Classic tariff проверку settings/onboarding и разделение setup/renew expiration. Новый G8 закрыт; реализация продолжается по стекам.
- Ответ: «Да: единая тарифная проверка и разделение setup/renew». Технический provider должен переиспользовать подтверждённый paid setup PIN, а не заменить его бесплатным.

## Проверки и ограничения

- Выполнено: чтение producer/consumer и preflight, полный SELECT baseline, проверка arithmetic coverage.
- Не выполнено: изменение shared/selector/frontend, миграция, реальная оплата/эмуляция в runtime, PIN/7011, деплой. K1/K2 имеют только локальные проверки; серверная классификация не применена.
- Cleanup: scratch script передан через stdin в docker exec, файлов в контейнерах не создавалось; test credentials и sessions не создавались.
- Дальнейший шаг: после ответа G8 продолжить этап3 shared/PB migration на фактических K0/K1/K2; затем MB producer шаг4. Live773 исключение и Classic lifecycle уже приняты, повторного согласования не требуют.


## K3 — фактический локальный контракт схемы

- Migration031, down_revision030: только terminals.l4desk_subscription_enabled Boolean NOT NULL DEFAULT false. Нет массового enrollment, переписывания identity/License или новых финансовых таблиц.
- Схема/manifest v031 опубликованы в shared/docs/l4desk; snapshots v030 не перезаписаны. ORM остаётся декларативным. Shared pytest65passed; Ruff/Pyrightpassed. Offline PostgreSQL SQL030→031 сформирован успешно; реальная миграция не запускалась.
- Pure zero-dependency etranprocessing-access0.1.0 — отдельный пакет shared/etranprocessing_access, вне ORM. Решение принимает только факты и now; календарные deadlines принадлежат caller. Матрица64cases: deleted32/admin_disabled16/free8/payments_disabled4/unpaid1/active1/grace1/expired1; allowed10. Exact paid/grace boundaries проверены,2tests passed.
- Совместимость выкладки: текущий старый MenuBuilder принимает только030. Сначала нужен отдельный old-ORM bridge image с guard030/031, затем031 migration, потом новые ORM consumers. Новый MB guard требует031 и колонку; старый guard нельзя использовать при откате. Bridge ещё не собран/не выложен.

## K4 — фактические producer изменения MB (валидация продолжается)

- Флаг product_scope_split_enabled по умолчаниюFalse. При выключенном флаге сохраняется прежний выбор продукта; включение требует классификации52/4 и address enrollment. Никакого автоматического enroll по profile/technical row.
- При ON tenant_product принимает только site_mode classic/l4desk, unknown/both409. Terminal writer выставляет enrollment только для L4Desk. Registration выставляет classic_licenses_enabledFalse/l4deskTrue. L4Desk administrative create/enable не создаёт Classic License; старые строки остаются, license writes для L4Desk заморожены.
- Подписки/free selection/DTO используют enrollment + tenant/runtime binding. Classic check_terminal возвращаетNone до чтения profile; enrolled безprofile403. DTO сохраняет существующие состояния, MB вызывает pure evaluator.
- Обычному Classic пользователю subscription APIs запрещены; superuser работает в выбранном tenant. /auth/me показываетboth только SU в Classic с явно enrolled dev terminal; не меняет product данных организации.
- Новый G9 согласован: запрет обычного cross-product transfer; все transfers enrolled L4Desk требуют отдельной миграции. Same-org no-op разрешён, Classic→Classic перенос без enrolled сохраняется. Матрица16 комбинаций проверена.
- Classic DELETE settings адресуется в существующее административное disable API: active-session409, запись остаётся, License/deleted_at/free quota не меняются. Видимость Classic rows больше не зависит от технического L4Desk deleted_at. Полный lifecycle/worker этап5 ещё впереди.
- Общая Classic certificate_permission проверяет master/selfservice/tariff и paid setup/renew entitlement. Матрица32cases purpose×SU×selfservice×master×charge; L4Desk не читает Classic tariff. G8 применяется settings PIN/onboarding/7011 при ON.
- Producer setup IssueCertificatePinRequest добавил optional order_item_id. Producer renew service body добавил order_item_id и admin_override (источник только проверенный SU). PB consumer пока не изменён: этот пакет нельзя выложить отдельно как готовый renewal flow.
- POST billing/terminals/{id}/certificate-pin query purpose=setup|renew; по умолчаниюsetup. paid item.snapshot.purpose=renew, старыеNULL snapshots означаютsetup. Free renew возвращает renew_ready безPIN; подтверждение paid renew не создаётPIN. PB должен создать/reuse одинPIN поpaid item, отклонитьexpired/used entitlement для новой операции. Setup paidPIN нужно вернуть в provider wire contract без повторной выдачи.
- RPC renewal browser body добавил optional order_id(UUID). Очередь сохраняет IoT-generated task_id. Lost queue response возвращает order_id вместе с pin_id, безPIN; повтор оплаты не требуется.
- Проверки до последних auth/guard изменений: MB666passed/20skipped52warnings; после guard031 и tariff matrices21target passed. Затем требуется полный rerun. Реальный PG/provider/browser не проверены.

## Оставшееся

Завершить K4 проверками и producer DTO ownership, затем K5 worker/lifecycle/frozen writers, K6 PB (pure evaluator + paid PIN consumer), K7 IoT security/lease review, K8 UI, K9 bridge/migration/address classification/deploy, K10 final freeze. Runtime всё ещё schema030 и прежние images. Деплой, восстановление773 и классификация не выполнялись.


## K4/K5 — завершённые локальные переходы к PB

- K4 полный MB suite666passed/20skipped, Ruff/Pyrightpassed. K5 полный MB suite679passed/20skipped,52baselinewarnings; качество прошло после исправления nested-if lint. Shared65passed, PB213-case baseline:212passed + устаревший head030 expectation; expectation заменён031 с отдельным additive/no-backfill SQL test,10targetpassed.
- K5 Classic settingsDELETE делегирует отключение; active-session409, Classic visibility не зависит от техническогоdeleted_at, free marker толькоenrolled. Legacy technical row creation при ON сериализуется org row lock, проверяет SN/tenant/runtime, пишет реальный device_id вместо internalID; enrollment при этом не меняется.
- Native session use-case допускает роль3 к console в Classic при ON, сохраняя selectedtenant/admin/security/sessionbusy gates. Прямой video_control уже допускал1/3/5 и require_activeTrue; его wire contract не менялся.
- Worker sessions/retry включает tenants с active/reserved/start_requested/stop_requested technical sessions даже безprofile. Subscription list поenrollment; common inactive stop для Classic/L4Desk и missing-profile deny для enrolled. У stop/retry current commercial allow не снимает общийadmin deny. Invalid subscription403 не запрещает cleanup.
- stop_provider operation покрыт wall timeout45sec(config.remote_session_stop_timeout_sec); timeout оставляет существующий stop_requested retry. Epoch validation и confirmed_end accounting прежние. Матрица12cases admin×commercial(None/deny/allow)×first/retry и независимость session loop от blockedpayment test прошли.
- Worker разделён на два TaskGroup loops: sessions и payments; provider задержка больше не задерживает административные checks. Interval60sec добавляется после своей работы; это не hard60sec реакция. Для sessions worst-case зависит от числа serial stops/notification IO/DB, каждыйstop≤45sec плюс localprocessing. Для payments ≤50provider checks в batch с существующим HTTP15sec phase timeout, отдельной общей wall bound пока нет; это не влияет на session loop.
- Paid subscription apply при ON перепроверяет runtime enrollment/tenant; не подключает Classic обратно через старый технический paid row. При конфликте409 требует сверки, деньги/история не удаляются.
- Live old monetary paths уже не подключены main/remote/iot consumer: financial_core сохранён frozen, старый API410, pending_finance backlog — толькоpresence. Usage/FinUsageDaily/session metering остаются живыми техническими writers.
- Wire producer для PB: setup body optionalorder_item_id; renew body optionalorder_item_id иadmin_override(толькоtrustedMB SU), browser order_id optionalUUID. PB должен проверитьpaid order itemtenant/terminal/purpose, создать/reuseодинrenewPINподTerminallock и не восстановитьexpiredpaidright какбесплатное новое право. Setup долженreusesamepaidpendingPINнеexpireего.
- K6 начинается после фактическогоK4/K5; finaldeployment ещё впереди. Runtime не менялся.

## Временная PG-проверка (удалить после работы)
Production-host отдельный контейнер codex-licensing-test-20261005 (postgres18), tmpfs512MiB, memory512MiB, CPU0.5, loopback-only55439, отдельная subscription_test DB без production data. SSH tunnel process PID220468, loopback55439. Начат existing PostgreSQL suite MB. Перед созданием RAM available2136MiB/disk45%/load0.23. Cleanup обязателен: удалить только этот контейнер и завершить только PID220468 после проверки его command line.

## K6 — проверенный переход PB → UI

- PB локально: 234passed/1skipped, Ruff/Pyrightpassed до оптимизации disposable fixture. Policy матрица64 cases использует ту же pure decision и один now; unenrolled Classic не читает subscription profile, enrolled безprofile denied.
- Provider проверяет paid item tenant/terminal/purpose. Setup переиспользует действующий оплаченный PIN; pending setup expiration не затрагивает renew. Renewal сериализован Terminal row lock; expired/used paid entitlement не восстанавливается автоматически. Admin override приходит только из service-authenticated MB с проверенным SU.
- Реальный PostgreSQL18: MB21passed (109.35sec), PB paid PIN concurrent/purpose/expand test1passed (11.71sec). Два concurrent renew возвращают одинPIN; wrong purpose403; expired paid renew410; serial неизменен. Реальная миграция030→031 в disposable DB сохранила обаPIN и выставила enrollmentFalse. Production schema/data не затрагивались.
- UI контракт: certificate-renewal POST принимает optional pin_id/order_id(UUID);402 detail содержит terminal_id/purpose/checkout_path. Billing certificate-pin?purpose=renew возвращает payment_required с order_id/payment_url или renew_ready безPIN. После confirm paid renew PIN не возвращается; UI отправляет order_id в renewal POST. Queue failure сохраняет pin_id/order_id для ручного повтора. Setup по умолчанию остаётся отдельным PIN-флоу.
- K7 readonly: IoT source8c2be800567f074b079bc0c61e993e98184bd897. CertificateRenewalPayload строго dt[1], PIN6digits, pin_expires_at, ttl_sec120; task_id генерируется IoT при persistence. Новые коммерческие поля остаются между MB/PB, IoT/terminal wire не меняется. Внешние dirty docs не редактировались; collision/lease policies сохраняются.


## K8/K9/G10 — финальные локальные проверки и bridge

- K8 build TypeScript/Vite прошёл, 78 unit tests, 2 Playwright fake-REST tests прошли. Classic common disable, console reuse, тариф setup/renew, paid-order-preserving retry реализованы. Late responses при смене tenant/device игнорируются, polling не перекрывается.
- G10 пользователь подтвердил: продукт меняется только миграцией. API409 до записей; 16 комбинаций product/field/equality + freeze11 Classic tariff fields. Admin create Classic, selfregister L4Desk. UI поля продукта readonly, Classic тариф не отправляется/не отображается для L4Desk.
- PB product_scope_cutover выпущен как обычный модуль, dry-run/apply и отдельный finalize-dev. Actual PG проверка additive031, сухой откат, идемпотентность base/dev, сохранение License/cert_serial/is_active, устройство773 ровно runtime1. PG FOR UPDATE OF Terminal исправлен после проверки outer join.
- Bridge PR21 merged main b6772ba32d8ea1b2aec48cbbd5c88fab8e6abb39; deployed menubuilder-backend digest888749058c152f48e95abac2771267c6c2e7efbae22768c9ef4b0322ece87788. Launcher verified image/revision/health. Никаких schema/data/flag изменений bridge не делает.
- Временный PostgreSQL контейнер codex-licensing-test-20261005 удалён после проверки полного containerID; SSH tunnel220468 остановлен после проверки commandline. Disposable test data не импортировались.
- Final production cutover ещё не выполнен; YooKassa OFF. Полные последние PB234passed/1skipped20warnings, MB suite/G10 в процессе финальной проверки. Реальный paid/native E2E не заявляется.


## K10 — production завершён (2026-10-05 12:24 UTC)

- PR22 accepted main464dac7911b6dcc5e543d512db0853f4eb6fbb65: основной контракт. PR23 accepted main0a426affead9f9a9dee5d9fadc6314a3455ad292: запрет Classic billing для SU в L4Desk и обязательный enrollment при subscription correction. Legacy/native/IoT исходники не менялись.
- PB registry digest d7f59a27d9dd1847c929807dbad9625ede58177c3227894fd16cd475c222906a из464dac7. MB digest9091f3ec30302ca91e3d7d26c81fd62c13ba062ef4929e49333f72b6f0f592a5 из0a426af. Frontend digest960aed7f4e77364bca02cbb48c38966481a6987583e8ebae593a13c85be37736 из0a426af; registry static artifact published, nginx не перезапускался.
- Builder checks PB234passed/1skipped, final MB700passed/22skipped, frontend78passed/build; Ruff/Pyright прошли. Local новая correction test первоначально неверно ожидала0reads; штатный tenant lock делает1read, assertion исправлен; полный accepted builder suite прошёл до deploy. Playwright2passed fakeREST.
- Migration030→031 applied из нового PB образа до его запуска; bridge MB продолжал работать. Финальные оба consumers сначала splitFalse, после reviewed cutover splitTrue; YooKassaFalse сохранено.
- Base dry-run:56site_mode +56default_site +52l4deskFlagFalse +4classicFlagFalse +11enrollment +9technicalDeviceID исправлений. Reviewed report сохранён в2026-10-05-licensing-cutover-review.json; применение атомарно,52Classic/4L4Desk(3/4/1000/10000). DeviceID технической строки сверялся с неизменённым runtime/SN/tenant.
- Dev-finalize после health/True обоих consumers: только profile1 created и technical1.deleted_at=NULL. Runtime1/device773/org1/License/serial/is_active не менялись. Повтор dry-run dev changes[]; enrollment: org1=1,org3=2,org4=1,org1000=1,org10000=6.
- Production fingerprints до и после: licenses rows2556/checksum12037d5c58f60aa89e3be12fed7338e3; terminal identity rows2586/checksum0ffae48b827dad1435797d79fa602431. License snapshot включает все поля; terminal snapshot включает id/org_id/device_id/sn/cert_serial/is_active. Technical rows23 сохранены.
- Read-only released function checks на реальной БД (НЕ authenticated HTTP E2E): org1/339 ordinary subscription403 +Classic billing allowed; org10000subscriptionallowed/Classic403 для ordinary иSU; product switching409 для1/339/10000. auth.me effective org1SU both/can_switch_orgTrue, ordinaryClassic; org339SUClassic/can_switchTrue; org10000L4Desk/can_switchSUTrue.
- list_subscriptions1 содержит только773 free/allowed. Org339 technical rows не попали в пул. PB actual policy773/70(org1)/6487(org339) mqtt_rtpTrue;1000009(org10000)False поpayments_disabled. Старый stop_facts=terminal_inactive для subscription deny сохранён как wire compatibility, причина отдельно видна вsubscription state.
- Оба backendhealth200 и exactrevision/digest проверены. Соседи unchanged IDs: app145c49c8b42fa; media-nginx8927aebcc8cc; l4mcp17481e60bc24; Rabbit41777886db72; ingress1f82241ae520; Janus1cd35cea4177; Redisf7f10d65139d; legacyNginx224ac66f457b; defaultNginxf57699a9f1ca.
- Feature flag записан адресно в существующие два env_file, без вывода/переноса секретов. Другие flags не менялись. Новый recurring timer не создавался, используются существующие runtime worker loops.
- Cleanup выполнен: disposable PG/container/tunnel удалены; тестовые credentials/production browser sessions/новые PIN и RPC в этой задаче не создавались. Builder checkout/release journal — штатные артефакты, не мусор. Bridge local worktree подлежит обычной проверенной очистке.
- Ограничения evidence: реальная YooKassa оплата, role3 paid setup/7011 на живом терминале, authenticated browser tenant switch и native/video новый happy-path не выполнены. Предыдущая1.11.0 успешная ротация773 не считается проверкой нового платного флоу. HTTP auth границы покрыты existing tests, domain checks на production не подменяют auth E2E.

- Базовый Compose в репозитории и production адресно закреплён на тех же двух digest; persistent image override совпадает. Это изменение не перезапускает сервисы.


## Follow-up: действие renew в L4Desk terminals

- Intake: frontend UI исправление; владелец MenuBuilder. User сообщил отсутствие действия уSU вL4Desk, inventory подтвердил отсутствие CertificateRenewal вL4DeskTerminalsPage при наличии вClassic/settings/passport.
- Producer→consumer: выбранный row.device_id → общая CertificateRenewal → GET/POST /api/devices/{device_id}/certificate-renewal. Backend контракт, tariff/admission/TTL/IoT7011 не меняются.
- UI: отдельный canRenewCertificate дляSU иroles1/3/5, независимый от запретаSU редактировать tenant settings. Кнопка встроке; административно inactive disabled; offline не блокирует. Eligibility/deny запрашивается общим компонентом только после открытия, нетN дополнительных проверок наlist render. Tenant change закрывает форму, компонент keytenant/runtimeID.
- Local build и78unit passed. Browser fakeREST matrix: SU/owner queued offline; serverexpired denial; common inactive noGET/POST; Classic paidretry регрессия. Первоначальный SU fakeREST не возвращал arrayavailabletenants и ломал OrgSwitcher; fixture исправлен пофактическомуAPI. Finalbrowser/release результат ниже.
- Scope: только frontend иконтекст; Python/schema/native/providers не изменяются, production PIN/RPC тестовыми сценариями не создаются.

- Final local validation: frontend build passed; unit78passed; Playwright6passed32.4s (4L4Desk +2Classic regression), fakeREST. Changed-file credential scan3files0matches, diffcheckpassed. Backend tests не перезапускались — Pythonкод/schemaне менялись.

- Production follow-up: PR27 main68298fc7daf304cfde623243692e50e20962c19c; frontend digest327721ffb10529a89accf9d023dbefc67e35d2b5cae9d70d55ec8496f67aae5c. Builder78tests/buildpassed, standardregistry→static publishverified. Currententryindex-CSQLkaAU.js references L4DeskTerminalsPage-BATcb0e-.js with renewal action; nginx mountedindexmatchespublishedhash. IDsunchanged: nginx-defaultf57699a9f1ca,PB78c464b78d4c,MB18b13c023e00. No live PIN/order/RPC created. Resourcepreflight RAM2122MiB/root43.4%/load0.436 passed. No temporary credentials or custom services created.
