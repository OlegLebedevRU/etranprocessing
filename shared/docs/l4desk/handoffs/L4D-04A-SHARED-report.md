# L4D-04A-SHARED — Declarative expand schema

## Contract gate

- Scope: `shared`; branch: `l4desk/l4d-04a-shared`.
- Вход: `H-L4D-03-MB-v1`, ровно один блок `ACCEPTED`, consumer `L4D-04A-SHARED`, без отзыва на момент проверки.
- Producer commit: `4da76eca24bab856bdad764df6e6a1dc9de44f1c`; contract/artifact version: `1.0.0`; schema revision: `2026-09-17-v1`.
- Проверены SHA-256 входных артефактов:
  - `MenuBuilder/backend/tests/fixtures/iot_event_feed_examples_v1.json`: `1fafb1d27010917f43f5d36502cbfaa1decd5cce36c80a6dc397f4180556df2b`.
  - `MenuBuilder/backend/tests/test_iot_event_feed_consumer.py`: `6664cc17db932fd4c84566c197cd8182a7521bb73589256a78fd3fa530ca9589`.
- Вход 03 подтверждает последовательность и IoT facts, но не является полной схемой БД.
- Пользователь явно разрешил read-only исследование MenuBuilder, ProcessingBackend и iot-rpc-rest-app и принял найденные объявления как baseline. Live-сверка БД остаётся 04B.
- Исходник существующих IoT-таблиц: `MenuBuilder/backend/app/models_iot_consumer.py`, SHA-256 `e05c23656be70bfac461d8cb0e5abb32d301888825b9e3f6f855f9ce826427f2`; исходный checkout `384a65bdb9855a30a6e49318ac31618378536ba6`.
- Уточнение входа: фактический PK quarantine называется `id`, не `quarantine_id`; схема сохраняет `id`. Внешние identifiers остаются строками, без UUID-only ограничения для импортированных событий.

## Согласованные границы

- Все новые модели подключаются явно; обычный импорт пакета не регистрирует новые таблицы и не конфликтует с локальными моделями MenuBuilder.
- Старые модели не меняются. Нет Alembic, бизнес-логики, сетевого/DB I/O, MQTT-клиента, переключения флагов или деплоя.
- Только shared-local проверки; интеграция и lockfiles потребителей остаются 04B/04C.
- Пользователь согласовал публикацию **исходного пакета через Git**: версия + точный коммит + каталог `shared`. Внешний реестр и публикация wheel/sdist не требуются. Архивы собираются только для проверки упаковки и не являются обязательным внешним входом.
- Не сливать в main: документированный автоматический выпуск отслеживает main. Чужие изменения журнала и двух документов не включаются в коммит.

## Результат

**ACCEPTED / PUBLISHED** — в пределах согласованной публикации исходного пакета, без service deployment. Дата проверки: `2026-09-18T00:45:41Z`.

- Package: `etranprocessing-db==0.1.1`, contract `1.0.0`, schema `L4D-04A-v1`.
- Producer commit: `537a1e493c83d1fa8e8cb765228be8d1b24a1d62` в `origin/l4desk/l4d-04a-shared`; совпадение remote ref проверено после push.
- Отчёт публикуется отдельным documentation-коммитом поверх producer commit, чтобы не создавать самоссылку SHA. Все три immutable artifacts ниже уже присутствуют в producer commit.
- Добавлены 23 opt-in модели: 6 L4Desk onboarding/membership/terminal/audit/session, 3 существующие IoT declarations, 14 финансовых моделей. Все 31 прежняя таблица и обычные импорты сохранены.
- Полный model/table/constraint/default/index contract: `shared/docs/l4desk/schema-v1.json`; семантика, compatibility/consumer responsibilities: `shared/docs/l4desk/schema-v1.md`.
- Публикация source-пакета: `shared/docs/l4desk/package-source-v011.json`, SHA-256 каждого из 20 source/config/schema файлов. Post-publish проверка всех digest против Git blob bytes прошла. Потребитель использует каталог shared **из producer commit**, а не перемещаемый HEAD.

## Артефакты и digest

| Артефакт | SHA-256 |
|---|---|
| `shared/docs/l4desk/schema-v1.json` | `52f481dd3d9985c54b5388a1d9e63062a8fdbe626870b58a83b3461c3e08e49f` |
| `shared/docs/l4desk/schema-v1.md` | `d80626d6f84bab0e44d2236a172b3ffa385c7779bccd26b42c2947b071f77935` |
| `shared/docs/l4desk/package-source-v011.json` — package source digest | `364efa7b369cdcb8da12025b377518834b6013fd693a28f321a81dcbe18a68c6` |
| Локальный `shared/dist/etranprocessing_db-0.1.1-py3-none-any.whl` | `779503adc4635b8bed8cb7c3ba74fcf98430375aa7ebaccdb584ab6f3a75a896` |
| Локальный `shared/dist/etranprocessing_db-0.1.1.tar.gz` | `fed4d8cbdf9128ee56258f95310d8a002ddbce79e38020f212fc941be15261b1` |

Wheel/sdist **не опубликованы удалённо**, не добавлены в Git и не являются required artifacts каскада. Их digest относится к проверенной локальной сборке; идентичность исходников определяется source manifest и producer commit. JSON artifacts имеют LF независимо от Windows newline translation, это защищено тестом.

## Проверки

Команды выполнены из `shared` на Python `3.14.0`, SQLAlchemy `2.0.52`:

```powershell
uv run ruff check --fix etranprocessing_db tests
uv run ruff format etranprocessing_db tests
uv run pyright etranprocessing_db tests
uv run python tests\capture_schema.py docs\l4desk\schema-v1.json --expanded
uv run python tests\capture_release.py
uv run pytest --basetemp=.pytest_tmp -q
uv build --out-dir dist
```

- Ruff `0.16.4`: passed; Pyright `1.1.411`: **0 errors, 0 warnings**.
- Pytest `9.1.1`: **65 passed**; build wheel из sdist: passed.
- Проверены root-import isolation, неизменность старого PostgreSQL DDL/defaults, mapper/FK resolution, type/timezone/fin_ naming и source artifact freshness.
- SQL tests исполняют CHECK/FK/UNIQUE/partial indexes: console/video reservation, duplicate event/payment/charge/notification, rounding и discarded kopecks, negative/zero sums, tenant mismatch, paid anchor, archive cursor guard, preservation после удаления runtime terminal.
- Wheel импортирован в отдельном subprocess с приоритетом wheel path и проверкой `__file__`; старые 31 и все 54 таблицы после opt-in, все mapper declarations работают. Sdist не включает gauge, окружение или handoff отчёты.
- Post-publish smoke: проверены 20 Git blob SHA-256 и повторно выполнены package tests: **2 passed**.
- В ходе проверки найдены и исправлены sdist glob leakage и CRLF/LF digest mismatch; соответствующие проверки не ослаблены. Оставшихся failed tests нет.
- PostgreSQL live DDL, миграции, backend/runtime suites и production smoke здесь **не запускались**: согласно согласованному scope это 04B/04C. SQLite-тесты не заявляются проверкой PostgreSQL concurrency/trigger semantics.

## Rollout, rollback и риски

1. `L4D-04B-PB` принимает immutable schema/source artifacts, обновляет package/lockfile в своём scope и явно регистрирует opt-in metadata. Сверяет live IoT DDL с baseline, затем создаёт недеструктивную Alembic expand-миграцию. `checkfirst` без сверки не доказывает совместимость.
2. `L4D-04C-MB` заменяет локальные IoT declarations shared-импортами, обновляет lockfile и перезапускает процесс. Одновременное объявление двух классов для одной таблицы в общей Base запрещено; `extend_existing` не используется.
3. CHECK проверяет declared transaction totals, но не сумму entries. Cross-row balanced posting, account ownership и append-only guards описаны в schema-v1.md и обязательны до финансовой активации; package не содержит trigger/business implementation.
4. Исторические terminal IDs/ordinals не переиспользуются; runtime terminal удаляется через nullable SET NULL связь. Token/PIN/secret plaintext не добавлен. Архивный manifest пока expand seam, не замена будущему wire contract 15A.
5. Ничего не слито в main; деплоя, миграций, seed/backfill и переключения feature flags нет. Runtime dependencies не изменены.
6. Rollback до интеграции — прежний source commit/0.1.0. После expand migration rollback приложения не должен удалять populated таблицы или финансовую историю; lockfiles/импорты согласуются потребителями.
7. Контроллер независимо проверяет артефакты и добавляет candidate в общий журнал. До фактической записи `ACCEPTED` в журнале запуск 04B запрещён. Provider журнал не редактировал; исходные пользовательские изменения сохранены вне коммитов.

## Candidate для контроллера

`deployed_environment: artifact-registry` ниже обозначает Git-репозиторий как место публикации source artifacts, **не Python index или GitHub Release**. Это согласованное пользователем исключение к `publish package`.

<!-- HANDOFF:H-L4D-04A-SHARED-v1:BEGIN -->
```yaml
handoff_id: H-L4D-04A-SHARED-v1
status: ACCEPTED
contract_kinds:
  - SCHEMA
producer_prompt_id: L4D-04A-SHARED
producer_scope_project: shared/etranprocessing_db
producer_report_path: shared/docs/l4desk/handoffs/L4D-04A-SHARED-report.md
producer_branch: l4desk/l4d-04a-shared
producer_commit: 537a1e493c83d1fa8e8cb765228be8d1b24a1d62
accepted_at_utc: 2026-09-18T00:45:41Z
contract_version: 1.0.0
schema_revision: L4D-04A-v1
artifact_version: 0.1.1
artifact_paths:
  - shared/docs/l4desk/schema-v1.json
  - shared/docs/l4desk/schema-v1.md
  - shared/docs/l4desk/package-source-v011.json
artifact_sha256:
  - 52f481dd3d9985c54b5388a1d9e63062a8fdbe626870b58a83b3461c3e08e49f
  - d80626d6f84bab0e44d2236a172b3ffa385c7779bccd26b42c2947b071f77935
  - 364efa7b369cdcb8da12025b377518834b6013fd693a28f321a81dcbe18a68c6
compatibility:
  backward_compatible_with:
    - 0.1.0
  breaking_changes: false
  notes: "23 opt-in declarative models; 31 legacy tables/root exports unchanged. Existing IoT DDL/client defaults preserved. Replace MenuBuilder-local IoT declarations before opt-in import; update consumer lockfiles. No migration or policy activation. Source package published through Git by explicit user approval."
deployment_status: PUBLISHED
deployed_environment: artifact-registry
feature_flags: {}
contract_payload:
  package_name: etranprocessing-db
  package_version: 0.1.1
  delivery: git-source
  source_repository: https://github.com/OlegLebedevRU/etranprocessing.git
  source_root: shared
  package_source_sha256: 364efa7b369cdcb8da12025b377518834b6013fd693a28f321a81dcbe18a68c6
  schema_import: etranprocessing_db.l4desk
  identifiers:
    tenant_user_terminal: "Integer; tenant=orgs.org_id, user=users.id; durable terminal identity retains original terminals.id"
    external_event_session_operation_correlation_terminal: "Opaque String(128), not UUID-only"
    cursor: "BigInteger; inbox primary key event_id; quarantine primary key id"
    archive: "String(128) batch id, composite manifest PK (id, source_project)"
  operations_events: "Schema only; no new HTTP/MQTT contract or runtime behavior"
  errors: "Named PK/FK/UNIQUE/CHECK violations; exact names and PostgreSQL DDL in schema-v1.json"
  invariants:
    - "Financial table/constraint/index names start fin_; integer kopecks; timezone-aware timestamps"
    - "Unique original terminal/date usage, terminal/cycle charge, provider payment, tenant/cycle/type notification"
    - "One reserved/start_requested/active/stop_requested console-or-video reservation per terminal"
    - "Calculated = posted + discarded; 0 <= discarded < 100; posted/payment/ledger amounts are whole rubles"
    - "Ledger metadata supports balanced transactions; cross-row entry totals and append-only require consumer/DB guards"
    - "Financial source identifiers/hashes have no mandatory FK to purgeable technical events"
    - "Source-only additive rollout; no destructive changes, migration, feature activation or main merge"
  verification:
    shared_tests: "65 passed"
    lint_format_type_build: "passed; pyright 0 errors/0 warnings; wheel/sdist and isolated wheel import verified"
    post_publish_smoke: "20 Git blob SHA-256 checks and 2 package tests passed; origin ref matched producer commit"
supersedes: []
known_risks:
  - "04B must reconcile live IoT DDL and run PostgreSQL Alembic tests; this provider did not access the live DB"
  - "04C must replace duplicate local IoT declarations before opt-in import; both consumers must update their lockfiles"
  - "Cross-row ledger balance/account ownership/append-only enforcement and archive retention verification are not implemented in this thin package"
  - "Local wheel/sdist are verification artifacts only; required published artifact is Git source at producer_commit"
consumers:
  - L4D-04B-PB
next_prompt_id: L4D-04B-PB
```
<!-- HANDOFF:H-L4D-04A-SHARED-v1:END -->