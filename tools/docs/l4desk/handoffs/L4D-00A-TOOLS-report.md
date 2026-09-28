# Handoff Report: Baseline опубликованного Агента и leo4proxy (L4D-00A-TOOLS)

## Candidate H-L4D-00A-TOOLS-v1

```yaml
candidate_handoff_id: H-L4D-00A-TOOLS-v1
prompt_id: L4D-00A-TOOLS
source_prompt_id: L4D-00A-TOOLS
target_prompt_id: L4D-00B-IOT
branch: l4desk/l4d-00a-tools
scope_project: tools
contract_kinds:
  - FIXTURES
deployment_status: PUBLISHED
target_runtime: tools
artifact_version: 1.7.7
artifact_hash: 874f5444d2d4cc9bdee39e7c25a1265a2dd98f388aca49ef205ffb764531cd88
registry_url: https://l4tools-generic.ar.cloud.ru/l4tools/1.7.7/l4setup.exe
status: CANDIDATE
block_status: CANDIDATE
created_at: 2026-09-17T16:35:00Z
```

---

## 1. Резюме шага и контекст выполнения

Шаг `L4D-00A-TOOLS` выполнен строго в рамках изолированного репозиторного каталога `tools`.
Цель шага: сформировать проверяемый baseline опубликованного Агента и `leo4proxy` без догадок на основе наблюдаемого исходного кода, релизного манифеста, тестов и реестра опубликованных артефактов, зафиксировав контракт для следующего этапа каскада — `L4D-00B-IOT`.

- **Bootstrap Gate:** проверка `contract-handoff.md` завершена со статусом `READY_FOR_00A` (активных конфликтующих блоков нет).
- **Изолированное окружение:** ветка `l4desk/l4d-00a-tools`. Сторонние проекты (`MenuBuilder`, `iot-rpc-rest-app`, `ProcessingBackend`, `l4media`, `shared`) не открывались и не модифицировались.
- **Статус артефакта:** версия `1.7.7` официально опубликована в Generic Artifact Registry (`deployment_status: PUBLISHED`).

---

## 2. Release Metadata и опубликованный артефакт

Информация из `tools/dist/l4tools-release.json`, `releases.jsonl` и `deploy/publish_l4tools.py check 1.7.7`:

- **Component:** `l4tools`
- **Release Version:** `1.7.7`
- **Git SHA:** `297068f8044b5e40e51257e828c50d6137c9af7c`
- **Built At:** `2026-09-15T21:30:11Z`
- **Published At:** `2026-09-15T21:35:40.494422+00:00`
- **Registry Download URL:** `https://l4tools-generic.ar.cloud.ru/l4tools/1.7.7/l4setup.exe`
- **Artifact SHA-256 (`l4setup.exe`):** `874f5444d2d4cc9bdee39e7c25a1265a2dd98f388aca49ef205ffb764531cd88`
- **Artifact File Size:** `28,730,368` байт (~27.4 MB)
- **Embedded Payloads SHA-256:**
  - `payload_x86.bin`: `04ff16def88939bd5cff0ba2964aa66df5a0235e11ab6a71feff1294e197686a`
  - `payload_x64.bin`: `0ae082d93bfd8ee5d5bd22b7de01737296b126e33c708079426ab85f2c3b2bb8`
- **Компонентный состав релизного бандла:**
  - `leo4proxy`: `1.2.0` (mTLS/TCP проксирование, туннель L4RTP/1, SChannel)
  - `l4superv`: `1.7.6` (системная служба Windows, супервизор, генерация mosquitto.conf, переключение Standby/Active)
  - `l4desk`: `1.7.6` (агент видеотрансляции рабочего стола и веб-камер, оркестрация FFmpeg, перехват/ввод управления)
  - `l4pin`: `1.7.2` (утилита bootstrap сертификата терминала по одноразовому PIN, интеграция с CryptoAPI/CNG)
  - `l4con`: `1.7.2` (сервис удаленной диагностики и исполнения команд cmd/powershell, extra_service)
  - `l4sql`: `1.0.0` (утилита диагностики локальной БД)
  - `mosquitto`: `2.1.2` (локальный MQTT-брокер / мост)
  - `ffmpeg`: `9.0` (статическая сборка захвата GDI/DirectShow и H.264 RTP стриминга)
- **Минимальные системные требования (Min OS):**
  - Windows NT 6.1 (Windows 7 SP1 x86/x64, Windows Server 2008 R2 SP1)
  - Включенный протокол TLS 1.2 в подсистеме Windows SChannel (KB3140245)

---

## 3. Фактическая топология MQTT topics и RPC Lifecycle

### 3.1. MQTT Presence и LWT Lifecycle (dev/{SN}/svc и dev/{SN}/app)

В соответствии со спецификацией и правилами разработки клиентов:
- **`l4con` / `leo4proxy` (Роль `extra_service`):**
  - **Connect Will Topic:** `dev/{SN}/svc`
  - **Connect Will Payload:** `svc_offline` (QoS 1, retain: true)
  - **After CONNACK:** `PUBLISH dev/{SN}/svc = svc_online`, QoS 1, retain: true
  - **Clean Shutdown:** `PUBLISH dev/{SN}/svc = svc_offline`, QoS 1, retain: true -> `DISCONNECT`
- **Основное терминальное приложение (Роль `main_app`):**
  - **Connect Will Topic:** `dev/{SN}/app`
  - **Connect Will Payload:** `app_offline` (QoS 1, retain: true)
  - **After CONNACK:** `PUBLISH dev/{SN}/app = app_online`, QoS 1, retain: true
  - **Clean Shutdown:** `PUBLISH dev/{SN}/app = app_offline`, QoS 1, retain: true -> `DISCONNECT`

### 3.2. RPC Lifecycle и маршрутизация топиков

Взаимодействие между сервером (`iot-rpc-rest-app` / `MenuBuilder`) и клиентом на терминале:

1. **Сервер -> IoT Broker:** Сервер публикует RPC-задачу в топик `srv/{SN}/tsk` или `srv/{SN}/rsp` с заголовками MQTT v5 User Properties (`taskId`, `clientId`) и `CorrelationData`.
2. **Маршрутизация Mosquitto Bridge:** В `mosquitto.conf` сконфигурированы bridge-директивы:
   - `topic tsk in 1 srv/{SN}/ srv/{SN}/`
   - `topic rsp in 1 srv/{SN}/ srv/{SN}/`
   - `topic res out 1 dev/{SN}/ dev/{SN}/`
   - `topic out out 1 dev/{SN}/ dev/{SN}/`
   - `topic svc out 1 dev/{SN}/ dev/{SN}/`
3. **Локальный агент (`l4desk` / `l4con`):** подписан на входящие сообщения в топик `srv/{SN}/rsp` (или `dev/{SN}/req`).
4. **Потоковый вывод (для длительных команд):** публикуется чанками в топик `dev/{SN}/out`.
5. **Итоговый результат задачи:** агент публикует финальный ответ в топик `dev/{SN}/res`, возвращая исходные `CorrelationData` и `taskId` в свойствах MQTT v5.

---

## 4. Inventory кодов методов и формат payload

### 4.1. Метод `7000` — `CMD_DIAG_STREAM_CONTROL` (l4desk)
Управление видеопотоком, опрос устройств захвата и удаленный ввод:

- **Action `inventory_get`:**
  - *Request:* `{"method_code": 7000, "action": "inventory_get", "command_id": "string", "sn": "string"}`
  - *Response:* `{"status": "ok", "command_id": "...", "displays": [{"device_name": "\\\\.\\DISPLAY1", "display_index": 0, "width": 1920, "height": 1080, "is_primary": true}], "cameras": [{"device_name": "...", "camera_index": 0}], "active_stream": null}`
- **Action `stream_start`:**
  - *Request:*
    ```json
    {
      "method_code": 7000,
      "action": "stream_start",
      "command_id": "str_001",
      "sn": "000100773",
      "stream_mode": "desktop",
      "display_index": 0,
      "camera_index": 0,
      "fps": 25,
      "bitrate_kbps": 2000,
      "lease_sec": 60,
      "input_enabled": true
    }
    ```
  - *Response:*
    ```json
    {
      "status": "started",
      "command_id": "str_001",
      "stream_instance_id": "s_000100773_1726593684",
      "stream_mode": "desktop",
      "display_index": 0,
      "camera_index": 0,
      "fps": 25,
      "bitrate_kbps": 2000,
      "lease_sec": 60,
      "expires_at_ms": 1726593744000,
      "rtp_port": 5004,
      "rtcp_port": 5005,
      "input_enabled": true,
      "desktop_locked": false,
      "session_available": true
    }
    ```
- **Action `stream_stop`:**
  - *Request:* `{"method_code": 7000, "action": "stream_stop", "command_id": "stp_001", "sn": "000100773", "stream_instance_id": "..."}`
  - *Response:* `{"status": "stopped", "command_id": "stp_001", "stream_instance_id": "..."}`
- **Action `lease_renew`:**
  - *Request:* `{"method_code": 7000, "action": "lease_renew", "command_id": "rnw_001", "sn": "000100773", "stream_instance_id": "...", "lease_sec": 60}`
  - *Response:* `{"status": "renewed", "command_id": "rnw_001", "expires_at_ms": 1726593804000}`
- **Action `mouse_click`:**
  - *Request:* `{"method_code": 7000, "action": "mouse_click", "command_id": "clk_001", "sn": "...", "stream_instance_id": "...", "x_norm": 0.5, "y_norm": 0.5, "button": "left"|"right", "click_type": "click"|"down"|"up"|"dblclick"}`
  - *Response:* `{"status": "injected"|"rejected", "command_id": "clk_001", "error": null}`
- **Action `key_event`:**
  - *Request:* `{"method_code": 7000, "action": "key_event", "command_id": "key_001", "sn": "...", "stream_instance_id": "...", "vk": 65, "scan_code": 30, "is_down": true, "is_extended": false}`
  - *Response:* `{"status": "injected"|"rejected", "command_id": "key_001", "error": null}`
- **Action `shortcut_action`:**
  - *Request:* `{"method_code": 7000, "action": "shortcut_action", "command_id": "sc_001", "sn": "...", "stream_instance_id": "...", "shortcut": "ctrl_alt_del"|"alt_f4"|"win_d"|"win_l"|"ctrl_esc"}`
  - *Response:* `{"status": "injected"|"rejected", "command_id": "sc_001", "error": null}`

### 4.2. Метод `7001` — `CMD_DIAG_EXEC` (l4con)
Выполнение удаленных команд командной строки:

- **Request (`srv/{SN}/rsp`):**
  ```json
  {
    "id": "task-uuid-001",
    "method_code": 7001,
    "session_id": "session-uuid-001",
    "command_line": "hostname",
    "shell": "cmd",
    "ttl_sec": 30,
    "topic": "dev/{SN}/out"
  }
  ```
- **Stream Output Chunks (`dev/{SN}/out`):**
  ```json
  {
    "session_id": "session-uuid-001",
    "seq": 1,
    "data": "TERM-001\r\n",
    "eof": false
  }
  ```
  И завершающий чанк:
  ```json
  {
    "session_id": "session-uuid-001",
    "seq": 2,
    "data": "",
    "eof": true,
    "exit_code": 0
  }
  ```
- **Response Result (`dev/{SN}/res`):**
  ```json
  {
    "id": "task-uuid-001",
    "session_id": "session-uuid-001",
    "method_code": 7001,
    "status": "completed",
    "exit_code": 0,
    "execution_time_ms": 52
  }
  ```

### 4.3. Метод `7002` — `CMD_DIAG_CANCEL` (l4con)
Отмена выполняющейся задачи диагностики и принудительное завершение дерева процессов:

- **Request (`srv/{SN}/rsp`):**
  ```json
  {
    "id": "cancel-uuid-001",
    "method_code": 7002,
    "target_task_id": "task-uuid-001",
    "target_session_id": "session-uuid-001"
  }
  ```
- **Response (`dev/{SN}/res`):**
  ```json
  {
    "id": "cancel-uuid-001",
    "method_code": 7002,
    "target_task_id": "task-uuid-001",
    "status": "cancelled" | "not_found",
    "message": "Task was successfully cancelled"
  }
  ```

---

## 5. Протокол передачи видео L4RTP/1 и туннель mTLS

Канал передачи видео между терминалом и сервером `l4media` реализуется через компонент `leo4proxy`:

1. **Транспорт:** TCP mTLS соединение на порт `dev.leo4.ru:8443` (или входной порт Ingress L4Media).
2. **Аутентификация:** клиентский X.509 сертификат терминала из хранилища `LocalMachine\My`, привязанный через Windows SChannel.
3. **Preamble (Wire Layout):**
   - Первые 4 байта: `0x4C, 0x34, 0x52, 0x54` (`"L4RT"`)
   - Байт 4: `0x01` (версия протокола)
   - Байт 5: `0x00` (reserved)
   - Байты 6-7: длина серийного номера (Big-Endian uint16)
   - Байты 8..8+N-1: ASCII строка серийного номера терминала
   - *Golden пример для SN "000100773" (9 байт):*
     `4C 34 52 54 01 00 00 09 30 30 30 31 30 30 37 37 33`
4. **Multiplexed Frames (RTP / RTCP):**
   - Байт 0: Channel ID (`0x01` = RTP видео, `0x02` = RTCP контроль)
   - Байт 1: Reserved (`0x00`)
   - Байты 2-3: Длина payload (Big-Endian uint16)
   - Байты 4..4+M-1: Payload пакета

---

## 6. Certificate Bootstrap и работа с PIN

1. **Bootstrap механизм (`l4pin`):**
   - Утилита `l4pin.exe` принимает 6-значный одноразовый PIN (`--pin 123456`) и адрес сервера выпуска сертификатов (`--server dev.leo4.ru`).
   - Генерирует ключевую пару RSA 2048 через Windows CNG (KSP: `Microsoft Software Key Storage Provider`).
   - Формирует CSR (PKCS#10) с subject `CN={SN}`.
   - Отправляет HTTP POST запрос на эндпоинт `/api/certificate-pin/exchange`.
   - Полученный X.509 сертификат и закрытый ключ устанавливаются в защищенное системное хранилище `LocalMachine\My`.
   - Корневой CA сертификат устанавливается в `LocalMachine\ROOT` (SHA-1 отпечаток: `B0A01EB219110CAC1077DD5171EF42A442AE5A87`).
2. **Сохранение отложенного PIN (`l4superv`):**
   - Если PIN передается при установке без сети, `l4superv` шифрует PIN через Windows DPAPI (`CryptProtectData`) с привязкой к локальной машине (`CRYPTPROTECT_LOCAL_MACHINE`) и сохраняет в защищенном реестре/состоянии до появления связи.

---

## 7. Capability Matrix и известные ограничения

| ID | Область | Наблюдаемое поведение / Ограничение | Рекомендация для верхних слоев (`iot-rpc`, `MenuBuilder`) |
|---|---|---|---|
| **CAP-01** | Стриминг | Ровно 1 активный видеопоток на терминал единовременно. | Повторный `stream_start` возвращает `already_running` с текущим `stream_instance_id` либо переключает источник при смене режима. |
| **CAP-02** | Lease/TTL | Поток автоматически завершается при истечении `expires_at_ms`. | Клиентский Web-интерфейс обязан регулярно посылать `lease_renew` (интервал по умолчанию: 30-60 с). |
| **CAP-03** | Ввод | Ввод отклоняется (`desktop_locked` или `session_unavailable`), если сессия Windows заблокирована. | UI должен отображать индикатор блокировки и блокировать пользовательский ввод в браузере. |
| **CAP-04** | SAS Ввод | Прямой ввод `Ctrl+Alt+Del` через `key_event` аппаратно перехватывается ядром Windows. | Использовать исключительно `shortcut_action` со значением `"ctrl_alt_del"`. |
| **CAP-05** | Shell Exec | Максимальное время жизни (`ttl_sec`) команды ограничено в `l4con` (таймаут по умолчанию 30 с, потоковая передача `seq` в `dev/{SN}/out`). | Сервер `iot-rpc-rest-app` должен учитывать `session_id` и накапливать чанки до получения `eof=true`. |
| **CAP-06** | ОС | Поддерживаются Windows 7 SP1 x86/x64, Windows 10, Windows 11, Windows Server 2008 R2+. | Для Windows 7 SP1 обязателен установленный пакет TLS 1.2. |

---

## 8. Доказательства тестирования (Test Evidence)

Все профильные локальные тесты компонентов `tools` были собраны и запущены в среде MSVC 2022 (x86/x64). Ниже приведены фактические результаты прогона:

1. **`tools/l4desk` (Агент видеотрансляции и управления):**
   - Команда: `tests\run_tests.cmd`
   - `test_ctl_protocol.exe`:
     ```text
     [PASS] test_json_min
     [PASS] test_fnv1a_stability (disp:2eec3b8c, cam:4fd3e579)
     [PASS] test_mouse_mapping
     [PASS] test_keyboard_whitelist
     [PASS] test_protocol_payloads
     [PASS] test_command_handling_validation
     [PASS] test_input_gate_and_shortcuts
     === ALL PROTOCOL UNIT TESTS PASSED ===
     ```
   - `test_orchestrator.exe` (10 сценариев):
     ```text
     [TEST 1] Testing stream_start (desktop)... [OK]
     [TEST 2] Testing already_running idempotency... [OK]
     [TEST 3] Testing controlled switch to camera... [OK]
     [TEST 4] Testing two-phase soft stop (phase 1: q\n)... [OK]
     [TEST 5] Testing already_stopped idempotency... [OK]
     [TEST 6] Testing hard kill phase 2 on hanging process... [OK]
     [TEST 7] Testing crash detection... [OK]
     [TEST 8] Testing reconciliation & PID reuse protection... [OK]
     [TEST 9] Testing recovery-loop auto-restart... [OK]
     [TEST 10] Testing restart budget limit (>5 attempts)... [OK]
     ALL ORCHESTRATOR TESTS PASSED SUCCESSFULLY!
     ```

2. **`tools/leo4proxy` (Туннель L4RTP/1 и прокси):**
   - `test_rtp_wire.exe`:
     ```text
     [TEST] Checking L4RTP/1 preamble wire format...
       [PASS] Preamble matches expected wire specification byte-for-byte.
     [TEST] Checking L4RTP/1 frame wire format (RTP & RTCP)...
       [PASS] Frame headers and payload lengths match wire specification.
     [TEST] Checking start validation (SN and ports)...
       [PASS] Empty SN correctly rejected.
       [PASS] Identical RTP and RTCP port correctly rejected.
       [PASS] Sockets successfully created and bound to loopback.
       [PASS] RTP tunnel stopped cleanly.
     ALL RTP TUNNEL TESTS PASSED SUCCESSFULLY!
     ```

3. **`tools/l4pin` (Обнаружение и валидация сертификатов):**
   - `test_cert_discovery.exe`:
     ```text
     [PASS] test_absent
     [PASS] test_valid
     [PASS] test_expiring
     [PASS] test_broken_no_key
     [PASS] test_broken_wrong_sn
     [PASS] test_duplicates
     [PASS] test_non_leo4_issuer
     Test Results: 7 / 7 PASSED
     ```

4. **`tools/l4superv` (Супервизор и управление состоянием):**
   - `test_pending_pin.exe`: `4 / 4 PASSED` (`test_pin_masking`, `test_dpapi_and_pending_pin`, `test_state_unknown_keys_preservation`, `test_state_machine_matrix`).
   - `test_session_proc.exe`: `5 / 5 PASSED` (запуск в пользовательских WTS сессиях, elevation, TokenLinkedToken).
   - `test_wait_active_component.exe`: `PASS` (проверка перехода `Standby` -> `Active` за 1031 мс при нормативе <= 15 с).

---

## 9. Фикстуры baseline

Созданные файлы фикстур сохранены в каталоге `tools/docs/l4desk/fixtures/`:
1. `tools/docs/l4desk/fixtures/rpc_7000_stream_control.json` — golden fixtures запросов/ответов для видеопотока, lease renew, inventory и удаленного ввода.
2. `tools/docs/l4desk/fixtures/rpc_7001_exec.json` — golden fixtures удаленного выполнения команд, чанков вывода (`dev/{SN}/out`) и итогового статуса задачи.
3. `tools/docs/l4desk/fixtures/rpc_7002_cancel.json` — golden fixtures отмены активных задач диагностики.
4. `tools/docs/l4desk/fixtures/mqtt_presence_lifecycle.json` — golden fixtures LWT will / online / offline сообщений для ролей `main_app` и `extra_service`.
5. `tools/docs/l4desk/fixtures/l4rtp_wire_protocol.json` — побайтовая раскладка и hex-представление preamble и кадров L4RTP/1.
6. `tools/docs/l4desk/fixtures/baseline_capabilities.json` — матрица поддерживаемых возможностей, системных требований и известных ограничений.

---

## 10. Заключение и передача эстафеты в `L4D-00B-IOT`

Контракт опубликованного Агента зафиксирован в виде неизменяемых фикстур и отчёта с верификацией артефакта.
Контракт полностью готов для потребления следующим шагом каскада `L4D-00B-IOT` (`iot-rpc-rest-app`).
Код MQTT-клиентов и runtime-артефакты не модифицировались.
Артефакт `1.7.7` имеет статус `PUBLISHED`.
Candidate handoff ID: `H-L4D-00A-TOOLS-v1`.
