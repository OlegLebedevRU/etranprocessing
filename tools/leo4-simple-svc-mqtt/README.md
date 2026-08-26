# leo4-simple-svc-mqtt

Легковесный автономный MQTT-клиент (роль `extra_service`) для Windows, предназначенный для подключения к локальному Mosquitto Bridge (`localhost:1883`) без внешних библиотек и зависимостей (Zero-Dependency C / WinSock2 / WinHTTP / SCM).

---

## 1. Назначение и функциональность

- **Роль**: `extra_service`
- **Протокол**: MQTT 3.1.1 (TCP Plain No-SSL)
- **Целевой брокер**: `tcp://127.0.0.1:1883` (внутренний мост Mosquitto / Leo4Proxy)
- **Presence & LWT сценарий**:
  1. При подключении (CONNECT):
     - `will_topic = dev/{SN}/svc`
     - `will_payload = svc_offline`
     - `will_retain = true`
     - `will_qos = 1`
  2. После успешного `CONNACK` (rc=0):
     - `PUBLISH dev/{SN}/svc = svc_online (retain=1, qos=1)`
  3. При штатном завершении (Normal Shutdown / Service Stop / Ctrl+C):
     - `PUBLISH dev/{SN}/svc = svc_offline (retain=1, qos=1)`
     - `DISCONNECT`
     - Закрытие сокета
  4. При аварийном отключении (Crash / Process Kill / Network drop):
     - Брокер автоматически публикует Will Message `dev/{SN}/svc = svc_offline (retain=1)`.
- **Ограничения**:
  - **НЕ** отправляет сообщения в топик `evt`.
  - **НЕ** подписывается на топики (no SUBSCRIBE).
- **Определение Serial Number (SN)**:
  - Автоматический опрос REST API локального `Leo4Proxy` (`GET http://127.0.0.1:18443/_leo4/sn`) через WinHTTP с авто-повторами при старте системы.
  - Поддержка переменной окружения `DEVICE_SN`.
  - Поддержка CLI-аргумента `--sn <SN>`.
- **Универсальный бинарник (x86/x64)**:
  - Собран статически (`/MT`) без зависимостей от Visual C++ Redistributable и OpenSSL.
  - 32-битная версия (`bin\leo4-simple-svc-mqtt.exe`) нативно работает как на 32-битных (x86), так и на 64-битных (x64) версиях Windows (через WOW64). Также собирается нативный x64 бинарник (`bin\x64\leo4-simple-svc-mqtt.exe`).

---

## 2. Сборка

Для сборки используется MSVC компилятор (`cl.exe` / `rc.exe`):

```cmd
cd tools\leo4-simple-svc-mqtt
build.cmd
```

Скрипт компилирует:
- `bin\x86\leo4-simple-svc-mqtt.exe` (32-битная статическая сборка)
- `bin\x64\leo4-simple-svc-mqtt.exe` (64-битная статическая сборка)
- `bin\leo4-simple-svc-mqtt.exe` (универсальный исполняемый файл для x86/x64)

---

## 3. Установка и управление службой Windows

Служба регистрируется под именем **`Leo4SimpleSvcMqtt`** с автозапуском (`SERVICE_AUTO_START`) и автоматическим перезапуском при сбоях (auto-recovery: 5с, 10с, 30с).

### Команды CLI:

| Команда | Описание |
|---|---|
| `leo4-simple-svc-mqtt.exe --install` | Установка службы в Windows SCM с автозапуском и сохранением параметров |
| `leo4-simple-svc-mqtt.exe --uninstall` | Остановка и полное удаление службы из Windows |
| `leo4-simple-svc-mqtt.exe --start` | Запуск зарегистрированной службы |
| `leo4-simple-svc-mqtt.exe --stop` | Корректная остановка службы с отправкой `svc_offline` |
| `leo4-simple-svc-mqtt.exe --restart` | Перезапуск службы |
| `leo4-simple-svc-mqtt.exe --status` | Проверка текущего статуса службы (RUNNING, STOPPED, PID) |
| `leo4-simple-svc-mqtt.exe --console` (или `-f`) | Запуск в интерактивном режиме консоли (остановка по Ctrl+C) |

### Готовые `.cmd` скрипты (с автоматическим запросом прав Администратора UAC):

- `leo4_svc_mqtt_install.cmd` — инсталляция и старт службы.
- `leo4_svc_mqtt_uninstall.cmd` — остановка и удаление службы.
- `leo4_svc_mqtt_start.cmd` — запуск службы.
- `leo4_svc_mqtt_stop.cmd` — остановка службы.
- `leo4_svc_mqtt_restart.cmd` — перезапуск службы.
- `leo4_svc_mqtt_status.cmd` — статус службы.

---

## 4. Параметры командной строки

```text
  --host <ip>        Хост MQTT-брокера (по умолчанию: 127.0.0.1)
  --port <port>      Порт MQTT-брокера (по умолчанию: 1883)
  --uri <uri>        URI MQTT-брокера (например, tcp://127.0.0.1:1883)
  --proxy-port <p>   HTTP-порт Leo4Proxy для запроса SN (по умолчанию: 18443)
  --sn <SN>          Явное задание серийного номера устройства
  --role <role>      MQTT role / username (по умолчанию: extra_service)
  --keepalive <sec>  Интервал keepalive в секундах (по умолчанию: 60)
  --reconnect <sec>  Пауза перед переподключением в секундах (по умолчанию: 5)
  --verbose          Подробный вывод отладочных сообщений
  --version, -v      Версия приложения
  --help, -h         Справка по использованию
```
