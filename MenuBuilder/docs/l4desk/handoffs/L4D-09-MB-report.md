# Handoff Report: L4D-09-MB — Double-Entry Financial Core Subledger & Balance Projection

**Prompt ID:** `L4D-09-MB`  
**Prompt Type:** `financial-core-implementation`  
**Scope Project:** `MenuBuilder`  
**Scope Root:** `D:\repo\platerra\Public\etranprocessing\MenuBuilder`  
**Required Handoff IDs:** `[H-L4D-08B-MB-v1, H-L4D-04C-MB-v1]`  
**Output Handoff ID:** `H-L4D-09-MB-v1`  
**Next Prompt ID:** `L4D-10-MB`  
**Branch:** `l4desk/l4d-09-mb`  
**Producer Commit:** `3d0dddba68df211a1d6c89844e0311b43bccd44d`  
**Report Path:** `MenuBuilder/docs/l4desk/handoffs/L4D-09-MB-report.md`  
**Candidate Path:** `MenuBuilder/docs/l4desk/handoffs/L4D-09-MB-candidate.md`  

---

## 1. Резюме выполнения задания

В соответствии с требованиями архитектуры L4Desk (разделы 3, 5, 7, 9, 10, 13, 14, 15, 16, 17) и спецификацией `L4D-09-MB` в `MenuBuilder` реализовано минимальное операционное финансовое ядро двойной записи (`fin_*` subledger) и быстрая проекция баланса тенантов.

Потребительские начисления, биллинг использования и платежный шлюз на данном этапе намеренно не активированы ("dark deployment" без пользовательских списаний). Все операции ядра строго изолированы, идемпотентны, защищены оптимистическими блокировками версий и полностью восстановимы из журнала проводок.

---

## 2. Реализованные компоненты и сервисы

### 2.1. Управление счетами (`FinAccountService`)
- Обеспечивает существование системных глобальных счетов (`tenant_id IS NULL`):
  - `payment_clearing` (валюта `RUB`) — транзитный счет входящих платежей/эквайринга;
  - `usage_revenue` (валюта `RUB`) — счет признания выручки от потребления услуг.
- Обеспечивает создание и привязку расчетного счета тенанта (`tenant_settlement`, валюта `RUB`, `tenant_id` обязателен).
- Инициализирует запись проекции баланса `fin_balance_projections` (`balance_kopecks = 0`, `version = 0`).

### 2.2. Сервис проведения проводок (`FinPostingService`)
- **Неизменяемость (Append-Only):** создание строго неизменяемых документов `FinLedgerTransaction` (`status = 'posted'`) и строк проводок `FinLedgerEntry`. Запрет `UPDATE` и `DELETE` проведенных записей защищен на уровне моделей и сервиса (`FinImmutableError`).
- **Инвариант двойной записи:** каждая транзакция обязана удовлетворять условию `sum(debit_kopecks) == sum(credit_kopecks) > 0`. Нарушение вызывает `FinImbalanceError`.
- **Запрет `float` и целочисленные копейки:** все суммы принимаются исключительно в целых копейках (`int`). Использование типа `float` или нецелых значений строго запрещено (`FinValidationError`).
- **Правило целых рублей:** в соответствии с архитектурным контрактом суммы проводок кратны 100 копейкам (`debit_kopecks % 100 == 0`, `credit_kopecks % 100 == 0`). Округление выполняется до передачи в проводку; остатки копеек фиксируются в документе расчета (`calculation_snapshot["discarded_kopecks"]`), а не в строках проводок.
- **Односторонность строк:** каждая строка `FinLedgerEntry` имеет строго одну положительную сторону (`(debit > 0 and credit == 0) or (credit > 0 and debit == 0)`).
- **Идемпотентность и дедупликация:** проверка уникальности по `(tenant_id, operation_id)` и `(tenant_id, source_project, source_type, source_id)`. При повторном запросе с идентичными параметрами возвращается существующая транзакция. Конфликт параметров с тем же ключом вызывает `FinDuplicatePostingError`.
- **Изоляция тенантов:** все проводки транзакции принадлежат `request.tenant_id`. Попытка указать счет другого тенанта пресекается ошибкой `FinTenantIsolationError`.

### 2.3. Быстрая проекция баланса (`FinProjectionService`)
- Направление баланса тенанта:
  $$\text{balance\_kopecks} = \sum \text{credits}(\text{tenant\_settlement}) - \sum \text{debits}(\text{tenant\_settlement})$$
  - Пополнение баланса: `Dr payment_clearing, Cr tenant_settlement` $\rightarrow$ увеличение баланса тенанта.
  - Начисление расхода: `Dr tenant_settlement, Cr usage_revenue` $\rightarrow$ уменьшение баланса тенанта.
- **Атомарное инкрементальное обновление:** проекция баланса в `fin_balance_projections` обновляется в той же транзакции базы данных, что и создание проводок.
- **Оптимистическая защита версий:** инкремент `version`, поддержка параметра `expected_projection_version` для предотвращения гонок и защита блокировкой строки `with_for_update()`.
- **Полная восстановимость:** метод `rebuild_projection` агрегирует историю из `fin_ledger_entries` по подтвержденным транзакциям и восстанавливает актуальный баланс и последнюю транзакцию.

### 2.4. Сервис сторнирования и коррекций (`FinReversalService`)
- Создает корректирующую транзакцию (`kind = 'reversal'`) с обязательной ссылкой `corrects_transaction_id` на исходную транзакцию.
- Формирует зеркально инвертированные проводки (новый дебет = старый кредит, новый кредит = старый дебет).
- Запрещает повторное сторнирование уже отмененной транзакции и отмену самих сторнирующих транзакций (`FinReversalError`).
- Автоматически восстанавливает баланс тенанта до исходного значения через атомарное обновление проекции.

### 2.5. Комплексная сверка и аудит (`FinReconciliationService`)
Выполняет регламентный аудит финансового ядра за указанный временной диапазон и формирует запись `FinReconciliationRun`:
1. **Ledger balance:** сверка баланса заголовка и строк всех транзакций периода (`sum(debit) == sum(credit)`).
2. **Reversal invariants:** проверка корректности ссылок на сторнируемые транзакции внутри одного тенанта.
3. **Tenant isolation:** проверка отсутствия перекрестных ссылок на чужие счета.
4. **Projection consistency & corruption detection:** расчет истинного сальдо из журнала проводок и сопоставление с кэшированной проекцией `fin_balance_projections.balance_kopecks`.
5. **Auto-rebuild:** возможность автоматического восстановления проекции при выявлении расхождений.
6. **PostgreSQL Check Constraints compliance:** строгое соблюдение проверочных констрейнтов таблицы `fin_reconciliation_runs` для статуса `matched` (`mismatch_count = 0`, `balance_difference_kopecks = 0`, `debit_kopecks = credit_kopecks`, `calculated_kopecks = posted_kopecks + discarded_kopecks`).

### 2.6. REST API эндпоинты (`app/routers/finance.py`)
- **Tenant-Facing (JWT с `org_id`):**
  - `GET /api/v1/finance/balance` — получение текущей проекции баланса (в копейках и рублях), версии и временной метки.
  - `GET /api/v1/finance/transactions` — постраничный список транзакций и проводок активного тенанта.
- **Internal / Service API (`X-Internal-Service-Key` или суперпользователь):**
  - `POST /api/internal/v1/finance/post` — проведение транзакции двойной записи.
  - `POST /api/internal/v1/finance/reversal` — сторнирование транзакции.
  - `POST /api/internal/v1/finance/rebuild-projection/{tenant_id}` — пересчет проекции из журнала проводок.
  - `POST /api/internal/v1/finance/reconciliation` — запуск регламентной сверки.
  - `GET /api/internal/v1/finance/reconciliation/runs` — история запусков сверки.

---

## 3. Верификация тестов и качества кода

### 3.1. Специализированные тесты финансового ядра
- **Файл:** `MenuBuilder/backend/tests/test_financial_core.py`
- **Результат запуска:** `20 passed in 7.67s`
- **Покрытые сценарии:**
  1. `test_ensure_system_accounts_and_tenant_settlement` — создание глобальных и тенантных счетов, идемпотентность.
  2. `test_post_payment_transaction_double_entry_and_projection` — проводка пополнения (Dr clearing, Cr settlement) и обновление проекции.
  3. `test_post_usage_charge_decrements_tenant_balance` — проводка расхода (Dr settlement, Cr revenue) и списание с баланса.
  4. `test_float_rejection` — строгий отказ при передаче float-значений на уровне Pydantic и ядра.
  5. `test_kopecks_whole_rubles_rounding_rule` — проверка кратности 100 копейкам.
  6. `test_double_entry_imbalance_rejection` — отказ при нарушении равенства дебета и кредита.
  7. `test_single_side_entry_rule` — запрет одновременного заполнения дебета и кредита в одной строке.
  8. `test_idempotent_posting_replay` — идемпотентный повтор без повторного списания и без инкремента версии.
  9. `test_duplicate_posting_conflict_detection` — выявление конфликта параметров при дублировании ключа.
  10. `test_tenant_isolation_violation_rejection` — блокировка попытки проводки на счет чужого тенанта.
  11. `test_reversal_happy_path_and_invariants` — успешное сторнирование с инверсией проводок и восстановлением баланса.
  12. `test_reversal_edge_cases_and_prohibitions` — запрет повторного сторно и запрет отмены сторно.
  13. `test_projection_rebuild_from_ledger` — восстановление испорченной проекции из журнала проводок.
  14. `test_optimistic_concurrency_version_protection` — защита от состояния гонки при несовпадении версий.
  15. `test_reconciliation_matched_happy_path` — прохождение сверки без расхождений со статусом `matched`.
  16. `test_immutability_guard` — запрет модификации проведенных строк.
  17. `test_http_api_finance_balance_and_transactions` — HTTP API чтения баланса и транзакций тенанта.
  18. `test_http_api_internal_post_and_reversal` — внутреннее API создания проводки и ее сторнирования.
  19. `test_reconciliation_corruption_detection_and_auto_rebuild` — обнаружение расхождений в проекции и авто-пересчет.
  20. `test_http_api_internal_reconciliation_and_rebuild` — внутреннее API сверки и пересчета проекции.

### 3.2. Полный регрессионный тестовый набор MenuBuilder
- **Команда:** `uv --directory MenuBuilder/backend run pytest`
- **Результат:** `376 passed in 105.42s` (0 ошибок, 0 падений).

### 3.3. Статический анализ и форматирование
- `uv run ruff check --fix app tests` $\rightarrow$ `All checks passed!`
- `uv run ruff format app tests` $\rightarrow$ `103 files left unchanged`
- `uv run pyright app tests/test_financial_core.py` $\rightarrow$ `0 errors, 0 warnings, 0 informations`

---

## 4. Развёртывание и боевой Live Smoke Evidence

Деплой выполнен на сервере `87.242.100.34`:
1. Синхронизация файлов исходного кода `MenuBuilder/backend/app` на целевой сервер и в рабочий контейнер `menubuilder-backend`.
2. Перезапуск контейнера `menubuilder-backend`:
   - Логи запуска: `Schema compatibility check PASSED: revision in ['027'], all 23 required tables present.`
   - Uvicorn успешно инициализирован на порту 8000.
3. Выполнение сквозного live smoke-теста на реальной базе данных PostgreSQL:
   - Автоматически выбран тенант `tenant_id=1` (ПЛАТЕРРА).
   - Проверены и созданы системные счета `payment_clearing` (id=1), `usage_revenue` (id=2), `tenant_settlement` (id=3).
   - Проведена синтетическая транзакция пополнения `tx_id=1` на сумму 10000 копеек (100.00 RUB). Баланс увеличился на 10000 копеек, версия выросла с 0 до 1.
   - Выполнено обязательное сторнирование синтетической транзакции: создана транзакция `rev_tx_id=2` (`kind='reversal'`, `corrects_transaction_id=1`). Баланс возвращен в 0 копеек, версия увеличилась до 2.
   - Запущен регламент сверки: создана запись `run_id=1` (`status='matched'`, `mismatches=0`, `diff=0`).
   - Проверена процедура восстановления проекции `rebuild_projection`: баланс подтвержден равным 0, версия обновлена до 3.

**Протокол боевого smoke-теста:**
```text
=== [SMOKE TEST] L4D-09-MB Financial Core Double-Entry Subledger ===
Step 1: Using tenant_id=1 (ПЛАТЕРРА)
Step 2: System accounts verified: clearing=1, revenue=2
Step 3: Tenant settlement account=3, initial_balance=0 kopecks, version=0
Step 4: Synthetic payment posted tx_id=1, new_balance=10000 kopecks, new_version=1 [OK]
Step 5: Mandatory reversal posted rev_tx_id=2, restored_balance=0 kopecks, restored_version=2 [OK]
Step 6: Reconciliation run executed run_id=1, status='matched', mismatches=0, diff=0 [OK]
Step 7: Projection rebuild verified: previous=0, true=0, version=3 [OK]
=== ALL FINANCIAL CORE SMOKE VERIFICATIONS PASSED SUCCESSFULLY (ROLLBACK COMPLETE) ===
```

---

## 5. Перечень артефактов и контрольные суммы SHA-256

| № | Артефакт | SHA-256 |
| :-: | :--- | :--- |
| 1 | `MenuBuilder/backend/app/config.py` | `a5157b6de75e801a9e8c169fb40f4b0e3af2172d5ccf9282f99bb6a0ec49ba07` |
| 2 | `MenuBuilder/backend/app/main.py` | `ea8e5f4bc9ff71ffd45d8aee6533be20fc2081304d73f6f5dfd66fcb2e0ac74a` |
| 3 | `MenuBuilder/backend/app/repositories/l4desk_repository.py` | `5cd4aa664214126f3cb8dba146d1da14400081cf551992a4a944e3d24cfabcb7` |
| 4 | `MenuBuilder/backend/app/routers/finance.py` | `2b9cfab30ae756600b413fa2b2dd2c111f22c93a712643a1d22569d33ea93974` |
| 5 | `MenuBuilder/backend/app/services/financial_core/__init__.py` | `8fb7e50c8d1b8c0da185f3daf785c821d16270ccb47a71442fbc4d01055d582b` |
| 6 | `MenuBuilder/backend/app/services/financial_core/accounts.py` | `731512c8cc5e10c65325b6f746c259f5a01cc0c15e39eaba39e612c8b060c2a8` |
| 7 | `MenuBuilder/backend/app/services/financial_core/exceptions.py` | `d075e093ca2e8c33503af5a2b16f84946dbcf7b3412f6f6a9d86e31d8fda173d` |
| 8 | `MenuBuilder/backend/app/services/financial_core/posting.py` | `e0033fcf38e1d0a704a3aabb9c8decd44bfb0533c3e8eac67e27879e3557a30b` |
| 9 | `MenuBuilder/backend/app/services/financial_core/projection.py` | `765a69fad521a893d7cd5fe0f7325f190d53645627aa609ae8e0297a71654af0` |
| 10 | `MenuBuilder/backend/app/services/financial_core/reconciliation.py` | `64169deadcc64c71187ed0b3f9131a27c36ee8e8490faddd75bbcde91f83064f` |
| 11 | `MenuBuilder/backend/app/services/financial_core/reversal.py` | `a0c39a49c132b88f159e6dcd7c5df1e733941ffe073b1b2701a93dc8bee8c7a1` |
| 12 | `MenuBuilder/backend/app/services/financial_core/schemas.py` | `6f839a54bacc139225a20164a3c22623e4e71e845fd112891a4edfb09c8984f0` |
| 13 | `MenuBuilder/backend/tests/test_financial_core.py` | `5b01420ff87ed7d42af838344e79c63b2087d41c39cc74ac404984dac4279aea` |
| 14 | `MenuBuilder/docs/l4desk/handoffs/L4D-09-MB-report.md` | *(рассчитывается побайтно после сохранения отчёта)* |

---

## 6. Отдельный кандидат handoff (DETACHED_V1)

В соответствии с §9 `PROMPT-STANDARD.md` окончательный candidate-блок вынесен из отчёта во избежание циклической зависимости хеша и размещен в отдельном файле:  
`MenuBuilder/docs/l4desk/handoffs/L4D-09-MB-candidate.md`.
