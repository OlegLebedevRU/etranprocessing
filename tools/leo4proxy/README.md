# Leo4Proxy — Унифицированный C / SChannel mTLS Прокси для Leo4 IoT & Etranprocessing

**`leo4proxy.exe`** — это высокопроизводительный нативный Win32 прокси-сервер и системная служба Windows (Windows Service) на чистом C, обеспечивающий прозрачное mTLS-подключение клиентских приложений (MQTT и HTTPS) к облачной платформе **Leo4 IoT** (`dev.leo4.ru:8883`) и бэкенду **Etranprocessing** (`https://iot-processing.ru:443`) с использованием клиентских сертификатов и **неэкспортируемых (non-exportable) приватных ключей** из **Windows Certificate Store** (`LocalMachine\MY`).

---

## 1. Решаемая проблема

1. **Non-Exportable ключи:** Все стандартные MQTT и HTTP библиотеки (Eclipse Paho C, Paho Python, Mosquitto, OpenSSL) требуют приватный ключ в виде незашифрованного PEM-файла на диске. В защищенной архитектуре терминалов приватные ключи генерируются внутри Windows CNG KSP (`MS_KEY_STORAGE_PROVIDER`) с запретом экспорта (`NCRYPT_ALLOW_EXPORT_NONE = 0`).
2. **Zero-Dependency & No OpenSSL:** Клиенты на C (Paho C) собираются без SSL (`PAHO_WITH_SSL=OFF`), клиенты на Python работают через стандартный plain TCP без вызова `tls_set()`, полностью исключая конфликт версий OpenSSL (`libssl-*.dll`, `libcrypto-*.dll`).
3. **Универсальность для всех стеков:** Любое клиентское приложение на **C, Python, C#, Go, Rust, PowerShell, cURL** подключается к локальному адресу `127.0.0.1:18883` (MQTT) или `127.0.0.1:18443` (HTTP), а `leo4proxy` выполняет аппаратное/системное шифрование Windows SChannel и mTLS аутентификацию.

---

## 2. Архитектура работы

```
 ┌─────────────────────────────────────────────────────────────────────────────────┐
 │                           Windows Terminal / Host                               │
 │                                                                                 │
 │   ┌──────────────────────┐   ┌──────────────────────┐   ┌────────────────────┐  │
 │   │ C Client (Paho C)    │   │ Python (paho-mqtt)   │   │ C# / PowerShell    │  │
 │   │ (PAHO_WITH_SSL=OFF)  │   │ (plain TCP loop)     │   │ HttpClient / curl  │  │
 │   └──────────┬───────────┘   └──────────┬───────────┘   └─────────┬──────────┘  │
 │              │                          │                         │             │
 │              │ tcp://127.0.0.1:18883    │ tcp://127.0.0.1:18883   │ :18443      │
 │              └──────────────────────────┼─────────────────────────┘             │
 │                                         ▼                                       │
 │  ┌───────────────────────────────────────────────────────────────────────────┐  │
 │  │                         leo4proxy.exe (C / Win32)                         │  │
 │  │                                                                           │  │
 │  │  • Windows Certificate Store: LocalMachine\MY                             │  │
 │  │    (Приоритет: свежий *.terminal@leo4.ru -> fallback: *.terminal@forpay.ru)│  │
 │  │  • SChannel SSPI mTLS Engine: TLS 1.2 / TLS 1.3, dwKeySpec = 0            │  │
 │  │  • Channel 1 (MQTT): 127.0.0.1:18883  ──mTLS──► dev.leo4.ru:8883         │  │
 │  │  • Channel 2 (HTTP): 127.0.0.1:18443  ──mTLS──► https://iot-processing.ru │  │
 │  │    - Автоматическая инжекция заголовка: X-Leo4-Proxy-Sn: <SN>             │  │
 │  │  • Встроенный REST API обнаружения SN:                                    │  │
 │  │    - GET http://127.0.0.1:18443/_leo4/info (JSON метаданные устройства)   │  │
 │  │    - GET http://127.0.0.1:18443/_leo4/sn   (Plaintext серийный номер SN) │  │
 │  │  • Windows Service Manager: автозапуск, авторестарт при сбоях (5s,10s,30s)│  │
 │  └──────────────────────────────────────┬────────────────────────────────────┘  │
 └─────────────────────────────────────────┼───────────────────────────────────────┘
                                           │
                    ┌──────────────────────┴──────────────────────┐
                    │ mTLS (Client Cert Auth)                     │ mTLS (Client Cert Auth)
                    ▼                                             ▼
     ┌─────────────────────────────┐               ┌─────────────────────────────┐
     │  Leo4 IoT Broker (RabbitMQ) │               │   Etranprocessing Backend   │
     │      dev.leo4.ru:8883       │               │    iot-processing.ru:443    │
     └─────────────────────────────┘               └─────────────────────────────┘
```

---

## 3. Сборка (Zero-Dependency)

### Вариант 1: Через MSVC Build Tools 2022 (Статическая сборка `/MT`)
```cmd
cd tools\leo4proxy
build.cmd
```
Результат: `tools\leo4proxy\bin\leo4proxy.exe` (~120 КБ, не требует внешних DLL, кроме стандартных системных библиотек Windows: `ws2_32.dll`, `crypt32.dll`, `ncrypt.dll`, `secur32.dll`, `advapi32.dll`).

### Вариант 2: Через CMake (CLion / MinGW / Ninja)
```powershell
cmake -B build -G Ninja
cmake --build build --config Release
```

---

## 4. Использование и CLI Команды

```cmd
leo4proxy.exe [ОПЦИИ]
```

### 4.1. Диагностика и получение SN
* **Вывод только серийного номера (SN) для скриптов:**
  ```cmd
  leo4proxy.exe --get-sn
  ```
  Выводит в stdout строку вида `a4b0000773c82116d210826` с кодом возврата `0`.

* **Тестирование сертификата и криптопровайдера:**
  ```cmd
  leo4proxy.exe --test-cert
  ```
  Проверяет хранилище `LocalMachine\MY`, выводит детальную информацию (Subject, Issuer, Serial, Thumbprint, срок действия) и выполняет тестовый захват контекста учетных данных SChannel SSPI (`AcquireCredentialsHandle`).

### 4.2. Управление службой Windows Service
При интерактивном запуске `leo4proxy.exe` автоматически регистрирует/обновляет службу `Leo4Proxy` в Windows Service Control Manager (SCM) с текущими аргументами:
* `--install` — установить / обновить конфигурацию службы в SCM и сохранить переданные аргументы командной строки как постоянный контекст службы.
* `--uninstall` — остановить и удалить службу.
* `--start` — запустить службу.
* `--stop` — остановить службу.
* `--restart` — перезапустить службу.
* `--status` — проверить статус службы (RUNNING, STOPPED, PID).

*Служба настраивается со свойствами:*
- Тип запуска: `SERVICE_AUTO_START` (автоматический запуск при старте Windows).
- Восстановление при сбоях (`SERVICE_FAILURE_ACTIONS`): автоматический перезапуск службы через 5 сек (1-й сбой), 10 сек (2-й сбой), 30 сек (последующие сбои).

### 4.3. Запуск в консольном режиме
* **Запуск в консоли с подробным логированием:**
  ```cmd
  leo4proxy.exe -f --verbose
  ```

### 4.4. Настройка параметров подключения
| Параметр | По умолчанию | Описание |
|---|---|---|
| `--mqtt-remote <host:port>` | `dev.leo4.ru:8883` | Удаленный адрес MQTT-брокера |
| `--mqtt-local <ip:port>` | `127.0.0.1:18883` | Локальный TCP-порт прокси MQTT |
| `--http-remote <host:port>` | `iot-processing.ru:443` | Удаленный адрес HTTPS бэкенда |
| `--http-local <ip:port>` | `127.0.0.1:18443` | Локальный HTTP-порт прокси |
| `--cert-email <pattern>` | `*.terminal@leo4.ru` -> `*.terminal@forpay.ru` | Шаблон поиска email в сертификате |
| `--cert-thumbprint <sha1>` | auto (самый свежий) | Выбор сертификата по отпечатку SHA-1 |
| `--user-store` | `LocalMachine\MY` | Использовать хранилище `CurrentUser\MY` |
| `--secure` | lax / manual validation | Включить строгую валидацию CA сервера |

---

## 5. Механизмы передачи Device SN внутренним клиентам

Для того чтобы клиентские модули (C, Python, C#, PowerShell, cURL) не зависели от низкоуровневых Win32 CryptoAPI, `leo4proxy` предоставляет 4 способа получения SN:

### Способ 1: Локальный JSON API (`GET /_leo4/info`)
Любой клиент делает HTTP GET запрос на `http://127.0.0.1:18443/_leo4/info` и получает JSON:
```json
{
  "status": "ok",
  "version": "1.0.0",
  "sn": "a4b0000773c82116d210826",
  "email": "1.terminal@forpay.ru",
  "subject": "CN=a4b0000773c82116d210826, O=1, OU=773, S=msk, C=ru, L=1, E=1.terminal@forpay.ru",
  "issuer": "C=RU, S=Moscow, L=Moscow, O=Leo4, OU=IT, CN=iot.leo4.ru, E=iot@leo4.ru",
  "serial": "20E0B7ED4A12548E80F8B987F04D075FE7C79878",
  "thumbprint": "63DD6951F1ED0C12186118C5A5A6D9AAA6A3C68E",
  "not_before": "2026-08-22 19:03:33 UTC",
  "not_after": "2027-08-22 19:03:33 UTC",
  "has_private_key": true,
  "endpoints": {
    "mqtt_local": "127.0.0.1:18883",
    "mqtt_remote": "dev.leo4.ru:8883",
    "http_local": "127.0.0.1:18443",
    "http_remote": "https://iot-processing.ru:443"
  }
}
```

### Способ 2: Plaintext SN API (`GET /_leo4/sn`)
Запрос на `http://127.0.0.1:18443/_leo4/sn` возвращает чистый текст:
```
a4b0000773c82116d210826
```

### Способ 3: CLI вызов в скриптах
```powershell
$sn = & "C:\Program Files\Leo4Proxy\leo4proxy.exe" --get-sn
```

### Способ 4: Автоматический HTTP-заголовок `X-Leo4-Proxy-Sn`
При любом HTTP-запросе через прокси `http://127.0.0.1:18443/...` прокси автоматически добавляет заголовок `X-Leo4-Proxy-Sn: <SN>` к исходящему запросу к бэкенду.

---

## 6. Примеры интеграции для различных языков

Все примеры доступны в каталоге `tools/leo4proxy/examples/`:

### 1. Python (`examples/mqtt_test_client.py` и `examples/python_client.py`)
- **`examples/mqtt_test_client.py`** — полнофункциональный эталонный клиент (соответствует `proxy_app/mqtt_test_client.py`):
  - Автоматическое получение метаданных и `client_id` через `http://127.0.0.1:18443/_leo4/info`.
  - Подключение к `127.0.0.1:18883` по plain TCP (MQTT 5.0).
  - Полный жизненный цикл RPC: `srv/<SN>/tsk` -> `dev/<SN>/req` -> `srv/<SN>/rsp` -> `dev/<SN>/res` (статусы 206/200).
  - Встроенная подсистема **Remote Diagnostics** (методы 7000 STREAM_CONTROL, 7001 EXEC, 7002 CANCEL, streaming логов, 19 диагностических команд для Windows/Linux/SQL).
  - Фоновый цикл опроса (polling RPC) и публикация событий состояния (`dev/<SN>/evt`, gauge 44, event 90).
- **`examples/python_client.py`** — быстрый демонстрационный скрипт (HTTP API + MQTT за 5 секунд).

```bash
# Запуск полнофункционального клиента через Leo4Proxy (благодаря PEP 723 зависимости разрешаются автоматически)
uv run tools/leo4proxy/examples/mqtt_test_client.py
```

### 2. C / Paho MQTT C (`examples/c_client_example.c`)
```c
// Собирается с PAHO_WITH_SSL=OFF без OpenSSL!
MQTTAsync client;
MQTTAsync_createOptions create_opts = MQTTAsync_createOptions_initializer5;
create_opts.MQTTVersion = MQTTVERSION_5;

// client_id = sn (серийный номер устройства)
MQTTAsync_createWithOptions(&client, "tcp://127.0.0.1:18883", sn, MQTTCLIENT_PERSISTENCE_NONE, NULL, &create_opts);

MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer5;
conn_opts.MQTTVersion = MQTTVERSION_5;
conn_opts.ssl = NULL; // Чистый TCP к прокси

MQTTAsync_connect(client, &conn_opts);
```

### 3. PowerShell (`examples/powershell_example.ps1`)
```powershell
# 1. Получение информации об устройстве
$info = Invoke-RestMethod -Uri "http://127.0.0.1:18443/_leo4/info"
Write-Host "Device SN: $($info.sn)"

# 2. Вызов mTLS бэкенда
$resp = Invoke-WebRequest -Uri "http://127.0.0.1:18443/licensebilling/" `
                          -Method Post `
                          -Body "function=check&Signature=..." `
                          -ContentType "application/x-www-form-urlencoded"
```

### 4. cURL (`examples/curl_example.cmd`)
```cmd
:: Запрос баланса через mTLS прокси
curl -X POST http://127.0.0.1:18443/licensebilling/ -d "function=check&Signature=..."
```
