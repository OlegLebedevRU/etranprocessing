# Handoff Report: Подключить IoT Contract Consumer v1 (L4D-03-MB)

## Candidate H-L4D-03-MB-v1

<!-- HANDOFF:H-L4D-03-MB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-03-MB-v1
status: ACCEPTED
contract_kinds:
  - REPORT
  - DEPLOYMENT
producer_prompt_id: L4D-03-MB
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-03-MB-report.md
producer_branch: l4desk/l4d-03-mb
producer_commit: 4da76eca24bab856bdad764df6e6a1dc9de44f1c
accepted_at_utc: 2026-09-17T22:20:00Z
contract_version: 1.0.0
schema_revision: 2026-09-17-v1
artifact_version: 1.0.0
artifact_paths:
  - MenuBuilder/backend/tests/fixtures/iot_event_feed_examples_v1.json
  - MenuBuilder/backend/tests/test_iot_event_feed_consumer.py
artifact_sha256:
  - 1fafb1d27010917f43f5d36502cbfaa1decd5cce36c80a6dc397f4180556df2b
  - 6664cc17db932fd4c84566c197cd8182a7521bb73589256a78fd3fa530ca9589
consumed_contracts:
  - handoff_id: H-L4D-02-IOT-v1
    contract_id: iot_event_feed_contract_v1
    contract_version: 1.0.0
    schema_revision: 2026-09-17-v1
    producer: iot-rpc-rest-app
    artifacts:
      - path: docs/l4desk/contracts/iot_event_feed_contract_v1.json
        sha256: 7acd49cb4d761a074e6e41f04380bef5db78d465fa8f6417bdf1fb16167e46c3
      - path: docs/l4desk/contracts/schemas/iot_event_feed_openapi.json
        sha256: 07b0b3e4e54b96e08ffc716b00f9e1a4dabe0f0ba90ca09708485cf068ccba56
      - path: docs/l4desk/contracts/schemas/remote_session_event.schema.json
        sha256: 230a22727a493b2980ba85cb2735e50d5ac42ac9b110cf3a6f3ba03ea0ebb12b
      - path: docs/l4desk/contracts/schemas/remote_session.schema.json
        sha256: 1de26a6fc47ebbe1d97d2f93108c3760ebf4a742908825c7d73e5a905da18f36
      - path: docs/l4desk/fixtures/iot_event_feed_examples_v1.json
        sha256: 1fafb1d27010917f43f5d36502cbfaa1decd5cce36c80a6dc397f4180556df2b
compatibility:
  backward_compatible_with:
    - 0.1.0
    - 0.2.0
  breaking_changes: false
  notes: Versioned IoT event feed consumer v1 connected strictly in MenuBuilder scope. Zero financial mutations or alternative session state; technical projections stored in idempotent inbox. Monotonic cursor checkpointing with lag observability and quarantine for contract violations. Dark consumer deployed with iot_consumer_enabled=false and shadow mode active.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  iot_consumer_enabled: false
  iot_consumer_shadow_mode: true
contract_payload:
  identifiers:
    consumer_id: "menubuilder_iot_event_consumer"
    cursor_type: "int64 (BIGSERIAL monotonic, strictly positive)"
    event_id_pattern: "^evt_[a-z0-9_]+$"
    session_id_pattern: "^sess-(console|video)-[a-z0-9-]+$"
  models:
    checkpoint_model: "IotConsumerCheckpoint (table: iot_consumer_checkpoints, last_cursor: BigInteger)"
    inbox_model: "IotEventInbox (table: iot_event_inbox, event_id: primary key)"
    quarantine_model: "IotEventQuarantine (table: iot_event_quarantine, quarantine_id: primary key, error_code: String)"
  endpoints:
    - method: GET
      path: "/api/internal/v1/iot-consumer/status"
      auth: "Bearer superuser or X-Internal-Service-Key"
    - method: POST
      path: "/api/internal/v1/iot-consumer/poll"
      auth: "Bearer superuser or X-Internal-Service-Key"
  invariants:
    - "Strict scope isolation: MenuBuilder does not mutate financial ledger, balances, or license entitlements"
    - "Dark consumer defaults: iot_consumer_enabled=false, iot_consumer_shadow_mode=true"
    - "Monotonic cursor tracking: checkpoint cursor advances only upon contiguous, error-free event consumption"
    - "Idempotent inbox: duplicate events suppressed by event_id without cursor regression"
    - "Strict quarantine: contract-violating payloads or out-of-order anomalies quarantined without blocking valid stream"
    - "Zero alternative flows: remote session telemetry feeds solely into inbox/projection tables"
supersedes:
  - H-L4D-00E-MB-v1
known_risks:
  - "Consumer relies on polling /api/internal/v1/remote-session-events; real-time latency bounded by poll_interval_seconds (default 5.0s)"
  - "Dark consumer is disabled by default (iot_consumer_enabled=false) and in shadow mode (iot_consumer_shadow_mode=true) until L4D-04C-MB activation"
  - "Storage schema migrations in MenuBuilder use idempotent DDL on startup; central Alembic migrations remain in ProcessingBackend"
consumers:
  - L4D-04A-SHARED
next_prompt_id: L4D-04A-SHARED
```
<!-- HANDOFF:H-L4D-03-MB-v1:END -->

---

## 1. Резюме шага и контекст выполнения

Шаг `L4D-03-MB` выполнен строго в рамках изолированного репозиторного каталога `MenuBuilder` без чтения репозитория IoT (`iot-rpc-rest-app`), брокеров RabbitMQ или протоколов MQTT.
Цель шага — подключить в `MenuBuilder` consumer принятого IoT event-feed contract v1 (`iot_event_feed_contract_v1`, версия `1.0.0`), обеспечить безопасное версионированное взаимодействие, монотонный cursor checkpoint, идемпотентный inbox по `event_id`, транзакционную обработку, карантин для контрактных ошибок и наблюдаемость lag без создания начислений или финансовой политики.

- **Ветка:** `l4desk/l4d-03-mb`
- **Проект (Scope):** `MenuBuilder` (`D:\repo\platerra\Public\etranprocessing\MenuBuilder`)
- **Статус выполнения:** `ACCEPTED`
- **Кодовый коммит:** `4da76eca24bab856bdad764df6e6a1dc9de44f1c`
- **Развертывание:** Production сервер `87.242.100.34`, контейнер `menubuilder-backend` (`Up`, status `200` OK, dark consumer flag `iot_consumer_enabled = false`).

---

## 2. Contract Gate & Проверка входных контрактов

1. **Входной контракт:** `H-L4D-02-IOT-v1`
   - Статус: `ACCEPTED` в `l4desk-service/docs/prompts/contract-handoff.md`
   - Провайдер: `L4D-02-IOT` (`iot-rpc-rest-app`)
   - Потребитель: `L4D-03-MB` (`MenuBuilder`) — текущий шаг авторизован.
   - Версия контракта: `1.0.0`
   - Ревизия схемы: `2026-09-17-v1`
2. **Верификация контрольных сумм SHA-256 артефактов контракта:**
   - `docs/l4desk/contracts/iot_event_feed_contract_v1.json`: `7acd49cb4d761a074e6e41f04380bef5db78d465fa8f6417bdf1fb16167e46c3` — **MATCH**
   - `docs/l4desk/contracts/schemas/iot_event_feed_openapi.json`: `07b0b3e4e54b96e08ffc716b00f9e1a4dabe0f0ba90ca09708485cf068ccba56` — **MATCH**
   - `docs/l4desk/contracts/schemas/remote_session_event.schema.json`: `230a22727a493b2980ba85cb2735e50d5ac42ac9b110cf3a6f3ba03ea0ebb12b` — **MATCH**
   - `docs/l4desk/contracts/schemas/remote_session.schema.json`: `1de26a6fc47ebbe1d97d2f93108c3760ebf4a742908825c7d73e5a905da18f36` — **MATCH**
   - `docs/l4desk/fixtures/iot_event_feed_examples_v1.json`: `1fafb1d27010917f43f5d36502cbfaa1decd5cce36c80a6dc397f4180556df2b` — **MATCH**
   Все 5 контрольных сумм совпадают бит-в-бит с утвержденным handoff-блоком.

---

## 3. Архитектура и реализация Consumer v1

### 3.1. Конфигурация без секретных default (`app/config.py`)
Добавлены параметры управления IoT Consumer в класс `Settings`:
- `iot_event_feed_base_url: str = ""` (без дефолтного хоста/секрета; fallback на `internal_api_base_url`)
- `iot_event_feed_service_token: str = ""` (без дефолтного секрета; fallback на `internal_service_key_value`)
- `iot_event_feed_timeout_seconds: float = 10.0`
- `iot_event_feed_max_retries: int = 3`
- `iot_event_feed_retry_backoff_sec: float = 0.5`
- `iot_consumer_enabled: bool = False` — флаг dark consumer: по умолчанию выключен
- `iot_consumer_shadow_mode: bool = True` — shadow flag: прием и сохранение только технических проекций без модификации коммерческих правил
- `iot_consumer_poll_interval_sec: float = 5.0`
- `iot_consumer_batch_size: int = 100`
- `iot_consumer_id: str = "menubuilder_iot_event_consumer"`

### 3.2. Версионированный асинхронный клиент (`app/services/iot_event_feed_client.py`)
- Класс `IotEventFeedClient`:
  - Строгое соответствие `iot_event_feed_contract_v1` (версия `1.0.0`, ревизия `2026-09-17-v1`).
  - Межсервисная аутентификация: заголовки `X-Internal-Service-Key` и `Authorization: Bearer <token>`.
  - Заголовок `X-Contract-Version: 1.0.0`.
  - Эндпоинты:
    - `GET /api/internal/v1/remote-session-events` (параметры `after`, `limit`, `tenant_id`, `sn`, `session_id`, `event_type`).
    - `GET /api/internal/v1/remote-session-events/reconciliation` (параметры `tenant_id`, `from_cursor`, `to_cursor`, `from_time`, `to_time`).
    - `POST /api/internal/v1/remote-sessions` (идемпотентный запуск по `operation_id`).
    - `GET /api/internal/v1/remote-sessions/{session_id}`.
    - `POST /api/internal/v1/remote-sessions/{session_id}/stop`.
- Ограниченные повторные попытки (bounded retries):
  - Повторяются только сетевые сбои (`ConnectTimeout`, `ReadTimeout`, `NetworkError`) и серверные ошибки 5xx (`500`, `502`, `503`, `504`) с экспоненциальной задержкой.
  - Клиентские ошибки 4xx (`400`, `401`, `403`, `404`, `422`) повторно **не** запрашиваются.
- Явный маппинг ошибок по контракту:
  - `IotEventFeedAuthError` (401, 403)
  - `IotEventFeedValidationError` (400, 422)
  - `IotEventFeedNotFoundError` (404)
  - `IotEventFeedServerError` (500–599)
  - `IotEventFeedNetworkError` / `IotEventFeedTimeoutError`
  - `IotEventFeedContractError` (нарушение JSON/схемы)

### 3.3. Модели хранения и семантика Checkpoint/Inbox (`app/models_iot_consumer.py`)
Декларативные модели SQLAlchemy 2.0:
1. `IotConsumerCheckpoint` (`iot_consumer_checkpoints`):
   - `consumer_id` (PK, `String(64)`)
   - `feed_name` (`String(64)`)
   - `last_cursor` (`BigInteger`, монотонно возрастающий)
   - `last_event_id` (`String(128)`, nullable)
   - `last_event_occurred_at` (`DateTime(timezone=True)`, nullable)
   - `updated_at` (`DateTime(timezone=True)`)
2. `IotEventInbox` (`iot_event_inbox`):
   - `event_id` (PK, `String(128)`) — дедупликация на уровне ключа таблицы
   - `cursor` (`BigInteger`, index)
   - `event_type`, `event_version`, `occurred_at`, `tenant_id`, `terminal_id`, `device_id`, `sn`, `session_id`, `session_type`, `lifecycle_state`, `reason`, `operation_id`, `correlation_id`, `payload`, `received_at`, `processed_at`, `status`
3. `IotEventQuarantine` (`iot_event_quarantine`):
   - `id` (PK autoincrement)
   - `event_id`, `cursor`, `error_code`, `error_detail`, `raw_event`, `quarantined_at`, `retry_count`, `resolved`

### 3.4. Уровень абстракции хранилища (`app/services/iot_consumer_storage.py`)
Интерфейс `IotConsumerStorage` реализован в двух адаптерах:
- `DatabaseIotConsumerStorage`: работа с PostgreSQL через `AsyncSession`, создание таблиц при отсутствии через `Base.metadata.create_all`, безопасный fallback при сбоях подключения.
- `InMemoryIotConsumerStorage`: изолированное быстрое in-memory хранилище для unit/contract тестов.

### 3.5. Процессор событий и политика карантина (`app/services/iot_event_consumer.py`)
- Монотонный прогресс: при обработке событий cursor продвигается строго вперед (`item.cursor > last_cursor`).
- Идемпотентность по `event_id`: если событие с данным `event_id` уже сохранено в `iot_event_inbox`, оно повторно не сохраняется, а фиксируется как duplicate delivery без сдвига курсора назад.
- Политика карантина: в карантин помещаются **только контрактно определённые ошибки**:
  1. `CONTRACT_VALIDATION_FAILED`: нарушение схемы, отсутствие обязательных полей (`event_id`, `sn`, `cursor <= 0`) или попытка инъекции коммерческих полей (`amount`, `rubles`, `kopecks`, `balance`, `price`, `tariff`, `billing_id`) в payload события.
  2. `OUT_OF_ORDER_CURSOR`: нарушение монотонности курсора (приход неизвестного `event_id` с `cursor <= last_cursor` или немонотонный порядок событий внутри одного батча).
- Транзакционность: фиксация события в inbox и обновление checkpoint выполняются атомарно.

### 3.6. Наблюдаемость и расчет Lag (`app/routers/iot_consumer.py`)
- Метрики:
  - `checkpoint_cursor`: текущий зафиксированный курсор MenuBuilder.
  - `remote_latest_cursor`: максимальный курсор, известный от источника событий.
  - `cursor_lag`: `max(0, remote_latest_cursor - checkpoint_cursor)`.
  - `time_lag_seconds`: разница между текущим временем и `occurred_at` последнего обработанного события.
  - Счетчики: `total_processed`, `total_duplicates`, `total_quarantined`, `inbox_count`, `quarantine_count`.
- Эндпоинты диагностики:
  - `GET /api/internal/v1/iot-consumer/status` (авторизация по `X-Internal-Service-Key` или superuser Bearer token).
  - `POST /api/internal/v1/iot-consumer/poll` (ручной запуск итерации опроса).

---

## 4. Верификация: Linting, Type Check и Тесты

Все проверки выполнены локально в соответствии с требованиями раздела 4 Project Guidelines:

1. **Backend Linting (Ruff)**:
   - Команда: `uv run ruff check app tests`
   - Результат: **PASSED** (0 ошибок, 0 предупреждений).
2. **Backend Formatting (Ruff format)**:
   - Команда: `uv run ruff format --check app tests`
   - Результат: **PASSED** (все 78 файлов форматированы).
3. **Backend Type Checking (Pyright)**:
   - Команда: `uv run pyright app`
   - Результат: **PASSED** (0 errors, 0 warnings, 0 informations).
4. **Consumer Contract Tests (Pytest)**:
   - Команда: `uv run pytest -v tests/test_iot_event_feed_consumer.py`
   - Результат: **14 passed** за 3.58s.
   - Покрытие сценариев:
     - `test_client_auth_error_mapping`: маппинг 403 Forbidden в `IotEventFeedAuthError`.
     - `test_client_validation_and_not_found_errors`: маппинг 400 и 404.
     - `test_client_server_error_and_bounded_retry`: 3 попытки при 502 Bad Gateway с последующей ошибкой.
     - `test_client_timeout_and_bounded_retry`: 3 попытки при ReadTimeout с последующим `IotEventFeedTimeoutError`.
     - `test_consumer_empty_feed`: пустой ответ feed, отсутствие смещения курсора, lag=0.
     - `test_consumer_duplicate_page`: повторная доставка одной страницы с 3 событиями, идемпотентная дедупликация, duplicate_count=3, inbox=3.
     - `test_consumer_duplicate_event_in_batch`: дедупликация дубликата внутри одного батча.
     - `test_consumer_resume_from_checkpoint`: возобновление чтения строго с сохраненного курсора (`after=3`), загрузка событий 4..9.
     - `test_consumer_pagination`: постраничное чтение 3 страниц по 3 события через `poll_until_caught_up`, итоговый курсор 9.
     - `test_consumer_out_of_order_rejection`: изоляция и отправка в карантин события с `cursor <= checkpoint`.
     - `test_consumer_commercial_field_quarantine`: изоляция события при наличии в payload запрещенного поля `rubles`.
     - `test_consumer_restart_resilience`: симуляция падения и рестарта воркера с сохранением состояния через общий storage.
     - `test_reconciliation_response_parsing`: валидация модели `ReconciliationResponse` по fixture.
     - `test_consumer_status_api_endpoint`: авторизация и валидация схемы эндпоинта статуса/метрик.
5. **Регрессионные тесты смежных модулей MenuBuilder**:
   - `test_monitoring.py`, `test_settings.py`, `test_integrations_api.py`, `test_step4_video_contracts.py`, `test_step6_quick_actions.py`: **49 passed**, 0 failed.

---

## 5. Развертывание на Production (87.242.100.34)

Развертывание выполнено на утвержденный production host `87.242.100.34`:
- **Хост:** `user1@87.242.100.34` (SSH-ключ `d:\.ssh\id_ed25519`).
- **Транспорт артефактов:** SCP файлов из `MenuBuilder/backend/app/` в `/home/user1/MenuBuilder/backend/app/`.
- **Сборка и перезапуск:**
  `sudo docker compose build menubuilder-backend && sudo docker compose up -d menubuilder-backend`
- **Состояние контейнера:**
  - Container ID: `7c16d2a0968f01d9d01e461fdcc95508d9c1bc3918666c7c582323444aad38b7`
  - Image ID: `sha256:0fe13c6c895912d52f7156909b0278c08a3a6b84f43bd057529499dc11189bb7`
  - Статус: `Up` (running)
  - Логи: `Uvicorn running on http://0.0.0.0:8000`
- **Smoke Probes:**
  - `GET /api/internal/v1/iot-consumer/status`: возвращает `403 Forbidden` для неавторизованных запросов.
  - Внутренняя проверка метрик через python в контейнере:
    `{"consumer_id": "menubuilder_iot_event_consumer", "enabled": false, "shadow_mode": true, "checkpoint_cursor": 0, "remote_latest_cursor": 0, "cursor_lag": 0, "last_event_id": null, "total_processed": 0, "total_duplicates": 0, "total_quarantined": 0, "inbox_count": 0, "quarantine_count": 0, "last_error": null}`
  - `GET /api/auth/me`: возвращает `401 Unauthorized` (существующее поведение не нарушено).
  - Влияние на пользователей: **нулевое** (consumer выключен флагом `iot_consumer_enabled = false`).

---

## 6. Стратегия отката (Rollback)

1. **Мгновенный откат без деплоя:**
   Если консьюмер был активирован переменной окружения `IOT_CONSUMER_ENABLED=true`, отключить его, выставив `IOT_CONSUMER_ENABLED=false` в `/home/user1/MenuBuilder/backend/.env` и перезапустив контейнер `sudo docker restart menubuilder-backend`.
2. **Кодовый откат:**
   - Выполнить `git revert 4da76eca24bab856bdad764df6e6a1dc9de44f1c`.
   - Скопировать предыдущую версию `app/` на сервер и пересобрать контейнер.

---

## 7. Следующие шаги каскада

Согласно реестру промптов (`l4desk-service/docs/l4desk-architecture.md`, раздел 18):
- Следующий шаг: `L4D-04A-SHARED` — `shared/etranprocessing_db`: декларативные модели финансового сабледжера L4Desk (fin_accounts, fin_ledger, fin_usage_daily, fin_terminal_months).
