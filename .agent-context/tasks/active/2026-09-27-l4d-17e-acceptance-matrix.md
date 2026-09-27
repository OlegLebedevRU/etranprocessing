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
| Бесплатный terminal и лимит | Только разрешённый terminal; после лимита 403 | tenant 1000, 600 с opt-in, 913 доказанных секунд; `403/free_quota_exceeded`, без новой session/ledger | passed (E2E) | Production 7200 с проверен локально, не ждали 2 часа |
| Порции внутри сессии, retry и округление | Курсор не дублируется, хвост округлён вниз | session 481: 62+62+62+1=187 с из 187,85 с; повторного начисления нет | passed (E2E) | Провайдер БД иногда сбрасывает новые соединения |
| Paid continuation после free | Положительный баланс допускает сессию | `test_positive_paid_continuation_after_free_quota` | passed (local) | Нужен адресный HTTP E2E с тестовым оплаченным tenant |
| Первый платёж и anchor | Один успешный платёж задаёт дату; повтор её не меняет | tenant 3 mock succeeded + webhook/replay/poll; anchor сверялся; тест `test_first_payment_anchor_and_subsequent_immunity` | passed (E2E + local) | Нет реального списания, и оно не требуется |
| Terminal-month online once | Событие online даёт не более одного charge | tenant 3: нулевая free charge, duplicate event подавлен | passed (E2E) | Платный monthly charge пока только local |
| DST, месяц, последний день | Секунды/anchor сохраняются на границах | `test_split_interval_dst_spring_and_autumn`, `test_anchor_add_months_rule_of_last_existing_day` | passed (local) | Не ждать календарную границу в runtime |
| Grace, поздний online/payment, graceful block | Разрешение до deadline, отказ ровно на нём, возврат после оплаты | `test_all_cycle_and_grace_boundaries`, `test_online_after_deadline_creates_charge_and_immediate_blocked`, `test_late_payment_preserves_anchor_and_unblocks`, `test_active_session_stop_on_blocked`; короткий цикл 30/10 минут | passed (local) | Нет адресного runtime E2E блокировки активного потока |
| YooKassa mock webhook+poll | Повтор не даёт вторую проводку | tenant 3 mock, payment 2 → transaction 3, 1000/1000 коп.; replay/poll без дубля | passed (E2E) | Реальный YooKassa не затронут |
| Ручной платёж и storno | Обратные записи, баланс и RBAC корректны | `test_manual_payment_creation_and_storno_reversal`, Hub test code 11 | passed (local) | Нет изолированного HTTP E2E |
| Суточное закрытие и replay | Одна проводка за закрытый локальный день | tenant 3: 100/100 коп., balance 1000→900; повтор worker без новой записи | passed (E2E) | Следующее окно запускать только после завершения сессий |
| Double-entry, rebuild, reconciliation | Нулевая разница и воспроизводимая projection | `test_reconciliation_all_invariants`, `test_reconciliation_post_metering_transactions` | passed (local) | Нужен текущий read-only runtime audit всех test tenant |
| Rounding/discarded после reconciliation | Только целые рубли в posting, остаток учтён в snapshot | `test_daily_usage_exact_120m_and_120m01s`, `test_general_future_tariff_formula_rounding_invariants` | passed (local) | На runtime проверен лишь конкретный posting 100 коп. |
| Hub filters/correlation/mismatch | Поиск и drilldown сохраняют источник, mismatch обнаруживается | `test_correlation_drilldown_nodes_and_mismatches`, `test_hub_http_api_rbac_and_views` | passed (local) | Нет browser/runtime E2E |
| Archive import/retention | Manifest, запрет удаления финансовых данных | `test_archive_manifests.py` | passed (local) | Нет runtime import/retention E2E |
| Immutable consumer fixtures | Старый/новый контракт совместим | Нужна итоговая сверка provider report и fixtures с финальным commit | not run | Нельзя утверждать по историческим отчётам |
| Full release parity, migration, flags, rollback | Полный source/image match, 027 и disabled/restricted флаги | Image с тегом `224daaf` работает, но `config.py` и `entitlement.py` в нём расходятся с Git `224daaf`; production флаги registration/billing/policy/worker=false | failed (parity) | Требуется воспроизводимая пересборка из Git archive и повторная проверка |
| Production-safe smoke | Изолированный tenant, остальные не затронуты | Ранее test05 и tenant1000 адресно работали, production registration выключена | partial (E2E) | Повторить после финального candidate image |

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
