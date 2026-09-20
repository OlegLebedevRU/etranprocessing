# Handoff Report: L4D-12-MB — Entitlement, Grace & Idempotent Notifications

**Prompt ID:** `L4D-12-MB`  
**Prompt Type:** `entitlement-implementation`  
**Scope Project:** `MenuBuilder`  
**Scope Root:** `D:\repo\platerra\Public\etranprocessing\MenuBuilder`  
**Required Handoff IDs:** `[H-L4D-11-MB-v1, H-L4D-10-MB-v1, H-L4D-07-IOT-v1, H-L4D-08B-MB-v1]`  
**Output Handoff ID:** `H-L4D-12-MB-v1`  
**Next Prompt ID:** `L4D-13-MB`  
**Branch:** `l4desk/l4d-12-mb`  
**Producer Commit:** `95ba8b7915c1d9e34e76169706b5aa862c2e9954`  
**Report Path:** `MenuBuilder/docs/l4desk/handoffs/L4D-12-MB-report.md`  
**Candidate Path:** `MenuBuilder/docs/l4desk/handoffs/L4D-12-MB-candidate.md`  
**Architecture Sections:** `[2, 3, 5, 6, 8, 9, 10, 13, 14, 15, 16, 17]`  

---

## 1. Резюме выполнения задания и нормативная логика

В рамках спецификации `L4D-12-MB` и архитектурных разделов L4Desk в проекте `MenuBuilder` реализованы коммерческое решение start/continue/stop, cycle-bound grace, механизм stop outbox с повторными попытками и идемпотентные email-уведомления:

1. **До первой успешной оплаты (Free Tier)**:
   - Доступны только единственный бесплатный терминал (`FinTerminalService.is_terminal_free`) и его суточная квота `120 min/local day` (7200 сек).
   - Вторичные терминалы немедленно блокируются (`unpaid_secondary_terminal`).
   - Льготный период (grace) отсутствует (`grace_deadline = None`).
   - При исчерпании 120 минут работа продолжается платно только при положительном балансе лицевого счёта (`balance > 0`); при нулевом или отрицательном балансе сессия завершается с причиной `free_quota_exceeded`.
2. **После первой оплаты (Active, Grace, Blocked)**:
   - Статус определяется балансом и календарной границей цикла:
     - `active`: при `balance >= 0`.
     - `grace`: при `balance < 0` и `now < cycle_start + 3 days` (в локальной таймзоне тенанта).
     - `blocked`: при `balance < 0` и `now >= cycle_start + 3 days`.
3. **Строгая привязка Grace к границе цикла (Cycle Boundary)**:
   - Grace всегда привязан к дате начала текущего цикла (`cycle_start + 3 days`), а не к моментам online, начисления или платежей.
   - Выход терминала online на 5-й день месяца создаёт charge текущего цикла и при недостатке средств немедленно переводит статус в `blocked` (без повторного предоставления grace).
4. **Поздняя оплата и иммунитет якоря**:
   - Поздняя оплата погашает задолженность текущего периода и не переносит существующий `anchor_at`.
   - При восстановлении баланса до `balance >= 0` тенант немедленно разблокируется (`active`).
5. **Остановка сессий при блокировке (Stop Outbox & IoT Execution)**:
   - При `blocked` старт новых сессий запрещён (`HTTP 403`, `entitlement_blocked`).
   - Активные видео-сессии немедленно останавливаются через вызов остановки медиапотока (`stop_remote_session`).
   - Консольные сессии останавливаются в соответствии с контрактом `H-L4D-07-IOT-v1` (`command_aware_wait_or_timeout_new_commands_rejected`): новые команды отклоняются, сервис ожидает ответ текущей команды или ограниченный таймаут, после чего сессия закрывается.
   - Механизм **Stop Outbox** сохраняет статус `stop_requested` в базе данных при сбоях IoT-провайдера и выполняет повторные попытки (`process_stop_outbox`) до успешного подтверждения.
6. **Идемпотентные Email-уведомления**:
   - Уведомления `renewal-7d` (`cycle_minus_7`), `renewal-3d` (`cycle_minus_3`), `renewal-1d` (`cycle_minus_1`), `grace_started` (`grace`), `blocked` (`blocked`) строго уникальны по составному ключу `(tenant_id, billing_cycle_id, notification_type)`.
   - Повторные прогоны воркера или сетевые сбои не создают дубликатов в БД и почтовом ящике.
   - Учитываются таймзоны и переходы DST при расчёте дедлайнов.

---

## 2. Реализованные сервисы и компоненты (`MenuBuilder`)

### 2.1. Сервис Entitlement State Machine (`entitlement.py`)
- **Файл:** `app/services/financial_core/entitlement.py`.
- **Класс `FinEntitlementService`**:
  - `get_tenant_entitlement_status(db, tenant_id, as_of)`: авторитетное вычисление состояния тенанта (`free`, `active`, `grace`, `blocked`), проекции баланса, якорного дня, суточного использования квоты и дедлайна grace. При смене статуса атомарно обновляет `profile.entitlement` и `profile.entitlement_changed_at`.
  - `evaluate_session_request(db, tenant_id, terminal_id, session_type, user, as_of)`: контрактная оценка возможности запуска или продолжения сессии. Возвращает нормативные reason codes: `entitlement_blocked`, `free_quota_exceeded`, `unpaid_secondary_terminal`, `payment_required`.

### 2.2. Идемпотентный сервис уведомлений (`notifications.py`)
- **Файл:** `app/services/financial_core/notifications.py`.
- **Класс `FinNotificationService`**:
  - `schedule_notification_if_missing(db, tenant_id, billing_cycle_id, notification_type, scheduled_at)`: гарантирует уникальность записи в `fin_notification_deliveries`.
  - `check_and_schedule_cycle_notifications(db, tenant_id, as_of)`: проверяет границы цикла (`-7d`, `-3d`, `-1d`) и переходы статусов (`grace`, `blocked`).
  - `dispatch_pending_notifications(db, email_client, as_of)`: отправка неотправленных писем владельцу тенанта через `ServerlessEmailClient` с инкрементом попыток, регистрацией audit events и обработкой сбоев провайдера.

### 2.3. Stop Outbox и координатор остановки сессий (`stop_outbox.py`)
- **Файл:** `app/services/financial_core/stop_outbox.py`.
- **Класс `FinStopOutboxService`**:
  - `stop_sessions_for_blocked_tenant(db, tenant_id, iot_adapter, actor)`: перевод всех активных сессий тенанта в `stop_requested` и вызов `stop_remote_session`. При успехе переводит в `closed`, при сбое оставляет в outbox.
  - `process_stop_outbox(db, iot_adapter, actor)`: фоновый повтор подвисших запросов на остановку с логированием audit events.

### 2.4. Периодический воркер (`worker.py`)
- **Файл:** `app/services/financial_core/worker.py`.
- **Класс `FinEntitlementWorker`**:
  - `run_single_tick(db, as_of)`: атомарный цикл оценки всех активных тенантов, проверки границ циклов, генерации уведомлений, остановки заблокированных сессий и диспетчеризации Stop Outbox.
  - `run_worker()`: фоновый цикл выполнения с умеренным интервалом (`l4desk_entitlement_worker_interval_sec`).
  - Интеграция в `lifespan` приложения `app/main.py` под управлением флага `l4desk_entitlement_worker_enabled`.

### 2.5. Интеграция с Policy Seam 08B (`remote_session_policy.py`)
- **Файл:** `app/services/remote_session_policy.py`.
- **Класс `L4DeskEntitlementPolicy`**:
  - Подключение к `FinEntitlementService.evaluate_session_request`.
  - Поддержка двух режимов:
    - **Shadow Mode** (`l4desk_policy_enforcement_enabled = False`): фиксация решений и warning-логов без блокировки сессий пользователей.
    - **Enforced Mode** (`l4desk_policy_enforcement_enabled = True`): строгое коммерческое исполнение блокировок.

### 2.6. REST API Endpoints (`finance.py`)
- **Файл:** `app/routers/finance.py`:
  - `GET /api/v1/finance/entitlement` — получение статуса entitlement, баланса, дедлайна grace и reason codes текущего тенанта.
  - `GET /api/v1/finance/notifications` — история уведомлений текущего тенанта.
  - `GET /api/internal/v1/finance/entitlement/{tenant_id}` — просмотр статуса entitlement любого тенанта (superuser).
  - `POST /api/internal/v1/finance/entitlement/worker/tick` — ручной запуск такта воркера (superuser / cron).
  - `POST /api/internal/v1/finance/stop-outbox/process` — ручной запуск обработки Stop Outbox (superuser / retry).
  - `GET /api/internal/v1/finance/notifications` — глобальный реестр уведомлений по всем тенантам с фильтрацией (superuser).

---

## 3. Конфигурационные флаги (`MenuBuilder/backend/app/config.py`)

| Параметр | Тип | Значение по умолчанию | Описание |
| :--- | :--- | :--- | :--- |
| `L4DESK_POLICY_ENFORCEMENT_ENABLED` | `bool` | `False` | Флаг жесткого применения блокировок (false = shadow/disabled) |
| `L4DESK_POLICY_SHADOW_MODE` | `bool` | `True` | Логирование решений entitlement без прерывания сессий |
| `L4DESK_ENTITLEMENT_WORKER_ENABLED` | `bool` | `False` | Фоновый воркер границ циклов и grace в lifespan |
| `L4DESK_ENTITLEMENT_WORKER_INTERVAL_SEC`| `float`| `60.0` | Интервал такта фонового воркера (сек) |
| `L4DESK_STOP_OUTBOX_MAX_RETRIES` | `int` | `5` | Максимальное число попыток Stop Outbox |
| `L4DESK_STOP_OUTBOX_RETRY_INTERVAL_SEC` | `float`| `10.0` | Интервал повторных попыток Stop Outbox |
| `L4DESK_EMAIL_NOTIFICATIONS_ENABLED` | `bool` | `True` | Флаг отправки email-уведомлений |
| `L4DESK_NOTIFICATION_MAX_RETRIES` | `int` | `3` | Максимальное число попыток отправки email |

---

## 4. Результаты тестирования и верификации

### 4.1. Специализированный тестовый набор (`test_l4d_12_entitlement_grace_and_notifications.py`)
- **Результат:** `11 passed in 3.63s`
- **Проверенные сценарии:**
  1. `test_no_payment_free_quota_and_secondary_terminal_block` — блокировка вторичного терминала, доступ к основному терминалу до 120 минут, блокировка после исчерпания 120 минут при 0 балансе, отсутствие grace на бесплатном тарифе.
  2. `test_positive_paid_continuation_after_free_quota` — продолжение работы на платном тарифе при исчерпании квоты и положительном балансе.
  3. `test_all_cycle_and_grace_boundaries` — смена состояний: `active` $\rightarrow$ `grace` (отрицательный баланс до 3 дней) $\rightarrow$ `blocked` (после 3 дней grace).
  4. `test_online_after_deadline_creates_charge_and_immediate_blocked` — online на 5-й день создает списание и при нехватке баланса немедленно блокирует тенанта без повторного grace.
  5. `test_late_payment_preserves_anchor_and_unblocks` — поздняя оплата не смещает `anchor_at` и восстанавливает статус `active` при балансе >= 0.
  6. `test_active_session_stop_on_blocked` — остановка активных видео и консольных сессий заблокированного тенанта с вызовом IoT stop.
  7. `test_stop_outbox_retry_on_provider_failure` — удержание сессии в `stop_requested` при ошибке провайдера и успешное закрытие при повторе через `process_stop_outbox`.
  8. `test_email_notifications_idempotency_and_boundaries` — планирование уведомлений `-7d`, `-3d`, `-1d`, `grace`, отсутствие дубликатов при повторных проверках и отправках.
  9. `test_email_provider_failure_and_retry_resilience` — фиксация попыток и статуса `failed` при исчерпании лимита попыток отправки писем.
  10. `test_remote_session_policy_shadow_vs_enforced_mode` — валидация работы `L4DeskEntitlementPolicy` в shadow режиме (разрешает с предупреждением) и в enforced режиме (блокирует).
  11. `test_http_endpoints_and_worker_tick` — сквозное тестирование HTTP REST API (entitlement, notifications, stop outbox, worker single tick).

### 4.2. Полный регрессионный тестовый набор
- **Команда:** `uv run --directory MenuBuilder/backend pytest`
- **Результат:** `411 passed in 79.46s` (0 ошибок, 0 регрессий).

### 4.3. Статический анализ и форматирование кода
- `uv run --directory MenuBuilder/backend ruff check app tests` $\rightarrow$ `All checks passed!`
- `uv run --directory MenuBuilder/backend ruff format app tests` $\rightarrow$ `114 files already formatted`
- `uv run --directory MenuBuilder/backend pyright app` $\rightarrow$ `0 errors, 0 warnings, 0 informations`

---

## 5. Развертывание и Live Smoke Evidence

Деплой выполнен на боевой сервер `87.242.100.34`:
1. Синхронизация файлов бэкенда в `/home/user1/MenuBuilder/backend/app/` и контейнер `menubuilder-backend`.
2. Перезапуск контейнера:
   - Лог: `Schema compatibility check PASSED: revision in ['027'], all 23 required tables present.`
   - Uvicorn инициализирован на порту 8000.
3. Запуск live smoke-проверки на реальной базе данных PostgreSQL:
   ```text
   === [LIVE SMOKE TEST] L4D-12-MB Entitlement, Grace & Notifications ===
   Step 1: Entitlement worker tick: {
     "as_of": "2026-09-20T17:37:19.073993+00:00",
     "tenants_evaluated": 0,
     "tenants_blocked": 0,
     "notifications_scheduled": 0,
     "notifications_dispatched": 0,
     "sessions_stopped": 0,
     "outbox_retries_processed": 0
   }
   Step 2: Stop outbox retry check: pending_processed = 0
   Step 3: Notification deliveries dispatched = 0
   === ALL LIVE SMOKE CHECKS PASSED SUCCESSFULLY ===
   ```

---

## 6. Перечень артефактов и контрольные суммы SHA-256

| № | Артефакт | SHA-256 |
| :-: | :--- | :--- |
| 1 | `MenuBuilder/backend/app/config.py` | `71ea749442cb9c791863fc15e86633d49c3d36d87064ef9cc677b6254686f018` |
| 2 | `MenuBuilder/backend/app/main.py` | `a57b8afddfbaac01772f3f05dbae4b7892a864bcf24283eddb3ce1cad1e3f091` |
| 3 | `MenuBuilder/backend/app/repositories/l4desk_repository.py` | `d6b7c1421174016861b75375483463b74eb24336686c9f4da76922d927ec38b0` |
| 4 | `MenuBuilder/backend/app/routers/finance.py` | `5b0bf751e3149681624ae213cc2ccd4747f55093c76cf1a3ab5c8770731938b1` |
| 5 | `MenuBuilder/backend/app/routers/video_control.py` | `08af1e5fcd9f7e24ac4aba869dcc84eccfb16b304ff2026f89495d1785d2e7c1` |
| 6 | `MenuBuilder/backend/app/services/financial_core/__init__.py` | `1e7d2698992f0ecbaf07698279d880731b53bb10372e8096bb89e8fd831e03a1` |
| 7 | `MenuBuilder/backend/app/services/financial_core/entitlement.py` | `c8928efcd52530c71975bbe9b009e5841b8026d0222ee9d1af114d2003976f57` |
| 8 | `MenuBuilder/backend/app/services/financial_core/notifications.py` | `fd414844d5f26a86607057e947f8a87aa54904deee81f6262f5b3266f8e6759b` |
| 9 | `MenuBuilder/backend/app/services/financial_core/schemas.py` | `8438eda2f13c5cc9729a3398e15390a2453f2cdc55e992f1b3b20db12401b468` |
| 10 | `MenuBuilder/backend/app/services/financial_core/stop_outbox.py` | `45119132562a10f1eac9e017c54600c751ff015e67806e1c23b73a6989c127d4` |
| 11 | `MenuBuilder/backend/app/services/financial_core/worker.py` | `d4e485c04a30af8471fb095476719dcd15169de91945ed232a696acb186e13d3` |
| 12 | `MenuBuilder/backend/app/services/remote_session_policy.py` | `1778f36290937ef239415a55d8581ca812c3e6517b7e6186a90ab6100fa9574e` |
| 13 | `MenuBuilder/backend/tests/test_l4d_12_entitlement_grace_and_notifications.py` | `560ae3a021a1ac0943d02f0acfac8091cd14adae75ad175dac0f4ba9d54fbfbd` |
| 14 | `MenuBuilder/docs/l4desk/handoffs/L4D-12-MB-report.md` | *(рассчитывается побайтно после сохранения отчёта)* |

---

## 7. Отдельный кандидат handoff (DETACHED_V1)

В соответствии с §9 `PROMPT-STANDARD.md` окончательный candidate-блок вынесен из отчёта во избежание циклической зависимости хеша и размещен в отдельном файле:  
`MenuBuilder/docs/l4desk/handoffs/L4D-12-MB-candidate.md`.
