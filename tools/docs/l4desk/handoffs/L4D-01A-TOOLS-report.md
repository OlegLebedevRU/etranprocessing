# Handoff Report: Публикация Agent Compatibility Contract v1 (L4D-01A-TOOLS)

## Candidate H-L4D-01A-TOOLS-v1

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

---

## 1. Резюме шага и контекст выполнения

Шаг `L4D-01A-TOOLS` выполнен строго в рамках изолированного репозиторного каталога `tools`.
Цель шага: превратить наблюдаемый Agent baseline в неизменяемый (immutable) нормативный **Agent Compatibility Contract v1** и исполняемые golden vectors (`golden_vectors_v1.json`), снабжённые полным набором JSON-схем и тестов обратной совместимости, **без изменения поведения и бинарных файлов опубликованного Агента**.

Все артефакты созданы строго в рамках `tools/`, не затрагивают другие репозитории и подготовлены для потребления следующим шагом каскада — `L4D-01B-IOT` (`iot-rpc-rest-app`).

---

## 2. Contract Gate Verification

- **Входной handoff:** `H-L4D-00G-DOCS-v1` (принят в `contract-handoff.md`).
- **Consumer подтверждён:** `L4D-01A-TOOLS` указан в секции `consumers` блока `H-L4D-00G-DOCS-v1`.
- **Ветка:** `l4desk/l4d-01a-tools`.
- **Scope isolation:** строго проект `tools`.

---

## 3. Спецификация контракта: Agent Compatibility Contract v1

Создана нормативная спецификация:
`tools/docs/l4desk/contracts/agent_compatibility_contract_v1.json`

### 3.1. Топология топиков и QoS/Retain
1. **Presence:**
   - `dev/{SN}/app` — присутствие основного приложения (`main_app`). QoS 1, `retain: true`.
     - LWT Will: `app_offline`
     - После CONNACK: `app_online`
     - Штатное завершение: `app_offline`
   - `dev/{SN}/svc` — присутствие фоновой службы (`extra_service`). QoS 1, `retain: true`.
     - LWT Will: `svc_offline`
     - После CONNACK: `svc_online`
     - Штатное завершение: `svc_offline`
2. **RPC Requests:**
   - `srv/{SN}/tsk` — брокер/сервер отправляет задачу в очередь терминала (QoS 1, `retain: false`).
   - `srv/{SN}/rsp` — агент слушает прямые ответы и задачи от сервера (QoS 1, `retain: false`).
3. **RPC Results & Streaming:**
   - `dev/{SN}/out` — потоковые чанки вывода выполнения команд (QoS 1, `retain: false`). Чанки содержат монотонно возрастающий `seq`, признак `eof: false|true` и данные вывода.
   - `dev/{SN}/res` — финальный статус выполнения RPC-команды (QoS 1, `retain: false`).

### 3.2. Коды методов и RPC Lifecycle
- **Метод 7000 (`STREAM_CONTROL`):**
  - Компонент: `l4desk`
  - Действия: `inventory_get`, `stream_start`, `lease_renew`, `stream_stop`, `mouse_click`, `key_event`, `shortcut_action`
  - Режимы потока: `desktop` (индекс экрана), `camera` (индекс DirectShow-камеры)
  - Коды ошибок: `desktop_locked`, `session_unavailable`, `already_running`, `invalid_button`, `forbidden_key`, `unsupported_action`, `device_not_found`, `encoder_failure`
- **Метод 7001 (`EXEC_COMMAND`):**
  - Компонент: `l4con`
  - Оболочки: `cmd`, `powershell`
  - Потоковый вывод: чанки в `dev/{SN}/out` (`seq >= 1`, `eof: true` на завершающем чанке)
  - Финальный ответ: `dev/{SN}/res` со статусами: `completed`, `timed_out`, `failed`, `cancelled`
- **Метод 7002 (`CANCEL_TASK`):**
  - Компонент: `l4superv / l4con`
  - Отмена активной задачи по `target_task_id`
  - Статусы ответа: `cancelled`, `not_found`, `already_finished`

### 3.3. Транспорт и идентификация устройства
- Транспорт: MQTT v3.1.1 поверх TLS 1.2 с mutual TLS (mTLS).
- TLS-стек клиента: Windows SChannel (SSPI / CryptoAPI), встроенный в ОС.
- Идентификатор устройства: серийный номер `{SN}` извлекается из `Subject: CN={SN}` клиентского сертификата.
- Стриминг видео: протокол L4RTP/1 поверх защищённого TLS TCP-туннеля (порт 8443) с преамбулой `L4RT\x01\x00` + `{SN}` и мультиплексированием каналов 1 (RTP) и 2 (RTCP).

---

## 4. Набор JSON-схем

В каталоге `tools/docs/l4desk/contracts/schemas/` созданы строгие JSON-схемы стандарта Draft 2020-12 / Draft-07:
1. `agent_contract_v1.schema.json` — корневая мета-схема валидации спецификации контракта.
2. `presence_event.schema.json` — валидация presence событий (`app_online`, `app_offline`, `svc_online`, `svc_offline`).
3. `rpc_7000_stream_control.schema.json` — валидация запросов и ответов управления видеопотоком и вводом.
4. `rpc_7001_exec.schema.json` — валидация запуска консольных команд, потоковых чанков и завершения задач.
5. `rpc_7002_cancel.schema.json` — валидация запросов и ответов отмены задач.
6. `l4rtp_wire_protocol.schema.json` — валидация преамбулы и фреймов L4RTP/1.

---

## 5. Исполняемые Golden Vectors (`golden_vectors_v1.json`)

В файле `tools/docs/l4desk/fixtures/golden_vectors_v1.json` зафиксированы исполняемые тестовые векторы:
- **6 presence-векторов:** подключение, отключение, LWT для `main_app` и `extra_service`.
- **10 векторов метода 7000:**
  - `vec-7000-inv-01`: опрос инвентаря экранов и камер.
  - `vec-7000-str-desktop-01`: запуск стрима рабочего стола (RTP 5004, RTCP 5005).
  - `vec-7000-str-camera-01`: запуск стрима веб-камеры.
  - `vec-7000-rnw-01`: продление аренды потока (`lease_renew`).
  - `vec-7000-clk-01`: инъекция нормализованного клика мыши.
  - `vec-7000-key-01`: инъекция событий клавиатуры (`vk: 65`).
  - `vec-7000-sc-01`: системный хоткей (`win_d`).
  - `vec-7000-stp-01`: штатная остановка стрима.
  - `vec-7000-err-locked`: отказ запуска стрима при заблокированном экране (`desktop_locked`).
  - `vec-7000-err-busy`: отказ запуска при параллельной активной сессии (`already_running`).
- **4 вектора метода 7001:**
  - `vec-7001-cmd-hostname`: выполнение `hostname` через `cmd` (2 чанка, exit code 0).
  - `vec-7001-ps-date`: выполнение PowerShell скрипта (2 чанка, exit code 0).
  - `vec-7001-err-failed`: выполнение ошибочной команды (вывод в stderr, exit code 1).
  - `vec-7001-timeout`: превышение таймаута `ttl_sec` (exit code -1, статус `timed_out`).
- **2 вектора метода 7002:**
  - `vec-7002-cncl-01`: успешная отмена запущенной задачи (`cancelled`).
  - `vec-7002-cncl-not-found`: запрос отмены отсутствующей задачи (`not_found`).
- **3 вектора L4RTP Wire Protocol:**
  - `vec-rtp-preamble`: побайтовая преамбула `4c34525401000009303030313030373733`.
  - `vec-rtp-video-frame`: заголовок медиа-фрейма Channel 1 (RTP, длина 1400 байт).
  - `vec-rtp-rtcp-frame`: заголовок feedback-фрейма Channel 2 (RTCP, длина 72 байта).

---

## 6. Проверка обратной совместимости с опубликованным Агентом

Тестовый набор `tools/tests/test_agent_compatibility_contract_v1.py` автоматически валидирует как новые `golden_vectors_v1.json`, так и существующие фикстуры baseline (`H-L4D-00A-TOOLS-v1`):
- `mqtt_presence_lifecycle.json` — 100% совместимость со схемой presence;
- `rpc_7000_stream_control.json` — 100% совместимость со схемой 7000;
- `rpc_7001_exec.json` — 100% совместимость со схемой 7001;
- `rpc_7002_cancel.json` — 100% совместимость со схемой 7002;
- `l4rtp_wire_protocol.json` — 100% совместимость с декодером wire-формата.

Сохранены все инварианты совместимости с релизами Агента **1.7.6** и **1.7.7**.

---

## 7. Аудит непереговорных правил и инвариантов

1. **Коммерческая изоляция:** Ни одно поле биллинга, тарифов, подписок, организаций или платежей (`balance`, `payment`, `tariff`, `billing`, `entitlement`, `subledger` и т.д.) не присутствует ни в схемах, ни в топиках, ни в полезной нагрузке. Проверено тестом `test_no_financial_fields_in_contract_and_fixtures`.
2. **Топология топиков:** Все топики строго следуют префиксам `dev/{SN}/...` (от устройства к серверу) и `srv/{SN}/...` (от сервера к устройству). Проверено тестом `test_all_topics_have_strict_device_or_server_prefix`.
3. **MQTT Client Rule (Раздел 10 Guidelines):** Никаких MQTT-клиентов в рамках шага не создавалось и не модифицировалось. Код `tools/leo4proxy/examples/python_client.py` и C-модули `l4con`/`l4superv` не изменялись.
4. **Неизменность бинарных файлов:** Бинарные файлы Агента `l4setup.exe` версии 1.7.7 не пересобирались и остаются опубликованными в неизменном виде.

---

## 8. Результаты выполнения проверок и тестов

### 8.1. Тесты pytest (`tools/tests/test_agent_compatibility_contract_v1.py`)

Команда: `uv run --project tools pytest tools/tests -v`
Результат: **14 passed in 6.32s** (0 failed, 0 errors).

```text
tools\tests\test_agent_compatibility_contract_v1.py::test_contract_spec_meta_schema PASSED [  7%]
tools\tests\test_agent_compatibility_contract_v1.py::test_contract_spec_methods_and_topics PASSED [ 14%]
tools\tests\test_agent_compatibility_contract_v1.py::test_presence_golden_vectors PASSED [ 21%]
tools\tests\test_agent_compatibility_contract_v1.py::test_presence_baseline_fixture_backward_compat PASSED [ 28%]
tools\tests\test_agent_compatibility_contract_v1.py::test_method_7000_golden_vectors PASSED [ 35%]
tools\tests\test_agent_compatibility_contract_v1.py::test_method_7000_baseline_fixture_backward_compat PASSED [ 42%]
tools\tests\test_agent_compatibility_contract_v1.py::test_method_7001_golden_vectors PASSED [ 50%]
tools\tests\test_agent_compatibility_contract_v1.py::test_method_7001_baseline_fixture_backward_compat PASSED [ 57%]
tools\tests\test_agent_compatibility_contract_v1.py::test_method_7002_golden_vectors PASSED [ 64%]
tools\tests\test_agent_compatibility_contract_v1.py::test_method_7002_baseline_fixture_backward_compat PASSED [ 71%]
tools\tests\test_agent_compatibility_contract_v1.py::test_l4rtp_preamble_wire_parsing PASSED [ 78%]
tools\tests\test_agent_compatibility_contract_v1.py::test_l4rtp_frame_headers PASSED [ 85%]
tools\tests\test_agent_compatibility_contract_v1.py::test_no_financial_fields_in_contract_and_fixtures PASSED [ 92%]
tools\tests\test_agent_compatibility_contract_v1.py::test_all_topics_have_strict_device_or_server_prefix PASSED [100%]
```

### 8.2. Тесты wire-протокола C (`tools/leo4proxy/bin/test_rtp_wire.exe`)

Команда: `.\tools\leo4proxy\bin\test_rtp_wire.exe`
Результат: **ALL RTP TUNNEL TESTS PASSED SUCCESSFULLY!**

```text
=======================================================
 Running Leo4Proxy RTP Tunnel Wire & Socket Tests
=======================================================
[TEST] Checking L4RTP/1 preamble wire format...
  [PASS] Preamble matches expected wire specification byte-for-byte.
[TEST] Checking L4RTP/1 frame wire format (RTP & RTCP)...
  [PASS] Frame headers and payload lengths match wire specification.
[TEST] Checking start validation (SN and ports)...
  [PASS] Empty SN correctly rejected.
  [PASS] Identical RTP and RTCP port correctly rejected.
[RTP-TUNNEL] Ready: UDP 127.0.0.1:5504 (RTP) and 127.0.0.1:5505 (RTCP) -> dev.leo4.ru:8443 (L4RTP/1 mTLS, SN: 000100773)
  [PASS] Sockets successfully created and bound to loopback.
  [INFO] RTP socket SO_RCVBUF = 524288 bytes (>= 512KB)
  [PASS] RTP tunnel stopped cleanly.
=======================================================
```

---

## 9. План отката (Rollback Strategy)

В случае обнаружения несовместимости на стороне провайдера `L4D-01B-IOT`:
1. Контракт `H-L4D-01A-TOOLS-v1` отзывается через процедуру отзыва (`<!-- REVOCATION:... -->`) контроллером.
2. Серверный адаптер IoT откатывается к потреблению базовых фикстур `H-L4D-00A-TOOLS-v1`.
3. Бинарные сборки терминалов не требуют переустановки, так как в рамках шага код Агента не модифицировался.

---

## 10. Передача эстафеты в `L4D-01B-IOT`

Контракт `Agent Compatibility Contract v1` и исполняемые golden vectors полностью зафиксированы, протестированы и готовы к потреблению шагом `L4D-01B-IOT` в репозитории `iot-rpc-rest-app`.
Следующий шаг каскада: `L4D-01B-IOT`.
Candidate Handoff ID: `H-L4D-01A-TOOLS-v1`.
