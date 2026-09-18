# L4D-06A-PB — Опубликовать идемпотентный Certificate PIN Contract

## Contract gate

- **Scope project:** `ProcessingBackend`
- **Scope root:** `D:\repo\platerra\Public\etranprocessing\ProcessingBackend`
- **Branch:** `l4desk/l4d-06a-pb`
- **Producer commit:** `083138f223b723098e9a188803e3fc802e8a6011`
- **Входные handoffs:**
  - `H-L4D-05-MB-v1`: статус `ACCEPTED` (onboarding sequence gate, tenant/user registration baseline).
  - `H-L4D-00G-DOCS-v1`: статус `ACCEPTED` (принятый certificate/CSR baseline и каталог рисков).
- **Идентификаторы сущностей:**
  - `tenant_id` (`int`): `orgs.org_id` / `terminals.org_id` / `l4desk_terminals.tenant_id`
  - `terminal_id` (`int`): `terminals.id` / `l4desk_terminals.terminal_id`
  - `sn` (`str`): `terminals.sn` / `l4desk_terminals.sn`
  - `operation_id` (`str` <= 128): сквозной идемпотентный ключ саги онбординга
  - `correlation_id` (`str` <= 128): сквозной идентификатор трассировки
  - `pin` (`str`, 6 цифр): одноразовый числовой PIN (`certificate_pins.pin`)
  - Соответствие между БД и артефактами 05/00G однозначное; статус `BLOCKED_CONTRACT` не требуется.
- **Границы ответственности:**
  - Логика онбординга терминалов и UI мастера подключения остаются в `MenuBuilder` (задача `L4D-06C-MB`).
  - Провижининг устройств в IoT контур остаётся в `iot-rpc-rest-app` (задача `L4D-06B-IOT`).
  - В `ProcessingBackend` реализован исключительно аддитивный контракт выпуска PIN, идемпотентность и усиление безопасности терминального CSR/X.509 контура.

---

## Реализация

### 1. Сервисная аутентификация (`require_service_auth`)
- В `app/config.py` добавлены параметры конфигурации:
  - `service_auth_token: str = ""` (переменные `SERVICE_AUTH_TOKEN`, `INTERNAL_SERVICE_KEY`).
  - `cert_pin_ttl_seconds: int = 86400` (TTL PIN по умолчанию — 24 часа).
- В `app/dependencies.py` реализована зависимость `require_service_auth(request: Request)`:
  - Поддерживает заголовки `Authorization: Bearer <token>`, `X-Service-Token: <token>`, `X-Internal-Service-Key: <token>`.
  - При настроенном токене выполняет константную проверку времени `secrets.compare_digest`.
  - При отсутствии или несовпадении возвращает `401 Unauthorized` со структурированной ошибкой `SERVICE_AUTH_FAILED`.

### 2. Схемы Pydantic (`app/schemas/certificates.py`)
- `IssueCertificatePinRequest`:
  - `operation_id` (`str`, min=1, max=128) — уникальный ключ операции.
  - `correlation_id` (`str | None`, max=128) — ID корреляции.
  - `tenant_id` (`int`) — ID организации/тенант.
  - `terminal_id` (`int`) — ID терминала.
  - `sn` (`str`, min=1, max=100) — серийный номер терминала.
  - `ttl_seconds` (`int`, ge=60, le=2592000, default=86400) — время жизни PIN.
  - `actor` (`str | None`, default="system") — субъект запроса.
- `IssueCertificatePinResponse`:
  - `operation_id`, `correlation_id`, `tenant_id`, `terminal_id`, `sn`.
  - `pin` (`str | None`) — открытый PIN (только при первичном выпуске или активном replay; `None` после использования или истечения).
  - `pin_masked` (`str`) — маскированный PIN (например `***773`) для безопасного отображения/логов.
  - `status` (`issued` | `consumed` | `expired`).
  - `expires_at`, `created_at` (`datetime` UTC).
  - `replayed` (`bool`) — признак идемпотентного повтора.
- `CertificatePinErrorDetail`:
  - `error`, `message`, `error_code`, `operation_id`.

### 3. Эндпоинты выпуска и запроса PIN (`app/routers/certificates.py`)
- `POST /api/certificates/pins/issue` (и алиас `POST /api/certificates/pins`):
  1. **Идемпотентность по `operation_id`**: проверяется наличие события `certificate_pin_issued` в `l4desk_audit_events`.
     - При совпадении `operation_id` и идентичных параметрах (`tenant_id`, `terminal_id`, `sn`): возвращается результат прежней операции (`200 OK`, `replayed=True`), второй PIN в базе данных не создаётся.
     - При повторном использовании `operation_id` с другими параметрами: возвращается `409 Conflict` (`OPERATION_ID_CONFLICT`).
  2. **Привязка владения (Ownership binding)**:
     - Терминал должен существовать в `terminals` (иначе `404 Not Found`, `TERMINAL_NOT_FOUND`).
     - `terminal.org_id` должен строго совпадать с `tenant_id` (иначе `403 Forbidden`, `TENANT_OWNERSHIP_MISMATCH`).
     - `terminal.sn` должен строго совпадать с `sn` (иначе `400 Bad Request`, `SERIAL_NUMBER_MISMATCH`).
  3. **Генерация и запись**:
     - Предыдущие ожидающие (`pending`) PIN для этого терминала переводятся в статус `expired`.
     - Генерируется уникальный криптостойкий 6-значный PIN через `generate_unique_pin`.
     - Создаётся запись `CertificatePin(status="pending", created_by="op:<operation_id>")`.
     - Если существует запись `l4desk_terminals`, её `pin_state` обновляется на `issued`.
     - В `l4desk_audit_events` фиксируется событие `certificate_pin_issued` с маскированным значением PIN.
     - Возвращается `201 Created` (`replayed=False`).
- `GET /api/certificates/pins/by-operation/{operation_id}`:
  - Возвращает текущее безопасное состояние PIN по `operation_id` (или `404 Not Found`, `OPERATION_NOT_FOUND`).

### 4. Устранение уязвимостей и рисков терминального CSR-контура (`H-L4D-00C-PB-v1`)
- **Row-level locking (concurrency race)**:
  В функции `_find_terminal_by_pin` при `for_update=True` добавлен пессимистический лок `with_for_update(of=CertificatePin)`, исключающий одновременное использование одного PIN параллельными потоками.
- **Криптографическая проверка CSR**:
  В `_handle_setup` проверяется валидность структуры и подписи: `csr.is_signature_valid`. Неподписанные или повреждённые CSR отклоняются с кодом 4.
- **Валидация несоответствия CSR (CSR Mismatch)**:
  - Проверка `OU`: если указан, обязан соответствовать `terminal.device_id`.
  - Проверка `O`: если указан, обязан соответствовать `terminal.org_id`.
  - Проверка `CN`: если указан, обязан соответствовать `terminal.sn` или `cpserial`.
  - При несоответствии возвращается код 4 `Несоответствие данных CSR терминалу`.
- **Безопасный Retry при обрыве сети**:
  Реализован кэш недавних успешных выпусков `_recent_setup_cache` (TTL 15 минут). При повторном обращении терминала с тем же `pin` и `cpserial` возвращается ранее собранный PKCS#7 пакет без повторного вызова CA и без ложной ошибки "Пин-код уже использован".
- **Информативные статусы PIN**:
  При проверке (`check`) и выпуске (`setup`) различаются ошибки:
  - Пин-код просрочен (`code=2`).
  - Пин-код уже использован (`code=2`).
  - Пин-код не существует (`code=2`).

### 5. Безопасность и сокрытие секретов
- Открытый PIN возвращается **только** клиенту в ответе API выпуска.
- В логах приложения и журнале аудита `l4desk_audit_events` значение PIN **всегда** маскируется (`mask_pin(pin)` -> `***773`).
- При повторном запросе уже использованного (`consumed`) или просроченного (`expired`) PIN поле `pin` в ответе равно `None`.

---

## Тестирование и верификация

Добавлен специализированный набор тестов `ProcessingBackend/backend/tests/test_certificate_pin_contract.py`:
1. `test_issue_pin_success` — выпуск PIN, валидация полей, маскирование в аудите.
2. `test_issue_pin_idempotent_replay` — повтор запроса с тем же `operation_id`, возврат `replayed=True`, отсутствие дублирования PIN в БД.
3. `test_issue_pin_reused_operation_id_conflict` — отказ `409 Conflict` (`OPERATION_ID_CONFLICT`) при reuse `operation_id` с другими параметрами.
4. `test_issue_pin_ownership_mismatch` — отказ `403 Forbidden` (`TENANT_OWNERSHIP_MISMATCH`) при несовпадении тенанта.
5. `test_issue_pin_sn_mismatch` — отказ `400 Bad Request` (`SERIAL_NUMBER_MISMATCH`) при несовпадении SN.
6. `test_issue_pin_terminal_not_found` — отказ `404 Not Found` (`TERMINAL_NOT_FOUND`) при отсутствии терминала.
7. `test_service_auth_security` — проверка `401 Unauthorized` без токена, с неверным токеном, успешный доступ по `Authorization: Bearer` и `X-Service-Token`.
8. `test_lookup_pin_by_operation_id` — чтение по `operation_id` и `404 Not Found` для неизвестного ID.
9. `test_terminal_facing_check_flow` — обратная совместимость legacy `check` (SubCA, windows-1251, DN).
10. `test_terminal_facing_setup_success` — legacy `setup`, выпуск X.509, PKCS#7 chain, статус `used`, запись аудита.
11. `test_terminal_facing_setup_network_drop_safe_retry` — безопасный повтор при сетевом сбое с возвратом сохранённого ответа без перевыпуска.
12. `test_terminal_facing_setup_csr_mismatch_device_id` — отклонение CSR с неверным `OU`.
13. `test_terminal_facing_setup_csr_mismatch_cn` — отклонение CSR с неверным `CN`.
14. `test_terminal_facing_setup_invalid_csr_signature` — отклонение CSR с невалидной подписью.
15. `test_terminal_facing_expired_pin` — ошибка "Пин-код просрочен" (`code=2`).
16. `test_terminal_facing_used_pin_no_cache` — ошибка "Пин-код уже использован" (`code=2`).
17. `test_row_locking_concurrency_protection` — проверка `with_for_update(of=CertificatePin)` в запросе.

### Результаты проверок качества
- **Linters (Ruff):** `uv run ruff check app tests` — **PASSED** (0 ошибок, 0 предупреждений).
- **Formatters (Ruff):** `uv run ruff format --check app tests` — **PASSED** (все файлы соответствуют стилю).
- **Type Checker (Pyright):** `uv run pyright app` — **PASSED** (0 errors, 0 warnings).
- **Test suite (Pytest):** `uv run pytest` — **130 passed** (17 новых контрактных тестов + 113 существующих регрессионных тестов).

---

## Развёртывание на Production (87.242.100.34)

- **Хост:** `user1@87.242.100.34` (SSH-ключ `d:\.ssh\id_ed25519`).
- **MCP Ops Readiness:** `[MCP Ops Readiness: UNAVAILABLE]` (сервер `server-ops` вернул `Not connected`). Согласно разделу 8 Guidelines выполнен регламентный non-blocking переход на стандартный SSH-транспорт.
- **Host Pre-flight Check:**
  - RAM: 2.1 GiB свободно (порог > 300 MiB соблюдён).
  - Диск: 52% (порог < 90% соблюдён).
  - Load average: 0.20 (порог < 2.0 соблюдён).
- **Доставка артефактов:**
  Файлы скопированы через `scp` в `/home/user1/ProcessingBackend/backend/app/`.
- **Сборка и перезапуск:**
  `sudo docker compose build processing-backend && sudo docker compose up -d processing-backend`
  Контейнер успешно запущен (`STATUS: Up`).
- **Smoke-тестирование на хосте:**
  1. `GET /api/health` -> `HTTP/1.1 200 OK` (`{"status":"ok"}`).
  2. `GET /api/certificates/?function=check&pin=000000` -> `HTTP/1.1 200 OK` (`<code>2</code><Description>Пин-код не существует</Description>`).
  3. `GET /api/certificates/pins/by-operation/nonexistent-test-op` -> `HTTP/1.1 404 Not Found` (`{"detail":{"error":"operation_not_found","message":"Operation 'nonexistent-test-op' not found","error_code":"OPERATION_NOT_FOUND","operation_id":"nonexistent-test-op"}}`).
  4. `POST /api/certificates/pins/issue` (терминал не найден) -> `HTTP/1.1 404 Not Found` (`{"detail":{"error":"terminal_not_found","message":"Terminal 99999 not found","error_code":"TERMINAL_NOT_FOUND","operation_id":"smoke-1"}}`).
  5. `POST /api/certificates/pins/issue` (несовпадение тенанта) -> `HTTP/1.1 403 Forbidden` (`{"detail":{"error":"tenant_ownership_mismatch","message":"Terminal 1 does not belong to tenant 99999","error_code":"TENANT_OWNERSHIP_MISMATCH","operation_id":"smoke-2"}}`).
  6. Проверка логов контейнера: штатный приём входящих запросов `POST /api/gategauge` и `POST /api/licensebilling/`, 0 ошибок.

---

## Артефакты и контрольные суммы

| Артефакт | SHA-256 |
|---|---|
| `ProcessingBackend/backend/app/routers/certificates.py` | `0a0e925b1dfad9c6b96f5063cc33b5bea822cd558062681d9b011f0fe02ed93a` |
| `ProcessingBackend/backend/app/schemas/certificates.py` | `5e5327e51703cabb08c696c72477fed5abb75093782bd1514c43649b8681b6f0` |
| `ProcessingBackend/backend/app/services/cert_billing.py` | `50ccee8cb7311e2a55d47ae127ffdcb41e48b2f121aeb1c6a2c8606026770ce5` |
| `ProcessingBackend/backend/app/dependencies.py` | `9b716bf58d274ef478f220ee398c13560d02919092a340fce008178ca955018c` |
| `ProcessingBackend/backend/app/config.py` | `8873a6ecff0b01e045b6e3f66f39e866c0cbb8476aa2ecf6f97ca4b60b30c0ba` |
| `ProcessingBackend/backend/app/models.py` | `5d6b0c36127443bc2d02f90341ad494a7fd86c021f60ef7ff5d81ff3574f403a` |
| `ProcessingBackend/backend/tests/test_certificate_pin_contract.py` | `4eaa876994b3be8037763685188ef0ade2b1a36a9a1a9c947de0300bf8dae37e` |

---

## Handoff Block

<!-- HANDOFF:H-L4D-06A-PB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-06A-PB-v1
status: ACCEPTED
contract_kinds:
  - API
  - DEPLOYMENT
producer_prompt_id: L4D-06A-PB
producer_scope_project: ProcessingBackend
producer_report_path: ProcessingBackend/docs/l4desk/handoffs/L4D-06A-PB-report.md
producer_branch: l4desk/l4d-06a-pb
producer_commit: 083138f223b723098e9a188803e3fc802e8a6011
accepted_at_utc: 2026-09-18T15:45:00Z
contract_version: 1.0.0
artifact_paths:
  - ProcessingBackend/backend/app/routers/certificates.py
  - ProcessingBackend/backend/app/schemas/certificates.py
  - ProcessingBackend/backend/app/services/cert_billing.py
  - ProcessingBackend/backend/app/dependencies.py
  - ProcessingBackend/backend/app/config.py
  - ProcessingBackend/backend/app/models.py
  - ProcessingBackend/backend/tests/test_certificate_pin_contract.py
artifact_sha256:
  - 0a0e925b1dfad9c6b96f5063cc33b5bea822cd558062681d9b011f0fe02ed93a
  - 5e5327e51703cabb08c696c72477fed5abb75093782bd1514c43649b8681b6f0
  - 50ccee8cb7311e2a55d47ae127ffdcb41e48b2f121aeb1c6a2c8606026770ce5
  - 9b716bf58d274ef478f220ee398c13560d02919092a340fce008178ca955018c
  - 8873a6ecff0b01e045b6e3f66f39e866c0cbb8476aa2ecf6f97ca4b60b30c0ba
  - 5d6b0c36127443bc2d02f90341ad494a7fd86c021f60ef7ff5d81ff3574f403a
  - 4eaa876994b3be8037763685188ef0ade2b1a36a9a1a9c947de0300bf8dae37e
compatibility:
  backward_compatible_with:
    - H-L4D-00C-PB-v1
    - H-L4D-00G-DOCS-v1
  breaking_changes: false
  notes: "Additive service-to-service PIN issuance and query endpoints (/api/certificates/pins/issue, /api/certificates/pins/by-operation/{operation_id}). Existing terminal endpoints (function=check, function=setup) preserved with full backward compatibility and reinforced with row-level locking, CSR signature checking, CSR mismatch rejection, and safe retry caching."
deployment_status: DEPLOYED
deployed_environment: production
feature_flags: {}
contract_payload:
  endpoints:
    - path: /api/certificates/pins/issue
      method: POST
      auth: require_service_auth
      request_schema: IssueCertificatePinRequest
      response_schema: IssueCertificatePinResponse
      status_codes:
        201: Created (new PIN issued)
        200: OK (idempotent replay of existing operation_id)
        400: Bad Request (SERIAL_NUMBER_MISMATCH)
        401: Unauthorized (SERVICE_AUTH_FAILED)
        403: Forbidden (TENANT_OWNERSHIP_MISMATCH)
        404: Not Found (TERMINAL_NOT_FOUND)
        409: Conflict (OPERATION_ID_CONFLICT)
    - path: /api/certificates/pins/by-operation/{operation_id}
      method: GET
      auth: require_service_auth
      response_schema: IssueCertificatePinResponse
      status_codes:
        200: OK
        401: Unauthorized (SERVICE_AUTH_FAILED)
        404: Not Found (OPERATION_NOT_FOUND)
  identifiers:
    tenant_id: int
    terminal_id: int
    sn: str
    operation_id: str
    correlation_id: str | None
  idempotency_semantics:
    replayed_flag: boolean
    conflict_on_parameter_change: true
    duplicate_pin_issuance: prohibited
    consumed_pin_visibility: "pin=None, pin_masked preserved, status=consumed"
    expired_pin_visibility: "pin=None, pin_masked preserved, status=expired"
  deployed_host: 87.242.100.34
  mcp_ops_readiness: UNAVAILABLE (fallback to SSH)
  verification:
    pytest_tests: "130 passed"
    linters: "ruff check passed, ruff format passed, pyright 0 errors/0 warnings"
    live_smoke_probes: "health 200, check code=2, issue 404/403 with error_code"
supersedes: []
known_risks: []
consumers:
  - L4D-06B-IOT
  - L4D-06C-MB
next_prompt_id: L4D-06B-IOT
```
<!-- HANDOFF:H-L4D-06A-PB-v1:END -->
