# Единый журнал передачи контрактов L4Desk

**Назначение:** единственный источник фактически зафиксированных межагентных контрактов каскада.  
**Режим:** append-only после секции «Принятые handoff-блоки».  
**Начальное состояние:** `READY_FOR_00A`.  
**Версия формата:** `1.0.0`.

Ни архитектурный документ, ни исходный код соседнего проекта, ни deployed endpoint, ни память агента не заменяют запись в этом журнале. Если требуемого принятого блока нет, агент не предполагает контракт и возвращает `BLOCKED_CONTRACT`.

## 1. Однозначное чтение входного контракта

Для каждого `required_handoff_id`, указанного в локальном промпте, агент обязан:

1. Искать точную пару HTML-маркеров:
   - `<!-- HANDOFF:<required_handoff_id>:BEGIN -->`;
   - `<!-- HANDOFF:<required_handoff_id>:END -->`.
2. Требовать ровно одну пару маркеров. Ноль или более одной пары означает `BLOCKED_CONTRACT`.
3. Разбирать только YAML-блок между маркерами; окружающий текст не является контрактом.
4. Проверять одновременно:
   - `handoff_id` точно совпадает с искомым;
   - `status` равен `ACCEPTED`;
   - `consumers` содержит текущий `prompt_id` либо точное значение `ALL_FOLLOWING`;
   - `contract_version` не пуст;
   - `contract_kinds` — непустой список разрешённых значений;
   - `artifact_paths` и `artifact_sha256` имеют одинаковое ненулевое число элементов, если `contract_kinds` не состоит только из `SEQUENCE_GATE`;
   - отсутствуют `TBD`, `TODO`, `UNKNOWN`, незаполненные обязательные поля;
   - `accepted_at_utc`, `producer_commit`, `producer_report_path` заполнены;
   - `deployment_status` соответствует требованию локального prompt;
   - блок не отозван более поздним `REVOCATION` и не заменён несовместимой записью `supersedes`.
5. Зафиксировать `handoff_id`, `contract_version` и digest артефактов в отчёте до начала реализации.
6. Не объединять конфликтующие версии самостоятельно. При конфликте остановиться.

`artifact_paths` — пути или immutable URL, доступные агенту без просмотра исходного кода чужого проекта. Digest считается по байтам опубликованного артефакта. Текст `contract_payload` может содержать краткую нормативную семантику, но крупные OpenAPI/JSON Schema/golden fixtures передаются отдельными immutable artifacts.

## 2. Кто пишет журнал

- Runtime-агент с `scope_project`, отличным от `l4desk-service`, **не редактирует этот файл**. Он помещает полностью заполненный candidate-блок в отчёт своего проекта.
- Контроллер каскада независимо проверяет отчёт, commit, push, deploy и smoke, затем добавляет candidate-блок в конец этого файла одной append-only операцией.
- Агент `l4desk-service` может сам добавить свой блок, потому что файл находится в его единственном scope.
- Принятый блок не исправляется на месте. Ошибка оформляется новым `REVOCATION`, затем новым handoff с новым идентификатором/версией и явным `supersedes`.
- До фактического появления принятого блока следующий агент не запускается.

## 3. Канонический формат принятого handoff

````text
<!-- HANDOFF:H-L4D-XX-v1:BEGIN -->
```yaml
handoff_id: H-L4D-XX-v1
status: ACCEPTED
contract_kinds:
  - API
  - EVENT
producer_prompt_id: L4D-XX-SCOPE
producer_scope_project: exact-project-name
producer_report_path: immutable/path/or/url/to/report.md
producer_branch: l4desk/l4d-xx-scope
producer_commit: full-commit-sha
accepted_at_utc: 2026-09-17T12:00:00Z
contract_version: exact-semver-or-revision
schema_revision: exact-revision-or-N/A
artifact_version: exact-version-or-N/A
artifact_paths:
  - immutable/path/or/url/to/artifact
artifact_sha256:
  - lowercase-sha256
compatibility:
  backward_compatible_with:
    - exact-version
  breaking_changes: false
  notes: exact-normative-notes
deployment_status: DEPLOYED | PUBLISHED | DOCS_PUBLISHED
deployed_environment: production | artifact-registry | documentation
feature_flags:
  exact_flag: disabled | shadow | enabled
contract_payload:
  identifiers: exact identifiers or N/A
  operations_events: exact operations/events or N/A
  errors: exact error model or N/A
  invariants: exact normative invariants
supersedes: []
known_risks: []
consumers:
  - L4D-NEXT-PROMPT
next_prompt_id: L4D-NEXT-PROMPT
```
<!-- HANDOFF:H-L4D-XX-v1:END -->
````

В реальном блоке внешняя ограда Markdown вокруг YAML не добавляется: между HTML-маркерами размещается один fenced `yaml` block. Маркеры и `handoff_id` должны совпадать посимвольно.

## 4. Формат отзыва

````text
<!-- REVOCATION:H-L4D-XX-v1:BEGIN -->
```yaml
handoff_id: H-L4D-XX-v1
status: REVOKED
revoked_at_utc: 2026-09-17T13:00:00Z
reason: точная причина
replacement_handoff_id: H-L4D-XX-v2 | NONE
controller_commit: full-commit-sha
```
<!-- REVOCATION:H-L4D-XX-v1:END -->
````

Отозванный handoff запрещено использовать, даже если replacement ещё не принят.

## 5. Правила candidate-блока агента

Candidate обязан быть готов к дословной вставке и содержать только факты завершённой работы. Агенту запрещено:

- ставить `ACCEPTED` до зелёных проверок, push, deploy/publish и smoke;
- указывать несуществующий artifact path или вычислять digest «на глаз»;
- передавать секреты;
- описывать незавершённую семантику словами «будет», `TBD` или `TODO`;
- расширять `consumers` без необходимости;
- менять входной контракт внутри выходного блока.

Если контракт не создаётся, шаг всё равно выпускает handoff с `contract_kinds: [SEQUENCE_GATE]` либо `[REPORT]` и immutable отчётом с digest, чтобы следующий prompt мог доказать соблюдение последовательности.

## 6. Bootstrap

Каскад ещё не запускался. Единственный prompt, разрешённый без входного handoff: `L4D-00A-TOOLS`. После его принятия каждый следующий prompt обязан иметь как минимум handoff непосредственно предыдущего шага.

## 7. Принятые handoff-блоки

Новые блоки добавляются только ниже этой строки в порядке каскада. На момент создания журнала принятых runtime-контрактов нет.

<!-- HANDOFF:H-L4D-00A-TOOLS-v1:BEGIN -->
```yaml
handoff_id: H-L4D-00A-TOOLS-v1
status: ACCEPTED
contract_kinds:
  - FIXTURES
producer_prompt_id: L4D-00A-TOOLS
producer_scope_project: tools
producer_report_path: tools/docs/l4desk/handoffs/L4D-00A-TOOLS-report.md
producer_branch: l4desk/l4d-00a-tools
producer_commit: cff9ddfb4a023eb10e01cab84fe918847849a943
accepted_at_utc: 2026-09-17T17:10:00Z
contract_version: 1.7.7
schema_revision: N/A
artifact_version: 1.7.7
artifact_paths:
  - tools/docs/l4desk/fixtures/rpc_7000_stream_control.json
  - tools/docs/l4desk/fixtures/rpc_7001_exec.json
  - tools/docs/l4desk/fixtures/rpc_7002_cancel.json
  - tools/docs/l4desk/fixtures/mqtt_presence_lifecycle.json
  - tools/docs/l4desk/fixtures/l4rtp_wire_protocol.json
  - tools/docs/l4desk/fixtures/baseline_capabilities.json
  - https://l4tools-generic.ar.cloud.ru/l4tools/1.7.7/l4setup.exe
artifact_sha256:
  - dba8ff769f12239765c2317b96a8fa2e8f60cdccbc0de00113b9d4ce7e54b9b8
  - 4007bbf50a5280abfaf747f7c8a28344a4f61e2ac4f2a60a0c80aec5018578bf
  - 2965f6e24d8c88b82b08b6dec892c271750a91f1a89f82cc8eb1b19fa8779d22
  - f6a59507fc9cbed6c2f4bf707360affedb6344cac01b7f49b12d8cf3cff08e28
  - eb8500895d8f679cd0baa59e150c8b94e8a78d8eca5b7c9f086654c066c35277
  - 375412eaa61e31e72b2ea5f002862a1d1f02becdd4626a71af31d9400df560cb
  - 874f5444d2d4cc9bdee39e7c25a1265a2dd98f388aca49ef205ffb764531cd88
compatibility:
  backward_compatible_with:
    - 1.7.6
    - 1.7.7
  breaking_changes: false
  notes: Опубликованный baseline агента l4tools 1.7.7. Поддержка Windows NT 6.1+ (Win7 SP1, Win10, Win11), TLS 1.2 SChannel.
deployment_status: PUBLISHED
deployed_environment: artifact-registry
feature_flags:
  l4con_cmd_exec: enabled
  l4desk_desktop_stream: enabled
contract_payload:
  identifiers: SN (ASCII, 9-10 символов), stream_instance_id, command_id, session_id, taskId
  operations_events: Метод 7000 (CMD_DIAG_STREAM_CONTROL - inventory_get, stream_start, stream_stop, lease_renew, mouse_click, key_event, shortcut_action), Метод 7001 (CMD_DIAG_EXEC - cmd/powershell execution, выгрузка чанков в dev/{SN}/out и итоговый ответ в dev/{SN}/res), Метод 7002 (CMD_DIAG_CANCEL), LWT presence dev/{SN}/svc (extra_service) и dev/{SN}/app (main_app)
  errors: stream start rejected/already_running, desktop_locked, session_unavailable, input_rejected, exec ttl_sec timeout, sas ctrl_alt_del hardware intercept
  invariants: Ровно 1 активный видеопоток на терминал; автоматическое завершение по expires_at_ms; mTLS авторизация по CN={SN}; протокол L4RTP/1
supersedes: []
known_risks:
  - Прямой ввод Ctrl+Alt+Del через key_event перехватывается ядром Windows (требуется shortcut_action)
  - Пользовательский ввод отклоняется при заблокированной сессии Windows
  - Для Windows 7 SP1 требуется обновление безопасности с поддержкой TLS 1.2 (KB3140245)
consumers:
  - L4D-00B-IOT
  - L4D-00G-DOCS
next_prompt_id: L4D-00B-IOT
```
<!-- HANDOFF:H-L4D-00A-TOOLS-v1:END -->

<!-- HANDOFF:H-L4D-00B-IOT-v1:BEGIN -->
```yaml
handoff_id: H-L4D-00B-IOT-v1
status: ACCEPTED
contract_kinds:
  - REPORT
  - SEQUENCE_GATE
producer_prompt_id: L4D-00B-IOT
producer_scope_project: iot-rpc-rest-app
producer_report_path: docs/l4desk/handoffs/L4D-00B-IOT-report.md
producer_branch: l4desk/l4d-00b-iot
producer_commit: de431c15ca0064a9c2cbd3d9233d0b9921d85cff
accepted_at_utc: 2026-09-17T17:56:00Z
contract_version: 0.2.1
schema_revision: 2026_08_30_0003
artifact_version: 0.2.1
artifact_paths:
  - docs/l4desk/handoffs/L4D-00B-IOT-report.md
  - app-service/tests/core/test_l4d_00b_baseline_fixtures.py
artifact_sha256:
  - 136a65867165f6e33f55ef9c48054cd1020fb5d8f296bc4e62437032e0ceb7d8
  - a6a936fd972bdd6d04fe02290b8b14d852f0b8d1588cc479c0958a90c2e0b142
compatibility:
  backward_compatible_with:
    - 1.7.7
  breaking_changes: false
  notes: Совместимо с Agent fixtures 1.7.7 (все 333 теста iot-rpc-rest-app успешны, включая 12 тестов baseline fixtures). Подтверждена маршрутизация RabbitMQ srv.<SN>.{tsk,rsp,cmt,eva,ctl} и dev.<SN>.{req,ack,res,evt,out,ctl,app,svc}.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  alpha_billing: enabled
contract_payload:
  identifiers: SN (ASCII), taskId (UUID), clientId, session_id (UUID), command_id (UUID), lease_id (UUID)
  operations_events: RPC 7000 (CMD_DIAG_STREAM_CONTROL), 7001 (CMD_DIAG_EXEC), 7002 (CMD_DIAG_CANCEL), ctl low-latency input (pointer_move, mouse_click, key_event, shortcut_action), LWT app (app_online/app_offline), LWT svc (svc_online/svc_offline)
  errors: Стандартная модель ошибок REST API (400/401/403/404/409), MQTT NACK, task cancellation reason
  invariants: Инвариант единого воркера (WEB_CONCURRENCY=1 из-за in-memory lease/presence); не более 1 активного lease на терминал; app1 задеплоен на хосте 87.242.100.34 (Up 3 days, git commit 2488395)
supersedes: []
known_risks:
  - Состояние LeaseRegistry и pending-команд хранится в памяти процесса и требует WEB_CONCURRENCY=1 до внедрения распределённых блокировок
  - Отсутствует персистентный стрим событий с курсорами (будет реализован в L4D-02-IOT)
  - Взаимное исключение сессий консоли и видео на уровне всего сервиса ещё не объединено (будет реализовано в L4D-07-IOT)
consumers:
  - L4D-00C-PB
  - L4D-00G-DOCS
next_prompt_id: L4D-00C-PB
```
<!-- HANDOFF:H-L4D-00B-IOT-v1:END -->

<!-- HANDOFF:H-L4D-00C-PB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-00C-PB-v1
status: ACCEPTED
contract_kinds:
  - REPORT
  - SEQUENCE_GATE
producer_prompt_id: L4D-00C-PB
producer_scope_project: ProcessingBackend
producer_report_path: ProcessingBackend/docs/l4desk/handoffs/L4D-00C-PB-report.md
producer_branch: l4desk/l4d-00c-pb
producer_commit: 3cec17844193e7b0cf094f2018380afe6bb14498
accepted_at_utc: 2026-09-17T18:25:00Z
contract_version: 1.0.0
schema_revision: "026"
artifact_version: 0.1.0
artifact_paths:
  - ProcessingBackend/docs/l4desk/handoffs/L4D-00C-PB-report.md
artifact_sha256:
  - 55aa7353f518edb6af1beec73622496139bbb70e5e8d84ccd4ad887a510b42cc
compatibility:
  backward_compatible_with:
    - N/A
  breaking_changes: false
  notes: Baseline audit and inventory of ProcessingBackend certificate contour, terminal auth, Alembic migration chain (head 026), and runtime state. Deployed commit on host 87.242.100.34: 5de0e6a39d89caae3fe0da517435f3708dfba2f7. No code or schema changes introduced.
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
```
<!-- HANDOFF:H-L4D-00C-PB-v1:END -->

<!-- HANDOFF:H-L4D-00D-MEDIA-v1:BEGIN -->
```yaml
handoff_id: H-L4D-00D-MEDIA-v1
status: ACCEPTED
contract_kinds:
  - REPORT
  - SEQUENCE_GATE
producer_prompt_id: L4D-00D-MEDIA
producer_scope_project: l4media
producer_report_path: l4media/docs/l4desk/handoffs/L4D-00D-MEDIA-report.md
producer_branch: l4desk/l4d-00d-media
producer_commit: 47c9b3788aca3673effce91f372c0ebd66096069
accepted_at_utc: 2026-09-17T18:45:00Z
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - l4media/docs/l4desk/handoffs/L4D-00D-MEDIA-report.md
artifact_sha256:
  - e038e84838a27ea87ca389568215ae9c45886781eb3c262e18605d59273461b4
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
```
<!-- HANDOFF:H-L4D-00D-MEDIA-v1:END -->

<!-- HANDOFF:H-L4D-00E-MB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-00E-MB-v1
status: ACCEPTED
contract_kinds:
  - REPORT
  - SEQUENCE_GATE
producer_prompt_id: L4D-00E-MB
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-00E-MB-report.md
producer_branch: l4desk/l4d-00e-mb
producer_commit: dabccacf7d9d7227d898af6f4f50e12ca9bf3f99
accepted_at_utc: 2026-09-17T19:35:00Z
contract_version: 0.1.0
schema_revision: N/A
artifact_version: 0.1.0
artifact_paths:
  - MenuBuilder/docs/l4desk/handoffs/L4D-00E-MB-report.md
  - MenuBuilder/docs/l4desk/snapshots/openapi_baseline.json
  - MenuBuilder/docs/l4desk/snapshots/inventory_baseline.json
artifact_sha256:
  - cdae906ca929b14235b499331e7a562727a5c694a49ef6939714467f19891279
  - 3514eff4b7e0e1654314518564b5f290d155c139bc86eb0af04c32917e89c301
  - 31896fcace9f6f8331b8938cebb2913f28b2c9e19e7b69725a3cacd659211a28
compatibility:
  backward_compatible_with:
    - N/A
  breaking_changes: false
  notes: Baseline audit and inventory of MenuBuilder commercial and UX contour. Unified seams proven for VideoPlayerScreen + RemoteControlOverlay, DeviceConsoleTab, Billing/License flows and RBAC/Tenant isolation without alternative flows. All 289 backend tests and 20 frontend tests passing. Production deployed commit 890f485c558126ab4fb82905aa2ed2c4623eab7c verified on host 87.242.100.34. No runtime code changes.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  remote_control_enabled: enabled
contract_payload:
  identifiers:
    org_id: tenant organization ID (integer)
    user_id: user ID (integer)
    terminal_id: terminal primary key ID (integer)
    sn: terminal serial number (ASCII string, e.g. a4b0000773c12345d210826)
    order_id: billing order UUID (string)
    lease_id: remote input control lease UUID (string)
    mountpoint_id: Janus streaming mountpoint ID (integer)
  operations_events:
    - POST /api/auth/login, POST /api/auth/logout, POST /api/auth/refresh, GET /api/auth/me
    - GET /api/admin/tenants/available, POST /api/admin/tenants/switch
    - GET /api/billing/summary, GET /api/billing/status, GET /api/billing/licenses, POST /api/billing/checkout
    - POST /api/certificate-pin/generate, GET /api/certificate-pin/status/{terminal_id}
    - POST /api/v1/video/devices/{device_id}/session
    - POST /api/v1/video/control/lease/acquire, POST /api/v1/video/control/lease/{id}/keepalive, POST /api/v1/video/control/lease/{id}/release
    - GET /api/v1/video/control/devices/{sn}/inventory, POST /api/v1/video/control/devices/{sn}/stream/start, POST /api/v1/video/control/devices/{sn}/stream/stop
    - GET /api/v1/video/control/ws/lease/{lease_id}
    - Nginx /janus-ws -> l4media-janus:8188 WebRTC
    - Nginx /api/internal/v1/diagnostics/ -> app1:8000 WebSocket Console
  errors:
    http_rest: 400 Bad Request, 401 Unauthorized, 403 Forbidden, 404 Not Found, 409 Conflict, 502 Bad Gateway
    ws_control: close code 4400 (Invalid Frame/JSON), 4401 (Unauthorized/No Token), 4403 (Foreign Lease/Forbidden), 4404 (Lease Not Found), 4408 (Lease Timeout/Expired)
  invariants:
    - Multi-tenant strict boundary isolation by org_id in all queries
    - RBAC roles: role 1 (superuser), role 3 (tenant admin), role 4 (viewer with granular permissions)
    - Zero alternative flows: L4Desk interactive stream utilizes VideoPlayerScreen + RemoteControlOverlay, console utilizes DeviceConsoleTab
    - Billing operations use whole minor units (kopecks) without float calculations
    - MenuBuilder does not run Alembic migrations (sole authority ProcessingBackend)
supersedes: []
known_risks:
  - Console and video mutual exclusion locking is not yet globally unified (addressed in L4D-07-IOT / L4D-08B-MB)
  - Fin ledger models and migrations are pending in shared and ProcessingBackend (addressed in L4D-04A-C and L4D-09-MB)
  - User self-registration and public onboarding endpoint not yet implemented (addressed in L4D-05-MB)
consumers:
  - L4D-00F-SHARED
  - L4D-00G-DOCS
next_prompt_id: L4D-00F-SHARED
```
<!-- HANDOFF:H-L4D-00E-MB-v1:END -->

<!-- HANDOFF:H-L4D-00F-SHARED-v1:BEGIN -->
```yaml
handoff_id: H-L4D-00F-SHARED-v1
status: ACCEPTED
contract_kinds:
  - REPORT
  - MODEL_BASELINE
producer_prompt_id: L4D-00F-SHARED
producer_scope_project: shared/etranprocessing_db
producer_report_path: shared/docs/l4desk/handoffs/L4D-00F-SHARED-report.md
producer_branch: l4desk/l4d-00f-shared
producer_commit: 97094aedf52bb6d1c8285d8df30d7346307cb946
accepted_at_utc: 2026-09-17T20:15:00Z
contract_version: 0.1.0
schema_revision: N/A
artifact_version: 0.1.0
artifact_paths:
  - shared/docs/l4desk/handoffs/L4D-00F-SHARED-report.md
artifact_sha256:
  - e96ffab67cd53ef823afe88506b693305e22386b14331b2fe3aed6452235be21
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
```
<!-- HANDOFF:H-L4D-00F-SHARED-v1:END -->

<!-- HANDOFF:H-L4D-00G-DOCS-v1:BEGIN -->
```yaml
handoff_id: H-L4D-00G-DOCS-v1
status: ACCEPTED
contract_kinds:
  - REPORT
  - SEQUENCE_GATE
producer_prompt_id: L4D-00G-DOCS
producer_scope_project: l4desk-service
producer_report_path: l4desk-service/docs/handoffs/L4D-00G-DOCS-report.md
producer_branch: l4desk/l4d-00g-docs
producer_commit: 4f57f607c8ff34bf75cf1ad76fccc263e25f32f2
accepted_at_utc: 2026-09-17T20:30:00Z
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - l4desk-service/docs/handoffs/L4D-00G-DOCS-report.md
artifact_sha256:
  - fdc2817bd5a51e80f4ec36ba45ed7e33b3a4ef39771173d58c4330d37d38886a
compatibility:
  backward_compatible_with:
    - 1.7.7
    - 0.2.1
    - 1.0.0
    - 0.1.0
  breaking_changes: false
  notes: Central baseline contract matrix consolidating all six Stage 00 baseline handoffs (00A-TOOLS through 00F-SHARED). Zero contract conflicts detected. Exact baselines verified and locked for Stage 01.
deployment_status: DOCS_PUBLISHED
deployed_environment: documentation
feature_flags: {}
contract_payload:
  identifiers:
    sn: ASCII string (9-10 chars classic, up to 23+ extended)
    device_id: integer kiosk identifier
    terminal_id: integer primary key in terminals table
    org_id: integer tenant organization ID (mapped to tenant_id)
    user_id: integer user ID
    session_id: UUID remote session identifier
    stream_instance_id: UUID video streaming session identifier
    lease_id: UUID remote input control lease identifier
    mountpoint_id: integer Janus WebRTC mountpoint identifier
    pin: 6-digit one-time certificate enrollment code
    order_id: UUID billing order identifier
  baselines:
    tools: version 1.7.7, L4RTP/1, methods 7000/7001/7002, LWT dev/{SN}/svc & dev/{SN}/app
    iot_rpc_rest_app: version 0.2.1, schema 2026_08_30_0003, RabbitMQ srv/dev topics, WEB_CONCURRENCY=1
    processing_backend: version 1.0.0, Alembic head 026, PIN/CSR/X.509 API, mTLS Nginx
    l4media: version 1.0.0, mTLS :8443 ingress, L4RTP/1 demux, Janus WebRTC :8088/:8188
    menubuilder: version 0.1.0, VideoPlayerScreen + RemoteControlOverlay, DeviceConsoleTab, RBAC multi-tenant
    shared_etranprocessing_db: version 0.1.0, 31 declarative SQLAlchemy 2.0 models, Thin DB Layer
  invariants:
    - No unverified architectural claims treated as implemented
    - Strict cross-project boundaries without direct DB or private queue coupling
    - Zero alternative UX flows for console/video in MenuBuilder
    - Whole minor units (kopecks) for all financial representations
    - ProcessingBackend sole authority for database schema migrations
    - Append-only cascade governance via contract-handoff.md
supersedes: []
known_risks:
  - Windows SAS Ctrl+Alt+Del hardware intercept requires shortcut_action in Agent
  - In-memory LeaseRegistry in IoT requires WEB_CONCURRENCY=1 until distributed locks (L4D-07-IOT)
  - Missing persistent event feed with cursors in IoT (scheduled for L4D-02-IOT)
  - Console and video mutual exclusion not globally unified (scheduled for L4D-07-IOT / L4D-08B-MB)
  - Concurrency gap without row lock in PB PIN setup handler (scheduled for L4D-06A-PB)
  - Dynamic media routes in memory only with 0B host swap (monitored, scheduled for L4D-08A-MEDIA)
  - fin_* models and double-entry subledger not yet implemented (scheduled for L4D-04A-C, L4D-09-MB)
  - Public user self-registration not yet implemented (scheduled for L4D-05-MB)
consumers:
  - L4D-01A-TOOLS
next_prompt_id: L4D-01A-TOOLS
```
<!-- HANDOFF:H-L4D-00G-DOCS-v1:END -->

<!-- HANDOFF:H-L4D-01A-TOOLS-v1:BEGIN -->
```yaml
handoff_id: H-L4D-01A-TOOLS-v1
status: ACCEPTED
contract_kinds:
  - FIXTURES
producer_prompt_id: L4D-01A-TOOLS
producer_scope_project: tools
producer_report_path: tools/docs/l4desk/handoffs/L4D-01A-TOOLS-report.md
producer_branch: l4desk/l4d-01a-tools
producer_commit: c33030bc5f9ac07a010d5ff8c9b996c80c1def85
accepted_at_utc: 2026-09-17T20:45:00Z
contract_version: 1.0.0
schema_revision: 2026-09-17-v1
artifact_version: 1.7.7
artifact_paths:
  - tools/docs/l4desk/contracts/agent_compatibility_contract_v1.json
  - tools/docs/l4desk/contracts/schemas/agent_contract_v1.schema.json
  - tools/docs/l4desk/contracts/schemas/presence_event.schema.json
  - tools/docs/l4desk/contracts/schemas/rpc_7000_stream_control.schema.json
  - tools/docs/l4desk/contracts/schemas/rpc_7001_exec.schema.json
  - tools/docs/l4desk/contracts/schemas/rpc_7002_cancel.schema.json
  - tools/docs/l4desk/contracts/schemas/l4rtp_wire_protocol.schema.json
  - tools/docs/l4desk/fixtures/golden_vectors_v1.json
  - tools/tests/test_agent_compatibility_contract_v1.py
artifact_sha256:
  - 38e4ae5f13d563b3ae57a83259d63f9a63049d528d9667ae33efbd5a1e71267a
  - f065dd53101c4bf90b237c85082a05eea212e1a2b3de39d2bf890708e3a42f1d
  - fd061b7a1a113ec4ba518208e5693ffad98593cd8096d9a013f34a0ca7748d07
  - d7ed9deaeddc4a8a5f668cd59ac95aa0941aeff083e4ef6d94a1c9e75ba4ab6e
  - fb511cfb368f809fb042dbece552d7f24aeedb384f7186915f82e1dfb561ee1b
  - a1dac309f324b1d0e2073ac8ab251ed70f6cfd191c46ae38f3c9192854079a8f
  - b38aeeda96a5df81561117a0acb13606b417f136abb5250e72d41d38b630bd53
  - b4f3c1a465e88babf89c0850cfd8dffc29a4cd921ec51a73e2112e6cf9c3034b
  - 7e6ce52fc9351cc8a95134bcde0304fc208e0fdbec3287f279b2a7bfd0db0d52
compatibility:
  backward_compatible_with:
    - 1.7.6
    - 1.7.7
  breaking_changes: false
  notes: Исполняемый контракт совместимости Agent Compatibility Contract v1 и golden vectors для l4tools 1.7.7/1.7.6. Фиксация тем топиков, кодов методов (7000, 7001, 7002), JSON схем запросов и ответов, LWT/presence, L4RTP/1 wire protocol без изменения бинарных файлов агента.
deployment_status: PUBLISHED
deployed_environment: artifact-registry
feature_flags:
  l4con_cmd_exec: enabled
  l4desk_desktop_stream: enabled
contract_payload:
  identifiers:
    sn_pattern: "^[0-9A-Za-z_-]{6,32}$"
    cert_dn_pattern: "CN={SN}"
    task_id_pattern: "^task-[a-z0-9-]+$"
    session_id_pattern: "^sess-[a-z0-9-]+$"
  operations_events:
    presence_topics:
      - "dev/{SN}/app"
      - "dev/{SN}/svc"
    rpc_topics:
      - "srv/{SN}/tsk"
      - "srv/{SN}/rsp"
      - "dev/{SN}/out"
      - "dev/{SN}/res"
    methods:
      - code: 7000
        name: "STREAM_CONTROL"
        actions: ["inventory_get", "stream_start", "lease_renew", "stream_stop", "mouse_click", "key_event", "shortcut_action"]
      - code: 7001
        name: "EXEC_COMMAND"
        shells: ["cmd", "powershell"]
      - code: 7002
        name: "CANCEL_TASK"
        statuses: ["cancelled", "not_found", "already_finished"]
  errors:
    stream_errors: ["desktop_locked", "session_unavailable", "already_running", "invalid_button", "forbidden_key", "unsupported_action", "device_not_found", "encoder_failure"]
    exec_errors: ["timed_out", "failed", "cancelled"]
  invariants:
    - "No financial, billing, entitlement, or organization fields in agent payloads or topics"
    - "All device topics strictly prefixed with dev/{SN}/ and server topics with srv/{SN}/"
    - "Presence messages published with QoS 1 and retain = true"
    - "Method codes 7000, 7001, 7002 and action names are immutable and backward-compatible"
    - "Streaming output chunks over dev/{SN}/out have monotonic sequence numbering and boolean eof"
    - "No MQTT client was created or modified in tools"
    - "Published agent binary release remains 1.7.7"
supersedes:
  - H-L4D-00A-TOOLS-v1
known_risks:
  - "Interactive mouse/key input injection is rejected if screen is locked or session unavailable"
  - "L4RTP UDP loopback fallback on legacy Windows systems without native loopback fast path"
consumers:
  - L4D-01B-IOT
next_prompt_id: L4D-01B-IOT
```
<!-- HANDOFF:H-L4D-01A-TOOLS-v1:END -->

<!-- HANDOFF:H-L4D-01B-IOT-v1:BEGIN -->
```yaml
handoff_id: H-L4D-01B-IOT-v1
status: ACCEPTED
contract_kinds:
  - DEPLOYMENT
  - FIXTURES
producer_prompt_id: L4D-01B-IOT
producer_scope_project: iot-rpc-rest-app
producer_report_path: docs/l4desk/handoffs/L4D-01B-IOT-report.md
producer_branch: l4desk/l4d-01b-iot
producer_commit: 0307dd7953b339ee48c3068a490732c82ea4e401
accepted_at_utc: 2026-09-17T21:45:00Z
contract_version: 1.0.0
schema_revision: 2026-09-17-v1
artifact_version: 1.0.0
artifact_paths:
  - app-service/core/adapters/agent_contract_v1.py
  - app-service/tests/core/test_l4d_01b_agent_contract_v1.py
  - docs/l4desk/handoffs/L4D-01B-IOT-report.md
artifact_sha256:
  - 4c22174762d570dd70cbcbea28fc419748184a9a65ae9296f3f7577d7fa584e8
  - 0c5005d42935736030c793af7b4e3c9e08ba382523750d742d5a2d169e768a7f
  - 9e7bec92783a8cdab71777296d7e0440f2df0a56183f689883974cf0e390f69a
compatibility:
  backward_compatible_with:
    - 1.7.6
    - 1.7.7
  breaking_changes: false
  notes: Реализован и протестирован AgentContractV1Adapter (app-service/core/adapters/agent_contract_v1.py), обеспечивающий полную совместимость с Agent Compatibility Contract v1 (агенты 1.7.6 и 1.7.7). Подтверждена неизменяемость топиков dev/{SN}/app, dev/{SN}/svc, srv/{SN}/rsp, srv/{SN}/ctl, dev/{SN}/out, dev/{SN}/res, методов 7000/7001/7002, wire protocol L4RTP/1. Внедрен assert_no_commercial_fields, исключающий передачу биллинговых/финансовых полей. Пройдены все 25 golden vectors, 24 теста контракта, 357 тестов в репозитории.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  agent_contract_v1: enabled
  alpha_billing: enabled
contract_payload:
  identifiers:
    sn_pattern: "^[0-9A-Za-z_-]{6,32}$"
    session_id_pattern: "^(sess-[a-z0-9-]+|[0-9a-fA-F-]{36})$"
    command_id_pattern: "^[a-z0-9_-]{1,64}$"
    task_id_pattern: "^task-[a-z0-9-]+$"
  operations_events:
    presence_topics:
      - "dev/{SN}/app"
      - "dev/{SN}/svc"
    rpc_topics:
      - "srv/{SN}/rsp"
      - "srv/{SN}/ctl"
      - "dev/{SN}/out"
      - "dev/{SN}/res"
    methods:
      - code: 7000
        name: "STREAM_CONTROL"
        actions: ["inventory_get", "stream_start", "lease_renew", "stream_stop", "mouse_click", "key_event", "shortcut_action"]
      - code: 7001
        name: "EXEC_COMMAND"
        shells: ["cmd", "powershell"]
      - code: 7002
        name: "CANCEL_TASK"
        statuses: ["cancelled", "not_found", "already_finished"]
  errors:
    stream_errors: ["desktop_locked", "session_unavailable", "already_running", "invalid_button", "forbidden_key", "unsupported_action", "device_not_found", "encoder_failure"]
    exec_errors: ["timed_out", "failed", "cancelled"]
  invariants:
    - "No financial, billing, entitlement, or organization fields transmitted to Agent"
    - "Existing MQTT client preserved; no new client created"
    - "Streaming output chunks over dev/{SN}/out have monotonic seq >= 1 and boolean eof"
    - "Single-worker invariant preserved (WEB_CONCURRENCY=1)"
    - "Deployed container app1 running on etranprocessing (87.242.100.34)"
supersedes:
  - H-L4D-00B-IOT-v1
known_risks:
  - "In-memory session registry requires WEB_CONCURRENCY=1 until distributed Redis storage is added (L4D-07-IOT)"
  - "Interactive mouse/key input injection is rejected if screen is locked or session unavailable"
consumers:
  - L4D-01C-DOCS
next_prompt_id: L4D-01C-DOCS
```
<!-- HANDOFF:H-L4D-01B-IOT-v1:END -->

<!-- HANDOFF:H-L4D-01C-DOCS-v1:BEGIN -->
```yaml
handoff_id: H-L4D-01C-DOCS-v1
status: ACCEPTED
contract_kinds:
  - SEQUENCE_GATE
  - FIXTURES
producer_prompt_id: L4D-01C-DOCS
producer_scope_project: l4desk-service
producer_report_path: l4desk-service/docs/handoffs/L4D-01C-DOCS-report.md
producer_branch: l4desk/l4d-01c-docs
producer_commit: b07962f8805666581d87a7d4b064dcafb2e869fb
accepted_at_utc: 2026-09-17T22:05:00Z
contract_version: 1.0.0
schema_revision: 2026-09-17-v1
artifact_version: 1.7.7
artifact_paths:
  - l4desk-service/docs/handoffs/L4D-01C-DOCS-report.md
  - tools/docs/l4desk/contracts/agent_compatibility_contract_v1.json
  - tools/docs/l4desk/fixtures/golden_vectors_v1.json
artifact_sha256:
  - 130b56a4e67bd9b3b87df52629e85e5ac6230657c59e49d2b6aebc98a8fac358
  - 38e4ae5f13d563b3ae57a83259d63f9a63049d528d9667ae33efbd5a1e71267a
  - b4f3c1a465e88babf89c0850cfd8dffc29a4cd921ec51a73e2112e6cf9c3034b
compatibility:
  backward_compatible_with:
    - 1.7.6
    - 1.7.7
  breaking_changes: false
  notes: Зарегистрирована и нормативно принята доказанная пара контрактов Agent Compatibility Contract v1 (tools 1.7.7/1.7.6) и deployed provider AgentContractV1Adapter (iot-rpc-rest-app 1.0.0). Подтверждено нулевое расхождение по топикам, кодам методов (7000/7001/7002), LWT presence, L4RTP/1 wire protocol и кодам ошибок. Подтверждена изоляция коммерческих полей (assert_no_commercial_fields) и прохождение всех 25 golden vectors.
deployment_status: DOCS_PUBLISHED
deployed_environment: documentation
feature_flags:
  agent_contract_v1: enabled
  alpha_billing: enabled
contract_payload:
  identifiers:
    sn_pattern: "^[0-9A-Za-z_-]{6,32}$"
    session_id_pattern: "^(sess-[a-z0-9-]+|[0-9a-fA-F-]{36})$"
    command_id_pattern: "^[a-z0-9_-]{1,64}$"
    task_id_pattern: "^task-[a-z0-9-]+$"
  operations_events:
    presence_topics:
      - "dev/{SN}/app"
      - "dev/{SN}/svc"
    rpc_topics:
      - "srv/{SN}/rsp"
      - "srv/{SN}/ctl"
      - "dev/{SN}/out"
      - "dev/{SN}/res"
    methods:
      - code: 7000
        name: "STREAM_CONTROL"
        actions: ["inventory_get", "stream_start", "lease_renew", "stream_stop", "mouse_click", "key_event", "shortcut_action"]
      - code: 7001
        name: "EXEC_COMMAND"
        shells: ["cmd", "powershell"]
      - code: 7002
        name: "CANCEL_TASK"
        statuses: ["cancelled", "not_found", "already_finished"]
  errors:
    stream_errors: ["desktop_locked", "session_unavailable", "already_running", "invalid_button", "forbidden_key", "unsupported_action", "device_not_found", "encoder_failure"]
    exec_errors: ["timed_out", "failed", "cancelled"]
  invariants:
    - "No financial, billing, entitlement, or organization fields transmitted to Agent"
    - "Monotonic chunk sequencing seq >= 1 and boolean eof for command output"
    - "Zero changes to Agent MQTT protocol allowed in downstream prompts"
    - "Single-worker invariant preserved (WEB_CONCURRENCY=1) until L4D-07-IOT"
supersedes:
  - H-L4D-00G-DOCS-v1
known_risks:
  - "In-memory session registry requires WEB_CONCURRENCY=1 until distributed Redis storage is added (L4D-07-IOT)"
  - "Interactive mouse/key input injection is rejected if screen is locked or session unavailable"
consumers:
  - L4D-02-IOT
next_prompt_id: L4D-02-IOT
```
<!-- HANDOFF:H-L4D-01C-DOCS-v1:END -->

<!-- HANDOFF:H-L4D-02-IOT-v1:BEGIN -->
```yaml
handoff_id: H-L4D-02-IOT-v1
status: ACCEPTED
contract_kinds:
  - API
  - EVENT
  - DEPLOYMENT
producer_prompt_id: L4D-02-IOT
producer_scope_project: iot-rpc-rest-app
producer_report_path: docs/l4desk/handoffs/L4D-02-IOT-report.md
producer_branch: l4desk/l4d-02-iot
producer_commit: a5524d356dda343eca96010d16535d9f37ff4ece
accepted_at_utc: 2026-09-17T23:30:00Z
contract_version: 1.0.0
schema_revision: 2026-09-17-v1
artifact_version: 1.0.0
artifact_paths:
  - docs/l4desk/contracts/iot_event_feed_contract_v1.json
  - docs/l4desk/contracts/schemas/iot_event_feed_openapi.json
  - docs/l4desk/contracts/schemas/remote_session_event.schema.json
  - docs/l4desk/contracts/schemas/remote_session.schema.json
  - docs/l4desk/fixtures/iot_event_feed_examples_v1.json
artifact_sha256:
  - 7acd49cb4d761a074e6e41f04380bef5db78d465fa8f6417bdf1fb16167e46c3
  - 07b0b3e4e54b96e08ffc716b00f9e1a4dabe0f0ba90ca09708485cf068ccba56
  - 230a22727a493b2980ba85cb2735e50d5ac42ac9b110cf3a6f3ba03ea0ebb12b
  - 1de26a6fc47ebbe1d97d2f93108c3760ebf4a742908825c7d73e5a905da18f36
  - 1fafb1d27010917f43f5d36502cbfaa1decd5cce36c80a6dc397f4180556df2b
compatibility:
  backward_compatible_with:
    - 0.2.1
    - 1.7.7
  breaking_changes: false
  notes: Durable session facts and monotonic cursor event feed added via additive internal REST API (/api/internal/v1/remote-session-events and /api/internal/v1/remote-sessions). Agent MQTT protocol and existing broker topologies completely untouched and binary backward-compatible. Commercial/financial fields strictly excluded.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  remote_session_events_feed: enabled
  durable_session_facts: enabled
contract_payload:
  identifiers:
    cursor_type: "int64 (BIGSERIAL monotonic, strictly positive)"
    event_id_pattern: "^evt_[a-z0-9_]+$"
    session_id_pattern: "^sess-(console|video)-[a-z0-9-]+$"
    operation_id_pattern: "^[0-9a-fA-F-]{36}$"
    sn_pattern: "^[0-9A-Za-z_-]{6,32}$"
  endpoints:
    - method: GET
      path: "/api/internal/v1/remote-session-events"
      params: ["after", "limit", "tenant_id", "sn", "session_id", "event_type"]
    - method: GET
      path: "/api/internal/v1/remote-session-events/reconciliation"
      params: ["tenant_id", "from_cursor", "to_cursor", "from_time", "to_time"]
    - method: POST
      path: "/api/internal/v1/remote-sessions"
    - method: GET
      path: "/api/internal/v1/remote-sessions/{session_id}"
    - method: POST
      path: "/api/internal/v1/remote-sessions/{session_id}/stop"
  events:
    - "device_online"
    - "remote_session_start_requested"
    - "remote_session_active"
    - "remote_session_stop_requested"
    - "remote_session_closed"
    - "remote_session_failed"
    - "console_command_started"
    - "console_command_completed"
    - "console_command_timed_out"
  errors:
    - code: 400
      name: "BAD_REQUEST"
      reasons: ["negative_cursor", "limit_out_of_bounds", "commercial_field_detected"]
    - code: 403
      name: "FORBIDDEN"
      reasons: ["missing_or_invalid_internal_service_key"]
    - code: 404
      name: "NOT_FOUND"
      reasons: ["remote_session_not_found"]
  cursor_rules:
    - "Monotonically increasing sequence; no holes on successful commits"
    - "Query parameter 'after' returns items where cursor > after"
    - "Result ordered strictly by cursor ASC"
    - "Consumers resume from next_cursor returned in feed response"
    - "Duplicate event deliveries by event_id or operation_id are deduplicated without cursor progression"
supersedes:
  - H-L4D-01C-DOCS-v1
known_risks:
  - "Consumer must store last processed cursor in durable storage to ensure fault-tolerant resume after crash"
  - "Polling frequency should be configured based on SLA; typical interval 1-5 seconds"
consumers:
  - L4D-03-MB
next_prompt_id: L4D-03-MB
```
<!-- HANDOFF:H-L4D-02-IOT-v1:END -->

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

<!-- HANDOFF:H-L4D-04A-SHARED-v1:BEGIN -->
```yaml
handoff_id: H-L4D-04A-SHARED-v1
status: ACCEPTED
contract_kinds:
  - SCHEMA
producer_prompt_id: L4D-04A-SHARED
producer_scope_project: shared/etranprocessing_db
producer_report_path: shared/docs/l4desk/handoffs/L4D-04A-SHARED-report.md
producer_branch: l4desk/l4d-04a-shared
producer_commit: 537a1e493c83d1fa8e8cb765228be8d1b24a1d62
accepted_at_utc: 2026-09-18T00:45:41Z
contract_version: 1.0.0
schema_revision: L4D-04A-v1
artifact_version: 0.1.1
artifact_paths:
  - shared/docs/l4desk/schema-v1.json
  - shared/docs/l4desk/schema-v1.md
  - shared/docs/l4desk/package-source-v011.json
artifact_sha256:
  - 52f481dd3d9985c54b5388a1d9e63062a8fdbe626870b58a83b3461c3e08e49f
  - d80626d6f84bab0e44d2236a172b3ffa385c7779bccd26b42c2947b071f77935
  - 364efa7b369cdcb8da12025b377518834b6013fd693a28f321a81dcbe18a68c6
compatibility:
  backward_compatible_with:
    - 0.1.0
  breaking_changes: false
  notes: "23 opt-in declarative models; 31 legacy tables/root exports unchanged. Existing IoT DDL/client defaults preserved. Replace MenuBuilder-local IoT declarations before opt-in import; update consumer lockfiles. No migration or policy activation. Source package published through Git by explicit user approval."
deployment_status: PUBLISHED
deployed_environment: artifact-registry
feature_flags: {}
contract_payload:
  package_name: etranprocessing-db
  package_version: 0.1.1
  delivery: git-source
  source_repository: https://github.com/OlegLebedevRU/etranprocessing.git
  source_root: shared
  package_source_sha256: 364efa7b369cdcb8da12025b377518834b6013fd693a28f321a81dcbe18a68c6
  schema_import: etranprocessing_db.l4desk
  identifiers:
    tenant_user_terminal: "Integer; tenant=orgs.org_id, user=users.id; durable terminal identity retains original terminals.id"
    external_event_session_operation_correlation_terminal: "Opaque String(128), not UUID-only"
    cursor: "BigInteger; inbox primary key event_id; quarantine primary key id"
    archive: "String(128) batch id, composite manifest PK (id, source_project)"
  operations_events: "Schema only; no new HTTP/MQTT contract or runtime behavior"
  errors: "Named PK/FK/UNIQUE/CHECK violations; exact names and PostgreSQL DDL in schema-v1.json"
  invariants:
    - "Financial table/constraint/index names start fin_; integer kopecks; timezone-aware timestamps"
    - "Unique original terminal/date usage, terminal/cycle charge, provider payment, tenant/cycle/type notification"
    - "One reserved/start_requested/active/stop_requested console-or-video reservation per terminal"
    - "Calculated = posted + discarded; 0 <= discarded < 100; posted/payment/ledger amounts are whole rubles"
    - "Ledger metadata supports balanced transactions; cross-row entry totals and append-only require consumer/DB guards"
    - "Financial source identifiers/hashes have no mandatory FK to purgeable technical events"
    - "Source-only additive rollout; no destructive changes, migration, feature activation or main merge"
  verification:
    shared_tests: "65 passed"
    lint_format_type_build: "passed; pyright 0 errors/0 warnings; wheel/sdist and isolated wheel import verified"
    post_publish_smoke: "20 Git blob SHA-256 checks and 2 package tests passed; origin ref matched producer commit"
supersedes: []
known_risks:
  - "04B must reconcile live IoT DDL and run PostgreSQL Alembic tests; this provider did not access the live DB"
  - "04C must replace duplicate local IoT declarations before opt-in import; both consumers must update their lockfiles"
  - "Cross-row ledger balance/account ownership/append-only enforcement and archive retention verification are not implemented in this thin package"
  - "Local wheel/sdist are verification artifacts only; required published artifact is Git source at producer_commit"
consumers:
  - L4D-04B-PB
next_prompt_id: L4D-04B-PB
```
<!-- HANDOFF:H-L4D-04A-SHARED-v1:END -->

<!-- HANDOFF:H-L4D-04B-PB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-04B-PB-v1
status: ACCEPTED
contract_kinds:
  - SCHEMA
  - DEPLOYMENT
producer_prompt_id: L4D-04B-PB
producer_scope_project: ProcessingBackend
producer_report_path: ProcessingBackend/docs/l4desk/handoffs/L4D-04B-PB-report.md
producer_branch: l4desk/l4d-04b-pb
producer_commit: c889ec5f9b0366d3a61e908f82dcd2e8f4c0b367
accepted_at_utc: 2026-09-18T08:35:00Z
contract_version: 1.0.0
schema_revision: "027"
artifact_version: 0.1.1
artifact_paths:
  - ProcessingBackend/backend/alembic/versions/027_add_l4desk_and_fin_ledger.py
  - ProcessingBackend/docs/l4desk/schema-027.sql
  - ProcessingBackend/backend/tests/test_schema_migration.py
artifact_sha256:
  - 5993027230ffc6121e4bd76988cbbf21d2a2602f64bdcfd39aad72f5efe6177d
  - dfbf10b1249da4d9486309701722cc22b093dc335a1d73949081fc7a6a7ddbd0
  - dbbc3bf5f053eefbaea8bbcc71775796a5beba410a8277271e16f39ddc43557e
compatibility:
  backward_compatible_with:
    - "026"
  breaking_changes: false
  notes: "Non-destructive expand migration 027 deployed to production PostgreSQL. Added 23 tables (14 fin_*, 3 iot_*, 6 l4desk_*). Existing 31 core tables and application endpoints untouched. Clean transactional rollback 027->026 verified. menubuilder-backend restarted and healthy."
deployment_status: DEPLOYED
deployed_environment: production
feature_flags: {}
contract_payload:
  alembic_head: "027"
  alembic_down_revision: "026"
  package_name: etranprocessing-db
  package_version: 0.1.1
  input_schema_sha256: 52f481dd3d9985c54b5388a1d9e63062a8fdbe626870b58a83b3461c3e08e49f
  input_package_source_sha256: 364efa7b369cdcb8da12025b377518834b6013fd693a28f321a81dcbe18a68c6
  ddl_snapshot_sha256: dfbf10b1249da4d9486309701722cc22b093dc335a1d73949081fc7a6a7ddbd0
  deployed_tables_count: 23
  deployed_host: 87.242.100.34
  mcp_ops_readiness: UNAVAILABLE (fallback to SSH)
  verification:
    pytest_tests: "113 passed"
    linters: "ruff check passed, ruff format passed, pyright 0 errors/0 warnings"
    live_db_check: "alembic current is 027 (head); all 23 tables confirmed present in public schema"
    rollback_readiness: "alembic downgrade --sql 027:026 verified and transactional"
    service_health: "menubuilder-backend restarted and Up, processing-backend Up"
supersedes: []
known_risks:
  - "L4D-04C-MB must reconcile local IoT model classes with shared package models to avoid duplicate Base declarations"
  - "Financial triggers / balanced transaction invariants must be enforced in business logic prior to enabling financial writes"
consumers:
  - L4D-04C-MB
next_prompt_id: L4D-04C-MB
```
<!-- HANDOFF:H-L4D-04B-PB-v1:END -->

<!-- HANDOFF:H-L4D-04C-MB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-04C-MB-v1
status: ACCEPTED
contract_kinds:
  - SCHEMA
  - DEPLOYMENT
producer_prompt_id: L4D-04C-MB
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-04C-MB-report.md
producer_branch: l4desk/l4d-04c-mb
producer_commit: a726ec94022517697a59fee6644f598dc3b978d8
accepted_at_utc: 2026-09-18T12:40:00Z
contract_version: 1.0.0
schema_revision: "027"
artifact_version: 0.1.1
artifact_paths:
  - MenuBuilder/backend/app/schema_compatibility.py
  - MenuBuilder/backend/app/models_l4desk.py
  - MenuBuilder/backend/app/repositories/l4desk_repository.py
  - MenuBuilder/backend/tests/test_schema_compatibility.py
artifact_sha256:
  - 670e8d1e780e503625865a41d61b004b28b14adc93be4e5710aadb73dfa8630e
  - 7f708171cec710524a3f042701bec31524d7cd2ebb07f0dbf501aa4b019d5131
  - 316adf73ab2765377b64d55f26cadb2b41b541b260b1e0c63f55be817ebeeb89
  - 055e43be0acd36c34b3e932dcfc30b67d4afe51b66e6df3b82cbcdacbd9e978f
consumed_contracts:
  - handoff_id: H-L4D-04A-SHARED-v1
    contract_id: l4desk_shared_schema_v1
    contract_version: 1.0.0
    schema_revision: L4D-04A-v1
    producer: shared
    package_name: etranprocessing-db
    package_version: 0.1.1
    package_source_sha256: 364efa7b369cdcb8da12025b377518834b6013fd693a28f321a81dcbe18a68c6
  - handoff_id: H-L4D-04B-PB-v1
    contract_id: alembic_migration_027
    contract_version: 1.0.0
    schema_revision: "027"
    producer: ProcessingBackend
    alembic_head: "027"
    deployed_host: 87.242.100.34
compatibility:
  backward_compatible_with:
    - 0.1.0
  breaking_changes: false
  notes: "MenuBuilder connected to published etranprocessing-db==0.1.1 and deployed expand schema 027 in dark mode. Replaced duplicate local IoT model declarations with shared package re-exports. Added strict startup schema compatibility verification (reads alembic_version and 23 required tables; zero automatic DDL). Added dark mode feature flags (registration, billing, ui all disabled by default). Verified backward compatibility and rolling deploy resilience."
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  l4desk_registration_enabled: false
  l4desk_billing_enabled: false
  l4desk_ui_enabled: false
  schema_compatibility_check_enabled: true
  required_alembic_revision: "027"
contract_payload:
  package_name: etranprocessing-db
  package_version: 0.1.1
  package_source_sha256: 364efa7b369cdcb8da12025b377518834b6013fd693a28f321a81dcbe18a68c6
  alembic_head: "027"
  schema_revision: "027"
  deployed_host: 87.242.100.34
  deployed_service: menubuilder-backend
  deployed_image: user1-menubuilder-backend:latest
  tables_checked_count: 23
  invariants:
    - "Strict scope: MenuBuilder performs zero automatic DDL at startup (auto-DDL disabled in storage and lifespan)"
    - "Startup guard: verify_schema_compatibility verifies alembic_version == '027' and presence of all 23 L4Desk tables; aborts startup on mismatch"
    - "Dark mode: L4Desk features disabled by default via config flags (l4desk_registration_enabled=false, l4desk_billing_enabled=false, l4desk_ui_enabled=false)"
    - "Shared package integration: etranprocessing-db==0.1.1 consumed via etranprocessing_db.l4desk; duplicate local models reconciled"
    - "Rolling deploy resilience: models support omitted optional columns via null defaults and load_only queries"
supersedes: []
known_risks:
  - "L4Desk business routes (registration, billing, UI) remain dark and inaccessible until L4D-05-MB and subsequent prompts"
  - "Startup compatibility check requires PostgreSQL database connection; aborts startup if database schema revision is not 027"
consumers:
  - L4D-05-MB
next_prompt_id: L4D-05-MB
```
<!-- HANDOFF:H-L4D-04C-MB-v1:END -->
