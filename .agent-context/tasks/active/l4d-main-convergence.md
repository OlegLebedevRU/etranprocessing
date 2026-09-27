# L4D: единый кандидат к main

Дата сверки: 2026-09-27. Статус: **draft merge candidate**, каскад ещё не закрыт.

## Task intake

- Цель: свести стабильный L4D/L4C код к одному пути в `main` без продолжения
  параллельного каскада веток. Изолированный 17E backend и тестовые данные
  изменялись по отдельным подтверждённым сценариям; основной production
  backend не пересоздавался.
- Владелец данных: `shared/etranprocessing_db`; миграций — ProcessingBackend;
  viewing session/API — MenuBuilder; lease/ctl — внешний app1; media route —
  l4media; терминальный агент — `tools/l4desk` и `l4capture` (`svc_desk`).
- Producer → consumer: UI → MenuBuilder → app1 → MQTT ctl → l4desk;
  l4capture → leo4proxy → l4media/Janus → browser. Контракты:
  [lease](../../contracts/lease-lifecycle.md),
  [video](../../contracts/video-streaming.md),
  [MQTT](../../contracts/mqtt-topic-matrix.md).
- Инварианты: lease expiry останавливает агент без recovery; новый stream
  требует новую эпоху; маршрут живёт весь срок трансляции; stop освобождает
  terminal/media/viewing state; `svc_desk` не занимает `dev/{SN}/svc`.
- Суточное закрытие 17E: MenuBuilder владеет `FinUsageDaily` и ledger posting;
  producer — завершённая video/console session, consumer — суточная проводка.
  Только завершившиеся локальные сутки явно разрешённого tenant могут
  проводиться; повтор не создаёт вторую транзакцию. Проверка — адресные тесты
  границы суток/allowlist и полный backend suite; runtime финансовую мутацию
  выполнять лишь в изолированном test tenant после read-only снимка.

## Единая ветка и происхождение

- `origin/main` (`19c1dca`) является предком
  `release/l4tools-1.8.2-beta-1`. GitHub PR #4 остаётся draft и mergeable
  без конфликтов; по текущей сверке он меняет 500 файлов.
- Кандидат включает принятый каскад до 17C watch, серверный media renew/stop
  из `228dd57` и проверенный native capture из `4456586`. Сборка
  `l4-tools-1.8.2-beta-1` привязана к source commit `41b73bc`; её хеши и
  ограничения в [handoff](../l4tools-1.8.2-beta-1-handoff.md).
- Рабочий каталог на `l4desk/l4d-17e-e2e-fixture` содержит чужие изменения;
  merge проводить через PR из чистого release worktree. Следующие исправления
  кандидата вносить в эту PR-ветку и повторять затронутые проверки. Старые
  каскадные ветки не удалять до merge и проверки новой базы `main`.

## Проверки кандидата (локально, Windows, 2026-09-26)

| Слой | Команда / факт | Статус |
|---|---|---|
| ProcessingBackend | `uv run pytest -q`: 130 passed, 20 warnings | passed |
| MenuBuilder backend | `uv run pytest -q`: 496 passed, 50 warnings после 17E quota candidate | passed |
| shared DB | `uv run pytest -q --basetemp=.pytest_tmp`: 65 passed; первый запуск с default temp получил WinError 5 | passed после корректировки окружения |
| Python quality | во всех трёх проектах `ruff check`, `ruff format --check`, `pyright`: clean, 0 type errors | passed |
| MenuBuilder frontend | `npm ci --ignore-scripts`; `npm run build`; `npm test -- --run`: 58 passed | passed |
| Native suite | x86/x64, l4capture 129/129, l4desk 21/21, l4setup rollback и manifest — [release handoff](../l4tools-1.8.2-beta-1-handoff.md) | passed ранее |
| l4media ingress local C unit | Linux Makefile; Windows MinGW compile failed before test binary was produced, without diagnostic from cc1 | not run |
| app1 | код и текущий deployed digest находятся в отдельном репозитории; не проверены этой задачей | not run |

Первый `shared` pytest запуск завершился `64 passed, 1 error`: доступ к
системному `%TEMP%/pytest-of-oleg_` запрещён. Повтор с локальным `--basetemp`
прошёл 65/65. Не считать это исправлением кода.

## Незакрытые merge gates

1. `17E`/`17F` в опубликованном каскаде ещё `BLOCKED_CONTRACT`: нет полного
   commercial E2E на утверждённом test tenant и доказательства точной версии
   всего работающего backend image. Принятый watch handoff §51 сам по себе
   не закрывает эти шаги.
2. Подтвердить текущие app1/Media/MenuBuilder deploy revisions и совместимость
   с source candidate, миграцию `027` и rollback план перед production release.
3. 120-минутный media soak терминала `1000003` выполнен 2026-09-26 через
   обычный сайт под тестовым owner tenant 3. Сессия БД `462` шла
   18:01:26–20:06:18 UTC (7492 с, 2:04:52); перед stop ingress показывал
   `fresh_rtp=true`, `rtp_idle_sec=0`, маршрут и 0 unrouted packets.
   Владелец подтвердил живое видео без задержки и штатный stop. После stop
   сессия закрыта, route и затем transport исчезли. **Media soak: PASS**.
   `fin_usage_daily.id=1` после stop содержит 9244 с за день, включая
   предыдущие сессии: 7200 free, 2044 billable, рассчитано 100 копеек.
   Строка ещё открыта (`ledger_transaction_id`/`posted_at` NULL), поэтому
   баланс 1000 копеек не изменился. Это evidence измерения usage, не
   подтверждение окончательного списания или блокировки.
   Контрольный повторный запуск после этого stop создал сессию `465`:
   20:12:45–20:13:47 UTC (61 с). Пользователь подтвердил быстрый старт,
   движение без задержки и остановку без 500. `fin_usage_daily.id=1`
   вырос ровно на 61 с до 9305 source / 9279 video / 2105 billable;
   повторного начисления 120-минутной сессии нет. После stop ingress
   показывает `state=disconnected`, `transport_connected=false` и
   `route_exists=false`. **Повторный start/stop: PASS**.
4. Изолированный install/upgrade/rollback релизного `l4setup` и x86 runtime
   на Windows 7/POSReady 7 остаются за рамками подтверждённого теста.

## Возврат к 17E (2026-09-26)

- §51 журнала принял `H-L4D-17C-VIDEO-WATCH-MB-v1`; исторический
  `L4D-17E-MB-report.md` остаётся `BLOCKED_CONTRACT` до нового verdict.
- Изолированный контур уже описан в
  `MenuBuilder/docs/l4desk/handoffs/L4D-17E-MB-progress-2026-09-26.md` и
  `L4D-17E-corrective-2026-09-26.md`: test tenant 3, mock email/ЮKassa,
  успешный платёж без реального списания, owner lease, долговечная закрытая
  видеосессия с 5 секундами usage и одно бесплатное monthly начисление после
  подтверждённого `device_online`. Это частичное E2E, не полная 17E приёмка.
- Для следующего прогона сначала сверить точную версию test-backend с Git
  candidate и безопасные flags, затем повторить stop и убедиться, что одна
  закрытая сессия и usage не дублируются. Не прерывать текущую длительную
  трансляцию 1000003 до её контрольной отметки.
- Tenant 3 уже содержит posted payment. Сценарий **без платежа** с бесплатным
  терминалом и 120 минутами нельзя честно заявить на нём: нужен отдельный
  утверждённый no-payment tenant или чистый изолированный контур. Длительная
  видеотрансляция 1000003 сама по себе подтверждает медиатракт; коммерческий
  quota test требует измеренной usage, entitlement и ledger сверки.
- После первого платежа нулевой баланс в реализации остаётся `active`;
  отрицательный баланс попадает в `grace` до конца третьего дня billing cycle.
  У tenant 3 cycle 0 начался 2026-09-26 11:29:27 UTC, grace deadline —
  2026-09-29 11:29:27 UTC. Прямое обнуление проекции не проверяет блокировку
  и нарушило бы ledger. Для проверки blocked нужен отдельный изолированный
  time-bound сценарий и штатная проводка, без правки production clock/DB.
- На обычном backend проверены effective flags: `billing_enabled=false`,
  `policy_enforcement=false`, `entitlement_worker=false`; изолированный
  test-backend имеет `billing_enabled=true`, но `policy_enforcement=false` и
  `entitlement_worker=false`. Обнуление счёта в текущих runtime не может
  доказать реальную блокировку. Счёт и флаги не менялись.
- После media soak `fin_usage_daily.id=1` ещё открыт: рассчитано 100 копеек,
  но `ledger_transaction_id` и `posted_at` отсутствуют. В ledger tenant 3
  только прежний тестовый платёж: две записи с debit=credit=1000 копеек;
  projection=1000 копеек. В коде есть internal `metering/close-day`, но
  автоматический вызов закрытия суток в репозитории не найден. Это отдельный
  gate финансового завершения 17E, не повод вручную править projection.
- Перед полуночным прогоном сверены runtime-копии: все 90 канонических Python
  файлов основного `menubuilder-backend` совпадают по SHA-256 с текущим Git
  candidate; в контейнере остаются 91 дополнительных старых Python-файл,
  включая `app/app/`, которые не входят в candidate. Изолированный
  `l4desk-e2e-test-backend-1` совпадает 87/90: отстают
  `routers/admin_users.py`, `routers/video_control.py` и
  `services/media_orchestrator_client.py`. Перед новым 17E E2E тестовый
  backend нужно пересобрать из того же Git archive и повторить hash-аудит.
- В репозитории найден только ручной internal `close-day`; вызова по расписанию
  не найдено. На хосте root/user crontab и systemd timers с признаками finance
  или metering отсутствуют. После перехода суток сначала read-only проверить
  проводку, затем решать, как штатно автоматизировать закрытие. Текущий
  `FinEntitlementWorker.run_single_tick` обходит **все** billing profiles, а
  notification dispatch и stop outbox тоже глобальные. Включать этот worker
  в тестовом backend с общей БД до tenant scope нельзя.
- Локально добавлен отдельный `FinMeteringCloseWorker`: default disabled,
  пустой allowlist ничего не проводит, для явно указанных tenant проверяет
  локальную дату каждой открытой положительной строки и ждёт 300 с после
  полуночи. Повтор использует существующий идемпотентный `close_and_post`.
  В test compose добавлены opt-in flags, но runtime flags и сервер не менялись.
  Адресные тесты 3/3, полный MenuBuilder backend 495/495, Ruff и Pyright pass.
  После наблюдения полуночи нужен отдельный контролируемый rollout только
  изолированного test-backend и runtime-проверка одной проводки, баланса и replay.
- Полуночный browser E2E: session `466` у terminal `1000003` шла
  2026-09-26 20:58:14–21:03:31 UTC (317 с), штатно остановлена пользователем.
  `FinUsageDaily` за 26 сентября выросла с 9305 до 9411 с (+106), за 27-е
  появилась строка на 211 с; 106+211=317. Новые сутки дали 211 free и 0
  billable, старые — 7200 free, 2211 billable и 100 копеек расчёта.
  Ingress до/после midnight имел `fresh_rtp=true`, маршрут и 0 unrouted;
  после stop route и transport исчезли. **Midnight split/media: PASS**.
  В 00:06 МСК вчерашняя строка всё ещё без `ledger_transaction_id`, balance
  tenant 3 = 1000 копеек, ledger содержит только прежний тестовый payment.
  **Automatic close/post к 00:06 не наблюдался**; новый worker ещё не
  развёрнут, а иной scheduler до этого момента не сработал. Счёт не менялся.
- После явного одобрения установлен только изолированный test-backend из Git
  archive `e4ae6b9` (архив SHA-256
  `ff557a741c306bd7f37dc3ca13afa50226723a597a0afc0f4514c6791f10deee`).
  Новый image `sha256:6a09ea073c28071d64377d6febef3085b1e4e6a816ce80e51e3672c162c1a762`
  совпал с архивом по всем 91/91 Python-файлам, без лишних файлов.
  Server `MenuBuilder/e2e/compose.yaml` получил ровно три подтверждённые
  строки (image и два metering flags); прежний файл сохранён как
  `compose.yaml.pre-e4ae6b9`. Его старое расхождение с Git по IoT consumer
  осталось: сервер жёстко выключает consumer, в Git opt-in через env.
  Перед будущей полной синхронизацией compose этот diff надо разрешить.
- Изолированный worker был включён лишь с `tenant_ids=[3]`, при выключенных
  entitlement worker, policy enforcement и IoT consumer. В 00:14:21 МСК
  он провёл ровно одну `usage` транзакцию `id=4` на 100 копеек для дня
  2026-09-26: две записи debit=credit=100, balance 1000→900 копеек,
  projection version 1→2. Следующий тик не добавил транзакцию и не изменил
  баланс. День 2026-09-27 остался 211 free seconds, без ledger posting.
  Затем worker выключен (`enabled=false`, `tenant_ids=[]`), test-backend
  пересоздан из того же image, `/docs`=200; основной `menubuilder-backend`
  сохранил прежний image и статус running. **Daily posting/replay: PASS**.
- Затем проверить paid continuation, первый платёжный anchor, online once за
  месяц, DST/границы месяца, grace/late/block, webhook+poll replay, ручной
  платёж/storno, double-entry/rebuild, rounding, Hub и archive. Локальные
  unit/contract tests на эти случаи есть; per-scenario runtime verdict нет.
- После полного E2E сверить весь release image/source, актуальные app1/media
  provider версии, flags, migration `027`, rollback и нулевой ledger mismatch;
  только тогда выпускать новый report/candidate `H-L4D-17E-MB-v1` и передавать
  контроллеру для append-only приёмки. 17F начнётся после этой записи.

## Оперативная проверка 773 и следующий бесплатный E2E (2026-09-27)

- 773 около 00:26 МСК вернул 500 при старте. В логах основного
  `menubuilder-backend` сразу несколько запросов к video API завершились
  `ConnectionResetError: [Errno 104] Connection reset by peer` в
  `asyncpg.connect_utils._create_ssl_connection` при подключении к PostgreSQL.
  После обновления страницы пользователь подтвердил нормальный старт;
  00:28:49 МСК `control/lease`=201 и `stream/start`=200, штатный stop=200
  и release=204 в 00:29:00. Контейнеры не перезапускались; TCP к DB с
  backend снова доступен, за следующие 3 минуты новых reset/500 не было.
  Причина сброса на стороне БД/сети не установлена; это отдельный incident,
  не доказательство сбоя l4capture или media plane.
- Пользователь подтвердил email `test04@platerra.ru` и разрешил создать
  отдельный tenant и terminal+PIN для теста бесплатных 10 минут без платежа.
  Manifest регистрации находится в приватном volume изолированного E2E;
  повторный обычный payment smoke запрещён, так как совершил бы mock payment.
- Локальный candidate вводит `E2E_REGISTRATION_ONLY=true` в payment runner:
  после подтверждения email он только проверяет login/tenant/нулевой баланс и
  фиксирует `registration_confirmed`. Onboarding принимает этот статус.
- Обнаружено расхождение: metering делит сутки по `L4DeskTenantProfile.timezone`,
  entitlement у ещё не платившего tenant использовал UTC из пустого billing
  anchor. Исправлено на tenant timezone с fallback к billing anchor/UTC.
- Для test-backend добавлен выключенный по умолчанию адресный override квоты:
  `L4DESK_FREE_QUOTA_TEST_TENANT_IDS` и `L4DESK_FREE_QUOTA_TEST_SECONDS`.
  План E2E: 600 секунд только для tenant test04, `policy_enforcement=true`
  только в изолированном backend. Это проверяет отказ нового admission после
  штатного stop и записи usage; текущая трансляция автоматически на 10-й
  минуте не прекращается. Основной сайт пока работает с enforcement=false,
  поэтому его UI сам по себе не доказывает отказ.
- Локально: новый unit test московской полуночи/600 секунд и изоляции лимита;
  backend pytest 496/496, Ruff check/format и Pyright 0 ошибок.
  Пользователь дал отдельное явное разрешение на изолированный rollout.
- Коммит `f2f49ec` опубликован в release branch. Git archive для server release
  `sha256=8b65d514f5b38fb2009bcec2d0899179d8923e160e7dc4a0fcd2f385676d7c71`
  установлен в `/home/user1/.l4d-releases/f2f49ec`. Из него собраны только
  `l4desk-e2e-test-backend:f2f49ec` (`sha256:29c4ae6e9aa0...`) и
  `l4desk-e2e-mock-gateway:f2f49ec` (`sha256:c7770babae34...`); хэши четырёх
  изменённых Python-файлов совпали между archive и images. Основной backend
  не пересоздавался.
- В server `/home/user1/MenuBuilder/e2e/compose.yaml` добавлены ровно три
  разрешённые строки для test policy/quota; backup — `compose.yaml.pre-f2f49ec`.
  Существующее отличие сервера по `IOT_CONSUMER_ENABLED: "false"` сохранено.
  Secrets `.env.e2e.local` не читались и не изменялись.
- После регистрации без платежа `tenant_id=4`, `user_id=654`, баланс 0.
  Onboarding через штатный API: `terminal_id=3719`, `device_id=1000004`,
  SN `a4b1000004c94276d260926`, IoT ready, certificate issued, online offline.
  PIN передан пользователю в чате, в Git не сохраняется. Изолированный
  entitlement API: free, 0/600 секунд, free terminal 3719.
- В test-backend runtime flags через одноразовый `docker compose` вызов:
  onboarding=true, policy enforcement=true, tenant allowlist=[4], quota=600;
  entitlement worker, metering close worker и IoT consumer=false. Эти
  opt-in значения не записаны в `.env.e2e.local`: при последующем compose
  recreate их надо передать повторно. Проверка значений в запущенном контейнере
  пройдена.
- Во время recreate test-backend трижды не прошёл schema check из-за
  `ConnectionResetError` при asyncpg SSL upgrade к PostgreSQL; через несколько
  минут повторный `docker start` прошёл, revision 027 и 23 таблицы проверены.
  Свежий read-only asyncpg connect из основного backend периодически также
  получает тот же reset, при том что `openssl s_client -starttls postgres`
  из его контейнера устанавливает TLS. Причина upstream/сети не установлена;
  риск повторения HTTP 500 у 773 сохраняется. Отдельной правки БД/сервиса не
  производилось.

## Коррекция test04 после конфликта org_id (2026-09-27)

- Пользователь указал, что `org_id=4` уже занят в IoT, попросил оставить
  tenant 4, удалить терминал 1000004 и перенести ту же учётную запись
  `test04@platerra.ru` в новый tenant со свободным ID. Agent 1000004 он
  остановил. `DELETE /api/settings/terminals/3719` удалил терминал только
  в MenuBuilder; IoT и MQTT остались и потребовали отдельной очистки.
- Проверен `org_id=1000`: отсутствовал в `orgs` MenuBuilder, `tb_orgs`,
  `tb_device_org_binds` и `tb_device_provisionings` IoT. Локальный
  `MenuBuilder/e2e/reassign_free_test_tenant.py` провёл preflight и
  транзакционно перенёс пользователя id 654, owner membership и registration
  в новый tenant. Tenant 4, удалённый терминал и аудит сохранены; активные
  web-сессии пользователя отозваны. Повторный запуск вернул `ALREADY_MOVED`.
  Первый вариант скрипта откатился из-за порядка FK flush; исправление
  содержится в `d77441b`.
- Изолированный test-backend пересоздан с тем же image `f2f49ec` и флагами
  `policy_enforcement=true`, `free_quota_test_tenant_ids=[1000]`,
  `free_quota_test_seconds=600`, onboarding=true. Первый startup снова упал
  на DB connection reset; повторный `docker start` успешен, `/docs`=200.
- `MenuBuilder/e2e/onboard_reassigned_tenant.py` через штатный API создал
  terminal 3720 / device 1000005 / SN `a4b1000005c18263d260926` для
  tenant 1000: IoT ready, certificate issued. PIN введён пользователем на
  Agent; он подтвердил online. Скрипт читает прежний приватный manifest,
  но не меняет его: там остаётся история tenant 4/1000004. Пароль в manifest
  стал устаревшим после явной смены пользователем пароля test04; текущий
  пароль не запрашивался и не менялся.
- `MenuBuilder/e2e/deprovision_conflicted_iot_device.py` выполнил
  адресную очистку старого 1000004: IoT `tb_devices.is_deleted=true`,
  `tb_device_connections` удалена, запись `DEPROVISIONED` в аудите,
  MQTT-пользователь удалён. Org bind и historical provisioning остались.
  Повторные preflight подтвердили deleted=true, connections=0,
  mqtt_user=false. Tenant 4 сохранён. В ProcessingBackend `get_current_terminal`
  намеренно распознаёт даже `is_active=false` ради XML ответа
  `/api/licensebilling`, но payment-маршрут не имеет отдельной проверки
  активности. Пользователь 2026-09-27 сообщил, что сертификат 1000004
  удалён; этот конкретный сертификат больше не является gate для 17E.
  Общий authorization gate для неактивных терминалов остаётся отдельной
  задачей по контракту `/api/licensebilling`.
- Через основной сайт пользователь запустил видео 1000005; в main backend
  `stream/start`=200 в 22:30:35 UTC. В БД сессия id 477 active_at
  22:30:35.661123 UTC, tenant 1000, terminal 3720; до неё короткая
  console-сессия дала 8 секунд usage локального дня 2026-09-27. Для
  исчерпания лимита 600 секунд трансляция завершена штатно в
  22:41:10.496839 UTC: 634.835716 секунды, округление metering до 635.
  `FinUsageDaily` локального дня 2026-09-27: video=635, console=8,
  source=643, free=643, billable=0. Ledger=0 и баланс=0. Изолированный
  entitlement видит 643/600 секунд; реальная policy seam для нового video
  admission дала `allowed=false`, `free_quota_exceeded`. До сессии она
  давала `allowed=true` при 8/600. Проверка с `as_of=2026-09-28 00:01 МСК`
  дала usage=0 и allowed=true; это симуляция времени, не отдельный midnight
  runtime E2E. Пользователь подтвердил движение видео и штатную остановку.
  **Учёт 10 минут и блокировка policy seam: PASS.** HTTP-отказ через
  изолированный test-backend не проверен: пароль приватного manifest стал
  устаревшим после смены пользователем. Production backend ещё permissive;
  повторный старт через обычный сайт не доказывает блокировку. Пользователь
  действительно повторно запустил видео на обычном сайте и штатно остановил:
  session id 478 closed, 72.128374 секунды; cumulative FinUsageDaily
  video=707, console=8, source=715. Runtime env основного backend прямо
  подтвердил `L4DESK_POLICY_ENFORCEMENT_ENABLED=false`. Такое повторное
  включение ожидаемо; включать enforcement глобально пока нельзя.
- HTTP denial на изолированном backend проверен 2026-09-27 под текущим
  владельцем test04 без передачи пароля/токена агенту: `POST
  /api/v1/video/devices/1000005/control/lease` с ровно
  `{"scope":"stream"}` вернул `403/free_quota_exceeded`. Первый запрос
  из Swagger вернул `400/session_id_mismatch` до проверки политики;
  повтор с точным телом без `session_id`
  прошёл нужную ветку. После отказа read-only БД показывает последнюю
  remote session `id=480`, созданную 23:31:39 UTC до HTTP-теста,
  usage tenant 1000 = 726 s (video=718, console=8), ledger=0;
  новых строк сессий/usage/ledger от отказа нет. Последняя session 480
  остаётся active от прежнего запуска через основной сайт; пользователь
  уведомлён. **HTTP free quota admission: PASS.**
- После указанного пользователем завершения видео около 23:33 UTC UI получил
  500 на keepalive/release/status: backend traceback заканчивается
  `ConnectionResetError` в `asyncpg._create_ssl_connection`. Последняя
  коммерческая session 480 осталась `active`, `closed_at=NULL`, usage не
  увеличился. Read-only IoT status позже показал `lease.active=false` и
  `stream.state=failed` (`l4capture_exited`); media ingress видел последние
  RTP около 23:34 UTC. Точный момент прекращения просмотра не установлен,
  поэтому закрывать session 480 текущим временем и начислять весь интервал
  нельзя. Во время следующего запуска 1000005 браузер снова показал
  периодические 500; серверные логи за последние 10 минут подтвердили
  `ConnectionResetError` на `control/status` и `control/keepalive`.
- Локально реализован bounded retry создания нового соединения MenuBuilder:
  до пяти попыток `asyncpg.connect` при `OSError`/`TimeoutError`/
  `CannotConnectNow`, максимум 2 секунды на попытку и короткий jitter.
  `pool_pre_ping` сохранён; SQL и финансовые транзакции не повторяются.
  500 тестов MenuBuilder, ruff и pyright прошли. Runtime деплой и повторный
  smoke ещё не выполнены. Это уменьшает 500 при кратком сбросе, но не
  гарантирует работу при длительной недоступности БД. Для закрытия 17E
  нужен отдельный механизм сверки и корректного завершения осиротевших
  коммерческих сессий с доказанным временем окончания и идемпотентным usage.
  Commit `516bf7e` запушен и с разрешения пользователя установлен в
  production `menubuilder-backend` 2026-09-27 около 00:03 UTC. Изменён
  только `/home/user1/MenuBuilder/backend/app/database.py`; прежний файл и
  image ID сохранены в `/home/user1/.l4d-releases/516bf7e/`. Скопированный
  SHA-256 совпал с локальным. Docker build/up только этого сервиса прошли.
  В startup первый connect получил `ConnectionResetError`, второй прошёл;
  schema compatibility `revision 027`, 23 таблицы, status GET 200 и
  read-only SQL успешны. За первые 3 минуты после обновления в логах
  backend не было новых 500, но продолжительный пользовательский тест ещё
  не выполнен.
  После повторного запуска и штатной остановки session 480 закрылась в
  00:01:49 UTC с `active_at=23:31:41 UTC`, несмотря на разрыв видео в
  середине. `FinUsageDaily` tenant 1000 вырос до 2534 секунд
  (video=2526, console=8; free=2534, billable=0). Этот usage включает
  недоказанный промежуток простоя; до коррекции показания тестового tenant
  нельзя использовать для 17E billing acceptance. Новую трансляцию 1000005
  пользователь попросен не запускать до исправления recovery.
  Из app1 access log подтверждён второй `stream/start` в 23:52:40 UTC и
  штатный `stream/stop` в 00:01:49 UTC: старая local session 480 не была
  разорвана перед новым provider epoch. Пользователь после обновления
  backend наблюдал `control/status` 1–2 минуты без HTTP 500; контейнер
  оставался Up и за первые шесть минут новых 500 в его логах не появилось.
- PostgreSQL `10.0.0.7` продолжает периодически сбрасывать новые asyncpg
  SSL подключения и даёт HTTP 500 даже на finance read и keepalive, а
  `DatabaseUserStore` превращает этот сбой в ложный 401 `Invalid credentials`.
  Это отдельный production incident. По решению пользователя 2026-09-27
  расследование провайдера ведётся вне 17E и не является условием
  project-local приёмки; нестабильность подключения может сделать отдельный
  runtime-прогон неубедительным и остаётся release readiness risk. Read-only
  диагностика 2026-09-27: с хоста и трёх контейнеров 38–43 из 80
  соединений получили TCP reset сразу после PostgreSQL SSLRequest, до TLS,
  авторизации и SQL; при темпе 1 запрос/с — 10 из 24 reset. Успешные
  соединения используют TLS 1.3. Сервер PostgreSQL 18.4 имел
  `max_connections=100` и около 33 соединений; исчерпания серверного лимита
  не видно. В Cloud.ru Managed PostgreSQL включён пулер:
  `default_pool_size=20`, `max_client_conn=100`, `reserve_pool_size=0`,
  `query_wait_timeout=120s`, `server_idle_timeout=600s`,
  `server_lifetime=3600s`. `default_pool_size` — серверный пул на БД,
  а не лимит клиентов. Текущие цифры не объясняют сброс до TLS;
  требуются графики клиентов/очереди и расследование Cloud.ru входного
  endpoint/пулера. Параметры и код ещё не менялись. Тестовый
  пароль в manifest действительно отличается от текущего user hash, что
  пользователь подтвердил как намеренную смену пароля.
  Дополнительная выгрузка Cloud.ru `pg-log.json` за 22:09–23:09 UTC:
  2695 событий, в том числе 720 штатных unix health checks пулера,
  201 `TLS handshake ... unexpected eof` только в 22:56–22:59 UTC
  (период наших SSLRequest-проб, которые закрывали соединение сразу после
  ответа `S`; эти предупреждения не являются доказательством первопричины),
  35 `tls_sbufio_recv ... unexpected eof` ранее в течение часа. Нет записей
  о `max_client_conn`, очереди или исчерпании PostgreSQL соединений.
  Выгрузка логов не содержит трассы самих pre-TLS TCP reset; нужны метрики
  пула и проверка входного узла у Cloud.ru.
  Скриншот Cloud.ru `pg.png` за последние два часа: видны две реплики
  пулера, примерно 8–16 клиентских соединений на реплику (суммарно
  порядка 20–30 при лимите 100), без длительного роста. Ожидание серверного
  слота обычно 0, изредка 1–2 клиента; длительной очереди нет. Это
  дополнительно не поддерживает гипотезу исчерпания `max_client_conn` или
  `default_pool_size`, хотя одиночные ожидания требуют обычного наблюдения.
  Примерно половина pre-TLS соединений сбрасывается; неисправность одной
  из двух реплик/маршрута возможна, но по скриншоту не доказана.

## Решение для PR

### Task intake: восстановление video usage после сбоя БД

- Цель / тип задачи: корректирующий код и E2E для двух video provider epochs,
  которые сейчас могут слиться в одну коммерческую сессию.
- Затронутые компоненты / границы работ: MenuBuilder BFF и тесты;
  app1/agent/media остаются источником статуса, без изменения протокола.
- Владелец данных / контракта / миграций: MenuBuilder владеет
  `L4DeskRemoteSession` и `FinUsageDaily`; существующий `last_cursor` session
  будет курсором последней учтённой UTC-секунды. Новая миграция не нужна.
- Producer → transport → consumer: app1 lease/stream status → REST → BFF
  reconciliation → локальная session state и следующий stream start.
- Инварианты: не объединять разные provider epochs; учитывать подтверждённые
  порции внутри сессии примерно раз в минуту, а при штатном stop — остаток;
  при недоказанном конце спорный хвост не начислять; округлять вниз в пользу
  потребителя без накопления ошибок между порциями; не обходить tenant/access
  checks и не менять paid ledger.
- Риски / неизвестное: status app1 может быть stale; старый usage 480 уже
  агрегирован, требует отдельной адресной коррекции на тестовом tenant.
- Минимальные файлы: `video_control.py`, `iot_client.py`,
  `remote_session_metering.py`, `financial_core/metering.py`, contract
  remote-control и релевантные тесты. Redis для этого потока не нужен;
  курсор и порция фиксируются атомарно в PostgreSQL.
- Критерии готовности: повторный запуск после expired lease создаёт новую
  session; старую помечает с причиной waiver без спорного хвоста; active
  provider epoch остаётся прежней; подтверждённые минутные порции сохраняются
  и не повторяются; tenant 1000 usage после коррекции соответствует только
  доказанным интервалам; реальный старт/stop 1000005 без 500.
- Проверки: MenuBuilder pytest + ruff/format/pyright и адресный E2E после
  согласованного серверного деплоя; server data correction только после
  отдельной сверки суммы и разрешения пользователя.
- Локально проверено: 510 backend tests, ruff и pyright (2026-09-27);
  отдельный тест подтверждает накопление поздних порций после posting,
  включая нулевую денежную дельту.
- Server E2E 2026-09-27: commit `ed44bfd` доставлен в production
  `menubuilder-backend` после отдельного подтверждения; source и image SHA-256
  совпали. Старая session 480 осталась закрытой; новый start 1000005 создал
  session 481. При активном видео checkpoints 62+62+62 с записали курсор,
  usage и audit в одной транзакции; штатный stop добавил хвост 1 с.
  Фактическая длительность 187,85 с, учтено 187 с (округление вниз),
  HTTP 500 и задержки видео не было. Несколько reset новых DB-соединений
  восстановились штатными ретраями.
- Test-tenant correction: ошибочные 1808 с старой session 480 полностью
  waived в пользу потребителя; audit event 271
  (`tenant1000-session480-consumer-waiver-20260927`). После коррекции
  `FinUsageDaily.id=4`: 913 source/free seconds = 905 video + 8 console,
  billable 0, posted 0. Сессии 480/481 закрыты. Usage выше тестового
  лимита 600 с; повторный HTTP admission описан ниже.
- Повторный HTTP admission после коррекции: test-backend
  `l4desk-e2e-test-backend:f2f49ec` работает с
  `policy_enforcement=true`, tenant allowlist `[1000]` и лимитом 600 с.
  Владелец test04 выполнил через Swagger точный POST lease для 1000005
  с `{"scope":"stream"}` и получил `403/free_quota_exceeded`; лог
  test-backend подтвердил HTTP 403. До и после отказа последняя session
  `481 closed`, usage `913 = 905 video + 8 console`, ledger count `0`.
  Один read-only DB connect исчерпал пять попыток на provider reset;
  повторный read-only connect прошёл после двух сбросов. Отказ не создал
  коммерческих записей. HTTP free quota admission после коррекции: PASS.

### Путь закрытия 17E после бесплатного E2E

1. HTTP `403/free_quota_exceeded` под владельцем test04 на изолированном
   backend и отсутствие новой сессии, usage/ledger записи подтверждены.
   Кодовая ветка проверяет policy до вызова IoT lease; пароль/токен
   пользователя агент не получал. Этот шаг закрыт.
2. Исправить распределение `org_id` между MenuBuilder и IoT: регистрация
   сейчас проверяет свободный ID только в MenuBuilder; tenant 4 реально
   столкнулся с занятым IoT ID. До единого allocator/reservation нельзя
   считать публичную регистрацию принятой. Исправление вести в этой же
   release branch с адресными contract и concurrency проверками.
3. Составить по пунктам `L4D-17E-MB.md` таблицу evidence: runtime уже
   подтверждённые free quota, paid usage, payment/webhook, monthly charge,
   midnight split/post; локально покрытые DST, grace/block,
   reconciliation/rebuild, rounding, Hub и archive. Непокрытые пункты
   проверять в изолированном контуре; не выполнять реальные списания,
   массовые письма или ручную порчу production projection.
4. Сверить полный candidate image/source, миграцию 027, effective flags и
   rollback; выполнить ограниченный production smoke при выключенной
   глобальной коммерческой активации. Выпустить новый 17E report и
   `DETACHED_V1` candidate для независимой проверки контроллером.
   Исторический `BLOCKED_CONTRACT` report не переписывать как принятый.

Сбой managed PostgreSQL отслеживается отдельно по решению пользователя;
неоднозначный из-за него тест следует повторить, но расследование провайдера
не включать в 17E contract verdict.

Один draft PR из `release/l4tools-1.8.2-beta-1` в `main`. После закрытия
обязательных gate обновить этот документ, повторить затронутые проверки,
перевести PR в ready и объединить. До этого не удалять старые ветки и не
считать кандидата финальным production release. Основной production backend
в ходе последнего 17E теста не пересоздавался; изолированный backend и
тестовые tenant/terminal данные изменялись адресно, секреты не раскрывались.
