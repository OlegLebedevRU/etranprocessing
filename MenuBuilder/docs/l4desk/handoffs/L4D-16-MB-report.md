# L4D-16-MB — Реализовать финансовые archive manifests и retention

**Дата:** 2026-09-22  
**Промпт:** `L4D-16-MB`  
**Пакет / Scope:** `MenuBuilder` (`D:\repo\platerra\Public\etranprocessing\MenuBuilder`)  
**Ветка:** `l4desk/l4d-16-mb`  
**Статус:** `ACCEPTED`  
**Входные handoffs (Sequence Gate):** `[H-L4D-15A-DOCS-v1, H-L4D-15B-IOT-v1, H-L4D-15C-MEDIA-v1, H-L4D-14-MB-v1]`  
**Выходной handoff:** `H-L4D-16-MB-v1`  
**Следующий промпт (consumer):** `L4D-17A-TOOLS`  
**Разделы архитектуры:** 3, 5, 6, 7, 9, 10, 14, 15, 16, 17

```yaml
prompt_id: L4D-16-MB
scope_project: MenuBuilder
scope_root: D:\repo\platerra\Public\etranprocessing\MenuBuilder
prompt_type: archive-coordination
required_handoff_ids: [H-L4D-15A-DOCS-v1, H-L4D-15B-IOT-v1, H-L4D-15C-MEDIA-v1, H-L4D-14-MB-v1]
output_handoff_id: H-L4D-16-MB-v1
next_prompt_id: L4D-17A-TOOLS
branch: l4desk/l4d-16-mb
report_path: MenuBuilder/docs/l4desk/handoffs/L4D-16-MB-report.md
architecture_sections: [3, 5, 6, 7, 9, 10, 14, 15, 16, 17]
status: ACCEPTED
```

---

## 1. Резюме выполнения (Executive Summary)

В рамках задачи `L4D-16-MB` в `MenuBuilder` реализован полный цикл интеграции внешних архивных манифестов, финансового контроля retention и отображения состояния архивов в superuser Хабе.

### Ключевые результаты шага:

1. **Sequence Gate & Входной контроль:**
   - Все 4 обязательных входных handoff (`H-L4D-15A-DOCS-v1`, `H-L4D-15B-IOT-v1`, `H-L4D-15C-MEDIA-v1`, `H-L4D-14-MB-v1`) приняты со статусом `ACCEPTED`.
   - Использованы согласованные идентификаторы: `archive_batch_id`, `owner_project`, `source_event_id`, `source_events_hash`, составной PK `(id, source_project)` из `fin_archive_batches` (ревизия `027`).

2. **Импорт манифестов (идемпотентный):**
   - `ArchiveService.import_manifest()` — Pydantic v2 валидация `ArchiveManifestV1` против контракта Archive Manifest v1.0.0.
   - Проверка owner_project (enum: `iot-rpc-rest-app`, `l4media`, `MenuBuilder`).
   - Hot retention: source_month строго старше 3 полных закрытых месяцев.
   - Retention duration: `retain_until >= created_at + 3 years`.
   - Идемпотентный upsert: при повторном импорте — сверка immutable полей (month, row_count, checksum); при расхождении — `ArchiveConflictError`.
   - Связывание локальных финансовых записей: bulk UPDATE `archive_batch_id` на `FinLedgerTransaction`, `FinUsageDaily`, `FinTerminalMonthlyCharge` без FK.

3. **Hub Archive Tab:**
   - Новая 6-я вкладка «Архивы и Retention» в `AdminHubPage` с фильтрами: owner_project, state (prepared/verified/purged/failed), source_month, only_errors.
   - Таблица с колонками: archive_batch_id (Tag), owner_project, source_month, state (цветовые метки), record_types, row_count, checksum_sha256 (копируемый), location_reference (masked vol://), retain_until, issues.
   - Модальное окно деталей манифеста (raw JSON).
   - Модальный импорт манифеста (textarea → POST).

4. **Correlation Drill-Down (узел #7: Archive):**
   - Интеграция в `HubService.get_correlation_drilldown()` — автоматическое разрешение archive_batch_id из `FinUsageDaily.archive_batch_id` или `FinLedgerTransaction.archive_batch_id`.
   - Детекция расхождений: `CHECKSUM_MISMATCH` (reread_sha vs batch.checksum), `SOURCE_HASH_MISMATCH` (local hash vs manifest source_hashes), `ARCHIVE_BATCH_FAILED`, `ARCHIVE_BATCH_NOT_FOUND`.
   - Формирование fact dict: batch id, owner, schema_version, source_month, state, checksum, location_reference (masked), verified/purged dates.

5. **RBAC & Безопасность:**
   - `storage_reference` и `manifest` (raw JSON) раскрываются ТОЛЬКО суперпользователю (`is_superuser`).
   - Обычный пользователь видит только `location_reference` (masked `vol://` URI), без физического пути файловой системы.
   - `PROTECTED_FINANCIAL_TABLES` (14 таблиц) строго защищены от purge: `fin_accounts`, `fin_balance_projections`, `fin_billing_cycles`, `fin_ledger_entries`, `fin_ledger_transactions`, `fin_manual_payments`, `fin_notification_deliveries`, `fin_payments`, `fin_reconciliation_runs`, `fin_tariff_versions`, `fin_terminal_monthly_charges`, `fin_usage_daily`, `l4desk_remote_sessions`, `fin_archive_batches`.

6. **Retention & Purge Guards:**
   - `ArchiveService.check_retention_and_storage()` — hot retention (3 мес), min 3-year retention, backup evidence, volume availability.
   - `ArchiveService.enforce_no_financial_purge()` — блокировка при попытке удалить защищённые таблицы.
   - Эндпоинт `POST /api/internal/v1/archive/purge` с dry-run и проверкой `can_purge`.

7. **Архитектурные инварианты:**
   - Финансовый сабледжер (`fin_*`), платежи, тарифы, начисления, суточные агрегаты, балансы, итоговые строк сессий, реестр архивов и хеши событий — НЕ удаляются этим worker.
   - 3-летний minimum архивного хранения с `backup_required: true`.
   - Маскирование физических путей в `vol://` URI для non-superuser.

---

## 2. Sequence Gate и верификация входных контрактов

| Handoff ID | Статус | Producer | Consumer | Gate |
|---|---|---|---|---|
| `H-L4D-15A-DOCS-v1` | ACCEPTED | l4desk-service | L4D-15B-IOT, L4D-15C-MEDIA, **L4D-16-MB** | PASSED |
| `H-L4D-15B-IOT-v1` | ACCEPTED | iot-rpc-rest-app | L4D-15C-MEDIA, **L4D-16-MB** | PASSED |
| `H-L4D-15C-MEDIA-v1` | ACCEPTED | l4media | **L4D-16-MB** | PASSED |
| `H-L4D-14-MB-v1` | ACCEPTED | MenuBuilder | L4D-15A-DOCS | PASSED |

---

## 3. Архитектурное соответствие (Sections 3, 5, 6, 7, 9, 10, 14, 15, 16, 17)

- **Раздел 3 (Append-only финансы):** Архивные батчи — отдельная таблица `fin_archive_batches` с composite PK. Связь с финансовыми записями — через `archive_batch_id` (String 128) без FK, что обеспечивает независимость от lifecycle деталей.
- **Раздел 5 (Gates & Изоляция):** Архивный сервис обращается строго к локальным моделям `MenuBuilder` через SQLAlchemy 2.0. Никаких обращений к чужим БД, volumes или очередям.
- **Раздел 6 (State Machine):** Жизненный цикл манифеста — `prepared → verified → purged → failed` — валидируется на уровне Pydantic-схемы и контролируется БД-констрейнтами.
- **Раздел 7 (Purge Guards):** Реализованы все барьеры безопасности: hot retention (3 мес), cursor guard, active records, checksum/count verification, No-Financial-Purge.
- **Раздел 9 (Error Codes):** Коды ошибок манифеста включены в Pydantic-схему и отображаются в Hub.
- **Раздел 10 (Хаб):** Archives tab — 6-я вкладка superuser Хаба с drill-down интеграцией.
- **Раздел 14 (Retention):** 3-летний minimum архивного хранения, backup_required, hot retention window enforcement.
- **Раздел 15 (Volume layout):** `vol://` masking скрывает физические пути от обычных пользователей.
- **Раздел 16 (Archive coordination):** Идемпотентный import с owner+batch_id, link local records, state management.
- **Раздел 17 (Testing):** 48 backend тестов + 4 frontend contract теста.

---

## 4. Реализованные модули

### 4.1 Backend

| Файл | Назначение |
|---|---|
| `backend/app/services/financial_core/archive_schemas.py` | Pydantic v2 модели: `ArchiveManifestV1` (строгая валидация контракта), `HubArchiveBatchItem`, API contracts |
| `backend/app/services/financial_core/archive_service.py` | `ArchiveService`: import_manifest (идемпотентный), list/get_archive_batches (RBAC masking), check_retention_and_storage, enforce_no_financial_purge |
| `backend/app/routers/archive.py` | REST API: POST/GET manifests, POST retention-check, POST purge |
| `backend/app/services/financial_core/exceptions.py` | Archive-specific exceptions: ArchiveError, ArchiveManifestValidationError, ArchiveConflictError, ArchiveStorageUnavailableError, NoFinancialPurgeViolationError |
| `backend/app/services/financial_core/__init__.py` | Re-export archive classes |
| `backend/app/routers/hub.py` | Hub archives endpoints (/hub/archives, /hub/archives/{id}) |
| `backend/app/services/financial_core/hub_service.py` | Drill-down archive node (#7) with CHECKSUM_MISMATCH, SOURCE_HASH_MISMATCH, ARCHIVE_BATCH_FAILED detection |
| `backend/app/main.py` | Router registration |

### 4.2 Frontend

| Файл | Назначение |
|---|---|
| `frontend/src/pages/AdminHubPage.tsx` | Archives tab (filters, table, import/detail modals), drill-down archive node |
| `frontend/src/api/hub.ts` | TypeScript interfaces + API functions (fetchHubArchives, importHubArchiveManifest, etc.) |

### 4.3 Tests

| Файл | Назначение |
|---|---|
| `backend/tests/test_archive_manifests.py` | 48 тестов: schema validation (15), hot retention (5), location masking (3), No-Financial-Purge (4), owner projects (4), duplicate manifests (2), Hub response (4), import service (7), retention check (2), drill-down (2) |
| `frontend/src/tests/l4desk-archive-manifests.test.ts` | 4 contract теста: lifecycle states, location masking, retention invariants, drill-down archive node |

---

## 5. Свидетельства тестирования, линтинга и сборки

1. **Backend Tests (Pytest):** 466 passed in 146.90s (включая 48 новых archive тестов + существующие).
2. **Backend Quality:**
   - `ruff check app tests` — All checks passed.
   - `ruff format --check app tests` — 128 files already formatted.
   - `pyright app/services/financial_core/archive_service.py app/services/financial_core/archive_schemas.py app/routers/archive.py tests/test_archive_manifests.py` — **0 errors, 0 warnings, 0 informations**.
3. **Frontend Vitest:** 11 test files, 55 passed (включая 4 archive contract теста).
4. **Frontend TypeScript:** `tsc -b --noEmit` — 0 errors.

---

## 6. Deployment Status

| Компонент | Статус | Окружение |
|---|---|---|
| Backend archive service + schemas + router | Код закоммичен | local (ready for deploy) |
| Frontend archives tab + API client | Код закоммичен | local (ready for deploy) |
| DB migrations | Не требуется (rev 027 уже содержит `fin_archive_batches`) | — |
| Feature flags | Нет (archive import — по запросу superuser) | — |

**Примечание:** Деплой на production не выполнен в рамках данного шага (scope — MenuBuilder, ветка `l4desk/l4d-16-mb`). Деплой выполняется стандартным путём в контейнеры `menubuilder-backend` и `menubuilder-frontend`.

---

## 7. Потребители и следующий шаг

- **Выходной handoff:** `H-L4D-16-MB-v1`.
- **Потребитель:** `L4D-17A-TOOLS` (подключение архивных manifest fixtures в tools-утилиты).
- **Следующий промпт:** `L4D-17A-TOOLS`.
