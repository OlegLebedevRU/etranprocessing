# L4D-17E: матрица приёмки коммерческого контура

Срез: 2026-09-27 UTC. Кандидат: `release/l4tools-1.8.2-beta-1`.
Статус: **IN_PROGRESS**, `H-L4D-17E-MB-v1` не выпущен. Исторический
`L4D-17E-MB-report.md` со статусом `BLOCKED_CONTRACT` сохраняется.

## Task intake

- Цель: закрыть project-local и consumer-contract проверки `L4D-17E-MB` без
  глобального включения коммерческой политики.
- Владелец регистрации, сессий, usage и ledger: MenuBuilder; миграций общей
  схемы: ProcessingBackend; lease/online: app1; media: l4media.
- Producer → consumer: owner UI/API → MenuBuilder → app1/Agent → закрытая
  video/console session → `FinUsageDaily` → ledger/projection/entitlement.
- Инварианты: один tenant/terminal identity в обоих контурах; одна проводка на
  идемпотентный источник; debit=credit; подтверждённые секунды внутри сессии
  учитываются порциями; недоказанный хвост прощается потребителю; запрет
  новых сессий после исчерпания бесплатной квоты или истечения grace.
- Периоды проверки: реальные бесплатные 10 минут только для тестового tenant
  1000; для локальных переходов entitlement тестовый цикл 30 минут, grace
  10 минут. Календарные месяцы, три дня grace, DST и даты в production
  проверяются отдельными детерминированными тестами с `as_of`. Глобальное
  время сервера и длительность production цикла не меняются.
- Неизвестное: полный deployed image/source parity, текущая сверка ledger,
  оплаченный E2E после quota, ручной платёж/storno, Hub и архив на стенде.
- Проверка: локальный suite/quality; read-only аудит runtime; адресные E2E
  только в согласованном изолированном контуре и на утверждённых tenant.

## Матрица

`passed (local)` означает проверку кода с тестовой БД/моками, но не E2E.
Ссылки на runtime observations ведут к [журналу сближения](l4d-main-convergence.md),
[промежуточному отчёту](../../../MenuBuilder/docs/l4desk/handoffs/L4D-17E-MB-progress-2026-09-26.md),
[коррекциям](../../../MenuBuilder/docs/l4desk/handoffs/L4D-17E-corrective-2026-09-26.md)
и [org_id handoff](l4d-17e-org-id-handoff.md).

| Проверка / слой | Ожидается | Факт / evidence | Статус | Причина пропуска / риск |
|---|---|---|---|---|
| Required handoff IDs | Все 17E prerequisites приняты | `l4desk-service/docs/prompts/contract-handoff.md`, сверено в журнале сближения | passed (document) | Не заменяет E2E |
| Регистрация, письмо, tenant и owner | Подтверждение создаёт согласованный org ID и единственного owner | test05: tenant 10000 в MenuBuilder/IoT, роль 5, повтор ссылки идемпотентен | passed (E2E) | Production registration выключена |
| Коллизия org ID | Занятый IoT ID не выдаётся новому tenant | IoT ID 4 → 409; новый резерв 10000 совпал в двух системах | passed (E2E) | Неиспользованные резервы после сбоя требуют аудита |
| Terminal onboarding/readiness | PIN, certificate, MQTT и identity согласованы | 1000003 и 1000005 online; исходные несовпадения исправлены | passed (E2E) | Повторить на финальном candidate image |
| Owner video/console; old/new; взаимное исключение | Движущиеся кадры, консоль и 409 при занятой lease | 1000003 и 1000005: видео/console/stop подтверждены; lease_taken=409 | passed (E2E) | Повторить на финальном candidate image |
| Бесплатный terminal и лимит | Только разрешённый terminal; после лимита 403 | tenant 1000, 600 с opt-in, исторически 913 доказанных секунд; на чистом `df7598c` под правильным test04 повторно получен `403/free_quota_exceeded` без создания сессии или проводки | passed (E2E) | Production 7200 с проверен локально |
| Порции внутри сессии, retry и округление | Курсор не дублируется, хвост округлён вниз | session 481: 62+62+62+1=187 с из 187,85 с; повторного начисления нет | passed (E2E) | Провайдер БД иногда сбрасывает новые соединения |
| Paid continuation после free | Положительный баланс допускает сессию | tenant 1000: mock payment 10 руб. posted, баланс +1000 коп.; тот же HTTP lease после 403 принят с 201, release=204; local test | passed (E2E + local) | Полное видео на новом image не запускалось, проверено admission |
| Первый платёж и anchor | Один успешный платёж задаёт дату; повтор её не меняет | tenant 1000: payment 3/transaction 5, anchor 2026-09-27 09:35:43 UTC; два webhook вернули 200, poll вернул 200/succeeded/transaction 5, одна проводка; tenant 3 historical + local test | passed (E2E + local) | Реального списания нет |
| Terminal-month online once | Событие online даёт не более одного charge | tenant 3: нулевая free charge, duplicate event подавлен | passed (E2E) | Платный monthly charge пока только local |
| DST, месяц, последний день | Секунды/anchor сохраняются на границах | `test_split_interval_dst_spring_and_autumn`, `test_anchor_add_months_rule_of_last_existing_day` | passed (local) | Не ждать календарную границу в runtime |
| Grace, поздний online/payment, graceful block | Разрешение до deadline, отказ ровно на нём, возврат после оплаты | `test_all_cycle_and_grace_boundaries`, `test_online_after_deadline_creates_charge_and_immediate_blocked`, `test_late_payment_preserves_anchor_and_unblocks`, `test_active_session_stop_on_blocked`; короткий цикл 30/10 минут | passed (local) | Нет адресного runtime E2E блокировки активного потока |
| YooKassa mock webhook+poll | Повтор не даёт вторую проводку | tenant 3 mock, payment 2 → transaction 3, 1000/1000 коп.; replay/poll без дубля | passed (E2E) | Реальный YooKassa не затронут |
| Ручной платёж и storno | Обратные записи, баланс и RBAC корректны | `test_manual_payment_creation_and_storno_reversal`, Hub test code 11 | passed (local) | Нет изолированного HTTP E2E |
| Суточное закрытие и replay | Одна проводка за закрытый локальный день | tenant 3: 100/100 коп., balance 1000→900; повтор worker без новой записи | passed (E2E) | Следующее окно запускать только после завершения сессий |
| Double-entry, rebuild, reconciliation | Нулевая разница и воспроизводимая projection | Локальные тесты + read-only tenant 3 header/entry mismatch=0; tenant 1000 reconciler на 20-минутном окне с новым платежом: matched, debit=credit=1000, mismatch=0, projection difference=0 | partial (runtime + local) | Projection rebuild остаётся local |
| Rounding/discarded после reconciliation | Только целые рубли в posting, остаток учтён в snapshot | `test_daily_usage_exact_120m_and_120m01s`, `test_general_future_tariff_formula_rounding_invariants` | passed (local) | На runtime проверен лишь конкретный posting 100 коп. |
| Hub filters/correlation/mismatch | Поиск и drilldown сохраняют источник, mismatch обнаруживается | `test_correlation_drilldown_nodes_and_mismatches`, `test_hub_http_api_rbac_and_views` | passed (local) | Нет browser/runtime E2E |
| Archive import/retention | Manifest, запрет удаления финансовых данных | `test_archive_manifests.py` | passed (local) | Нет runtime import/retention E2E |
| Immutable consumer fixtures | Старый/новый контракт совместим | Принятый IoT fixture `1.1.0 / 2026-09-25-v2` скопирован без изменений из provider commit `22a50a1`, SHA-256 совпал с handoff; девять событий и примеры ответов совпадают со старым `1.0.0`, обе версии валидируются локальным consumer | passed (local contract) | Runtime feed app1 отдельно подтверждён историческим E2E; текущий тест не вызывает provider |
| Full release parity, migration, flags, rollback | Полный source/image match, 027 и disabled/restricted флаги | Чистый test-backend `df7598c`: 92/92 active Python-файла совпали с Git archive; schema 027; flags адресные; старый image сохранён | partial | Production image ещё расходится с Git candidate; весь release не принят |
| Production-safe smoke | Изолированный tenant, остальные не затронуты | Новый `/docs`=200, test-backend flags `true/true/true/[1000]/600`, production registration/billing/policy/worker=false; quota, mock payment и paid lease проверены, ledger/usage read-only инварианты нулевые | partial (runtime) | Production image ещё не обновлён; остальные контрактные сценарии |

## Условия выпуска handoff

1. Выполнить оставшиеся runtime проверки на адресных tenant без реальных
   списаний и массовых писем; после каждой сверить session/usage/ledger.
2. Снять read-only срез `debit=credit`, reconciliation mismatch и projection
   для test tenant; не исправлять финансовые строки ради прохождения gate.
3. Сверить полный image/source, миграцию 027, effective flags и rollback;
   зафиксировать точные версии и окно UTC.
4. Повторить project-local checks для финальной ревизии, выпустить новый отчёт
   и `DETACHED_V1` candidate; принятие handoff остаётся у контроллера.

## Проверено в текущей итерации

- Read-only `sudo docker ps` через утверждённый SSH host: test-backend
  `l4desk-e2e-test-backend:224daaf`, production MenuBuilder и app1 находятся
  в `Up` на 2026-09-27 08:54:58 UTC. `docker compose ps` без приватного
  env-файла не смог интерполировать mock credentials; состояние контейнеров
  взято из `docker ps`, секреты не читались.
- Read-only проверка active production settings: registration=false,
  billing=false, policy=false, entitlement worker=false. Текущий image ID
  production MenuBuilder `sha256:08b6d35b...`, изолированного backend
  `sha256:ba512a5a...`; полный digest можно получить через `docker inspect`.
- Parity defect: в изолированном image `config.py` SHA-256
  `8446f8aeaf7f92871fb927f15a16146c7130a188e950c07675ffc9265461252a`,
  `entitlement.py` SHA-256
  `c8928efcd52530c71975bbe9b009e5841b8026d0222ee9d1af114d2003976f57`.
  В Git HEAD соответственно `ee5092385870eb3b1246828e76d225315cac34b60b9fc1d72de2d9a2078bc4b8`
  и `8989cd55716b270745401b9d69e068769dbef928fdec62e93a9c087a45d91e08`.
  `video_control.py` совпал побайтно. Попытка прочитать поле
  `l4desk_free_quota_test_tenant_ids` из active `Settings` дала
  `AttributeError`: текущий изолированный image не содержит адресного лимита.
  `git show 224daaf` поле содержит. Это дефект сборки/поставки, не тест
  10-минутной квоты на финальном image.
- Полная сверка active `app/*.py`: 92 локальных и 93 серверных файла.
  Несовпадение хеша: `config.py`, `main.py`, `entitlement.py`; отсутствует
  `metering_close_worker.py`; лишние в образе `permissions.py` и
  `services/remote_session_stop.py`. Во вложенном `app/app/` есть ещё 89
  старых Python-файлов: требуется чистая сборка из Git archive, а не
  накопительное копирование в прежний контекст.
- Короткий локальный тест использует два заранее заданных 30-минутных цикла
  и 10-минутные deadline; он проверяет admission на точных границах и
  восстановление после пополнения. Он не проверяет генератор календарных
  циклов и не считается runtime E2E. Fake DB исправлена: теперь учитывает
  timestamp и sequence при запросе цикла. Первая итерация полного suite
  выявила это ограничение (516 passed, 1 failed), после исправления
  `uv run pytest -q --disable-warnings` — 517 passed, 50 warnings,
  exit 0 (2026-09-27 09:09 UTC). `uv run ruff check --fix app tests`,
  `uv run ruff format app tests`, `uv run pyright app` — exit 0;
  pyright: 0 errors/0 warnings.

## Изолированный backend из чистого Git archive

- Получено отдельное согласие пользователя на серверное изменение.
  `df7598c3f1830c86df585c380989e6b278b3a2b3` запушен в
  `origin/release/l4tools-1.8.2-beta-1`; локальный архив
  `D:\.codex\l4d-17e-df7598c.tar` имеет SHA-256
  `a1eeb8268221fcf50ea8e0f6014115cbccf15d2c871c633d2e4e323effbb3150`.
  Серверный архив `/home/user1/.l4d-releases/df7598c/source.tar` сверен
  с тем же SHA-256 и извлечён в отдельный пустой каталог.
- Preflight SSH: available RAM 2019 MiB, root disk 84%, load 0.11.
  MCP Ops не использовался; прямой SSH — утверждённый fallback.
- `sudo docker build` из этого каталога создал
  `l4desk-e2e-test-backend:df7598c` (image
  `sha256:42fa63c0495d71e4e9700564595aba9a4506f3ea6cefde4698f0b822223e301b`).
  Все 92 Python-файла нового image и затем active test-backend совпали с
  архивом; лишних файлов в active `app/` нет. Четыре локальных working-tree
  SHA отличаются из-за преобразования строк при checkout; сравнивался
  канонический Git archive, а не байты Windows checkout.
- Через существующий Compose пересоздан **только**
  `l4desk-e2e-test-backend-1`. Установлены разовые переменные Compose:
  `E2E_BACKEND_IMAGE=l4desk-e2e-test-backend:df7598c`,
  `E2E_POLICY_ENFORCEMENT_ENABLED=true`,
  `E2E_FREE_QUOTA_TEST_TENANT_IDS=[1000]`,
  `E2E_FREE_QUOTA_TEST_SECONDS=600`. Active settings: registration=true,
  billing=true, policy=true, tenant allowlist `[1000]`, quota 600,
  entitlement worker=false, IoT consumer=false. `/docs`=200.
  Compose и приватный env-файл не менялись: при будущей пересборке контейнера
  эти четыре override надо передать повторно. Rollback image
  `l4desk-e2e-test-backend:224daaf` сохранён (digest `sha256:ba512a5a...`).
- До переключения: test tenant 3, 1000 и 10000 не имели active session;
  schema `027`; tenant 3 ledger 2 transaction, debit=credit=1100 коп.,
  tenant 3 usage 9628 с, tenant 1000 usage 918 с. После переключения
  read-only SQL: bad ledger headers=0, bad entry sums=0, source seconds
  mismatch=0, `calculated != posted+discarded`=0, bad rounding=0,
  active sessions=0 для этих tenant. Это адресный audit, не полный
  `FinReconciliationService.run_reconciliation`.
- Затем `FinReconciliationService.run_reconciliation` запущен для tenant 3
  на текущем 20-минутном окне через read-only transaction и обёртку,
  которая не записывает `FinReconciliationRun` и запрещает rebuild.
  Результат `matched`, mismatch_count=0, balance_difference_kopecks=0,
  все категории mismatch пусты. В этом окне новых проводок не было;
  проверка projection охватывает ledger до текущего времени. Исторические
  header/entry суммы проверены отдельным read-only SQL выше.
- Отдельный production MenuBuilder image остался прежним. Его active `app/`
  расходится с архивом по шести SHA. Нормализация CRLF показала, что только
  `config.py`, `main.py`, `entitlement.py` отличаются по содержанию;
  `database.py`, `admin_organizations.py`, `remote_session_metering.py`
  совпадают после нормализации строк. В image нет
  `metering_close_worker.py`, зато есть два старых файла.
  Production rollout требует отдельного согласованного шага после
  завершения E2E, с проверкой совместимости и откатом.
- Первый пользовательский `POST /api/v1/video/devices/1000005/control/lease`
  на новом image вернул 403 с текстом «Доступ к устройству 1000005 запрещен»;
  серверный access log подтверждает HTTP 403. Этот ответ даёт
  `_verify_device_access` до проверки quota. Read-only БД: test04 user 654
  имеет `org_id=1000`, роль 5, membership 1000; terminal 3720/device
  1000005 также имеет `org_id=1000`. Вероятен старый tenant claim в JWT
  Swagger. Запрошен только `org_id/user_id/role_id` через `/api/auth/me`,
  без передачи токена.
- Пользователь исправил логин Swagger: тот же POST на новом image вернул
  `403/free_quota_exceeded`. До платежа read-only SQL: tenant 1000 —
  7 исторических sessions, 0 active, max id 482; usage 918 с, ledger 0.
  Повторный отказ не создал новую session или проводку.
- Через изолированный Swagger создан mock YooKassa payment `id=3` на
  10 руб., `provider_payment_id=e2e-77ada559-5214-4393-9511-3871d58986a2`.
  Шлюз подтвердил `succeeded`; первый и повторный webhook ответили 200.
  В БД ровно один posted `payment` transaction `id=5`, debit=credit=1000
  коп.; balance projection=1000 коп., version=1, first payment anchor
  `2026-09-27 09:35:43.543956 UTC`, entitlement=`active`; usage остался
  918 с. Реальной ЮKassa и списания денег не было.
- Штатный reconciler на 20-минутном окне tenant 1000 запущен с
  `auto_rebuild_projection=false`, read-only transaction и обёрткой,
  запрещающей сохранение `FinReconciliationRun`: `matched`,
  debit=credit=1000, posted=1000, discarded=0, mismatch_count=0,
  projection difference=0, все категории mismatch пусты.
- После оплаты тот же владелец выполнил POST stream lease для 1000005:
  HTTP 201 вместо прежнего `403/free_quota_exceeded`; затем штатный DELETE
  вернул 204. Read-only БД после release: active sessions=0, payment 3
  остаётся `succeeded` с transaction 5, ledger ровно одна проводка
  debit=credit=1000, balance=1000. Полный video-start здесь не нужен для
  проверки admission; его работа подтверждена историческим E2E.
- Последующий POST `/api/v1/finance/payments/3/poll` на новом image вернул
  HTTP 200, `succeeded`, `ledger_transaction_id=5`. Read-only SQL после poll:
  payment 3 ссылается на transaction 5; ledger tenant 1000 по-прежнему
  содержит одну posted проводку, debit=credit=1000 коп.; balance=1000 коп.,
  version=1, last_transaction_id=5. Две entry дают те же суммы.
- Frontend `npm test -- --run`: 58 passed; `npm run build`: exit 0.
  Это локальные проверки текущего дерева, browser E2E они не заменяют.
- Новый неизменяемый consumer fixture скопирован из принятого IoT provider
  commit `22a50a186da25dddb19612c475bf9bcbb4a7fab2` в
  `MenuBuilder/backend/tests/fixtures/iot_event_feed_examples_v1_1.json`.
  SHA-256 `771856c6cfed996aa8a9a99c122a73096afee123a089895f0089c17eb6bac1fe`
  совпал с принятым `H-L4D-07-IOT-STOP-v1`. Исходный v1 fixture сохранён.
  Тест парсит все девять событий и reconciliation example обеих версий;
  различаются только `contract_version` и `schema_revision`. После добавления
  теста `uv run pytest -q --disable-warnings`: **518 passed**, 50 warnings,
  exit 0; `uv run ruff check --fix app tests`, `uv run ruff format app tests`,
  `uv run pyright app`: exit 0, 0 ошибок/предупреждений Pyright.
