# Handoff Report: Baseline сертификатного контура и Alembic-цепочки (L4D-00C-PB)

## Candidate H-L4D-00C-PB-v1

```yaml
<!-- HANDOFF:H-L4D-00C-PB-v1:BEGIN -->
handoff_id: H-L4D-00C-PB-v1
status: CANDIDATE
contract_kinds:
  - REPORT
producer_prompt_id: L4D-00C-PB
producer_scope_project: ProcessingBackend
producer_report_path: ProcessingBackend/docs/l4desk/handoffs/L4D-00C-PB-report.md
producer_branch: l4desk/l4d-00c-pb
producer_commit: 5de0e6a39d89caae3fe0da517435f3708dfba2f7
created_at_utc: 2026-09-17T18:15:00Z
contract_version: 1.0.0
schema_revision: "026"
artifact_version: 0.1.0
artifact_paths:
  - ProcessingBackend/docs/l4desk/handoffs/L4D-00C-PB-report.md
compatibility:
  backward_compatible_with:
    - N/A
  breaking_changes: false
  notes: Baseline audit and inventory of ProcessingBackend certificate contour, terminal auth, Alembic migration chain, and runtime state. No code or schema changes introduced.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  auto_set_cert_serial_on_licensebilling: enabled
  cert_discovery_audit: enabled
contract_payload:
  identifiers:
    sn: terminal serial number (string, e.g. a4b0000773c12345d210826)
    device_id: legacy device number (integer, e.g. 773)
    org_id: tenant organization ID (integer)
    terminal_id: internal primary key in terminals table (integer)
    cert_serial: active X.509 certificate hex serial (string)
    pin: 6-digit one-time enrollment code (string)
  operations_events:
    - GET/POST /api/certificates/?function=check&pin={pin}
    - POST /api/certificates/?function=setup (with PKCS#10 CSR body)
    - POST /api/devices/map_legacy_crt/
    - GET/POST /api/licensebilling
    - mcp-pin-server tools: generate_pin, revoke_pin, list_pins, get_terminal_cert_history, get_terminal_cert_discovery
  errors:
    certificates_api: XML Windows-1251 (<Response><Result>Error</Result><code>{1|2|4}</code><Description>{msg}</Description></Response>)
    licensebilling: XML UTF-8 (<Response><Result>ERROR</Result><Description>{msg}</Description></Response>), HTTP 401 on unrecognized/mismatched terminal
    map_legacy_crt: JSON HTTP 400/401/500
  invariants:
    - ProcessingBackend sole authority for database schema migrations (Alembic head 026)
    - Strict validation for iot.leo4.ru CA: sn == cn AND cert_serial == db_cert_serial
    - Legacy CA auto-binds cert_serial into terminal_cert_history upon first valid auth
    - PIN is single-use, transitioning from pending to used upon successful CSR signing
    - All issued certificates recorded in terminal_cert_history; all ingress cert sightings tracked in terminal_cert_discovery
supersedes: []
known_risks:
  - CSR signature is not cryptographically verified prior to forwarding to CA function
  - Concurrency gap in setup handler without row-level lock (SELECT ... FOR UPDATE) on pending PIN
  - Inability to safely retry setup if network drops after PIN status marked as used
consumers:
  - L4D-00D-MEDIA
  - L4D-00G-DOCS
next_prompt_id: L4D-00D-MEDIA
<!-- HANDOFF:H-L4D-00C-PB-v1:END -->
```

---

## 1. Резюме шага и контекст выполнения

Шаг `L4D-00C-PB` выполнен строго в рамках изолированного репозиторного каталога `ProcessingBackend`.
Цель шага: сформировать проверяемый baseline сертификатного контура (`PIN`, `CSR`, `X.509`), привязки терминалов (`terminal binding`), подсистемы аудита (`TerminalCertHistory`, `TerminalCertDiscovery`), единственной цепочки миграций Alembic и production-состояния сервиса без внесения изменений в runtime-код и схему данных, подготовив контракт для следующего этапа каскада — `L4D-00D-MEDIA`.

- **Contract Gate:** проверка `contract-handoff.md` подтвердила валидность входного handoff `H-L4D-00B-IOT-v1` (`status: ACCEPTED`, sequence gate пройден, SHA-256 проверен: `8e28509e59d997a4db77d7ddfeac03c4f58c7044ae1425a17688439df689e47d`). Код `iot-rpc-rest-app` не исследовался согласно правилу разделения контекстов.
- **MCP Ops Readiness Protocol:** статус зафиксирован как `[MCP Ops Readiness: DEGRADED / UNAVAILABLE]` (локальный MCP `server-ops` вернул `Not connected`). Согласно п. 8 Guidelines, это состояние является **Non-Blocking**; оперативная диагностика и smoke-тестирование выполнены через штатный SSH-транспорт на хост `user1@87.242.100.34` (ключ `d:\.ssh\id_ed25519`).
- **Изолированное окружение:** рабочая ветка `l4desk/l4d-00c-pb`. Сторонние проекты не модифицировались.
- **Статус runtime-кода и БД:** `NO_CODE_CHANGES`, `NO_SCHEMA_CHANGES`.

---

## 2. Инвентаризация Certificate Endpoints и маршрутизация

В `ProcessingBackend` функционируют следующие эндпоинты управления сертификатами и терминальной авторизации:

| Эндпоинт | Метод | Протокол / Формат | Авторизация / Заголовки | Назначение |
|---|---|---|---|---|
| `/api/certificates/` | `GET`, `POST` | XML (Windows-1251) | Query `pin` или XML body | Проверка PIN (`function=check`) и выпуск сертификата по CSR (`function=setup`) |
| `/Dispatcher.ashx` | `GET`, `POST` | XML (Windows-1251) | Query `pin` или XML body | Legacy-псевдоним для совместимости со старыми киосками |
| `/api/devices/map_legacy_crt/` | `POST` | JSON (UTF-8) | mTLS terminal cert headers | Выпуск клиентского PFX/PKCS#12 сертификата для легаси устройств |
| `/api/v1/devices/map_legacy_crt/` | `POST` | JSON (UTF-8) | mTLS terminal cert headers | Псевдоним `/api/devices/map_legacy_crt/` |
| `/api/licensebilling` | `GET`, `POST` | XML (UTF-8) | mTLS (`X-Client-Cert-*`) | Терминальный биллинг, проверка лицензий и mTLS-аутентификация |
| `/licensebilling` | `GET`, `POST` | XML (UTF-8) | mTLS (`X-Client-Cert-*`) | Legacy alias эндпоинта лицензирования киосков |
| `/api/health` | `GET` | JSON | Public | Healthcheck статус бэкенда (`{"status":"ok"}`) |

### Сопутствующий MCP-сервис `mcp-pin-server`
В каталоге `ProcessingBackend/mcp-pin-server` развернут сопутствующий FastMCP-сервис, экспортирующий административные инструменты управления PIN и сертификатами:
- `generate_pin(terminal_identifier, pin_code, custom_validity_days, comment, created_by)` — генерация уникального 6-значного PIN (по умолчанию TTL 24 часа).
- `revoke_pin(terminal_identifier, comment)` — отзыв активного `pending` PIN (перевод в `cancelled`).
- `list_pins(terminal_identifier, status, limit)` — листинг истории PIN по терминалу.
- `get_terminal_cert_history(terminal_identifier, limit)` — история выпущенных сертификатов (`TerminalCertHistory`).
- `get_terminal_cert_discovery(sn, cert_serial, limit)` — диагностика фактов обнаружения сертификатов на ингрессе (`TerminalCertDiscovery`).

---

## 3. Модели идентификации и аутентификации терминалов

Аутентификация терминалов выполняется в middleware/dependency `app.dependencies.get_current_terminal` на основе заголовков клиентского сертификата, проксируемых Nginx:
- `X-Client-Cert-DN` / `X-SSL-Client-Cert-Subject`
- `X-Client-Cert-Serial`
- `X-Client-Cert-Issuer-DN` / `X-Client-Cert-Issuer` / `X-SSL-Client-Issuer`
- `X-Client-Cert-NotAfter` / `X-Client-Cert-End-Date`

### 3.1. Новый контур CA (`iot.leo4.ru`) — Строгая авторизация
- **Условие распознавания:** Issuer DN содержит `iot.leo4.ru`.
- **Семантика проверки:**
  - Извлекается Common Name (`CN`): `sn = cn`.
  - Извлекается серийный номер: `cert_serial`.
  - Выполняется строгий запрос к таблице `terminals`:
    `WHERE terminals.sn == cn AND terminals.cert_serial == cert_serial AND terminals.is_active == True`.
  - Любое несовпадение `cert_serial` или отсутствие активного терминала вызывает немедленный `HTTP 401 Unauthorized` с фиксацией инцидента в `TerminalCertDiscovery(is_valid=False, validation_status="serial_mismatch"|"terminal_not_found")`.

### 3.2. Легаси контур CA / SubCA — Адаптивная привязка (Auto-bind)
- **Условие распознавания:** Issuer DN не содержит `iot.leo4.ru`.
- **Семантика сопоставления:**
  1. Поиск по `OU` (`device_id`) и `O` (`org_id`).
  2. Fallback: поиск по `L` (`terminal.id`) и `O` (`org_id`).
  3. Fallback: поиск по `CN` (`terminal.sn`).
- **Автоматическая привязка (Auto-bind):**
  - Если у терминала отсутствует `cert_serial` либо текущий серийный номер имеет длину <= 20 шестнадцатеричных символов (легаси формат), система автоматически обновляет `terminal.cert_serial = incoming_serial`, обновляет `cert_not_valid_after` и создает запись в аудит-журнале `TerminalCertHistory` с `source="legacy_auth"`.
  - Поведение регулируется флагом настроек `settings.auto_set_cert_serial_on_licensebilling` (по умолчанию `True`).

---

## 4. Идемпотентность и защита от Replay

1. **Запрос проверки PIN (`function=check`):**
   - Является идемпотентным и read-only.
   - Ищет запись в `certificate_pins` со статусом `status == 'pending'` и `expires_at > now()`.
   - Если срок действия истек (`expires_at <= now()`), переводит статус в `expired` (lazy expiration) и возвращает ошибку `code=2`.
   - Может многократно вызываться клиентом до момента выполнения `setup`.
2. **Запрос выпуска сертификата (`function=setup`):**
   - Потребляет PIN: переводит статус в `status = 'used'`, проставляет `used_at = now()`.
   - Записывает новый серийный номер сертификата в `terminals.cert_serial` и создает аудит-запись `TerminalCertHistory(source='setup')`.
   - **Защита от Replay:** повторный запрос с тем же PIN не находит запись в состоянии `pending` и отклоняется с ошибкой `code=2 ("Пин-код не существует")`.
   - **Замеченный race-gap:** в текущей реализации `_handle_setup` отсутствует строгая блокировка строки `SELECT ... FOR UPDATE` перед обращением к CA. При высококонкурентных одновременных запросах с одним PIN существует теоретический риск двойного обращения к CA.

---

## 5. Жизненный цикл PIN и семантика состояний

### 5.1. Формат и хранение
- **Модель:** `shared.etranprocessing_db.models.CertificatePin` (таблица `certificate_pins`).
- **Формат PIN:** 6 цифр (десятичная строка `\d{6}`), генерируемая криптографически безопасным генератором случайных чисел (`secrets.randbelow(900000) + 100000`).
- **TTL (Time to Live):** 24 часа по умолчанию (`DEFAULT_PIN_TTL = 86400` сек); дата истечения сохраняется в поле `expires_at: DateTime(timezone=True)`.

### 5.2. Конечный автомат состояний PIN (`status`)
Ограничение целостности `ck_certificate_pins_status`:
```
              ┌───────────────┐
              │    pending    │ (создан, ожидает ввода на терминале)
              └──┬────┬────┬──┘
                 │    │    │
   setup успешен │    │    │ истек TTL
                 ▼    │    ▼
         ┌────────┐   │   ┌─────────┐
         │  used  │   │   │ expired │
         └────────┘   │   └─────────┘
                      │
                      │ отозван оператором / админом
                      ▼
                 ┌───────────┐
                 │ cancelled │
                 └───────────┘
```
- `pending`: активен, готов к валидации и выпуску сертификата.
- `used`: успешно использован в процедуре `function=setup` (одноразовый, повторное использование запрещено).
- `expired`: истек срок действия (переводится лениво при вызове `check` или регламентной задачей).
- `cancelled`: отозван вручную администратором через UI или MCP `revoke_pin`.

---

## 6. Валидация CSR и X.509 профиль сертификата

Процедура выпуска выполняется внешним сервисом/функцией CA (`ProcessingBackend/ca_sign_csr.py`), вызываемой через REST API:

### 6.1. Входные параметры
- Заголовок `X-Ssl-Client-Csr`: URL-encoded PKCS#10 PEM.
- Заголовок `X-Cn`: серийный номер терминала (`terminal.sn`), принудительно переопределяющий `Common Name` субъекта.
- Заголовок `X-Sign`: контрольный хеш (`sign`), передаваемый киоском для идентификации криптопровайдера.
- Заголовок `X-Ssl-Client-Exp-Days`: срок действия сертификата (по умолчанию 365 дней).

### 6.2. Профиль генерируемого X.509 v3 сертификата
- **Subject:**
  - `CN`: `{terminal.sn}` (override из `X-Cn`).
  - Сохраняются атрибуты из CSR: `O` (`org_id`), `OU` (`device_id`), `C=ru`, `ST=msk`, `L=...`.
- **Key Usage (Critical):**
  - `Digital Signature` (обязательно для mTLS `CertificateVerify` в TLS 1.2/1.3).
  - `Content Commitment` (Non-Repudiation).
  - `Key Encipherment`.
- **Extended Key Usage:**
  - `Server Authentication` (`1.3.6.1.5.5.7.3.1`) — поддержка обратного HTTPS-сервера киоска.
  - `Client Authentication` (`1.3.6.1.5.5.7.3.2`) — авторизация mTLS на Nginx.
- **Basic Constraints (Critical):** `CA: FALSE`.
- **Subject Alternative Names (SAN):**
  - `URI:{terminal.sn}`
  - `URI:urn:leo4:terminal:{dev_code}` (7-значный код устройства)
  - `DNS:leo4-{dev_code}.device.leo4.ru`
  - `DNS:leo4-{dev_code}.term.leo4.ru`
  - `DNS:leo4-{dev_code}.internal`
  - `DNS:leo4-{dev_code}.local`
  - `DNS:localhost`
  - `IP:127.0.0.1`, `IP:::1`
  - `URI:urn:sign:{sign}` (при наличии `X-Sign`)
- **Алгоритм подписи:** `SHA256withRSA`, открытый ключ извлекается из переданного CSR.
- **Формат ответа клиенту:** PKCS#7 SignedData chain в кодировке DER/Base64, объединяющий выпущенный сертификат киоска и корневой сертификат CA.

---

## 7. Модель аудита и логирования

### 7.1. Файловое логирование и маскирование секретов
- Выделенный ротируемый логгер `cert_logger` (файл `certificates.log`, лимит 10 МБ, 5 ротаций).
- **Маскирование PIN:** функция `mask_pin(pin)` маскирует значение PIN в логах: отображаются только последние 3 символа (`***773`). Открытые PIN никогда не пишутся в постоянные логи.

### 7.2. Базовые таблицы аудита в PostgreSQL
1. `terminal_cert_history`:
   - Назначение: неизменяемый журнал истории выпуска и привязки сертификатов к киоскам.
   - Поля: `id`, `terminal_id`, `cert_serial`, `not_valid_after`, `pin_id`, `source` (`'setup'` или `'legacy_auth'`), `issued_at`.
2. `terminal_cert_discovery`:
   - Назначение: аккумулирующий реестр всех уникальных комбинаций `(sn, cert_serial)`, зафиксированных на ingress-точках.
   - Поля: `sn`, `cert_serial`, `cert_dn`, `ou`, `o`, `is_valid`, `validation_status`, `terminal_id`, `db_cert_serial`, `request_count`, `last_endpoint`, `client_ip`, `first_seen_at`, `last_seen_at`.
   - Защита от спама: группировка и инкремент счетчика `request_count` без создания дублирующих строк.

---

## 8. Модель ошибок (Error Model)

1. **Сертификатный интерфейс `/api/certificates/` (Windows-1251 XML):**
   ```xml
   <?xml version="1.0" encoding="windows-1251"?>
   <Response>
       <Result>Error</Result>
       <code>{code}</code>
       <Description>{description}</Description>
   </Response>
   ```
   - `code=0`: Успешная операция (`<Result>OK</Result>`).
   - `code=1`: Системная ошибка, сбой обращения к CA, неподдерживаемая функция.
   - `code=2`: Ошибка валидации PIN («PIN не указан», «Пин-код не существует», истек срок действия).
   - `code=4`: Ошибка полезной нагрузки («PKCS10 не найден в теле запроса»).
2. **Терминальный биллинг `/api/licensebilling` (UTF-8 XML):**
   - `HTTP 401 Unauthorized`: клиентский сертификат отсутствует, терминал не найден в БД или `cert_serial` не совпадает со строгим значением `iot.leo4.ru`.
   - `HTTP 200 OK` с `<Result>ERROR</Result><Description>...</Description>`: бизнес-ошибки терминального шлюза.
3. **Pfx Mapping `/api/devices/map_legacy_crt/` (JSON):**
   - `HTTP 401`: отсутствие валидного mTLS контекста терминала.
   - `HTTP 400`: невалидный запрос или отсутствующий сертификат.
   - `HTTP 500`: сбой криптографической сборки PKCS#12.

---

## 9. Единственная Alembic-цепочка и владение схемой

- **Единственный авторитет миграций:** каталог `ProcessingBackend/backend/alembic/versions/`. Сервис `MenuBuilder` не содержит собственных миграций и использует схему `ProcessingBackend`.
- **Текущая ревизия head:** `026` (`026_add_terminal_gauge_states.py`).
- **История ревизий:** непрерывная линейная последовательность из 26 миграций (`001` -> `026`).
- **Связанный пакет моделей:** `shared/etranprocessing_db` (версия `0.1.0`), единый declarative layer SQLAlchemy 2.0.
- **Статус на production сервере (`87.242.100.34`):**
  - Команда `alembic current` вернула: `026 (head)`.
  - База данных полностью синхронизирована с репозиторием.

---

## 10. Production Deployment State & Read-only Smoke Evidence

Диагностика выполнена на боевом сервере `user1@87.242.100.34` (SSH-ключ `d:\.ssh\id_ed25519`):

### 10.1. Статус контейнеров и окружения
- **Контейнер:** `processing-backend` (ID: `6027b8f2e784`, создан: `2026-09-09T08:01:27Z`, статус: `Up 8 days`).
- **Образ:** `user1-processing-backend` (Digest: `sha256:60c1927cb745fd70370b148960f93548d360004772ee18db4c08245703023d00`).
- **Git Commit на хосте:** `5de0e6a39d89caae3fe0da517435f3708dfba2f7`.
- **Сеть / Внутренний IP:** `172.19.0.5:8000`.
- **Ресурсы хоста:** Load Average `0.18`, Свободно RAM `2219 MiB`, Корневой диск `50%` (25G свободно).

### 10.2. Read-only Provider Smoke Probes (Без выпуска PIN)
1. **Проверка жизнеспособности сервиса (`/api/health`):**
   - Запрос: `curl -s -i http://172.19.0.5:8000/api/health`
   - Ответ:
     ```http
     HTTP/1.1 200 OK
     date: Thu, 17 Sep 2026 15:17:24 GMT
     server: uvicorn
     content-length: 15
     content-type: application/json

     {"status":"ok"}
     ```
2. **Проверка роутера сертификатов с несуществующим PIN (`/api/certificates/?function=check`):**
   - Запрос: `curl -s -i "http://172.19.0.5:8000/api/certificates/?function=check&pin=999999"`
   - Ответ:
     ```http
     HTTP/1.1 200 OK
     date: Thu, 17 Sep 2026 15:17:41 GMT
     server: uvicorn
     content-length: 169
     content-type: text/xml; charset=utf-8

     <?xml version="1.0" encoding="windows-1251"?>
     <Response><Result>Error</Result><code>2</code><Description>Пин-код не существует</Description></Response>
     ```
   - **Результат:** Сервис штатно обратился к базе данных, подтвердил отсутствие PIN, сформировал корректный XML-ответ в Windows-1251 с `code=2`. Никакие данные в БД не модифицированы, PIN для реальных терминалов не создавался.

---

## 11. Результаты локального тестирования и статического анализа

Все проверки выполнены с помощью `uv` в окружении Python 3.14:

1. **Ruff linter:**
   - Команда: `uv --directory ProcessingBackend\backend run ruff check app/`
   - Результат: `All checks passed!` (0 замечаний).
2. **Ruff formatter:**
   - Команда: `uv --directory ProcessingBackend\backend run ruff format --check app/`
   - Результат: `30 files already formatted`.
3. **Pyright type checker:**
   - Команда: `uv --directory ProcessingBackend\backend run pyright app/`
   - Результат: `0 errors, 0 warnings, 0 informations`.
4. **Pytest (Сертификатные тесты `ProcessingBackend/backend`):**
   - Команда: `uv --directory ProcessingBackend\backend run pytest tests/test_ca_signing.py tests/test_cert_serial_auto_bind.py tests/test_terminal_cert_discovery.py tests/test_pfx.py tests/test_devices_legacy_router.py --basetemp=.pytest_tmp -v`
   - Результат: `22 passed in 7.60s`.
5. **Pytest (Полный набор тестов `ProcessingBackend/backend`):**
   - Команда: `uv --directory ProcessingBackend\backend run pytest --basetemp=.pytest_tmp`
   - Результат: `106 passed in 29.73s`.
6. **Pytest (`ProcessingBackend/mcp-pin-server`):**
   - Команда: `uv --directory ProcessingBackend\mcp-pin-server run pytest --basetemp=.pytest_tmp`
   - Результат: `10 passed in 3.84s`.

---

## 12. Gap Analysis для будущих фаз (`04B` и `06A`)

По результатам инвентаризации зафиксированы следующие пробелы (gaps), подлежащие реализации в целевых prompt-шагах:

### 12.1. Задачи для шага `04B` (Security & Certificate Enrollment Hardening)
1. **Криптографическая валидация подписи CSR:**
   - В текущем коде `_handle_setup` и `ca_sign_csr.py` CSR загружается методом `x509.load_pem_x509_csr`, но проверка валидности цифровой подписи (`csr.is_signature_valid`) явно не вызывается перед отправкой на подпись в CA.
2. **Конкурентная защита (Race Condition) при вводе PIN:**
   - В транзакции `_handle_setup` отсутствует пессимистическая блокировка строки (`SELECT ... FOR UPDATE`) для `certificate_pins`. Одновременные параллельные запросы с одним PIN могут инициировать параллельные вызовы подписи в CA.
3. **Поддержка безопасного повтора (Safe Retry):**
   - Если CA успешно выпустил сертификат, но на этапе отправки ответа клиенту произошел разрыв соединения, статус PIN уже переведен в `used`. Терминал не может повторить запрос и оказывается заблокирован. Требуется кэширование выпущенного сертификата на время жизни PIN для безопасного идемпотентного повтора.
4. **Валидация формата SAN и серийного номера:**
   - Включение строгой проверки регулярным выражением формата серийного номера киоска перед генерацией SAN-расширений.

### 12.2. Задачи для шага `06A` (Unified Orchestration & Terminal Provisioning)
1. **Событийная интеграция жизненного цикла PIN с брокером сообщений:**
   - Отсутствует публикация событий `pin_created`, `pin_used`, `cert_issued` в шину RabbitMQ для синхронизации с `iot-rpc-rest-app` и медиа-контуром `l4media`.
2. **Автоматическая инвалидация и отзыв сертификатов (CRL / OCSP):**
   - В архитектуре `ProcessingBackend` отсутствует эндпоинт генерации списка отзыва сертификатов (CRL) или responder OCSP для немедленного блокирования скомпрометированных киосков на уровне Nginx mTLS.
3. **Автоматическая ротация сертификатов (Zero-Touch Renewal):**
   - Отсутствует механизм автоматического продления сертификата по действующему mTLS-соединению без необходимости ручной генерации одноразового PIN через административный интерфейс.

---

## 13. Замечания по откату и верификации (Rollback & Read-only Note)

- Настоящий шаг носил исключительно исследовательский, инвентаризационный и baseline-характер.
- Никакие файлы исходного кода приложений (`ProcessingBackend`, `MenuBuilder`, `shared`), конфигурации контейнеров или схемы баз данных не модифицировались.
- Запросы к боевой среде `87.242.100.34` производились строго в режиме read-only.
- Процедура отката не требуется.

---

## 14. Целевой потребитель handoff-контракта

- **Выходной контракт:** `H-L4D-00C-PB-v1`
- **Непосредственный потребитель:** `L4D-00D-MEDIA`
- **Следующий prompt каскада:** `L4D-00D-MEDIA — Зафиксировать baseline медиаконтура`
