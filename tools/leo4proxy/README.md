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

## 3. Сборка (Zero-Dependency & Unified 32/64 Архитектура)

`leo4proxy` полностью поддерживает как **32-битные (x86)**, так и **64-битные (x64)** версии Windows (включая Windows 7 / POSReady 7 x86, Windows 10/11 x86 и x64).

### Вариант 1: Автоматическая унифицированная сборка через MSVC Build Tools 2022 (`/MT`)
Скрипт `build.cmd` автоматически компилирует статически слинкованные бинарники под обе архитектуры (x86 и x64):
```cmd
cd tools\leo4proxy
build.cmd
```
* **Параметры сборки:**
  - `build.cmd` (или `build.cmd all`) — собирает обе архитектуры (x86 и x64).
  - `build.cmd x86` — собирает только 32-битную версию.
  - `build.cmd x64` — собирает только 64-битную версию.
* **Результаты сборки в каталоге `bin/`:**
  - `bin\x86\leo4proxy.exe` — 32-битный нативный бинарник (универсален: работает на 32-битных ОС и на 64-битных через WOW64).
  - `bin\x64\leo4proxy.exe` — 64-битный нативный бинарник.
  - `bin\leo4proxy.exe` — стандартный исполняемый файл по умолчанию (копия x86 для максимальной совместимости со всеми терминалами).
* Не требует внешних DLL (`/MT`), использует системные библиотеки Windows: `ws2_32.dll`, `crypt32.dll`, `ncrypt.dll`, `secur32.dll`, `advapi32.dll`, `shell32.dll`, `user32.dll`, `gdi32.dll`, `iphlpapi.dll`.

### Вариант 2: Через CMake (CLion / MinGW / Ninja)
```powershell
# Сборка x64
cmake -B build -G Ninja
cmake --build build --config Release

# Сборка x86 (32-bit Win32)
cmake -B build32 -A Win32
cmake --build build32 --config Release
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

### 4.3. Готовые CMD-скрипты с автоматическим запросом прав администратора (UAC Elevation)
В папке `tools/leo4proxy/` (и в распространяемом дистрибутиве) доступен набор командных файлов с префиксом `leo4proxy_`. Все скрипты:
- Автоматически запрашивают права администратора Windows через UAC (Self-Elevation).
- Выводят все системные сообщения исключительно на **английском языке**, исключая любые проблемы с кодировками (CP866, CP1251, UTF-8).
- При успешном выполнении **автоматически закрывают консоль** без зависания окон.
- В случае возникновения ошибки выводят **акцентированный баннер ошибки** и ожидают нажатия клавиши (`pause`) для удобной диагностики.

| Скрипт | Назначение | Описание работы |
|---|---|---|
| `leo4proxy_install.cmd` (`leo4proxy_install_start.cmd`) | **Инсталляция + Старт** | Авто-UAC, настройка правил Брандмауэра Windows Defender, регистрация службы `Leo4Proxy` в SCM (тип запуска `SERVICE_AUTO_START` с автовосстановлением) и немедленный запуск службы. |
| `leo4proxy_uninstall.cmd` (`leo4proxy_stop_uninstall.cmd`) | **Стоп + Деинсталляция** | Авто-UAC, остановка службы и процессов `leo4proxy`, полное удаление службы из SCM и очистка правил Брандмауэра. |
| `leo4proxy_start.cmd` | **Идемпотентный старт** | Авто-UAC, проверяет наличие службы в системе (если не установлена — выполняет инсталляцию и настройку Firewall) и запускает службу `Leo4Proxy`. |
| `leo4proxy_stop.cmd` | **Остановка** | Авто-UAC, корректная остановка службы Windows `Leo4Proxy` и завершение фоновых процессов. |
| `leo4proxy_restart.cmd` | **Перезапуск** | Авто-UAC, отправка команды на перезапуск службы в SCM (или автоустановка при отсутствии). |
| `leo4proxy_status.cmd` | **Диагностика и статус** | Проверка статуса службы в SCM (`sc query`), детальный статус через CLI и тестирование сертификата/mTLS. |

Любой скрипт можно запускать как двойным кликом из Проводника Windows (UAC появится автоматически), так и передавать дополнительные параметры (например: `leo4proxy_install.cmd --reverse-target 127.0.0.1:8000`).

### 4.4. Запуск в консольном режиме
* **Запуск в консоли с подробным логированием:**
  ```cmd
  leo4proxy.exe -f --verbose
  ```

### 4.5. Настройка параметров подключения
| Параметр | По умолчанию | Описание |
|---|---|---|
| `--reverse-target <host:port>` | `127.0.0.1:8000` | Внутренний plain HTTP бэкенд для обратного проксирования |
| `--reverse-listen <ip:port>` | `0.0.0.0:443` | Внешний IP и порт для входящих HTTPS-клиентов |
| `--reverse-port <port>` | `443` | Внешний порт для входящего HTTPS |
| `--no-reverse` | `0` (включен) | Отключить обратный HTTPS-прокси |
| `--domain <name>` | авто (SAN DNS / `leo4-<sn>.local`) | Переопределить имя домена для mDNS/LLMNR |
| `--no-discovery` | `0` (включено) | Отключить анонсирование mDNS (:5353) и LLMNR (:5355) |
| `--no-firewall` | `0` (включено) | Отключить авто-настройку правил Windows Defender Firewall |
| `--no-elevate` | `0` (авто) | Отключить авто-элевацию прав администратора через UAC |
| `--mqtt-remote <host:port>` | `dev.leo4.ru:8883` | Удаленный адрес MQTT-брокера |
| `--mqtt-local <ip:port>` | `127.0.0.1:18883` | Локальный TCP-порт прямого прокси MQTT |
| `--http-remote <host:port>` | `iot-processing.ru:443` | Удаленный адрес прямого HTTPS бэкенда процессинга |
| `--http-local <ip:port>` | `127.0.0.1:18443` | Локальный HTTP-порт прямого прокси |
| `--local-ssl` | auto-detect | Включить SSL/TLS на локальных слушателях |
| `--cert-email <pattern>` | `*.terminal@leo4.ru` -> `*.terminal@forpay.ru` | Шаблон поиска email в сертификате |
| `--cert-thumbprint <sha1>` | auto (самый свежий) | Выбор сертификата по отпечатку SHA-1 |
| `--user-store` | `LocalMachine\MY` | Использовать хранилище `CurrentUser\MY` |
| `--secure` | lax / manual validation | Включить строгую валидацию CA сервера |

### 4.6. Системный трей Windows (System Tray & Notification Icon)
При запуске `leo4proxy.exe` в интерактивном режиме в области уведомлений Windows (System Tray) отображается значок состояния:
- **Индикация состояния**:
  - 🟢 **Зеленый значок**: прокси активен и обрабатывает входящие подключения (`RUNNING`).
  - 🔴 **Красный значок**: прокси временно приостановлен (`STOPPED`).
- **Контекстное меню по правому клику**:
  - **Заголовок**: информационный заголовок со статусом и серийным номером терминала (`[RUNNING] Leo4Proxy (leo4-0000773)`).
  - **Быстрый переход .local**:
    - «🌐 Open https://leo4-0000773.local in Browser» — мгновенный переход в браузере по mDNS-имени.
    - «📋 Copy https://leo4-0000773.local to Clipboard» — копирование локального URL.
  - **Селектор Reverse Proxy**:
    - Индикатор: `Reverse HTTPS: RUNNING (:443 -> :8000)` / `STOPPED`.
    - Переключатель: «■ Stop Reverse Proxy» / «▶ Start Reverse Proxy».
  - **Селектор Forward Proxies**:
    - Индикатор: `Forward Proxies: RUNNING (MQTT/HTTP)` / `STOPPED`.
    - Переключатель: «■ Stop Forward Proxies» / «▶ Start Forward Proxies».
  - **Глобальное управление**: «■ Stop All Proxies», «▶ Start All Proxies», «🔄 Restart All Proxies».
  - **Блок Информация и Утилиты**:
    - «ℹ Information & Status...» — модальное окно с деталями сертификата, SAN DNS, LAN IP, Reverse Proxy, Forward Proxy и статусом правил Firewall.
    - «🌐 Open /_leo4/info in Browser» — переход к локальному JSON API.
    - «📋 Copy Device SN to Clipboard» — копирование серийного номера в буфер обмена Windows.
  - **Управление окном**: скрыть / показать окно консоли («👁 Hide/Show Console Window»).
  - **Выход**: «✕ Exit» — корректное завершение работы всех прокси и удаление иконки из трея.
- **Двойной клик левой кнопкой мыши**: быстрое открытие окна с подробной информацией о терминале, портах и сертификате.

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

## 6. Примеры интеграции для различных языков (Mosquitto Bridge NoSSL & Leo4Proxy)

Все примеры доступны в каталоге **`tools/leo4proxy/examples/`** и разделены на две роли:

### 6.1. Роль `main_app` (Основное приложение — UI / Мастер)
Подключается к локальному Mosquitto Bridge (`127.0.0.1:1883`, plain TCP) как пользователь `main_app`, подписывается на серверные топики `srv/<SN>/#`, принимает RPC-задачи и удаленную диагностику, отправляет ответы `dev/<SN>/res` и выполняет опрос сервера `dev/<SN>/req`.

1. **Python Main App (`examples/mqtt_test_client.py`)**:
   - Автоматическое получение метаданных и `client_id` через `http://127.0.0.1:18443/_leo4/info`.
   - Подключение к `127.0.0.1:1883` по plain TCP (MQTT 5.0, user: `main_app`).
   - Полный жизненный цикл RPC: `srv/<SN>/tsk` -> `dev/<SN>/req` -> `srv/<SN>/rsp` -> `dev/<SN>/res` (статусы 206/200).
   - Встроенная подсистема **Remote Diagnostics** (методы 7000 STREAM_CONTROL, 7001 EXEC, 7002 CANCEL, streaming логов, 19 диагностических команд для Windows/Linux/SQL).
   - Фоновый цикл опроса (polling RPC `dev/<SN>/req`) и отправка событий состояния.
   ```bash
   uv run tools/leo4proxy/examples/mqtt_test_client.py
   ```

2. **C# Main App (`examples/csharp_example.cs` / `examples/csharp_example.csproj`)**:
   - Полнофункциональный C# клиент на базе `MQTTnet` (.NET 8 / 9).
   - Автоматическое разрешение SN через REST API прокси и проверка mTLS бэкенда (`/licensebilling/`).
   - Подписка на `srv/<SN>/#`, обработка серверных задач и отправка ответов `dev/<SN>/res`.
   - Цикл опроса сервера `dev/<SN>/req` с CorrelationData.
   ```powershell
   dotnet run --project tools/leo4proxy/examples/csharp_example.csproj
   ```

---

### 6.2. Роль `extra_service` (Вспомогательный сервис — цикл событий 10 минут)
Подключается к Mosquitto Bridge (`127.0.0.1:1883`) как пользователь `extra_service` и отправляет аппаратные события телеметрии в цикле раз в 10 минут (600 секунд) в топик `dev/<SN>/evt` (QoS 1, Retain 0) с пользовательскими свойствами MQTT 5.0:
- **Payload:** `{ "101": 36823, "102": "2026-08-03T12:41:33+03:00", "200": 888, "300": [ { "301": "044AFE42C76781", "302": 6, "303": 0 } ] }`
- **User Properties:** `event_type_code: 888`, `dev_event_id: 36823`, `dev_timestamp: 1740984093`, `correlation_id: <uuid>`

1. **Python Extra Service (`examples/python_client.py`)**:
   ```powershell
   uv run tools/leo4proxy/examples/python_client.py [--interval 600] [--once]
   ```
2. **C / Paho MQTT C (`examples/c_client_example.c`)**:
   - Сборка с `PAHO_WITH_SSL=OFF` (чистый TCP без OpenSSL).
   - Формирование MQTT 5.0 User Properties и циклическая публикация в `dev/<SN>/evt`.
3. **PowerShell (`examples/powershell_example.ps1`)**:
   ```powershell
   powershell -ExecutionPolicy Bypass -File tools/leo4proxy/examples/powershell_example.ps1 [-Once]
   ```
4. **cURL + Mosquitto_pub (`examples/curl_mosquitto_pub.cmd` / `examples/mosquitto_pub_example.cmd`)**:
   ```cmd
   tools\leo4proxy\examples\curl_mosquitto_pub.cmd [--once]
   ```
5. **MQTTX (`examples/mqttx_example.cmd`, `examples/mqttx_connection.json`, `examples/mqttx_scenario.js`)**:
   - Готовый CLI-раннер `mqttx_example.cmd`.
   - Импортируемый профиль подключений `mqttx_connection.json` для Desktop GUI MQTTX.
   - Сценарий `mqttx_scenario.js` для динамической генерации событий во вкладке Scripts MQTTX.

Подробная документация по всем примерам: **`tools/leo4proxy/examples/README.md`**.
