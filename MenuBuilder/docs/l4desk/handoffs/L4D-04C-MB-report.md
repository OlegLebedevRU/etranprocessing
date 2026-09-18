# Handoff Report: Подключить совместимую expand-схему L4Desk (L4D-04C-MB)

## Candidate H-L4D-04C-MB-v1

<!-- HANDOFF:H-L4D-04C-MB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-04C-MB-v1
status: ACCEPTED
contract_kinds:
  - SCHEMA
  - DEPLOYMENT
producer_prompt_id: L4D-04C-MB
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-04C-MB-report.md
producer_branch: l4desk/l4d-04c-mb
producer_commit: PENDING_COMMIT_SHA
accepted_at_utc: 2026-09-18T12:40:00Z
contract_version: 1.0.0
schema_revision: "027"
artifact_version: 0.1.1
artifact_paths:
  - MenuBuilder/backend/app/schema_compatibility.py
  - MenuBuilder/backend/app/models_l4desk.py
  - MenuBuilder/backend/app/repositories/l4desk_repository.py
  - MenuBuilder/backend/tests/test_schema_compatibility.py
artifact_sha256:
  - 670e8d1e780e503625865a41d61b004b28b14adc93be4e5710aadb73dfa8630e
  - 7f708171cec710524a3f042701bec31524d7cd2ebb07f0dbf501aa4b019d5131
  - 316adf73ab2765377b64d55f26cadb2b41b541b260b1e0c63f55be817ebeeb89
  - 055e43be0acd36c34b3e932dcfc30b67d4afe51b66e6df3b82cbcdacbd9e978f
consumed_contracts:
  - handoff_id: H-L4D-04A-SHARED-v1
    contract_id: l4desk_shared_schema_v1
    contract_version: 1.0.0
    schema_revision: L4D-04A-v1
    producer: shared
    package_name: etranprocessing-db
    package_version: 0.1.1
    package_source_sha256: 364efa7b369cdcb8da12025b377518834b6013fd693a28f321a81dcbe18a68c6
  - handoff_id: H-L4D-04B-PB-v1
    contract_id: alembic_migration_027
    contract_version: 1.0.0
    schema_revision: "027"
    producer: ProcessingBackend
    alembic_head: "027"
    deployed_host: 87.242.100.34
compatibility:
  backward_compatible_with:
    - 0.1.0
  breaking_changes: false
  notes: "MenuBuilder connected to published etranprocessing-db==0.1.1 and deployed expand schema 027 in dark mode. Replaced duplicate local IoT model declarations with shared package re-exports. Added strict startup schema compatibility verification (reads alembic_version and 23 required tables; zero automatic DDL). Added dark mode feature flags (registration, billing, ui all disabled by default). Verified backward compatibility and rolling deploy resilience."
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  l4desk_registration_enabled: false
  l4desk_billing_enabled: false
  l4desk_ui_enabled: false
  schema_compatibility_check_enabled: true
  required_alembic_revision: "027"
contract_payload:
  package_name: etranprocessing-db
  package_version: 0.1.1
  package_source_sha256: 364efa7b369cdcb8da12025b377518834b6013fd693a28f321a81dcbe18a68c6
  alembic_head: "027"
  schema_revision: "027"
  deployed_host: 87.242.100.34
  deployed_service: menubuilder-backend
  deployed_image: user1-menubuilder-backend:latest
  tables_checked_count: 23
  invariants:
    - "Strict scope: MenuBuilder performs zero automatic DDL at startup (auto-DDL disabled in storage and lifespan)"
    - "Startup guard: verify_schema_compatibility verifies alembic_version == '027' and presence of all 23 L4Desk tables; aborts startup on mismatch"
    - "Dark mode: L4Desk features disabled by default via config flags (l4desk_registration_enabled=false, l4desk_billing_enabled=false, l4desk_ui_enabled=false)"
    - "Shared package integration: etranprocessing-db==0.1.1 consumed via etranprocessing_db.l4desk; duplicate local models reconciled"
    - "Rolling deploy resilience: models support omitted optional columns via null defaults and load_only queries"
supersedes: []
known_risks:
  - "L4Desk business routes (registration, billing, UI) remain dark and inaccessible until L4D-05-MB and subsequent prompts"
  - "Startup compatibility check requires PostgreSQL database connection; aborts startup if database schema revision is not 027"
consumers:
  - L4D-05-MB
next_prompt_id: L4D-05-MB
```
<!-- HANDOFF:H-L4D-04C-MB-v1:END -->

---

## 1. Резюме шага и контекст выполнения

Шаг `L4D-04C-MB` выполнен строго в рамках изолированного репозиторного каталога `MenuBuilder` (`scope_project: MenuBuilder`, `scope_root: MenuBuilder/`).
Цель шага — подключить в `MenuBuilder` опубликованный пакет `etranprocessing-db==0.1.1` (контракт `H-L4D-04A-SHARED-v1`) и доказанно развёрнутую expand-схему Alembic `027` (контракт `H-L4D-04B-PB-v1`) в строгом тёмном режиме (dark mode).

Основные результаты:
1. **Contract Gate:** Проверены и приняты входные handoffs `H-L4D-04A-SHARED-v1` и `H-L4D-04B-PB-v1`. Сопоставлены package digest (`364efa7b369cdcb8da12025b377518834b6013fd693a28f321a81dcbe18a68c6`) и развёрнутая ревизия Alembic (`027`).
2. **Dependency Lock Gate:** `pyproject.toml` зафиксирован на `etranprocessing-db==0.1.1`, lockfile `uv.lock` обновлён.
3. **Устранение дублирования моделей:** Локальные объявления IoT (`IotConsumerCheckpoint`, `IotEventInbox`, `IotEventQuarantine`) в `app/models_iot_consumer.py` заменены на реэкспорт из `etranprocessing_db.l4desk`. Автоматический DDL (`create_all`) в `DatabaseIotConsumerStorage` отключён в пользу централизованной схемы Alembic.
4. **Startup Schema Compatibility Check (`app/schema_compatibility.py`):** Реализована строгая проверка совместимости схемы БД при старте приложения (чтение `alembic_version`, валидация соответствия ожидаемой ревизии `027` и наличия всех 23 таблиц L4Desk/fin/iot). Проверка строго read-only, не выполняет DDL и прерывает запуск (`SchemaCompatibilityError`) при несовпадении.
5. **Feature Flags & Dark Mode:** В `app/config.py` добавлены флаги тёмного режима:
   - `l4desk_registration_enabled = False`
   - `l4desk_billing_enabled = False`
   - `l4desk_ui_enabled = False`
   - `schema_compatibility_check_enabled = True`
   - `required_alembic_revision = "027"`
6. **Репозиторный слой:** Создан `app/repositories/l4desk_repository.py` для типобезопасного доступа к сущностям L4Desk без активации бизнес-эндпоинтов.
7. **Тесты совместимости:** Добавлен комплексный тестовый набор `tests/test_schema_compatibility.py` (13 тестов), покрывающий проверку контракта всех 23 таблиц, re-export моделей, startup error при отсутствии `alembic_version` / пустой таблице / неверной ревизии / неполном наборе таблиц, lifespan integration, persistence в тестовой БД, методы репозитория и устойчивость к отсутствующим колонкам при rolling deploy (`load_only`).
8. **Качество кода:**
   - `uv run ruff check .` — passed (0 ошибок).
   - `uv run ruff format --check .` — passed (85 файлов форматированы).
   - `uv run pyright .` — passed (0 errors, 0 warnings, 0 informations).
   - `uv run pytest` — **316 passed** (весь набор тестов `MenuBuilder/backend`).
9. **Развёртывание и Smoke:**
   - Сервер: `87.242.100.34`, контейнер `menubuilder-backend`.
   - Логи старта подтвердили успешную валидацию схемы:
     `Schema compatibility check PASSED: revision in ['027'], all 23 required tables present. Application startup complete.`
   - Внутриконтейнерный smoke-тест подтвердил: флаги `registration`, `billing`, `ui` выключены (`False`), проверка схемы включена (`True`), ревизия `027`, экспортировано 23 модели.
   - HTTP smoke: `/api/auth/me` и `/api/monitoring` возвращают 401 Unauthorized (штатное поведение сохранено).

---

## 2. Contract Gate & Входные контракты

| Контракт | Источник | Статус | Проверенные свойства |
|---|---|---|---|
| `H-L4D-04A-SHARED-v1` | `shared` | ACCEPTED | `etranprocessing-db==0.1.1`, 23 модели L4Desk, sha256 package source: `364efa7b369cdcb8da12025b377518834b6013fd693a28f321a81dcbe18a68c6` |
| `H-L4D-04B-PB-v1` | `ProcessingBackend` | ACCEPTED | Alembic migration `027`, head `027`, down_revision `026`, 23 таблицы развёрнуты на `87.242.100.34`, transactional downgrade verified |

---

## 3. Архитектурные изменения в MenuBuilder

### 3.1. Re-export и примирение моделей (`app/models_iot_consumer.py`, `app/models_l4desk.py`)
- До миграции 04C `MenuBuilder` содержал локальные копии `IotConsumerCheckpoint`, `IotEventInbox`, `IotEventQuarantine` со своим `Base`.
- В `app/models_iot_consumer.py` локальные декларации заменены на реэкспорт из `etranprocessing_db.l4desk`.
- В `app/models_l4desk.py` централизованно реэкспортированы все 23 модели L4Desk (`L4DeskRegistration`, `L4DeskTenantProfile`, `L4DeskTerminal`, `L4DeskTerminalSecret`, `L4DeskSessionReservation`, `L4DeskNotificationDispatch`, `FinAccount`, `FinLedgerTransaction`, `FinLedgerEntry`, `FinUsageDaily`, `FinTerminalMonth`, `FinPayment`, `FinAccountSnapshot`, `FinArchiveBatch`, `FinArchiveManifest`, `FinCycleCloseRun`, `FinCycleTenantSummary`, `FinNotificationLog`, `FinAuditLog`, `FinStornoTransaction`, `IotConsumerCheckpoint`, `IotEventInbox`, `IotEventQuarantine`).
- `Base.metadata` в MenuBuilder теперь содержит единый набор таблиц без коллизий.

### 3.2. Отключение автоматического DDL (`app/services/iot_consumer_storage.py`)
- В `DatabaseIotConsumerStorage.initialize()` удалён вызов `Base.metadata.create_all`.
- Управление схемой полностью закреплено за централизованными миграциями Alembic (`ProcessingBackend`).

### 3.3. Startup Schema Compatibility Gate (`app/schema_compatibility.py`)
- Реализована функция `verify_schema_compatibility(engine, expected_revision="027")`.
- Логика:
  1. Выполняет `SELECT version_num FROM alembic_version`. При отсутствии таблицы или записей выбрасывает `SchemaCompatibilityError`.
  2. Проверяет, что текущая ревизия входит в список допустимых (`['027']`).
  3. Проверяет наличие всех 23 таблиц L4Desk через `inspect(conn).get_table_names()`.
  4. При несовпадении логирует критическую ошибку и прерывает старт приложения.
- Интегрирована в `lifespan` (`app/main.py`) перед запуском фоновых воркеров.

### 3.4. Тёмный режим (Dark Mode) и Feature Flags (`app/config.py`)
- Добавлены параметры Pydantic Settings:
  - `l4desk_registration_enabled: bool = False`
  - `l4desk_billing_enabled: bool = False`
  - `l4desk_ui_enabled: bool = False`
  - `schema_compatibility_check_enabled: bool = True`
  - `required_alembic_revision: str = "027"`
- Все новые коммерческие функции надёжно закрыты флагами.

---

## 4. Верификация: Linting, Type Check и Тесты

1. **Ruff Linter:**
   ```bash
   uv run ruff check .
   # All checks passed!
   ```
2. **Ruff Formatter:**
   ```bash
   uv run ruff format --check .
   # 85 files already formatted
   ```
3. **Pyright Type Checker:**
   ```bash
   uv run pyright .
   # 0 errors, 0 warnings, 0 informations
   ```
4. **Pytest Compatibility Suite (`tests/test_schema_compatibility.py`):**
   - 13 passed in 10.91s.
   - Сценарии:
     - `test_schema_contract_gate_tables_and_columns`: сверка 23 таблиц и типов с контрактом 04A.
     - `test_import_and_model_reexport`: отсутствие дублирования в `Base.metadata`.
     - `test_startup_error_on_missing_alembic_version_table`: ошибка при отсутствии `alembic_version`.
     - `test_startup_error_on_empty_alembic_version_table`: ошибка при пустой `alembic_version`.
     - `test_startup_error_on_wrong_revision`: ошибка при несовпадении ревизии (например, `026`).
     - `test_startup_error_on_missing_required_tables`: ошибка при неполном наборе таблиц.
     - `test_startup_success_when_revision_and_tables_match`: чистый запуск при ревизии `027` и всех 23 таблицах.
     - `test_verify_schema_compatibility_async`: валидация асинхронного вызова.
     - `test_lifespan_aborts_startup_on_incompatible_schema`: прерывание старта в контексте lifespan.
     - `test_compatibility_against_migrated_db`: CRUD и связи сущностей на мигрированной БД.
     - `test_l4desk_repository_methods`: методы `L4DeskRepository`.
     - `test_absent_optional_columns_instantiation_defaults`: дефолтные значения `None` для опциональных полей.
     - `test_absent_optional_columns_partial_query_resilience`: успешное выполнение `load_only` запросов при отсутствии опциональных колонок в rolling deploy.
5. **Полный регрессионный тестовый прогон:**
   ```bash
   uv run pytest
   # 316 passed, 37 warnings in 86.64s
   ```

---

## 5. Развёртывание на Production (87.242.100.34)

- **Хост:** `user1@87.242.100.34` (SSH-ключ `d:\.ssh\id_ed25519`).
- **MCP Ops статус:** `[MCP Ops Readiness: READY]` (load 0.26, RAM 2.2 GiB free, disk 52%).
- **Синхронизация shared package:**
  - `shared/pyproject.toml` (0.1.1) и `shared/etranprocessing_db/` скопированы в `/home/user1/shared/`.
- **Синхронизация MenuBuilder:**
  - `MenuBuilder/backend/pyproject.toml`, `uv.lock`, `app/` скопированы в `/home/user1/MenuBuilder/backend/`.
- **Сборка и запуск контейнера:**
  ```bash
  cd /home/user1 && sudo docker compose build menubuilder-backend
  cd /home/user1 && sudo docker compose up -d menubuilder-backend
  ```
- **Логи приложения после старта:**
  ```
  2026-09-18 12:32:53,412 - app.schema_compatibility - INFO - Schema compatibility check PASSED: revision in ['027'], all 23 required tables present.
  INFO:     Application startup complete.
  INFO:     Uvicorn running on http://0.0.0.0:8000
  ```
- **Smoke Probes:**
  - `l4desk_registration_enabled`: `False` (подтверждено).
  - `l4desk_billing_enabled`: `False` (подтверждено).
  - `l4desk_ui_enabled`: `False` (подтверждено).
  - `schema_compatibility_check_enabled`: `True` (подтверждено).
  - `required_alembic_revision`: `"027"` (подтверждено).
  - HTTP `GET /api/auth/me`: `401 Unauthorized` (штатная работа).
  - HTTP `GET /api/monitoring`: `401 Unauthorized` (штатная работа).

---

## 6. Стратегия отката (Rollback)

1. **Экстренный откат конфигурации (без сборки):**
   При необходимости временного пропуска проверки схемы выставить в `/home/user1/MenuBuilder/backend/.env`:
   `SCHEMA_COMPATIBILITY_CHECK_ENABLED=false`
   и перезапустить контейнер: `sudo docker restart menubuilder-backend`.
2. **Кодовый откат:**
   - Выполнить `git revert` коммита `L4D-04C-MB`.
   - Скопировать предыдущую версию `MenuBuilder/backend` на хост и пересобрать образ `menubuilder-backend`.
3. **Откат схемы БД (на стороне ProcessingBackend):**
   - Если требуется откат Alembic схемы: `alembic downgrade 026` (верифицировано в 04B).

---

## 7. Следующие шаги каскада

Согласно реестру промптов (`l4desk-service/docs/l4desk-architecture.md`, раздел 18):
- Следующий шаг: `L4D-05-MB` — регистрация tenant и пользователя, confirmation email, первоначальный tenant profile, выпуск PIN при создании терминала.
