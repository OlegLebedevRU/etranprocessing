# Handoff Report: Укрепление On-Demand Media Lifecycle Contract (L4D-08A-MEDIA)

## Candidate H-L4D-08A-MEDIA-v1

```yaml
<!-- HANDOFF:H-L4D-08A-MEDIA-v1:BEGIN -->
handoff_id: H-L4D-08A-MEDIA-v1
status: CANDIDATE
contract_kinds:
  - API
  - DEPLOYMENT
producer_prompt_id: L4D-08A-MEDIA
producer_scope_project: l4media
producer_report_path: l4media/docs/l4desk/handoffs/L4D-08A-MEDIA-report.md
producer_branch: l4desk/l4d-08a-media
producer_commit: 37adfd01e5492e6b61e8ecb243579389ae2d858e
created_at_utc: 2026-09-20T08:35:00Z
contract_version: 1.0.0
schema_revision: 1.0.0
artifact_version: 1.0.0
artifact_paths:
  - l4media/ingress/openapi.json
artifact_sha256:
  - ba2b4a19fd5c568a4b758b57121b01bf3062533906fcbd9e55991fc59157fcec
compatibility:
  backward_compatible_with:
    - H-L4D-00D-MEDIA-v1
  breaking_changes: false
  notes: Additive service-authenticated on-demand media session lifecycle API on l4media-ingress port 9100. Retains 100% backward compatibility for legacy /health, /stats, /routes. Provides atomic start, health monitoring, stop, automated reconciliation of orphan Janus mountpoints/routes, and TTL watchdog.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags: {}
contract_payload:
  identifiers:
    session_id: correlation session identifier from 07 external contract (string)
    operation_id: client idempotency operation identifier (string)
    sn: terminal serial number (ASCII string)
    device_id: numeric device ID / Janus mountpoint ID (integer)
    rtp_port: dynamically allocated Janus video RTP port in range 6010-6200 (integer)
    rtcp_port: dynamically allocated Janus video RTCP port (rtp_port + 1) (integer)
  operations_events:
    - POST /api/v1/media/sessions/start (start session, allocate ports, create Janus mountpoint, insert ingress route, idempotent repeat)
    - GET /api/v1/media/sessions/{session_id} (session health, state, freshness, timestamps, elapsed/remaining TTL)
    - POST /api/v1/media/sessions/{session_id}/stop (stop session, harvest final stats, delete ingress route, destroy Janus mountpoint, idempotent repeat)
    - POST /api/v1/media/reconcile (reconciliation of orphan Janus mountpoints and Ingress routes)
    - GET /api/v1/media/metrics (aggregate technical and audit metrics)
    - GET /api/v1/openapi.json (OpenAPI 3.0.3 specification)
  errors:
    400: invalid_request (missing session_id or sn)
    401: unauthorized (missing or invalid X-Media-Service-Token / Bearer token)
    404: session_not_found (no session with specified session_id)
    409: session_busy (active session already exists on device sn), session_terminated
    502: janus_error (Janus Admin API gateway communication failure)
    503: port_exhaustion (RTP port range exhausted), max_sessions_exceeded
  invariants:
    - Service authentication enforced on all /api/v1/media/* endpoints via X-Media-Service-Token or Authorization: Bearer
    - Deterministic repeated start returns 200 OK with identical session connection parameters
    - Deterministic repeated stop returns 200 OK with state=stopped
    - Stop of non-existent or already-stopped session returns 200 OK
    - Exactly one active session per terminal SN at any given time (enforces device mutual exclusion)
    - Atomic rollback on partial start: failure during route upsert triggers compensating destroy of Janus mountpoint
    - Automated TTL watchdog terminates sessions and frees resources upon ttl_sec expiration
    - Periodic background reconciliation every 60s prunes orphan Janus mountpoints and routes while protecting static routes and default mountpoint 1
    - Technical timestamps (created_at, started_at, stopped_at, last_rtp_at) and streaming counters (rtp_packets, bytes) provided for audit; no billing decisions made in media layer
supersedes: []
known_risks: []
consumers:
  - L4D-08B-MB
next_prompt_id: L4D-08B-MB
<!-- HANDOFF:H-L4D-08A-MEDIA-v1:END -->
```

---

## 1. Резюме шага и контекст выполнения

Шаг **`L4D-08A-MEDIA`** выполнен строго в рамках изолированного репозиторного каталога `l4media`.

### Цель шага
Укрепить контракт жизненного цикла медиапотока по требованию (on-demand media lifecycle) без оркестрации IoT-сессий и без изменения Агента. Обеспечить идемпотентный start/health/stop, атомарное создание и удаление связки Ingress route + Janus streaming mountpoint, автоматическую очистку зависших ресурсов (orphan resources reconciliation), watchdog по таймаутам (TTL) и служебную авторизацию (Service Authentication) для потребителя `L4D-08B-MB`.

### Выполненные работы
1. **Input Gates & Correlation Protocol:** Проверены контракты `H-L4D-07-IOT-v1` и `H-L4D-00G-DOCS-v1`. Идентификаторы сессий (`session_id`) и операций (`operation_id`) из шага 07 приняты как внешний correlation contract без жесткой привязки к внутренней схеме БД.
2. **Additive Service-Auth API:** Реализованы защищенные эндпоинты `/api/v1/media/sessions/start`, `/sessions/{session_id}`, `/sessions/{session_id}/stop`, `/reconcile`, `/metrics`, `/openapi.json` на порту 9100 сервиса `l4media-ingress`.
3. **Idempotency & Mutual Exclusion:** Реализованы детерминированные повторные ответы для `start` (возврат 200 с теми же параметрами) и `stop` (возврат 200 `state: stopped`), взаимная блокировка устройства по серийному номеру (409 Conflict `session_busy`).
4. **Resilience & Reconciliation:** Добавлен compensating rollback (удаление mountpoint в Janus при сбое таблицы маршрутов Ingress), автоматическое закрытие сессий по TTL watchdog, периодическая (каждые 60 с) и on-demand очистка orphan-маунтпоинтов в Janus и маршрутов в Ingress с сохранением статических ресурсов.
5. **Audit Metrics:** Технические таймстампы (`created_at`, `started_at`, `stopped_at`, `last_rtp_at`) и счетчики пакетов/байт сохранены для аудита. Решения о тарификации в медиаконтуре отсутствуют согласно принципу разделения ответственности.
6. **Тестирование и валидация:** Написаны и успешно выполнены 7/7 C unit-тестов, 9/9 интеграционных тестов `test_media_lifecycle.py`, 6/6 регрессионных тестов `test_ingress_regression.py`.
7. **Деплой на прод-хост 87.242.100.34:** Сервисы `l4media-ingress`, `l4media-janus`, `l4media-nginx` пересобраны и запущены. Все 7 проверок `check.sh` успешно пройдены. Внешние контейнеры не затронуты.

---

## 2. Проверка входных контрактов (Input Gates)

| Поле | H-L4D-07-IOT-v1 | H-L4D-00G-DOCS-v1 | Статус |
|---|---|---|---|
| `status` | `ACCEPTED` | `ACCEPTED` | Валидно |
| `producer_scope_project` | `iot-rpc-rest-app` | `l4desk-service` | Валидно |
| `contract_kinds` | `[API, EVENT, DEPLOYMENT]` | `[SPECIFICATION, ARCHITECTURE]` | Валидно |
| `session_id format` | UUID / String correlation | Соответствует разделу 8 архитектуры | Принято |

Контракты приняты; входные correlation-параметры `session_id`, `operation_id`, `device_id`, `sn` интегрированы в схему API.

---

## 3. Спецификация и гарантии API (v1)

### 3.1. Эндпоинты

| Метод | Путь | Назначение | Авторизация |
|---|---|---|---|
| `POST` | `/api/v1/media/sessions/start` | Идемпотентный запуск медиасессии (Janus mountpoint + Ingress route). | Service Token |
| `GET` | `/api/v1/media/sessions/{session_id}` | Получение статуса сессии, свежести потока, счетчиков и таймингов. | Service Token |
| `POST` | `/api/v1/media/sessions/{session_id}/stop` | Идемпотентная остановка сессии с удалением маршрута и mountpoint. | Service Token |
| `POST` | `/api/v1/media/reconcile` | Принудительная сверка и удаление orphan-ресурсов. | Service Token |
| `GET` | `/api/v1/media/metrics` | Агрегированные метрики медиаконтура. | Service Token |
| `GET` | `/api/v1/openapi.json` | Машиночитаемая спецификация OpenAPI 3.0.3. | Публичный |

### 3.2. Матрица ошибок

| HTTP Код | Ошибка (`error`) | Описание и причина |
|---|---|---|
| `400` | `invalid_request` | Отсутствует `session_id` или `sn` в теле запроса. |
| `401` | `unauthorized` | Отсутствует или не совпадает токен `X-Media-Service-Token` / `Bearer`. |
| `404` | `session_not_found` | Сессия с указанным `session_id` не найдена при `GET`. |
| `409` | `session_busy` | Для устройства `sn` уже активна другая сессия. |
| `409` | `session_terminated` | Сессия была завершена и не может быть запущена повторно без нового `session_id`. |
| `502` | `janus_error` | Ошибка ответа от Janus Admin API (`/admin`). |
| `503` | `port_exhaustion` | Исчерпан пул портов RTP в диапазоне 6010-6200. |
| `503` | `max_sessions_exceeded` | Переполнена таблица сессий Ingress (максимум 128). |

---

## 4. Гарантии надежности и предотвращения утечек (Zero Orphan Guarantee)

1. **Компенсирующий откат (Compensating Rollback):** Если Janus mountpoint создан успешно, но добавление маршрута в таблицу Ingress завершилось ошибкой, `l4media-ingress` немедленно вызывает `destroy` созданного mountpoint в Janus и возвращает `500 Internal Error`.
2. **Watchdog по таймауту (TTL):** Поле `ttl_sec` (по умолчанию 600 с, лимиты 10–7200 с) контролируется ежесекундным циклом. При наступлении таймаута демон автоматически зачищает маршрут, уничтожает mountpoint в Janus и переводит сессию в `state: stopped, stop_reason: ttl_expired`.
3. **Фоновая и ручная Reconciliation:** Каждые 60 секунд (и при вызове `/reconcile`) демон сопоставляет активные сессии с реальным списком mountpoint в Janus и маршрутами в Ingress. Любые зависшие mountpoint (кроме статического demo mountpoint 1) и маршруты (кроме статических из `routes.conf`) уничтожаются.
4. **Graceful Shutdown:** При получении `SIGINT` или `SIGTERM` демон перед выходом останавливает все активные медиасессии, освобождает порты и удаляет mountpoints в Janus.

---

## 5. Доказательства тестирования и верификации

### 5.1. Unit-тесты C (компиляция и запуск внутри Docker-сборки)
```text
=========================================
 Running l4media-ingress C unit tests
=========================================
[UNIT] Testing preamble parsing...
  [PASS] Preamble parsed correctly.
[UNIT] Testing freshness and stale detection...
  [PASS] Freshness logic and JSON output verified.
[UNIT] Testing connection epoch isolation...
  [PASS] Connection epoch is strictly monotonic and resets per session.
[UNIT] Testing JSON parser helpers...
  [PASS] JSON parser helpers verified.
[UNIT] Testing Service Authentication checking...
  [PASS] Service Authentication checking verified.
[UNIT] Testing dynamic RTP port allocation...
  [PASS] Dynamic port allocation verified.
[UNIT] Testing session lifecycle state machine...
  [PASS] Session lifecycle state machine verified.
=========================================
 ALL C UNIT TESTS PASSED!
=========================================
```

### 5.2. Интеграционные тесты `test_media_lifecycle.py` (хост 87.242.100.34)
```text
================================================================================
 Starting l4media Media Lifecycle Contract & Integration Test Suite
 Ingress Control API: http://172.20.0.3:9100
 Janus WebRTC Admin:  http://172.20.0.2:7088/admin
================================================================================

[TEST 1] Verifying OpenAPI 3.0.3 specification...
  [PASS] OpenAPI specification valid and verified.

[TEST 2] Verifying Service Authentication security...
  [PASS] Service Authentication enforcement and legacy compatibility verified.

[TEST 3] Verifying On-Demand Media Lifecycle (Start -> Health -> Stop)...
  [PASS] Full media session lifecycle start -> health -> stop verified.

[TEST 4] Verifying Idempotency and Deterministic Repeated Responses...
  [PASS] Deterministic repeated start and stop idempotency verified.

[TEST 5] Verifying Device Mutual Exclusion (session_busy conflict)...
  [PASS] Device mutual exclusion and 409 session_busy verified.

[TEST 6] Verifying Concurrency (Parallel starts on same device)...
  -> Concurrency outcomes: [201, 409, 409, 409, 409]
  [PASS] Concurrency: exactly 1 winner, remainder cleanly rejected with 409.

[TEST 7] Verifying Automated Reconciliation & Orphan Resource Pruning...
  [PASS] Reconciliation successfully pruned orphans while protecting static resources.

[TEST 8] Verifying Automated TTL Watchdog Expiration...
  -> Waiting 11 seconds for TTL watchdog tick...
  [PASS] TTL Watchdog successfully expired and tore down media resources.

[TEST 9] Verifying Aggregate Technical Metrics...
  -> Metrics snapshot: {'status': 'ok', 'uptime_sec': 22, 'active_media_sessions': 0, 'total_sessions_started': 6, 'total_sessions_stopped': 6, 'total_sessions_failed': 0, 'orphans_cleaned_mountpoints': 1, 'orphans_cleaned_routes': 1, 'total_rtp_packets': 0, 'total_bytes': 0}
  [PASS] Metrics endpoint operational and valid.

================================================================================
 ALL 9 MEDIA LIFECYCLE TESTS PASSED SUCCESSFULLY!
================================================================================
```

### 5.3. Регрессионные тесты `test_ingress_regression.py`
```text
================================================================
 ALL 6 REGRESSION & CONTRACT TESTS PASSED SUCCESSFULLY!
================================================================
```

### 5.4. Изоляция контейнеров
При пересборке и рестарте медиастека внешние контейнеры не перезапускались:
```text
External containers before: 7, after: 7
SUCCESS: ALL external containers remained untouched and running!
```

---

## 6. Рекомендации для потребителя L4D-08B-MB

При интеграции в `MenuBuilder`:
1. **URL сервиса:** `http://l4media-ingress:9100` (внутренняя docker-сеть `user1_default`).
2. **Заголовок авторизации:** `X-Media-Service-Token: l4media-service-secret-token` (из переменной окружения `L4MEDIA_SERVICE_TOKEN`).
3. **Старт сессии:**
   ```http
   POST /api/v1/media/sessions/start
   {
     "session_id": "<remote_session_uuid>",
     "operation_id": "<start_operation_uuid>",
     "sn": "<terminal_sn>",
     "device_id": <numeric_device_id>,
     "pin": "<optional_pin>",
     "ttl_sec": 600
   }
   ```
4. **Ответ на старт:** содержит `mountpoint_id`, `janus_ws: "/janus-ws"`, `pin`, `rtp_port`, `rtcp_port`.
5. **Остановка сессии:**
   ```http
   POST /api/v1/media/sessions/<remote_session_uuid>/stop
   {
     "operation_id": "<stop_operation_uuid>",
     "reason": "user_closed"
   }
   ```
6. **Compensating stop:** Если вызов команды в IoT-контур завершился ошибкой, `MenuBuilder` вызывает `/api/v1/media/sessions/{session_id}/stop` для освобождения ресурсов.
