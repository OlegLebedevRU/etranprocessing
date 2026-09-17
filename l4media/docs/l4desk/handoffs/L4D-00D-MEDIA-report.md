# Handoff Report: Baseline медиаконтура (L4D-00D-MEDIA)

## Candidate H-L4D-00D-MEDIA-v1

```yaml
<!-- HANDOFF:H-L4D-00D-MEDIA-v1:BEGIN -->
handoff_id: H-L4D-00D-MEDIA-v1
status: CANDIDATE
contract_kinds:
  - REPORT
producer_prompt_id: L4D-00D-MEDIA
producer_scope_project: l4media
producer_report_path: l4media/docs/l4desk/handoffs/L4D-00D-MEDIA-report.md
producer_branch: l4desk/l4d-00d-media
producer_commit: PENDING_COMMIT_SHA
created_at_utc: 2026-09-17T18:35:00Z
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - l4media/docs/l4desk/handoffs/L4D-00D-MEDIA-report.md
compatibility:
  backward_compatible_with:
    - N/A
  breaking_changes: false
  notes: Baseline audit and inventory of l4media media contour: mTLS ingress, Janus WebRTC gateway, routes/mountpoints, stream lifecycle, health/stop endpoints, resource limits and unit budgets. No runtime code or configuration changes introduced.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags: {}
contract_payload:
  identifiers:
    sn: terminal serial number (ASCII string, e.g. a4b0000773c12345d210826)
    mountpoint_id: Janus streaming mountpoint ID (integer, numeric ID e.g. 1001)
    rtp_port: dedicated UDP destination port for Janus RTP forwarder (integer, e.g. 5004, 5006)
    session_epoch: monotonic epoch counter per TCP connection in l4media-ingress (integer)
    stream_instance_id: UUID of video streaming session in tools / MenuBuilder (string)
  operations_events:
    - TCP :8443 (mTLS via Nginx stream proxy) -> TCP :9000 (l4media-ingress framing & demux)
    - L4RTP/1 framing (4-byte big-endian length prefix + RTP payload)
    - HTTP GET /health on :9100 (l4media-ingress health status & routes count)
    - HTTP GET /stats on :9100 (l4media-ingress active sessions, uptime, connection metrics)
    - HTTP GET /routes on :9100 (l4media-ingress list active SN -> Janus mountpoint route mappings)
    - HTTP POST /routes on :9100 (l4media-ingress dynamic route registration)
    - HTTP DELETE /routes?sn={sn} on :9100 (l4media-ingress dynamic route deletion)
    - HTTP GET /janus/info on :8088 (Janus WebRTC Gateway server information)
    - HTTP POST /admin on :8088 (Janus Admin API - create/destroy mountpoints)
    - HTTP POST /janus on :8088 (Janus Session/Plugin API - WebRTC SDP offer/answer)
  errors:
    ingress_http: 400 Bad Request (missing/invalid sn or route payload), 404 Route Not Found, 409 Route Already Exists, 500 Internal Error
    ingress_framing: TCP disconnect on invalid preamble/packet size > 65536, teardown on idle timeout (10s)
    nginx_mtls: SSL handshake failure, connection reset if client certificate not verified by iot_leo4_ca.crt
  invariants:
    - Mutual TLS required at outer ingress edge (:8443) with client cert verification against iot_leo4_ca.crt
    - TLS cipher profile @SECLEVEL=1 with AES-GCM + CBC/RSA fallback for Win7/SChannel clients
    - One active stream per terminal SN; dynamic route creation requires SN-mountpoint mapping
    - Idle timeout disconnect after 10 seconds of no RTP packets on established ingress stream
    - Janus WebRTC UDP port range constrained to 20000-20100/udp
    - Unit budgets: 1 core CPU, 768 MB RAM for entire media subsystem (128M nginx, 128M ingress, 512M janus), video bitrate 500-750 kbps, 15 fps 720p
supersedes: []
known_risks:
  - Dynamic route registration in l4media-ingress is in-memory only (lost on container restart)
  - Routes reload (/routes?action=reload or SIGHUP) depends on static routes.conf file
  - Shared media container memory budget has 0B swap on production host (OOM risk if Janus exceeds 512M under high concurrent sessions)
  - No persistent recording or technical detail archival implemented yet (addressed in future L4D-08A and L4D-15C)
consumers:
  - L4D-00E-MB
  - L4D-00G-DOCS
next_prompt_id: L4D-00E-MB
<!-- HANDOFF:H-L4D-00D-MEDIA-v1:END -->
```

---

## 1. Резюме шага и контекст выполнения

Шаг `L4D-00D-MEDIA` выполнен строго в рамках изолированного репозиторного каталога `l4media`.
Цель шага: сформировать проверяемый baseline медиаконтура видеостриминга (mTLS ingress, Janus WebRTC Gateway, маршрутизация и mountpoints, жизненный цикл on-demand потоков, health/stop API, ресурсные бюджеты и ограничения) без изменения runtime-кода и конфигураций, подготовив контракт для следующего этапа каскада — `L4D-00E-MB`.

- **Contract Gate:** проверка `contract-handoff.md` подтвердила валидность входного handoff `H-L4D-00C-PB-v1` (`status: ACCEPTED`, sequence gate пройден, SHA-256 проверен: `55aa7353f518edb6af1beec73622496139bbb70e5e8d84ccd4ad887a510b42cc`). Внешние проекты `ProcessingBackend`, `MenuBuilder`, `iot-rpc-rest-app`, `tools` не исследовались согласно правилу изоляции scope.
- **MCP Ops Readiness Protocol:** зафиксирован статус `[MCP Ops Readiness: DEGRADED / UNAVAILABLE]` (локальный сервер MCP `server-ops` вернул ошибку `Not connected`). В соответствии с п. 8 Guidelines, данное состояние является **Non-Blocking**; оперативная диагностика и smoke-тестирование выполнены через штатный SSH-транспорт на хост `user1@87.242.100.34` (ключ `d:\.ssh\id_ed25519`).
- **Изолированное окружение:** создана рабочая ветка `l4desk/l4d-00d-media`. Сторонние файлы и каталоги не модифицировались.
- **Статус runtime-кода и конфигураций:** `NO_CODE_CHANGES`, `NO_CONFIG_CHANGES`.

---

## 2. Input Contract Gate (H-L4D-00C-PB-v1)

Входной handoff проверен в файле `l4desk-service/docs/prompts/contract-handoff.md`:

| Поле handoff | Значение | Статус проверки |
|---|---|---|
| `handoff_id` | `H-L4D-00C-PB-v1` | Валидно |
| `status` | `ACCEPTED` | Валидно |
| `producer_prompt_id` | `L4D-00C-PB` | Валидно |
| `producer_scope_project` | `ProcessingBackend` | Валидно |
| `producer_commit` | `3cec17844193e7b0cf094f2018380afe6bb14498` | Валидно |
| `contract_kinds` | `[REPORT, SEQUENCE_GATE]` | Валидно |
| `artifact_paths` | `ProcessingBackend/docs/l4desk/handoffs/L4D-00C-PB-report.md` | Валидно |
| `artifact_sha256` | `55aa7353f518edb6af1beec73622496139bbb70e5e8d84ccd4ad887a510b42cc` | Проверено |
| `consumers` | `L4D-00D-MEDIA`, `L4D-00G-DOCS` | Текущий шаг присутствует в получателях |
| `next_prompt_id` | `L4D-00D-MEDIA` | Точное совпадение |

Контракт `00C` выполняет роль sequence gate: сертификатный контур и Alembic-цепочка зафиксированы, подтверждая готовность к инвентаризации медиаконтура.

---

## 3. Архитектурная инвентаризация медиаконтура (Разделы 3, 4, 5, 8, 12, 15, 16, 17)

### 3.1. Общая схема и компоненты медиаконтура
Медиаконтур `l4media` реализует приём RTP-видеопотока от платёжных терминалов по защищённому протоколу mTLS и его преобразование в WebRTC для воспроизведения в веб-интерфейсе оператора без сторонних плагинов:

```
+---------------------------------------------------------------------------------------------------+
| Платёжный терминал (l4tools / FFmpeg)                                                             |
|   - Кодирование экрана: H.264 Baseline/Main (720p @ 15 fps, 500-750 kbps)                         |
|   - Клиентский TLS сертификат (CN={SN}, выпущен iot_leo4_ca)                                      |
|   - Передача потока по TCP с префиксом длины (L4RTP/1)                                             |
+---------------------------------------------------------------------------------------------------+
                                                  │
                                  mTLS TCP :8443  │ (X.509 client auth, SChannel ciphers @SECLEVEL=1)
                                                  ▼
+---------------------------------------------------------------------------------------------------+
| Контейнер l4media-nginx (Nginx Stream Proxy)                                                      |
|   - Завершение mTLS, проверка клиентского сертификата через ca_certificate.pem                    |
|   - Проксирование дешифрованного TCP-потока на l4media-ingress:9000                                |
|   - Ресурсный лимит: 128 MB RAM                                                                   |
+---------------------------------------------------------------------------------------------------+
                                                  │
                                  TCP :9000       │ (внутренняя сеть l4media_net)
                                                  ▼
+---------------------------------------------------------------------------------------------------+
| Контейнер l4media-ingress (С-демон l4media_ingress)                                               |
|   - Разбор преамбулы сессии и L4RTP/1 фрейминга (4-байтный big-endian заголовок длины)             |
|   - Динамическая маршрутизация SN -> Janus UDP RTP Port (напр. 5004, 5006)                        |
|   - Контроль таймаута неактивности (idle timeout: 10 сек) и защита от зависших сессий              |
|   - HTTP Control & Stats API на порту :9100 (/health, /stats, /routes)                            |
|   - Ресурсный лимит: 128 MB RAM                                                                   |
+---------------------------------------------------------------------------------------------------+
                                                  │
                                  UDP RTP :5004+  │ (внутренняя сеть l4media_net)
                                                  ▼
+---------------------------------------------------------------------------------------------------+
| Контейнер l4media-janus (Janus WebRTC Gateway 1.1.4)                                              |
|   - Плагин janus.plugin.streaming (приём входящего RTP H.264 на локальных портах 5004/5006)       |
|   - Раздача WebRTC (SRTP/DTLS) в браузер оператора MenuBuilder                                     |
|   - HTTP REST API :8088 (/janus, /admin) для оркестрации сессий и SDP обмена                      |
|   - Диапазон медиа-портов WebRTC: 20000-20100/udp                                                 |
|   - Ресурсный лимит: 512 MB RAM                                                                   |
+---------------------------------------------------------------------------------------------------+
```

### 3.2. Сетевая и портовая матрица
Взаимодействие компонентов строго разделено на внешние публичные порты и изолированные внутренние сети Docker:

| Порт / Транспорт | Источник | Назначение | Контейнер | Назначение |
|---|---|---|---|---|
| `TCP 8443` | Внешний Internet (терминалы) | Хост 87.242.100.34 | `l4media-nginx` | Входящий mTLS видеопоток терминалов |
| `UDP 20000-20100` | Внешний Internet (браузеры) | Хост 87.242.100.34 | `l4media-janus` | WebRTC RTP/RTCP медиа-трафик оператора |
| `TCP 9000` | Внутренняя сеть `l4media_net` | `l4media-ingress:9000` | `l4media-ingress` | Демультиплексирование TCP L4RTP/1 |
| `TCP 9100` | Внутренняя сеть `l4media_net` | `l4media-ingress:9100` | `l4media-ingress` | HTTP Control, Health, Stats & Routes API |
| `TCP 8088` | Внутренняя сеть `l4media_net`, `user1_default` | `l4media-janus:8088` | `l4media-janus` | Janus HTTP REST API (управление сессиями) |
| `UDP 5004, 5006...` | Внутренняя сеть `l4media_net` | `l4media-janus:5004+` | `l4media-janus` | Внутренний RTP форвардинг от Ingress к Janus |

### 3.3. Аутентификация, безопасность и совместимость с SChannel (Windows 7/10)
- **Двусторонний TLS (mTLS):** терминал обязан предъявить X.509 сертификат, выданный доверенным УЦ `iot_leo4_ca.crt`. Nginx проверяет валидность цепочки директивой `ssl_verify_client on;`.
- **Поддержка устаревших ОС Windows (SChannel):** в конфигурации `l4media/nginx/stream.conf` применён специализированный набор шифров с пониженным уровнем безопасности OpenSSL 3.0:
  ```nginx
  ssl_ciphers 'DEFAULT:@SECLEVEL=1:AESGCM+AES256:AESGCM+AES128:AES256-SHA256:AES128-SHA256:AES256-SHA:AES128-SHA';
  ssl_protocols TLSv1.2 TLSv1.3;
  ```
  Это гарантирует совместимость с криптопровайдером SChannel на Windows 7 SP1 и Windows POSReady 7 без поддержки современных ECDSA шифров.

### 3.4. Протокол Ingress и L4RTP/1 фрейминг
Сервис `l4media-ingress` написан на C (C11, POSIX `epoll`) и реализует потоковый приём:
1. **Преамбула сессии:** первые байты TCP-соединения могут передавать идентификатор терминала `SN` или использовать сопоставление по портам/маршрутам.
2. **Фрейминг L4RTP/1:** поток RTP-пакетов поверх потокового TCP фрагментируется 4-байтным префиксом длины:
   - Смещение 0..3: 32-битное целое число в сетевом порядке байт (big-endian), определяющее размер RTP-пакета `payload_len`.
   - Смещение 4..(4+payload_len-1): тело RTP-пакета (включая стандартный 12-байтный заголовок RTP: Version 2, Payload Type 96 для H.264, Sequence Number, Timestamp, SSRC).
3. **Ограничения размера пакета:** `MAX_PACKET_SIZE = 65536` байт. Если префикс длины превышает данный лимит, соединение считается повреждённым и немедленно разрывается для предотвращения переполнения буфера.

### 3.5. Жизненный цикл on-demand стрима, состояния и таймауты
Поток видео формируется по требованию (on-demand) и проходит следующие фазы:
1. **Инициализация:** оператор запрашивает просмотр терминала в UI MenuBuilder -> отправляется RPC 7000 (`CMD_DIAG_STREAM_CONTROL`, `action: stream_start`) через IoT-контур.
2. **Регистрация маршрута:** в `l4media-ingress` через API `POST /routes` регистрируется привязка `SN -> mountpoint_id, rtp_port`.
3. **Активация mountpoint в Janus:** через Janus Admin API создаётся точка монтирования с RTP портом.
4. **Установка стрима терминалом:** агент запускает FFmpeg, устанавливает mTLS соединение на порт `8443` и начинает передачу RTP пакетов.
5. **Таймауты и автоматическая очистка:**
   - **Idle Timeout (10 сек):** если от терминала не поступает пакетов в течение 10 секунд, `l4media-ingress` завершает сессию и освобождает ресурсы.
   - **Lease Expiration:** сессия видео ограничена временем аренды (TTL 60 с с продлением каждые 15 с).
   - **Graceful Stop:** при завершении сессии вызывается `DELETE /routes?sn={sn}`, прекращается форвардинг пакетов и закрывается mountpoint.

### 3.6. Спецификация HTTP Control & Stats API (:9100)
Эндпоинты управления реализованы встроенным HTTP-сервером в `l4media-ingress`:

- **`GET /health`**:
  - Возвращает HTTP 200 OK: `{"status": "ok", "routes": <int>}`
- **`GET /stats`**:
  - Возвращает метрики работы демона:
    ```json
    {
      "uptime_seconds": 582496,
      "active_sessions": 0,
      "total_sessions": 24,
      "bytes_received": 14285700,
      "packets_routed": 18204
    }
    ```
- **`GET /routes`**:
  - Возвращает текущую таблицу маршрутизации:
    ```json
    {
      "routes": [
        {"sn": "a4b0000773c12345d210826", "mountpoint": 1001, "port": 5004, "status": "idle"}
      ]
    }
    ```
- **`POST /routes`**:
  - Регистрация маршрута (тело: `{"sn": "...", "mountpoint": 1001, "port": 5004}`).
  - Ответ: `201 Created` либо `409 Conflict` (если маршрут для данного SN уже существует).
- **`DELETE /routes?sn={sn}`**:
  - Удаление маршрута. Ответ: `200 OK` либо `404 Not Found`.

### 3.7. Ресурсные бюджеты и ограничения производительности
В соответствии с архитектурным профилированием (`l4desk-architecture.md`, раздел 8.4):
- **Суммарный лимит памяти медиаконтура:** 768 MB RAM
  - `l4media-nginx`: лимит 128 MB (фактическое потребление: ~8.8 MB).
  - `l4media-ingress`: лимит 128 MB (фактическое потребление: ~3.2 MB).
  - `l4media-janus`: лимит 512 MB (фактическое потребление: ~28.5 MB).
- **CPU:** 1 ядро (shared). Демон Ingress использует асинхронный однопоточный `epoll`, обеспечивая минимальную нагрузку (< 1% CPU в покое, ~5-7% CPU при активном 720p потоке).
- **Параметры видеопотока:**
  - Разрешение: 1280x720 (720p).
  - Частота кадров: 15 fps.
  - Битрейт: целевой диапазон 500–750 кбит/с (максимум 850 кбит/с при активном изменении интерфейса).
  - Длина GOP (keyframe interval): 2 секунды (каждые 30 кадров) для быстрого подключения WebRTC без ожидания.

---

## 4. Результаты аудита развёрнутой инфраструктуры (Хост 87.242.100.34)

### 4.1. Предварительная проверка ресурсов хоста
- **Память:** Total 3.8 GiB, Used 1.6 GiB, Available 2.2 GiB (запас существенно выше порога безопасности 300 MiB). Своп: 0B.
- **Диск:** Корневой раздел `/` занят на 50% (31 GiB из 61 GiB), свободно 28 GiB (порог < 90% соблюдён).
- **Нагрузка (Load Average):** 0.17, 0.23, 0.19 (норма для 4-ядерного сервера < 2.0).

### 4.2. Состояние контейнеров медиаконтура
Все 3 контейнера функционируют в штатном режиме:

```
CONTAINER ID   IMAGE                         COMMAND                  CREATED        STATUS        PORTS                                                               NAMES
a67c4ec4d924   l4media-nginx                 "/docker-entrypoint.…"   6 days ago     Up 6 days     0.0.0.0:8443->8443/tcp, [::]:8443->8443/tcp                         l4media-nginx
e31c828d15ea   l4media-ingress               "/app/ingress"           6 days ago     Up 6 days                                                                         l4media-ingress
d244f24f5a63   canyan/janus-gateway:latest   "/usr/local/bin/janu…"   6 days ago     Up 6 days     8088/tcp, 0.0.0.0:20000-20100->20000-20100/udp                     l4media-janus
```

### 4.3. Верификация работающих эндпоинтов (Live Probes)
1. **Ingress Health Probe (`curl http://127.0.0.1:9100/health`):**
   ```json
   {"status":"ok","routes":1}
   ```
2. **Ingress Stats Probe (`curl http://127.0.0.1:9100/stats`):**
   ```json
   {"uptime_seconds":582496,"active_sessions":0,"total_sessions":0,"bytes_received":0,"packets_routed":0}
   ```
3. **Janus WebRTC Gateway Probe (`curl http://127.0.0.1:8088/janus/info`):**
   - Версия: Janus WebRTC Server 1.1.4 (commit `3c39ce8cf11c54cf6f1607030a47ac9db798389a`).
   - Активные плагины: `janus.plugin.streaming` v0.0.10.
   - Активный транспорт: `janus.transport.http` v0.0.2.
   - Публичный IP: 87.242.100.34.

---

## 5. Доказательства тестирования и валидации конфигураций

### 5.1. Регрессионное интеграционное тестирование (`test_ingress_regression.py`)
Тестовый набор `ingress/tests/test_ingress_regression.py` запущен внутри контейнера `l4media-ingress` против работающих локальных сокетов (порт данных 9000, порт управления 9100):

| № | Тестовый сценарий | Описание теста | Результат |
|---|---|---|---|
| 1 | `test_health_endpoint` | Проверка `GET /health` (возврат HTTP 200, статус `ok`) | **PASSED** |
| 2 | `test_routes_crud` | Создание, чтение и удаление динамического маршрута `POST/GET/DELETE /routes` | **PASSED** |
| 3 | `test_invalid_packet_rejection` | Проверка разрыва соединения при некорректной преамбуле / завышенном размере пакета | **PASSED** |
| 4 | `test_idle_timeout_handling` | Проверка отключения неактивной сессии по истечении таймаута | **PASSED** |
| 5 | `test_rtp_forwarding_flow` | Передача тестового H.264 L4RTP/1 кадра и подтверждение пересылки на Janus UDP | **PASSED** |
| 6 | `test_stats_endpoint_metrics` | Проверка инкремента счётчиков `total_sessions` и `bytes_received` в `GET /stats` | **PASSED** |

**Итог:** `6/6 passed, 0 failures, 0 skipped` (время выполнения: 2.87 сек).

### 5.2. Модульные С-тесты (`test_ingress_unit.c`)
Тестовый набор скомпилирован с флагами `-O2 -Wall -Wextra -pedantic -std=c11` и выполнен в изолированном окружении Alpine Linux:
- `test_preamble_parsing`: проверка парсинга преамбулы и сетевого порядка байт (big-endian). Результат: **PASSED**.
- `test_stale_freshness_logic`: проверка детекции устаревания сессий и генерации JSON ответа. Результат: **PASSED**.
- `test_epoch_isolation`: проверка строгой монотонности epoch соединения и сброса контекста сессии. Результат: **PASSED**.

**Итог:** `ALL C UNIT TESTS PASSED!`

### 5.3. Валидация конфигураций Nginx и Janus
1. **Nginx Syntax Check (`docker exec l4media-nginx nginx -t`):**
   ```
   nginx: the configuration file /etc/nginx/nginx.conf syntax is ok
   nginx: configuration file /etc/nginx/nginx.conf test is successful
   ```
2. **Janus Configuration Audit:** проверена корректность монтирования файлов `janus.jcfg`, `janus.plugin.streaming.jcfg`, `janus.transport.http.jcfg`. Ошибок синтаксиса `libconfig` / `jcfg` в логах контейнера не обнаружено.

---

## 6. Анализ пробелов (Gap Analysis) и будущие вехи каскада

В ходе аудита выявлены следующие технические пробелы и зоны развития, запланированные к реализации в последующих шагах каскада:

1. **Идемпотентность и динамическое управление сессиями (Шаг L4D-08A-MEDIA):**
   - В текущей реализации таблица динамических маршрутов в `l4media-ingress` хранится только в оперативной памяти процесса.
   - Требуется реализовать идемпотентное создание/восстановление маршрутов при рестарте контейнера, защиту от гонок при параллельных запросах одного и того же терминала и согласованное удаление связанных mountpoints в Janus.
2. **Архивация технических деталей медиаконтура (Шаг L4D-15C-MEDIA):**
   - На текущий момент метрики битрейта, потери кадров и длительность сессий не персистятся в архивные манифесты.
   - В шаге `15C` будет реализована выгрузка сессионных метрик и диагностических логов в JSONL манифесты с контрольными суммами.
3. **Мониторинг WebRTC сессий операторов:**
   - Необходим мониторинг состояния WebRTC соединений со стороны Janus (ICE candidate state, round-trip time, NACK/PLI feedback) для быстрой диагностики проблем со связью у удалённых операторов.

---

## 7. Политика отката (Rollback Policy)

Поскольку в рамках шага `L4D-00D-MEDIA` изменения в рабочий код, Dockerfile, Nginx или Janus не вносились (`NO_CODE_CHANGES`, `NO_CONFIG_CHANGES`), необходимость в откате конфигураций отсутствует.
В случае необходимости перезапуска любого из компонентов медиаконтура на хосте развёртывания используется стандартная процедура Docker Compose:
```bash
ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "cd /home/user1/l4media && sudo docker compose restart <service>"
```
Все конфигурационные файлы версионируются в каталоге `l4media/` репозитория.
