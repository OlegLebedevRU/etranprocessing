# L4D: единый кандидат к main

Дата сверки: 2026-09-26. Статус: **draft merge candidate**, каскад ещё не закрыт.

## Task intake

- Цель: свести стабильный L4D/L4C код к одному пути в `main` без продолжения
  параллельного каскада веток. Эта задача не меняет production server.
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
  `release/l4tools-1.8.2-beta-1` (`41b73bc`): 111 коммитов только на стороне
  кандидата, конфликтов двух веток нет. Между ними 494 изменённых файла.
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
| MenuBuilder backend | `uv run pytest -q`: 495 passed, 50 warnings после close worker | passed |
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
- Затем проверить paid continuation, первый платёжный anchor, online once за
  месяц, DST/границы месяца, grace/late/block, webhook+poll replay, ручной
  платёж/storno, double-entry/rebuild, rounding, Hub и archive. Локальные
  unit/contract tests на эти случаи есть; per-scenario runtime verdict нет.
- После полного E2E сверить весь release image/source, актуальные app1/media
  provider версии, flags, migration `027`, rollback и нулевой ledger mismatch;
  только тогда выпускать новый report/candidate `H-L4D-17E-MB-v1` и передавать
  контроллеру для append-only приёмки. 17F начнётся после этой записи.

## Решение для PR

Один draft PR из `release/l4tools-1.8.2-beta-1` в `main`. После закрытия
обязательных gate обновить этот документ, повторить затронутые проверки,
перевести PR в ready и объединить. До этого не удалять старые ветки и не
считать кандидата финальным production release. В этой задаче серверные
файлы, службы и секреты не затрагивались.
