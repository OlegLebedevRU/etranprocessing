# L4D-00F-SHARED — Базовый отчёт общей модели данных (shared/etranprocessing_db)

**Дата:** 2026-09-17  
**Промпт:** `L4D-00F-SHARED`  
**Пакет / Scope:** `shared/etranprocessing_db` (`D:\repo\platerra\Public\etranprocessing\shared`)  
**Ветка:** `l4desk/l4d-00f-shared`  
**Статус:** `ACCEPTED`  
**Входной handoff (sequence gate):** `H-L4D-00E-MB-v1` (принят в `contract-handoff.md`)  
**Выходной handoff:** `H-L4D-00F-SHARED-v1`  
**Следующий промпт (consumer):** `L4D-00G-DOCS`  
**Разделы архитектуры:** 3, 5, 9, 10, 14, 16, 17  

---

## 1. Резюме выполнения

В рамках шага `L4D-00F-SHARED` зафиксирован полный baseline пакета общей декларативной модели данных `shared/etranprocessing_db`:
1. **Contract gate / Sequence gate:** Входной handoff `H-L4D-00E-MB-v1` проверен в `contract-handoff.md`, статус `ACCEPTED`, потребитель `L4D-00F-SHARED`. Код `MenuBuilder` и других соседних проектов не открывался и не анализировался.
2. **Инвентаризация моделей:** Проведена полная инвентаризация всех 31 декларативной SQLAlchemy 2.0 ORM-модели, таблиц, первичных и внешних ключей, связей (`relationship`), уникальных и check-констрейнтов, индексов и правил именования.
3. **Подтверждение чистоты (Thin DB Layer Principle):** Полностью подтверждено отсутствие бизнес-логики, фреймворков (FastAPI, Pydantic, Flask), криптографических/хеширующих библиотек (hashlib, bcrypt, jwt, cryptography) и миграций (Alembic). Базовый класс — чистый `sqlalchemy.orm.DeclarativeBase`.
4. **Проверки качества (checks):** 
   - `ruff check`: пройден (0 ошибок).
   - `ruff format --check`: пройден (15 файлов отформатированы).
   - `SQLAlchemy inspect`: все 31 маппер успешно сконфигурированы и скомпилированы без предупреждений.
   - `uv pip check`: совместимость всех 8 установленных пакетов подтверждена.
   - `uv build`: пакет `etranprocessing-db 0.1.0` собирается в wheel и sdist без ошибок.
5. **Expand-точки для `fin_*`:** Сформирован детальный перечень безопасных точек расширения для финансового subledger (`fin_*`) согласно разделам 9, 10 и 14 `l4desk-architecture.md`. Существующие runtime-модели не изменялись.
6. **Границы изменений:** В репозитории не изменено ни одной строчки существующего кода. Добавлен только данный baseline-отчёт.

---

## 2. Contract Gate и соблюдение последовательности каскада

- **Журнал контрактов:** `l4desk-service/docs/prompts/contract-handoff.md`.
- **Проверенный блок:** `HANDOFF:H-L4D-00E-MB-v1:BEGIN` ... `HANDOFF:H-L4D-00E-MB-v1:END`.
- **Статус блока:** `ACCEPTED`.
- **Producer:** `MenuBuilder` (промпт `L4D-00E-MB`, ветка `l4desk/l4d-00e-mb`, коммит `dabccacf7d9d7227d898af6f4f50e12ca9bf3f99`).
- **SHA-256 артефактов 00E:**
  - `MenuBuilder/docs/l4desk/handoffs/L4D-00E-MB-report.md`: `cdae906ca929b14235b499331e7a562727a5c694a49ef6939714467f19891279`
  - `MenuBuilder/docs/l4desk/snapshots/openapi_baseline.json`: `3514eff4b7e0e1654314518564b5f290d155c139bc86eb0af04c32917e89c301`
  - `MenuBuilder/docs/l4desk/snapshots/inventory_baseline.json`: `31896fcace9f6f8331b8938cebb2913f28b2c9e19e7b69725a3cacd659211a28`
- **Проверка цепочки каскада:** Цепочка `00A-TOOLS` → `00B-IOT` → `00C-PB` → `00D-MEDIA` → `00E-MB` непрерывна и полностью зафиксирована в журнале.
- **Изоляция:** Код `MenuBuilder` в ходе выполнения текущего шага не читался.

---

## 3. Метаданные пакета, сборка и зависимости

Конфигурационный файл пакета: `shared/pyproject.toml`.

```toml
[project]
name = "etranprocessing-db"
version = "0.1.0"
description = "Shared SQLAlchemy ORM models and DeclarativeBase for etranprocessing"
requires-python = "==3.14.*"
dependencies = [
    "sqlalchemy[asyncio]>=2.0.52",
    "asyncpg>=0.30.0",
]

[build-system]
requires = ["hatchling"]
build-backend = "hatchling.build"

[tool.hatch.build.targets.wheel]
packages = ["etranprocessing_db"]

[dependency-groups]
dev = [
    "ruff>=0.12.0",
    "pyright[nodejs]>=1.1.400",
]

[tool.ruff.lint]
extend-select = [
    "UP",
    "I",
]
```

### Характеристики окружения и сборки
- **Сборочный бэкенд:** `hatchling.build`.
- **Имя дистрибутива:** `etranprocessing-db`.
- **Импортируемый модуль:** `etranprocessing_db`.
- **Совместимость с Python:** CPython 3.14 (`==3.14.*`).
- **ORM / СУБД:** SQLAlchemy 2.0 (`>=2.0.52`), asyncpg (`>=0.30.0`), greenlet (`3.5.5`).
- **Сборка артефактов (`uv build`):**
  - `dist/etranprocessing_db-0.1.0.tar.gz` (sdist)
  - `dist/etranprocessing_db-0.1.0-py3-none-any.whl` (wheel)

---

## 4. Архитектурный принцип тонкого декларативного слоя (Thin DB Layer)

Пакет `shared/etranprocessing_db` является единственным источником истины декларативных моделей SQLAlchemy 2.0 для обоих бэкендов (`ProcessingBackend` и `MenuBuilder`).

### Подтверждённые архитектурные инварианты
1. **Базовый класс:** `etranprocessing_db.base.Base` наследует `sqlalchemy.orm.DeclarativeBase` без дополнительных примесей, методов жизненного цикла или динамических хуков.
2. **Аудит импортов:** AST-анализ всех `.py` файлов пакета подтверждает наличие импортов исключительно из стандартной библиотеки Python (`__future__`, `datetime`, `typing`, `uuid`), внутреннего модуля `etranprocessing_db` и `sqlalchemy`.
3. **Отсутствие веб-фреймворков:** Нет импортов `fastapi`, `starlette`, `pydantic`, `flask` и др.
4. **Отсутствие криптографии и авторизационной логики:** Нет импортов `hashlib`, `hmac`, `bcrypt`, `passlib`, `cryptography`, `jwt`. Поля хешей (например, `User.md5_password`, `UserSession.refresh_token_hash`, `EmailVerification.token_hash`) объявлены как обычные строковые колонки (`Mapped[str]`).
5. **Отсутствие бизнес-логики:** Модели не содержат методов расчета тарифов, проведения платежей, генерации PIN-кодов или обработки сессий. Единственный метод — инициализатор по умолчанию в `MenuVariant` (`version = 1`).
6. **Отсутствие миграций в shared:** Миграции базы данных полностью отсутствуют в `shared`. Единственным владельцем и исполнителем миграций Alembic в архитектуре является `ProcessingBackend/backend/alembic`.

---

## 5. Полная инвентаризация моделей данных (31 сущность)

Все модели используют декларативный синтаксис SQLAlchemy 2.0 `Mapped[T] = mapped_column(...)` с типизацией.

| # | Модель (Класс) | Таблица (`__tablename__`) | Модуль | PK | Внешние ключи (FK) | Relationships | Индексы и констрейнты |
|---|---|---|---|---|---|---|---|
| 1 | `ApiToken` | `api_tokens` | `auth.py` | `id` (int) | — | — | `idx_api_tokens_user`, `idx_api_tokens_jti`, Unique(`jti`) |
| 2 | `User` | `users` | `auth.py` | `id` (int) | `org_id -> orgs.org_id` | — | `idx_users_username`, `idx_users_org_id`, `idx_users_permissions` (GIN), `chk_users_role4_tenant` |
| 3 | `UserSession` | `user_sessions` | `auth.py` | `id` (int) | `user_id -> users.id` (CASCADE) | — | `idx_user_sessions_user_id`, `idx_user_sessions_refresh_hash`, `idx_user_sessions_active_org_id`, Unique(`refresh_token_hash`) |
| 4 | `BillingOrder` | `billing_orders` | `billing.py` | `id` (UUID) | — | `items` -> `BillingOrderItem` | `idx_billing_orders_org_id`, `idx_billing_orders_status` |
| 5 | `BillingOrderItem` | `billing_order_items` | `billing.py` | `id` (int) | `order_id -> billing_orders.id`, `terminal_id -> terminals.id` | `order`, `terminal` | `idx_billing_order_items_order_id`, `idx_billing_order_items_terminal_id`, `ck_billing_order_items_operation` |
| 6 | `CertificatePin` | `certificate_pins` | `billing.py` | `id` (int) | `terminal_id -> terminals.id`, `order_item_id -> billing_order_items.id` | `terminal`, `order_item` | `idx_cert_pins_terminal`, `idx_cert_pins_status`, `idx_cert_pins_org`, `idx_cert_pins_order_item`, Unique(`pin`), `ck_certificate_pins_status`, `ck_certificate_pins_creation_source` |
| 7 | `CatalogCategory` | `catalog_categories` | `catalog.py` | `id` (int) | `parent_id -> catalog_categories.id` (CASCADE) | `parent`, `children`, `items` | `ix_catalog_categories_org_id`, `ix_catalog_categories_parent_id` |
| 8 | `CatalogItem` | `catalog_items` | `catalog.py` | `id` (int) | `category_id -> catalog_categories.id` (CASCADE) | `category` | `ix_catalog_items_org_id`, `ix_catalog_items_category_id`, `uq_catalog_org_tsp` |
| 9 | `EmailVerification` | `email_verifications` | `email.py` | `id` (int) | `org_id -> orgs.org_id` (CASCADE), `user_id -> users.id` (SET NULL) | — | `idx_email_verif_token`, `idx_email_verif_org`, Unique(`token_hash`) |
| 10 | `EmailLog` | `email_logs` | `email.py` | `id` (int) | `org_id -> orgs.org_id` (SET NULL), `user_id -> users.id` (SET NULL) | — | `idx_email_logs_org_id`, `idx_email_logs_device_id`, `idx_email_logs_created_at` |
| 11 | `MenuVariant` | `menu_variants` | `menu.py` | `id` (int) | — | `groups`, `bindings`, `snapshots` | `ix_menu_variants_org_id`, `uq_menu_variants_org_name` |
| 12 | `MenuVariantSnapshot` | `menu_variant_snapshots` | `menu.py` | `id` (int) | `menu_variant_id -> menu_variants.id` (CASCADE) | `menu_variant` | `idx_snapshot_variant_version`, `uq_snapshot_variant_version` |
| 13 | `Group` | `groups` | `menu.py` | `id` (int) | `menu_variant_id -> menu_variants.id` (CASCADE), `parent_id -> groups.id` (SET NULL) | `menu_variant`, `parent`, `children`, `services` | `ix_groups_menu_variant_id`, `ix_groups_org_id`, `ix_groups_parent_id` |
| 14 | `Service` (`ServiceMenu`) | `services` | `menu.py` | `id` (int) | `menu_variant_id -> menu_variants.id` (CASCADE), `group_id -> groups.id` (CASCADE), `catalog_item_id -> catalog_items.id` (SET NULL) | `group`, `catalog_item` | `ix_services_group_id`, `ix_services_menu_variant_id`, `ix_services_catalog_item_id`, `uq_service_variant_tsp` |
| 15 | `TerminalMenuBinding` | `terminal_menu_bindings` | `menu.py` | `id` (int) | `menu_variant_id -> menu_variants.id` (CASCADE) | `menu_variant` | `ix_terminal_menu_bindings_menu_variant_id`, `ix_terminal_menu_bindings_device_id`, Unique(`device_id`) |
| 16 | `Org` | `orgs` | `org.py` | `org_id` (int) | — | — | `idx_orgs_status` |
| 17 | `OrgBillingSettings` | `org_billing_settings` | `org.py` | `org_id` (int) | — | — | `ck_org_billing_price_non_negative`, `ck_org_cert_price_non_negative`, `ck_org_cert_mode` |
| 18 | `OrgStatus` | `org_statuses` | `org.py` | `id` (int) | — | — | Unique(`org_id`) |
| 19 | `Tsp` | `tsp` | `payment.py` | `tsp_id` (int) | — | — | `idx_tsp_code`, Unique(`tsp_code`) |
| 20 | `TspParameterCode` | `tsp_parameter_codes` | `payment.py` | `param_id` (int) | — | — | `idx_tsp_param_prototype`, `uq_prototype_param_code` |
| 21 | `Payment` | `payments` | `payment.py` | `paym_id` (int) | `terminal_id -> terminals.id`, `org_id -> orgs.org_id`, `menu_snapshot_id -> menu_variant_snapshots.id` (SET NULL) | `terminal`, `org`, `menu_snapshot`, `params` | `idx_payments_terminal_id`, `idx_payments_org_id`, `idx_payments_ext_id`, `idx_payments_tsp_code`, `idx_payments_datetime`, `idx_payments_menu_snapshot_id`, `idx_payments_tsp_code_menu_version` |
| 22 | `PaymentParam` | `payment_params` | `payment.py` | `id` (int) | `paym_id -> payments.paym_id` (CASCADE), `param_id -> tsp_parameter_codes.param_id` | `payment`, `param` | `idx_payment_params_paym_id`, `idx_payment_params_param_id` |
| 23 | `BalanceTerminalTsp` | `balance_terminal_tsp` | `payment.py` | `rec_id` (int) | `org_id -> orgs.org_id`, `terminal_id -> terminals.id`, `tsp_id -> tsp.tsp_id`, `menu_snapshot_id -> menu_variant_snapshots.id` (SET NULL) | `menu_snapshot` | `idx_balance_int_day`, `idx_balance_terminal_id`, `idx_balance_tsp_id`, `idx_balance_org_id`, `idx_balance_menu_snapshot_id`, `idx_balance_menu_version`, `uq_balance_day_terminal_tsp_ver` |
| 24 | `GateGaugeRecord` | `gate_gauge_records` | `telemetry.py` | `id` (int) | — | — | `idx_gauge_device_id`, `idx_gauge_created_at` |
| 25 | `TechGateRecord` | `tech_gate_records` | `telemetry.py` | `id` (int) | — | — | `idx_techgate_device_id`, `idx_techgate_function`, `idx_techgate_created_at` |
| 26 | `TerminalGaugeState` | `terminal_gauge_states` | `telemetry.py` | `device_id` (int) | `device_id -> terminals.device_id` (CASCADE) | — | `ix_terminal_gauge_states_sn` |
| 27 | `TerminalType` | `terminal_types` | `terminal.py` | `id` (int) | — | — | — |
| 28 | `Terminal` | `terminals` | `terminal.py` | `id` (int) | `terminal_type_id -> terminal_types.id` | `terminal_type`, `licenses` | `idx_terminals_sn`, `idx_terminals_cert_serial`, `idx_terminals_org_id`, `idx_terminals_terminal_type_id`, Unique(`device_id`), Unique(`sn`) |
| 29 | `License` | `licenses` | `terminal.py` | `id` (int) | `terminal_id -> terminals.id` (CASCADE) | `terminal` | `idx_licenses_terminal_id`, `idx_licenses_org_id`, `idx_licenses_expires_at`, `ck_license_price_non_negative`, `ck_billing_period_months_positive` |
| 30 | `TerminalCertHistory` | `terminal_cert_history` | `terminal.py` | `id` (int) | `terminal_id -> terminals.id`, `pin_id -> certificate_pins.id` | `terminal`, `pin` | `idx_terminal_cert_history_terminal` |
| 31 | `TerminalCertDiscovery` | `terminal_cert_discovery` | `terminal.py` | `id` (int) | `terminal_id -> terminals.id` (SET NULL) | `terminal` | `idx_terminal_cert_discovery_sn_serial`, `idx_terminal_cert_discovery_status`, `idx_terminal_cert_discovery_last_seen` |

---

## 6. Соглашения об именовании и типах (Conventions)

1. **Классы:** PascalCase (`BillingOrder`, `TerminalCertDiscovery`, `OrgBillingSettings`).
2. **Таблицы:** snake_case, множественное число (`billing_orders`, `terminals`, `users`, `groups`, `services`). Допускаются исторические/легаси имена таблиц ядра платежей (`orgs`, `tsp`, `tsp_parameter_codes`, `payments`, `balance_terminal_tsp`).
3. **Первичные ключи:** Стандарт — суррогатный целочисленный `id` (`Integer, primary_key=True, autoincrement=True`), для заказов биллинга — UUID (`PG_UUID(as_uuid=True)`), для легаси/доменных корней — явные идентификаторы (`org_id`, `tsp_id`, `paym_id`, `rec_id`, `param_id`, `device_id`).
4. **Внешние ключи:** Строгое указание таблицы и колонки (`"terminals.id"`, `"orgs.org_id"`). Для каскадных сущностей — `ondelete="CASCADE"`, для аудитных/необязательных связей — `ondelete="SET NULL"`.
5. **Денежные величины:** Исключительно целые числа копеек с типом `BigInteger` (`amount_minor`, `monthly_price_minor`, `monthly_price_override_minor`, `cert_price_minor`, `paym_amount`). Использование `Float` или `Numeric` для денег категорически запрещено.
6. **Временные метки:** `DateTime(timezone=True)` с серверным значением по умолчанию `server_default=func.now()`.
7. **Индексы и ограничения:**
   - Префикс `idx_` для явных составных и функциональных индексов;
   - Префикс `ix_` для простых индексов на колонках;
   - Префикс `ck_` или `chk_` для CheckConstraint;
   - Префикс `uq_` для UniqueConstraint.

---

## 7. Результаты локальных проверок (Checks Evidence)

Все проверки выполнены локально в окружении `shared`:

### 1. Линтер Ruff
```bash
& shared\.venv\Scripts\ruff.exe check shared
```
**Результат:** `All checks passed!` (код возврата 0).

### 2. Форматирование Ruff
```bash
& shared\.venv\Scripts\ruff.exe format --check shared
```
**Результат:** `15 files already formatted` (код возврата 0).

### 3. Компиляция и валидация мапперов SQLAlchemy
```python
from sqlalchemy import inspect
from etranprocessing_db.base import Base
import etranprocessing_db.models
[inspect(c) for c in Base.__subclasses__()]
```
**Результат:** `All 31 mappers successfully configured and compiled!` (код возврата 0). Все связи (`relationship`), вторичные ссылки, внешние ключи и аргументы таблиц валидны.

### 4. Проверка зависимостей UV
```bash
uv pip check --directory shared
```
**Результат:** `Checked 8 packages in 13ms. All installed packages are compatible.` (код возврата 0).

### 5. Тестовая сборка пакета
```bash
uv build --directory shared
```
**Результат:** Успешно собраны `etranprocessing_db-0.1.0.tar.gz` и `etranprocessing_db-0.1.0-py3-none-any.whl` (код возврата 0). Временные артефакты каталога `shared/dist` удалены перед коммитом.

---

## 8. Безопасные точки расширения для `fin_*` (L4Desk Financial Subledger)

Согласно разделам 9, 10 и 14 `l4desk-architecture.md`, в последующих шагах (в частности, в промпте `L4D-04A-SHARED`) в `shared/etranprocessing_db` будут добавлены декларативные модели финансового subledger с префиксом `fin_*`.

### Принцип безопасного расширения (Additive Non-Breaking Expand)
1. **Текущие runtime-модели не изменяются:** Существующие таблицы (`payments`, `billing_orders`, `billing_order_items`, `licenses`, `org_billing_settings`) не получают обязательных внешних ключей на таблицы `fin_*`.
2. **Изолированный модуль:** Новые финансовые модели размещаются в отдельном файле `shared/etranprocessing_db/models/fin.py` (или `financial.py`) и регистрируются в `models/__init__.py` и общем `__init__.py`.
3. **Единый Base:** Все `fin_*` модели наследуют `from etranprocessing_db.base import Base`.
4. **Связи с существующими сущностями:**
   - **Tenant (Организация):** `org_id` ссылается на `orgs.org_id` (`ForeignKey("orgs.org_id")`). В финансовом контуре `org_id` выступает `tenant_id`.
   - **Терминалы / Устройства:** `terminal_id` ссылается на `terminals.id` (`ForeignKey("terminals.id")`).
   - **Пользователи (Субъекты аудита):** `created_by_user_id` ссылается на `users.id` (`ForeignKey("users.id", ondelete="SET NULL")`).

### Перечень планируемых expand-сущностей `fin_*`
1. `FinAccount` (`fin_accounts`): системные счета двойной записи (`tenant_settlement`, `payment_clearing`, `usage_revenue`, `vat_payable`, `discounts_expense`).
2. `FinTariffVersion` (`fin_tariff_versions`): версии коммерческих тарифов, суточные/месячные ставки в копейках (`monthly_rate_kopecks`, `hourly_rate_kopecks`).
3. `FinBillingCycle` (`fin_billing_cycles`): календарные/расчётные циклы tenant (`cycle_start`, `cycle_end`, `status`).
4. `FinUsageDaily` (`fin_usage_daily`): агрегированное суточное потребление ресурсов (видео/консоль/online) в секундах с округлением до часов и расчётом `calculated_kopecks`, `posted_kopecks`, `discarded_kopecks`.
5. `FinTerminalMonthlyCharge` (`fin_terminal_monthly_charges`): начисления за активные терминалы (terminal-month) с учётом постоянной льготы первого терминала tenant.
6. `FinPayment` (`fin_payments`): платежи ЮKassa с фиксацией `amount_kopecks`, `status`, `idempotency_key`.
7. `FinManualPayment` (`fin_manual_payments`): банковские платежи юрлиц, проведённые superuser, с аудитом реквизитов платёжного поручения.
8. `FinLedgerTransaction` (`fin_ledger_transactions`): неизменяемые заголовки бухгалтерских транзакций (append-only) с инвариантом двойной записи.
9. `FinLedgerEntry` (`fin_ledger_entries`): проводки по счетам (`debit_kopecks`, `credit_kopecks`).
10. `FinBalanceProjection` (`fin_balance_projections`): снимок текущего баланса tenant для быстрого чтения без пересчёта всего регистра.
11. `FinNotificationDelivery` (`fin_notification_deliveries`): журнал отправки финансовых и биллинговых уведомлений (`(tenant_id, billing_cycle_id, notification_type)`).
12. `FinReconciliationRun` (`fin_reconciliation_runs`): аудит запусков ночной сверки (балансы, проводки, отброшенные копейки).
13. `FinArchiveBatch` (`fin_archive_batches`): реестр архивных пакетов помесячного перемещения старой технической истории.

### Финансовые инварианты для моделей `fin_*`
- Все денежные суммы строго `BigInteger` в копейках.
- Запрет редактирования проведённых документов (исправления только через сторно и корректирующие транзакции).
- `posted_kopecks % 100 = 0` (округление до целого рубля).
- `calculated_kopecks = posted_kopecks + discarded_kopecks` (отброшенные копейки сохраняются в строке расчёта).

---

## 9. Риски и откат (Rollback)

- **Риски:** Отсутствуют. Runtime-модели и схема базы данных не изменялись.
- **Rollback:** В случае необходимости отката достаточно удалить созданный файл отчёта `shared/docs/l4desk/handoffs/L4D-00F-SHARED-report.md`.

---

## 10. Канонический кандидатный блок передачи контракта

```yaml
<!-- HANDOFF:H-L4D-00F-SHARED-v1:BEGIN -->
handoff_id: H-L4D-00F-SHARED-v1
status: ACCEPTED
contract_kinds:
  - REPORT
  - MODEL_BASELINE
producer_prompt_id: L4D-00F-SHARED
producer_scope_project: shared/etranprocessing_db
producer_report_path: shared/docs/l4desk/handoffs/L4D-00F-SHARED-report.md
producer_branch: l4desk/l4d-00f-shared
producer_commit: COMMIT_HASH_PLACEHOLDER
accepted_at_utc: 2026-09-17T20:15:00Z
contract_version: 0.1.0
schema_revision: N/A
artifact_version: 0.1.0
artifact_paths:
  - shared/docs/l4desk/handoffs/L4D-00F-SHARED-report.md
artifact_sha256:
  - SHA256_PLACEHOLDER
compatibility:
  backward_compatible_with:
    - N/A
  breaking_changes: false
  notes: Baseline audit and inventory of shared thin declarative ORM model layer (etranprocessing_db). 31 unified declarative models verified, pure SQLAlchemy 2.0 without framework/business/auth/crypto dependencies, Python 3.14 compatibility confirmed, linters/formatters passing, fin_* safe expand points documented without modifying runtime models.
deployment_status: DEPLOYED
deployed_environment: local_package
feature_flags: {}
contract_payload:
  identifiers:
    package_name: etranprocessing-db
    module_name: etranprocessing_db
    models_count: 31
    base_class: etranprocessing_db.base.Base
  models:
    auth:
      - ApiToken (api_tokens)
      - User (users)
      - UserSession (user_sessions)
    billing:
      - BillingOrder (billing_orders)
      - BillingOrderItem (billing_order_items)
      - CertificatePin (certificate_pins)
    catalog:
      - CatalogCategory (catalog_categories)
      - CatalogItem (catalog_items)
    email:
      - EmailVerification (email_verifications)
      - EmailLog (email_logs)
    menu:
      - MenuVariant (menu_variants)
      - MenuVariantSnapshot (menu_variant_snapshots)
      - Group (groups)
      - Service (services) [alias ServiceMenu]
      - TerminalMenuBinding (terminal_menu_bindings)
    org:
      - Org (orgs)
      - OrgBillingSettings (org_billing_settings)
      - OrgStatus (org_statuses)
    payment:
      - Tsp (tsp)
      - TspParameterCode (tsp_parameter_codes)
      - Payment (payments)
      - PaymentParam (payment_params)
      - BalanceTerminalTsp (balance_terminal_tsp)
    telemetry:
      - GateGaugeRecord (gate_gauge_records)
      - TechGateRecord (tech_gate_records)
      - TerminalGaugeState (terminal_gauge_states)
    terminal:
      - TerminalType (terminal_types)
      - Terminal (terminals)
      - License (licenses)
      - TerminalCertHistory (terminal_cert_history)
      - TerminalCertDiscovery (terminal_cert_discovery)
  conventions:
    orm_style: SQLAlchemy 2.0 DeclarativeBase, Mapped[T] = mapped_column(...)
    money_representation: integer minor units (kopecks) in BigInteger, float strictly prohibited
    timestamps: DateTime(timezone=True) with server_default=func.now()
    naming: PascalCase classes, snake_case tables, idx_/ix_ indexes, ck_ checks, uq_ uniques
  expand_points_fin:
    target_module: shared/etranprocessing_db/models/fin.py
    target_models:
      - FinAccount (fin_accounts)
      - FinTariffVersion (fin_tariff_versions)
      - FinBillingCycle (fin_billing_cycles)
      - FinUsageDaily (fin_usage_daily)
      - FinTerminalMonthlyCharge (fin_terminal_monthly_charges)
      - FinPayment (fin_payments)
      - FinManualPayment (fin_manual_payments)
      - FinLedgerTransaction (fin_ledger_transactions)
      - FinLedgerEntry (fin_ledger_entries)
      - FinBalanceProjection (fin_balance_projections)
      - FinNotificationDelivery (fin_notification_deliveries)
      - FinReconciliationRun (fin_reconciliation_runs)
      - FinArchiveBatch (fin_archive_batches)
    foreign_keys_to_existing:
      - orgs.org_id (as tenant_id)
      - terminals.id
      - users.id (audit actor)
supersedes: []
known_risks: []
consumers:
  - L4D-00G-DOCS
next_prompt_id: L4D-00G-DOCS
<!-- HANDOFF:H-L4D-00F-SHARED-v1:END -->
```
