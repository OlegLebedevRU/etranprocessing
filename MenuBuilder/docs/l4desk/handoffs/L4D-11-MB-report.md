# Handoff Report: L4D-11-MB — YooKassa & Manual Payments

**Prompt ID:** `L4D-11-MB`  
**Prompt Type:** `payment-implementation`  
**Scope Project:** `MenuBuilder`  
**Scope Root:** `D:\repo\platerra\Public\etranprocessing\MenuBuilder`  
**Required Handoff IDs:** `[H-L4D-10-MB-v1]`  
**Output Handoff ID:** `H-L4D-11-MB-v1`  
**Next Prompt ID:** `L4D-12-MB`  
**Branch:** `l4desk/l4d-11-mb`  
**Producer Commit:** `b6f793ad9880cf20489fe37366edc66af8229464`  
**Report Path:** `MenuBuilder/docs/l4desk/handoffs/L4D-11-MB-report.md`  
**Candidate Path:** `MenuBuilder/docs/l4desk/handoffs/L4D-11-MB-candidate.md`  
**Architecture Sections:** `[2, 3, 5, 7, 9, 10, 13, 14, 15, 16, 17]`  

---

## 1. Резюме выполнения задания и нормативная логика

В рамках спецификации `L4D-11-MB` и архитектурных разделов L4Desk в `MenuBuilder` реализованы:
1. Пополнение внутреннего баланса физических лиц через ЮKassa с подтверждением только через authoritative GET provider API.
2. Ручной неизменяемый банковский платёжный документ юридического лица одним superuser.
3. Механизм сторнирования ручных оплат через обратную транзакцию сабреджера двойной записи (`Dr tenant_settlement, Cr payment_clearing`) и создание нового документа сторно.
4. Атомарная фиксация неизменяемого якорного дня цикла (`anchor_at`) при первом успешном платеже тенанта (любые последующие платежи якорь не сдвигают).
5. Защита от открытых редиректов (safe return URL), фильтрация входящих вебхуков по белым спискам CIDR-подсетей ЮKassa и опциональному секрету вебхука.
6. Конфигурируемая фискализация чеков (54-ФЗ) без хардкода секретов.

---

## 2. Реализованные сервисы и компоненты (`MenuBuilder`)

### 2.1. Клиент интеграции ЮKassa (`yookassa.py`)
- **Файл:** `app/services/financial_core/yookassa.py`.
- **Протокол `YooKassaClientProtocol`**:
  - `create_payment`: отправка запроса на создание платежа в ЮKassa с заголовком `Idempotence-Key`, суммой в рублях (`amount.value`, `currency='RUB'`), `confirmation.type = 'redirect'`, `return_url`, `description`, `metadata` и опциональным `receipt` (snapshot фискального чека 54-ФЗ).
  - `get_payment`: авторитетный запрос актуального состояния платежа `GET /payments/{id}`.
- **Безопасность сетевого контура**:
  - Проверка сетевых таймаутов (`httpx.TimeoutException`) и ошибок соединения без падения приложения.
  - `MockYooKassaClient`: тестовый мок-клиент для контрактных тестов, изоляции sandbox и симуляции сбоев/таймаутов/ошибок ЮKassa.
  - `is_ip_trusted`: валидация IP-адресов вебхука по доверенным CIDR-сетям ЮKassa (`185.71.76.0/27`, `185.71.77.0/27`, `77.75.153.0/25`, `77.75.156.11/32`, `77.75.156.35/32`, `77.75.154.128/25`, `2a02:5180::/32`).
  - `is_safe_return_url`: защита от атак типа Open Redirect.

### 2.2. Сервис платежей ЮKassa (`payments.py`)
- **Файл:** `app/services/financial_core/payments.py`.
- **Создание платежа (`create_payment`)**:
  - Строгая валидация суммы: только положительное целое число рублей (`amount_rubles >= 1`).
  - Идемпотентность по уникальному `operation_id`: при повторном запросе с тем же ключом и суммой возвращается существующий платеж без повторного списания; при несовпадении суммы возвращается `FinConcurrencyError` (HTTP 409).
  - Инициализация записи `FinPayment` в статусе `pending`.
- **Авторитетная синхронизация и защита от подделки (`sync_payment_status`)**:
  - Блокировка строки через `with_for_update()`.
  - Запрос авторитарного состояния `GET /payments/{provider_payment_id}`.
  - Режим защиты от повторов: если статус уже `succeeded` и транзакция проведена, повторный постинг пропускается.
  - Строгие проверки на несоответствие (Security Mismatch Checks): несовпадение суммы, валюты, `tenant_id` в метаданных или `shop_id` мерчанта приводит к немедленной отмене платежа (`status = 'canceled'`), отказу в проводке и возбуждению исключения.
  - При `succeeded`: создание одной транзакции сабреджера двойной записи `FinPostingRequest(kind='payment', Dr payment_clearing, Cr tenant_settlement)` и атомарная фиксация якоря цикла тенанта через `FinBillingCycleService.initialize_anchor_from_payment`.

### 2.3. Ручные платежи юрлиц и сторно (`manual_payments.py`)
- **Файл:** `app/services/financial_core/manual_payments.py`.
- **Регистрация ручной оплаты (`create_manual_payment`)**:
  - Доступно только `superuser` (проверка `require_internal_or_superuser`).
  - Фиксация даты поступления на расчетный счет (`received_on: date`), номера банковского поручения (`document_number`), плательщика (`payer`), назначения (`purpose`) и ссылки на выписку/документ (`evidence_reference`).
  - Проведение транзакции двойной записи: `Dr payment_clearing, Cr tenant_settlement`.
  - Постинг неизменяем (`immutable`).
- **Сторнирование ручной оплаты (`storno_manual_payment`)**:
  - Вызов `FinReversalService.reverse_transaction` к `original.ledger_transaction_id`: создается сторнирующая транзакция `Dr tenant_settlement, Cr payment_clearing`, баланс тенанта восстанавливается.
  - Создание нового отдельного документа `FinManualPayment` с номером `STORNO-{original.document_number}` и фиксацией причины сторно (`reversal_reason`).
  - Повторное сторно или сторнирование документа сторно строго блокируется (`FinReversalError`).

### 2.4. Репозиторий и маршрутизация REST API
- **Файл:** `app/repositories/l4desk_repository.py`:
  - `get_payment`, `get_payment_by_provider_id`, `get_payment_by_operation_id`, `list_payments`.
  - `get_manual_payment`, `list_manual_payments`.
- **Файл:** `app/routers/finance.py`:
  - `POST /api/v1/finance/payments` — создание платежа ЮKassa (только целые рубли >= 1).
  - `GET /api/v1/finance/payments/{payment_id}` — получение платежа тенантом.
  - `GET /api/v1/finance/payments` — список платежей тенанта.
  - `POST /api/v1/finance/payments/{payment_id}/poll` — опрос и синхронизация статуса.
  - `POST /api/v1/finance/yookassa/webhook` — обработчик входящих уведомлений ЮKassa.
  - `POST /api/internal/v1/finance/manual-payments` — регистрация ручной оплаты (superuser).
  - `POST /api/internal/v1/finance/manual-payments/{id}/storno` — сторно ручной оплаты (superuser).
  - `GET /api/internal/v1/finance/manual-payments` — список ручных оплат (superuser).
  - `GET /api/internal/v1/finance/manual-payments/{id}` — просмотр ручной оплаты (superuser).

---

## 3. Конфигурация фискализации и ЮKassa (Ключи без значений)

Все ключи фискализации и интеграции вынесены в `MenuBuilder/backend/app/config.py` со строгой типизацией и безопасными значениями по умолчанию (без хардкода секретов):

| Ключ конфигурации | Тип | Описание |
| :--- | :--- | :--- |
| `YOOKASSA_ENABLED` | `bool` | Флаг включения провайдера ЮKassa |
| `YOOKASSA_SHOP_ID` | `str` | Идентификатор магазина (Shop ID) в ЮKassa |
| `YOOKASSA_SECRET_KEY` | `str` | Секретный ключ API магазина |
| `YOOKASSA_API_URL` | `str` | Базовый URL API ЮKassa (`https://api.yookassa.ru/v3`) |
| `YOOKASSA_WEBHOOK_SECRET` | `str` | Секретный ключ для подписи/проверки заголовков вебхука |
| `YOOKASSA_IP_FILTER_ENABLED` | `bool` | Флаг включения проверки белого списка IP-адресов вебхука |
| `YOOKASSA_TRUSTED_IPS_RAW` | `str` | CIDR подсети официальных IP-адресов ЮKassa через запятую |
| `YOOKASSA_RETURN_URL_BASE` | `str` | Базовый доверенный URL возврата пользователя из платежного шлюза |
| `YOOKASSA_RECEIPT_ENABLED` | `bool` | Флаг формирования фискального чека (54-ФЗ) |
| `YOOKASSA_TAX_SYSTEM_CODE` | `int \| None` | Код системы налогообложения (1..6) |
| `YOOKASSA_VAT_CODE` | `int` | Код ставки НДС (1 = без НДС, 2 = 0%, 3 = 10%, 4 = 20% и т.д.) |
| `YOOKASSA_PAYMENT_SUBJECT` | `str` | Признак предмета расчета (`service`) |
| `YOOKASSA_PAYMENT_MODE` | `str` | Признак способа расчета (`full_prepayment`) |
| `YOOKASSA_ITEM_DESCRIPTION` | `str` | Описание позиции в фискальном чеке |
| `YOOKASSA_REQUEST_TIMEOUT_SEC` | `float` | Таймаут HTTP-запросов к ЮKassa в секундах (по умолчанию 15.0) |

---

## 4. Результаты тестирования и верификации

### 4.1. Специализированный тестовый набор (`test_yookassa_and_manual_payments.py`)
- **Результат:** `10 passed in 3.42s`
- **Проверенные сценарии:**
  1. `test_mock_yookassa_provider_contract` — соответствие контракту официального протокола ЮKassa v3, формирование чека, Idempotence-Key.
  2. `test_payment_integer_rubles_and_safe_url_guard` — отказ в суммах <= 0, валидация safe return URL, защита от Open Redirect.
  3. `test_yookassa_payment_creation_and_successful_webhook_posting` — полный цикл оплаты, авторитарное подтверждение GET, проводка `Dr payment_clearing, Cr tenant_settlement`, обновление баланса проекции и фиксация якорного дня цикла (`anchor_at`).
  4. `test_duplicate_webhook_and_polling_idempotency` — устойчивость к дубликатам вебхука и fallback poll: проводка выполняется строго один раз.
  5. `test_yookassa_security_mismatch_rejection` — отмена платежа и отказ в проводке при несовпадении суммы, валюты или данных мерчанта.
  6. `test_yookassa_network_timeout_handling` — корректная обработка сетевых сбоев и таймаутов провайдера.
  7. `test_first_payment_anchor_and_subsequent_immunity` — первый платеж задает дату якоря цикла; последующие платежи не сдвигают якорь.
  8. `test_manual_payment_creation_and_storno_reversal` — создание ручной банковской оплаты superuser, неизменяемость, сторно через обратную транзакцию и создание документа `STORNO-PAY-ORDER-9912`.
  9. `test_reconciliation_post_yookassa_and_manual_payments` — аудит сабреджера после платежей: `status = 'matched'`, `mismatches = 0`, `balance_difference_kopecks = 0`.
  10. `test_http_api_yookassa_and_manual_payments` — сквозное тестирование HTTP REST API (платежи, вебхук, опрос, ручные платежи, ролевой доступ 403/201, сторно).

### 4.2. Полный регрессионный тестовый набор
- **Команда:** `uv run --directory MenuBuilder/backend pytest`
- **Результат:** `400 passed in 83.15s` (0 ошибок, 0 падений).
- **Тесты shared:** `uv run --directory shared pytest` $\rightarrow$ `65 passed in 19.40s`.

### 4.3. Статический анализ и форматирование кода
- `uv run --directory MenuBuilder/backend ruff check app tests` $\rightarrow$ `All checks passed!`
- `uv run --directory MenuBuilder/backend ruff format --check app tests` $\rightarrow$ `113 files already formatted`
- `uv run --directory MenuBuilder/backend pyright app tests` $\rightarrow$ `0 errors, 0 warnings, 0 informations`
- `npm --prefix MenuBuilder/frontend run build` $\rightarrow$ `✓ built in 1m 3s`

---

## 5. Развертывание и Live Smoke Evidence

Деплой выполнен на боевой хост `87.242.100.34`:
1. Синхронизация модулей в контейнер `menubuilder-backend` и перезапуск сервиса:
   - Лог: `Schema compatibility check PASSED: revision in ['027'], all 23 required tables present.`
   - Uvicorn инициализирован на порту 8000.
2. Обновление артефактов SPA фронтенда в `/home/user1/MenuBuilder/frontend/dist/`.
3. Запуск live smoke-проверки на реальной базе данных PostgreSQL:
   ```text
   === [LIVE SMOKE TEST] L4D-11-MB YooKassa & Manual Payments ===
   Step 1: Security guards (safe return URL, IP whitelist) [OK]
   Step 2: YooKassa protocol contract & payload [OK]
   Step 3: Real PostgreSQL ledger reconciliation: status=matched, mismatches=0, diff=0 [OK]
   === ALL LIVE SMOKE CHECKS PASSED (ZERO DISCREPANCIES, NO REAL CHARGES) ===
   ```

---

## 6. Перечень артефактов и контрольные суммы SHA-256

| № | Артефакт | SHA-256 |
| :-: | :--- | :--- |
| 1 | `MenuBuilder/backend/app/config.py` | `8944bbd842e287f55863172cbf92afcb65a9f26f7579c8b25edecaecc9b67498` |
| 2 | `MenuBuilder/backend/app/repositories/l4desk_repository.py` | `b8670d8abb462ae2a0a61bf6e782e82482403bf683ddc31345cc3c369e7d5714` |
| 3 | `MenuBuilder/backend/app/routers/finance.py` | `afeb55ca680d92a754923a575bbc3ee7eeb509322da9ecfebb8b957e7efadb45` |
| 4 | `MenuBuilder/backend/app/services/financial_core/__init__.py` | `6693513b08f168e6cb3a21f4bce6ec06773a59e3970654c2994ef78446907ae7` |
| 5 | `MenuBuilder/backend/app/services/financial_core/schemas.py` | `86c06f08d3f556925d3d8a9ae44d1a514a0a27f948e4af28b8a43605aaa53576` |
| 6 | `MenuBuilder/backend/app/services/financial_core/payments.py` | `6abbe80dbc69d60e0c79abc08391a3bb0f3df13cc8a36a4c86af36394f65c401` |
| 7 | `MenuBuilder/backend/app/services/financial_core/manual_payments.py` | `dd68c81271233654ca31a64cf57a7a559718b264225191157475f559042a1950` |
| 8 | `MenuBuilder/backend/app/services/financial_core/yookassa.py` | `aac015319eb62683c884c2a52933b26f41b49792ebc72c1272e78823dba10713` |
| 9 | `MenuBuilder/backend/tests/test_yookassa_and_manual_payments.py` | `655b60e3206ad5ac039f3fe794afb83254327876bf5682eb102b47e20c97098e` |
| 10 | `MenuBuilder/docs/l4desk/handoffs/L4D-11-MB-report.md` | *(рассчитывается побайтно после сохранения отчёта)* |

---

## 7. Отдельный кандидат handoff (DETACHED_V1)

В соответствии с §9 `PROMPT-STANDARD.md` окончательный candidate-блок вынесен из отчёта во избежание циклической зависимости хеша и размещен в отдельном файле:  
`MenuBuilder/docs/l4desk/handoffs/L4D-11-MB-candidate.md`.
