# Примеры No-SSL MQTT Клиентов для Leo4 & Mosquitto Bridge

В данном каталоге собраны примеры интеграции клиентских приложений на различных языках и стеках (**Python, C#, C, PowerShell, cURL / Mosquitto_pub, MQTTX**) для работы с облачной IoT-платформой **Leo4** через локальный **Mosquitto Bridge** (`127.0.0.1:1883`) и нативный mTLS-прокси **Leo4Proxy** (`127.0.0.1:18883`).

---

## 1. Архитектура подключения

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                             Терминал / Windows Host                             │
│                                                                                 │
│   ┌───────────────────────────────┐     ┌───────────────────────────────────┐   │
│   │   Main Application (Master)   │     │   Extra Service (Background Svc)  │   │
│   │   (Python / C# main_app)      │     │   (C / Python / PS / curl / MQTTX)│   │
│   └───────────────┬───────────────┘     └─────────────────┬─────────────────┘   │
│                   │                                       │                     │
│                   │ (TCP :1883, user: main_app)           │ (TCP :1883, user:   │
│                   │ readwrite: srv/<SN>/#, dev/<SN>/#     │  extra_service,     │
│                   │                                       │  write: dev/<SN>/evt│
│                   └───────────────────┬───────────────────┘                     │
│                                       ▼                                         │
│                      ┌─────────────────────────────────┐                        │
│                      │   Mosquitto Broker / Bridge     │                        │
│                      │        127.0.0.1:1883           │                        │
│                      │    (Локальный Plain TCP NoSSL)  │                        │
│                      └────────────────┬────────────────┘                        │
│                                       │                                         │
│                                       │ Upstream Bridge (remote_clientid=<SN>)  │
│                                       │ plain TCP :18883 (MQTT 5.0)             │
│                                       ▼                                         │
│                      ┌─────────────────────────────────┐                        │
│                      │     leo4proxy.exe (Win32)       │                        │
│                      │   127.0.0.1:18883 / 18443       │                        │
│                      │  (Windows SChannel mTLS Engine) │                        │
│                      │  - Сертификат: LocalMachine\MY  │                        │
│                      └────────────────┬────────────────┘                        │
└───────────────────────────────────────┼─────────────────────────────────────────┘
                                        │
                                        │ Внешнее защищенное соединение
                                        │ mTLS (TCP :8883)
                                        ▼
                       ┌─────────────────────────────────┐
                       │     Leo4 Cloud IoT Broker       │
                       │        dev.leo4.ru:8883         │
                       └─────────────────────────────────┘
```

---

## 2. Разделение ролей: `main_app` vs `extra_service`

| Роль | Назначение | Доступные топики (ACL) | Presence (LWT & Status) | QoS | Примеры |
|---|---|---|---|---|---|
| **`main_app`** | Основное приложение (UI / Мастер). Двусторонний обмен, прием задач (RPC), удаленная диагностика, опрос (poll) и отправка ответов. | `srv/<SN>/#` (readwrite)<br>`dev/<SN>/#` (readwrite) | **Will:** `dev/<SN>/app` -> `app_offline` (retain=1)<br>**Online:** `dev/<SN>/app` -> `app_online` (retain=1)<br>**Shutdown:** `dev/<SN>/app` -> `app_offline` (retain=1) | QoS 0 / 1 | • `mqtt_test_client.py`<br>• `csharp_example.cs` (`.csproj`) |
| **`extra_service`** | Вспомогательный фоновый сервис. Периодическая циклическая отправка телеметрии и аппаратных событий раз в 10 минут. | `dev/<SN>/evt` (write only)<br>`dev/<SN>/svc` (write only) | **Will:** `dev/<SN>/svc` -> `svc_offline` (retain=1)<br>**Online:** `dev/<SN>/svc` -> `svc_online` (retain=1)<br>**Shutdown:** `dev/<SN>/svc` -> `svc_offline` (retain=1) | QoS 1 | • `python_client.py`<br>• `c_client_example.c`<br>• `powershell_example.ps1`<br>• `curl_mosquitto_pub.cmd`<br>• `mqttx_example.cmd` / `mqttx_connection.json` |

---

## 3. Спецификация присутствия (Presence / LWT) и событий

### 3.1. Жизненный цикл присутствия для `main_app`
```
Client CONNECT:
  will_topic   = dev/{SN}/app
  will_payload = app_offline
  will_retain  = true (qos=1)

After CONNACK:
  PUBLISH dev/{SN}/app = app_online (retain=true, qos=1)

Normal shutdown:
  PUBLISH dev/{SN}/app = app_offline (retain=true, qos=1)
  DISCONNECT
```

### 3.2. Жизненный цикл присутствия для `extra_service`
```
Client CONNECT:
  will_topic   = dev/{SN}/svc
  will_payload = svc_offline
  will_retain  = true (qos=1)

After CONNACK:
  PUBLISH dev/{SN}/svc = svc_online (retain=true, qos=1)

Normal shutdown:
  PUBLISH dev/{SN}/svc = svc_offline (retain=true, qos=1)
  DISCONNECT
```

### 3.3. Спецификация события цикла (10 минут) для `extra_service`

Каждый `extra_service` отправляет сообщения в топик `dev/<SN>/evt` каждые 10 минут (600 секунд) со следующими параметрами:

* **Topic:** `dev/<SN>/evt` (где `<SN>` — серийный номер терминала, например `a3b1234567c10221d290825` или активный из прокси)
* **QoS:** `1`
* **Retain:** `0` (`false`)
* **Payload (JSON):**
  ```json
  {
    "101": 36823,
    "102": "2026-08-03T12:41:33+03:00",
    "200": 888,
    "300": [
      {
        "301": "044AFE42C76781",
        "302": 6,
        "303": 0
      }
    ]
  }
  ```
* **MQTT 5.0 User Properties:**
  * `event_type_code`: `888`
  * `dev_event_id`: `36823`
  * `dev_timestamp`: `1740984093` (текущий Unix Epoch Timestamp)
  * `correlation_id`: `d9afcbfa-3d2c-4304-8e69-f644fef29f1f` (UUID v4)

---

## 4. Обзор и запуск примеров

### 4.1. `main_app` — Основные приложения

#### 1. Python Main App (`mqtt_test_client.py`)
Полнофункциональный эталонный клиент с поддержкой жизненного цикла RPC, удаленной диагностики (методы 7000 STREAM_CONTROL, 7001 EXEC, 7002 CANCEL, 19 команд) и опроса сервера.
```powershell
# Запуск (зависимости paho-mqtt и pydantic разрешаются автоматически через uv)
uv run tools/leo4proxy/examples/mqtt_test_client.py

# Параметры окружения (при необходимости):
# $env:MQTT_PORT = "1883"
# $env:MQTT_USERNAME = "main_app"
# $env:MQTT_POLL_INTERVAL_SEC = "60"
# $env:MQTT_SEND_TEST_EVENTS = "1"
```

#### 2. C# Main App (`csharp_example.cs` + `csharp_example.csproj`)
Полнофункциональный C# (.NET 8 / 9) клиент на базе `MQTTnet`. Подключается к `127.0.0.1:1883` как `main_app`, подписывается на `srv/<SN>/#`, обрабатывает команды сервера, публикует ответы на `dev/<SN>/res` и выполняет опрос `dev/<SN>/req`.
```powershell
# Запуск через .NET SDK
dotnet run --project tools/leo4proxy/examples/csharp_example.csproj

# Сборка Release-бинарника
dotnet build tools/leo4proxy/examples/csharp_example.csproj -c Release
```

---

### 4.2. `extra_service` — Циклические отправщики событий (10 минут)

#### 1. Python Extra Service (`python_client.py`)
Скрипт на Python (PEP 723), отправляющий аппаратные события в цикле 10 минут (QoS 1, Retain 0, MQTT 5.0 User Properties).
```powershell
# Запуск циклической отправки (интервал по умолчанию: 600 сек / 10 минут)
uv run tools/leo4proxy/examples/python_client.py

# Отправка одного тестового события и выход
uv run tools/leo4proxy/examples/python_client.py --once

# Запуск с настраиваемым интервалом (например, 10 секунд)
uv run tools/leo4proxy/examples/python_client.py --interval 10
```

#### 2. C Extra Service (`c_client_example.c`)
Нативный C-клиент (Eclipse Paho MQTT C, `PAHO_WITH_SSL=OFF`, WinHTTP для получения SN).
```powershell
# Сборка в Visual Studio Developer Command Prompt:
cl.exe /MT /O2 /W3 c_client_example.c /I <path_to_paho_include> paho-mqtt3a.lib winhttp.lib ws2_32.lib

# Запуск:
c_client_example.exe [--interval 600] [--once]
```

#### 3. PowerShell Extra Service (`powershell_example.ps1`)
Скрипт PowerShell с автоматическим обнаружением SN, формированием JSON и отправкой событий через `mosquitto_pub` или Python-мост.
```powershell
# Запуск в цикле 10 минут
powershell -ExecutionPolicy Bypass -File tools/leo4proxy/examples/powershell_example.ps1

# Отправка одного события
powershell -ExecutionPolicy Bypass -File tools/leo4proxy/examples/powershell_example.ps1 -Once

# Кастомный интервал
powershell -ExecutionPolicy Bypass -File tools/leo4proxy/examples/powershell_example.ps1 -IntervalSeconds 300
```

#### 4. cURL + Mosquitto_pub (`curl_mosquitto_pub.cmd` / `mosquitto_pub_example.cmd`)
Пакетный файл Windows для тестирования HTTP mTLS API через `curl` и циклической отправки MQTT 5.0 событий через `mosquitto_pub`.
```cmd
:: Запуск в цикле 10 минут
tools\leo4proxy\examples\curl_mosquitto_pub.cmd

:: Отправка одного события
tools\leo4proxy\examples\curl_mosquitto_pub.cmd --once
```

#### 5. MQTTX (`mqttx_example.cmd`, `mqttx_connection.json`, `mqttx_scenario.js`)
* **CLI:** Запуск циклической публикации через утилиту `mqttx pub`:
  ```cmd
  tools\leo4proxy\examples\mqttx_example.cmd [--once]
  ```
* **GUI (Desktop):** Импорт файла конфигурации `mqttx_connection.json` в программу **MQTTX**:
  * Включает готовые профили `Leo4 Mosquitto Bridge (extra_service)` и `Leo4 Mosquitto Bridge (main_app)`.
  * Содержит преднастроенный шаблон сообщения с топиком `dev/<SN>/evt`, QoS 1, Retain 0 и User Properties.
* **Script / Simulation:** Скрипт `mqttx_scenario.js` для динамической генерации событий во вкладке *Scripts* клиента MQTTX.

---

## 5. Обнаружение Device SN

Все клиенты поддерживают иерархическое автоматическое определение серийного номера устройства (`SN`):
1. **REST API прокси:** `GET http://127.0.0.1:18443/_leo4/sn` (чистый текст) или `/_leo4/info` (JSON-метаданные).
2. **Переменные окружения:** `$env:DEVICE_SN` или `$env:MQTT_SN`.
3. **Fallback по умолчанию:** `a3b1234567c10221d290825` / `a4b0000773c82116d210826`.

---

## 6. Тестирование и верификация присутствия (Presence / LWT)

Для проверки статусов присутствия клиентов используется **специальный тестовый клиент без LWT**, который подписывается на топики `dev/+/app` и `dev/+/svc` на локальном брокере `127.0.0.1:1883`:

### 6.1. Режим онлайн-мониторинга статусов присутствия
```powershell
# Запуск специального клиента-наблюдателя (без LWT)
uv run tools/leo4proxy/examples/mqtt_test_client.py --monitor-presence

# С ограничением по времени (например, 30 секунд):
uv run tools/leo4proxy/examples/mqtt_test_client.py --monitor-presence --duration 30
```

### 6.2. Автоматический тестовый набор (Test Suite)
Запуск автоматической проверки всех примеров клиентов, сценариев `main_app`, `extra_service` и срабатывания LWT при аварийном завершении:
```powershell
uv run tools/leo4proxy/examples/test_presence_suite.py
```
