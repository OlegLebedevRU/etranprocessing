# Handoff Report: L4D-10-MB — Tariffs, Individual Billing Cycles & Confirmed Metering Engine

**Prompt ID:** `L4D-10-MB`  
**Prompt Type:** `billing-implementation`  
**Scope Project:** `MenuBuilder`  
**Scope Root:** `D:\repo\platerra\Public\etranprocessing\MenuBuilder`  
**Required Handoff IDs:** `[H-L4D-09-MB-v1, H-L4D-03-MB-v1, H-L4D-02-IOT-v1]`  
**Output Handoff ID:** `H-L4D-10-MB-v1`  
**Next Prompt ID:** `L4D-11-MB`  
**Branch:** `l4desk/l4d-10-mb`  
**Producer Commit:** `2d567c2262e8b37022312427e2f77ba21b61f144`  
**Report Path:** `MenuBuilder/docs/l4desk/handoffs/L4D-10-MB-report.md`  
**Candidate Path:** `MenuBuilder/docs/l4desk/handoffs/L4D-10-MB-candidate.md`  
**Architecture Sections:** `[2, 3, 5, 6, 7, 9, 10, 13, 14, 15, 16, 17]`  

---

## 1. Резюме выполнения задания и нормативная логика

В рамках спецификации `L4D-10-MB` и архитектурных разделов сервиса L4Desk в `MenuBuilder` реализованы версионированные тарифы, персональные биллинговые циклы с якорным смещением и движок учета потребления (metering engine) из подтвержденных фактов IoT с интеграцией в финансовый сабреджер двойной записи (`fin_*` ledger).

### 1.1. Нормативная финансовая и расчетная логика
1. **Якорь и биллинговые циклы (`add_months`):** Первый успешный платеж тенанта фиксирует индивидуальный якорь (`anchor_at`, `anchor_day`, `anchor_timezone`) в `fin_billing_profiles`. Границы месяцев вычисляются как `add_months(anchor, n)` с соблюдением правила последнего существующего календарного дня (31 января $\rightarrow$ 28 февраля в невисокосный год, 29 февраля в високосный, 31 марта; 29 февраля $\rightarrow$ 28 февраля $\rightarrow$ 29 февраля). Любые последующие платежи и события `device_online` **никогда не сдвигают** существующий якорь.
2. **Льгота первого терминала и передача только вперед:** Первый по неизменяемому порядковому номеру (`ordinal ASC`) существующий терминал (`deleted_at IS NULL`) является бесплатным (`is_free = True`). При удалении (`deleted_at IS NOT NULL`) льгота передается следующему существующему терминалу строго вперед. Закрытые периоды и ранее начисленные списания за месяц терминала не пересчитываются и не возвращаются задним числом.
3. **Ежемесячный сбор за терминал (10000 копеек):** Каждый другой (платный) терминал при первом подтвержденном событии `device_online` в текущем биллинговом цикле получает разовое начисление 10000 копеек (100 руб.) с дедупликацией по уникальному ключу `(terminal_id, billing_cycle_id)`. Начисление создается даже при появлении терминала в сети после границы льготного периода (`grace_deadline`).
4. **Разделение суток тенанта и суточная квота:** Локальные сутки тенанта (с учетом его таймзоны) разделяют интервалы сессий. Взаимное исключение гарантирует, что сессии консоли и видео не пересекаются. Для бесплатного терминала: `billable_seconds = max(0, console + video - 7200)` (бесплатная квота 2 часа в день); для остальных терминалов бесплатная квота равна 0 (`billable_seconds = console + video`).
5. **Суточное округление часов и ставка:** `paid_hours = ceil(billable_seconds / 3600)`, действующая ставка тарифа `100` копеек/час (1 руб./час). Агрегированное суточное округление выполняется строго один раз в сутки.
6. **Универсальная формула будущего тарифа:** Вычисляется теоретическая сумма `calculated = paid_hours * hourly_rate_kopecks`, затем сумма к списанию `posted = floor(calculated / 100) * 100` (кратность целым рублям), и отбрасываемый остаток `discarded = calculated - posted`. Соблюдается фундаментальный инвариант `calculated = posted + discarded`. Отброшенный остаток `discarded` строго неотрицателен, меньше 100, не переносится на следующие сутки и не проводится в проводках сабреджера.

---

## 2. Реализованные сервисы и компоненты (`MenuBuilder`)

### 2.1. Версионированные снимки тарифов (`FinTariffService`)
- Файл: `app/services/financial_core/tariffs.py`.
- Хранение неизменяемых снимков тарифов в таблице `fin_tariff_versions`: базовая версия `v1.0` (10000 коп./мес, 100 коп./час, 7200 с бесплатной суточной квоты).
- Разрешение эффективной версии тарифа на историческую дату (`get_effective_tariff(as_of)`) с поддержкой создания будущих версий тарифов.

### 2.2. Сервис циклов и якоря (`FinBillingCycleService`)
- Файл: `app/services/financial_core/cycles.py`.
- Метод `calculate_add_months`: точное добавление месяцев с правилом последнего существующего дня, сохранением времени суток и устойчивостью к сезонным переходам времени (DST).
- Метод `calculate_cycle_boundaries`: расчет границ цикла `[starts_at, ends_at)` и льготного окна `grace_deadline = starts_at + 3 calendar days` (констрейнт `starts_at < grace_deadline < ends_at`).
- Метод `initialize_anchor_from_payment`: атомарная инициализация якорной даты по первому успешному платежу с созданием нулевого цикла и блокировкой от повторного смещения.
- Метод `get_or_create_cycle_for_timestamp`: последовательное автоматическое развертывание циклов тенанта без пропусков.

### 2.3. Управление терминалами и ежемесячными списаниями (`FinTerminalService`)
- Файл: `app/services/financial_core/terminals.py`.
- Определение текущего бесплатного терминала (`get_free_terminal`, `is_terminal_free`) по устойчивому номеру `ordinal ASC` среди `deleted_at IS NULL`.
- Обработка первого в цикле `device_online` (`process_device_online_monthly_charge`):
  - Для бесплатного терминала: создание нулевой snapshot-записи `is_free=True`, `posted_kopecks=0` без проводок.
  - Для платного терминала: списание 10000 копеек проводкой двойной записи (`Dr tenant_settlement, Cr usage_revenue`), привязка `ledger_transaction_id` и фиксация `posted_at`.
  - Защита от дубликатов по уникальному ключу `(terminal_id, billing_cycle_id)`.

### 2.4. Суточный учет потребления и обработка late-events (`FinMeteringService`)
- Файл: `app/services/financial_core/metering.py`.
- Функция `split_interval_by_local_days`: нарезка временных интервалов сессий по локальным полуночам тенанта с сохранением суммарной длительности в секундах.
- Функции `calculate_daily_amounts` и `calculate_daily_metrics`: расчет суточных квот, округление часов `ceil(billable/3600)`, расчет `calculated`, `posted` и `discarded`.
- Метод `record_session_usage`:
  - Для открытых суток (`ledger_transaction_id IS NULL`): кумулятивное накопление секунд консоли/видео под блокировкой строки `with_for_update()`, пересчет сумм и обновление хеша событий.
  - Для закрытых/проведенных суток (`ledger_transaction_id IS NOT NULL`): исходная строка и проводка объявляются **неизменяемыми (immutable)**; вычисляется дельта и создается корректирующая проводка `kind='adjustment'` со ссылкой `corrects_transaction_id` и детальным снимком `calculation_snapshot`.
- Метод `close_and_post_daily_usage`: фиксация суток и проведение сабреджерной транзакции `kind='usage'` для всех строк с `posted_kopecks > 0`.

### 2.5. Кроссплатформенная обработка таймзон (`timezones.py`)
- Файл: `app/services/financial_core/timezones.py`.
- Функция `resolve_timezone`: надежное разрешение IANA-таймзон через `zoneinfo.ZoneInfo` с автоматическим fallback на `dateutil.tz` для сред без встроенной базы tzdata.

### 2.6. Расширение репозитория и REST API
- `app/repositories/l4desk_repository.py`: методы чтения профилей, циклов, суточного потребления, списаний и версий тарифов.
- `app/routers/finance.py`:
  - Tenant-facing:
    - `GET /api/v1/finance/profile` — статус биллингового профиля и якоря;
    - `GET /api/v1/finance/cycles` — список биллинговых циклов тенанта;
    - `GET /api/v1/finance/tariffs/current` — текущий действующий тариф;
    - `GET /api/v1/finance/usage` — суточный журнал потребления;
    - `GET /api/v1/finance/monthly-charges` — списания за месяцы терминалов.
  - Internal Service API:
    - `GET /api/internal/v1/finance/tariffs` — реестр версий тарифов;
    - `POST /api/internal/v1/finance/tariffs` — создание новой версии тарифа;
    - `POST /api/internal/v1/finance/metering/online` — обработка online терминала;
    - `POST /api/internal/v1/finance/metering/record-usage` — регистрация сессии;
    - `POST /api/internal/v1/finance/metering/close-day` — закрытие и проведение суток.

---

## 3. Результаты тестирования и верификации

### 3.1. Специализированный тестовый набор тарифов и metering
- **Файл:** `MenuBuilder/backend/tests/test_tariffs_and_metering.py`
- **Результат:** `14 passed in 4.07s`
- **Покрытые сценарии:**
  1. `test_split_interval_dst_spring_and_autumn` — проверка переходов весеннего и осеннего времени (DST) в `Europe/London`, корректное разбиение интервалов по локальным полуночам.
  2. `test_anchor_add_months_rule_of_last_existing_day` — проверка дней 29, 30, 31 и високосных годов 2028/2032.
  3. `test_cycle_boundaries_and_grace_deadline` — проверка констрейнта `starts_at < grace_deadline < ends_at` и ровно 3 дней отсрочки.
  4. `test_online_and_subsequent_payments_do_not_shift_anchor` — подтверждение неизменности якоря при `device_online` и повторных платежах.
  5. `test_free_terminal_order_and_forward_deletion_transfer` — проверка минимального `ordinal` и переноса льготы строго вперед при удалении.
  6. `test_terminal_monthly_charge_free_and_paid` — списание 0 коп. для бесплатного терминала и 10000 коп. для платного (включая online после grace) с дедупликацией.
  7. `test_daily_usage_exact_120m_and_120m01s` — ровно 120 минут (7200 с) дают 0 коп. списания, 120 минут 1 секунда (7201 с) дают 1 час (100 коп.).
  8. `test_general_future_tariff_formula_rounding_invariants` — проверка формулы `calculated = posted + discarded`, `0 <= discarded < 100`, кратность целым рублям.
  9. `test_open_closed_daily_usage_and_late_event_delta` — накопление в открытых сутках, проведение при закрытии, создание `adjustment` проводки для late events.
  10. `test_reconciliation_post_metering_transactions` — прохождение аудита сабреджера после начислений со статусом `matched`.
  11. `test_rest_api_finance_profile_cycles_tariffs_and_metering` — сквозная интеграция REST API профилей, циклов, тарифов, сессий и закрытия суток.
  12. `test_tariff_versioning_and_snapshot_selection` — разрешение снимков тарифов до и после границы действия новой версии.
  13. `test_midnight_session_splitting_exact_seconds_preservation` — точное сохранение секунд при пересечении нескольких полуночей.
  14. `test_concurrent_worker_recording_and_idempotency` — конкурентная запись воркерами и идемпотентность закрытия суток.

### 3.2. Полный регрессионный тестовый набор
- **Команда:** `uv run --directory MenuBuilder/backend pytest`
- **Результат:** `390 passed in 79.52s` (0 ошибок, 0 падений).
- **Тесты shared:** `uv run --directory shared pytest` $\rightarrow$ `65 passed in 19.29s`.

### 3.3. Статический анализ и форматирование кода
- `uv run --directory MenuBuilder/backend ruff check app tests` $\rightarrow$ `All checks passed!`
- `uv run --directory MenuBuilder/backend ruff format --check app tests` $\rightarrow$ `109 files already formatted`
- `uv run --directory MenuBuilder/backend pyright app tests` $\rightarrow$ `0 errors, 0 warnings, 0 informations`

---

## 4. Развертывание в Shadow Mode и Live Smoke Evidence

Деплой выполнен на продакшен-хост `87.242.100.34`:
1. Синхронизация обновленных модулей `MenuBuilder/backend/app` в рабочий контейнер `menubuilder-backend`.
2. Перезапуск контейнера `menubuilder-backend`:
   - Лог: `Schema compatibility check PASSED: revision in ['027'], all 23 required tables present.`
   - Uvicorn инициализирован на порту 8000.
3. Выполнение live shadow smoke-теста на реальной базе данных PostgreSQL:

**Протокол боевого shadow smoke-теста:**
```text
=== [SHADOW SMOKE TEST] L4D-10-MB Tariffs, Billing Cycles & Metering ===
Step 1: Baseline tariff resolved: version=v1.0, monthly=10000, hourly=100, free_daily_sec=7200 [OK]
Step 2: Anchor add_months rule of last existing day verified (Jan 31 -> Feb 28 -> Mar 31) [OK]
Step 3: Cycle boundaries verified: starts=2026-01-31, grace=2026-02-03, ends=2026-02-28 [OK]
Step 4: Metering metrics verified: exact 120m=0k, 120m01s=100k [OK]
Step 5: Tariff formula rounding verified: calc=150, posted=100, discarded=50 [OK]
Step 6: Live ledger reconciliation executed: status=matched, mismatches=0, diff=0 [OK]
=== ALL SHADOW SMOKE CHECKS PASSED (ZERO DISCREPANCIES, NO REAL DEDUCTIONS) ===
```

---

## 5. Перечень артефактов и контрольные суммы SHA-256

| № | Артефакт | SHA-256 |
| :-: | :--- | :--- |
| 1 | `MenuBuilder/backend/app/repositories/l4desk_repository.py` | `78b98c9bd4d92c64e7e9161084cb2f226a796ee5bd06242c8feea6377d738085` |
| 2 | `MenuBuilder/backend/app/routers/finance.py` | `29e5e214ab32e270f3320d4cc22b32a537d3377a8b2e143dd59f543cf61eda4f` |
| 3 | `MenuBuilder/backend/app/services/financial_core/__init__.py` | `73cbaa7f3784d4983331353ca11c394f96de2816736280a339d231f9f821c746` |
| 4 | `MenuBuilder/backend/app/services/financial_core/cycles.py` | `bc72f8e02692feba1d7294185b7511880d1fba324ba599694f0e35424189dcb8` |
| 5 | `MenuBuilder/backend/app/services/financial_core/exceptions.py` | `19fa02c95d11aa6c1b80eb99150652c73322835d14f6086c12713c862b6d9067` |
| 6 | `MenuBuilder/backend/app/services/financial_core/metering.py` | `f4b05a9f574ea94b121400e1f493e19998d880b4b6306fc98b993af192ca4835` |
| 7 | `MenuBuilder/backend/app/services/financial_core/schemas.py` | `cf92379b04b71cd37d49d268a63796ad2b0504bb81f139caa9eb91c3f643123d` |
| 8 | `MenuBuilder/backend/app/services/financial_core/tariffs.py` | `a09e6c426441e0d9dffa9ccf8389b35c36aaf5f5d1ed480f4c1b8ca87c2f8855` |
| 9 | `MenuBuilder/backend/app/services/financial_core/terminals.py` | `cdce9eec1abfcb91aa23b587dba7616a3daf65ce440c7dce9a412218dce49eaf` |
| 10 | `MenuBuilder/backend/app/services/financial_core/timezones.py` | `d0d8a075cecbc4f4cfe372e0f19340d593fa44f52abec095c48e73b7077e6935` |
| 11 | `MenuBuilder/backend/tests/test_tariffs_and_metering.py` | `19164d31773a7bb624276d5c070f6b8bbc556b3e94e0f71c71428ddf84e639bf` |
| 12 | `MenuBuilder/backend/tests/test_terminal_onboarding.py` | `0ab63806bcc023406611f669f9424ff2bbad6993a7c0f62d025cc4b63db9fd8c` |
| 13 | `MenuBuilder/docs/l4desk/handoffs/L4D-10-MB-report.md` | *(рассчитывается побайтно после сохранения отчёта)* |

---

## 6. Отдельный кандидат handoff (DETACHED_V1)

В соответствии с §9 `PROMPT-STANDARD.md` окончательный candidate-блок вынесен из отчёта во избежание циклической зависимости хеша и размещен в отдельном файле:  
`MenuBuilder/docs/l4desk/handoffs/L4D-10-MB-candidate.md`.
