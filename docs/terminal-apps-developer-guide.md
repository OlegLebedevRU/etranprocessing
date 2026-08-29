# Руководство разработчика: Интеграция с терминальным комплексом служб Leo4

Настоящее руководство предназначено для разработчиков приложений, работающих на стороне платёжных терминалов и киосков самообслуживания платформы **etranprocessing**. Документ описывает принципы взаимодействия с комплексом системных служб `leo4proxy`, `mosquitto`, `l4con` и `l4superv`.

---

## 1. Архитектура взаимодействия на терминале

На каждом терминале развёрнут автономный стек системных служб Windows, изолирующий криптографию, маршрутизацию и управление жизненным циклом соединений:

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              Платёжный терминал                                 │
│                                                                                 │
│   ┌───────────────────────────┐         ┌───────────────────────────────────┐   │
│   │   Основное приложение     │         │   Вспомогательные службы          │   │
│   │   (UI, Core / main_app)   │         │   (l4con, телеметрия / extra_svc) │   │
│   └─────────────┬─────────────┘         └─────────────────┬─────────────────┘   │
│                 │                                         │                     │
│                 │      Локальный MQTT (порт 1883, No-SSL) │                     │
│                 ▼                                         ▼                     │
│       ┌────────────────────────────────────────────────────────┐                │
│       │               Mosquitto (Локальный брокер)             │                │
│       └───────────────────────────┬────────────────────────────┘                │
│                                   │ Локальный Bridge (порт 18883)               │
│                                   ▼                                             │
│       ┌────────────────────────────────────────────────────────┐                │
│       │                 Leo4Proxy (mTLS туннель)               │                │
│       │   - Источник истины по SN и сертификату                │                │
│       │   - Защищённый mTLS мост к dev.leo4.ru:8883           │                │
│       │   - Reverse Proxy: внешний порт 443 -> локальный 8000   │                │
│       │   - HTTP API метрик и метаданных: порт 18443           │                │
│       └───────────────────────────┬────────────────────────────┘                │
│                                   │                                             │
│                                   │ Внешний mTLS туннель (HTTPS / MQTTS)        │
└───────────────────────────────────┼─────────────────────────────────────────────┘
                                    ▼
                     Облачная платформа etranprocessing
```

### Ключевые компоненты:
1. **`leo4proxy`**: Источник истины по серийному номеру (`SN`) и сертификату. Обеспечивает аппаратную mTLS-аутентификацию с использованием Windows CNG KSP.
2. **`mosquitto`**: Локальный высокопроизводительный MQTT-брокер на `127.0.0.1:1883`. Автоматически мостирует сообщения во внешнее облако через туннель `leo4proxy`.
3. **`l4superv`**: Системный супервизор. Следит за состоянием служб, генерирует конфигурации и перезапускает брокер при появлении/ротации сертификата.
4. **Внутренний HTTP Backend**: Приложение терминала (или бэкенд меню), слушающее порт `127.0.0.1:8000`, доступ к которому из облака проксируется через `leo4proxy` на внешнем порту `443`.

---

## 2. Интеграция MQTT-клиентов

### 2.1. Определение роли MQTT-клиента
Перед реализацией клиента необходимо строго определить его роль:
* **`main_app`** — Основное платежное или управляющее приложение терминала (интерфейс пользователя, процессинг платежей).
* **`extra_service`** — Вспомогательный фоновый сервис (агент удалённой диагностики `l4con`, модуль сбора логов, мониторинг оборудования).

### 2.2. Параметры подключения к брокеру
Все клиентские приложения терминала подключаются **только к локальному брокеру**:
* **Хост**: `127.0.0.1` (или `localhost`)
* **Порт**: `1883`
* **Протокол**: MQTT v3.1.1 или MQTT v5.0 (No-SSL / TCP)
* **Аутентификация**: Anonymous (или пользователь `main_app` / `extra_service` в зависимости от настроек ACL)
* **Keep Alive**: `60` секунд (рекомендуется)
* **Clean Session**: `true`

---

### 2.3. Получение серийного номера (`SN`) устройства
Клиенты **не должны** читать реестр или хранилище сертификатов напрямую. Серийный номер запрашивается по локальному REST API у `leo4proxy`:

* **URL**: `GET http://127.0.0.1:18443/_leo4/info`
* **Быстрый эндпоинт**: `GET http://127.0.0.1:18443/_leo4/sn`

**Пример ответа `GET /_leo4/info`**:
```json
{
  "status": "ready",
  "certificate_found": true,
  "sn": "a4b0000773c82116d210826",
  "client_id": "a4b0000773c82116d210826",
  "local_hostname": "leo4-0000773.device.leo4.ru"
}
```

> **Важно**: Если `status == "waiting_for_certificate"`, поле `sn` пустое. Приложение должно периодически опрашивать эндпоинт (раз в 5–10 секунд) до получения `sn`.

---

### 2.4. Сценарий статуса и присутствия (Presence / LWT)

#### Сценарий для `main_app` (Основное приложение):
```text
1. До подключения (CONNECT):
   will_topic   = dev/{SN}/app
   will_payload = app_offline
   will_qos     = 1
   will_retain  = true

2. После успешного ответа брокера (CONNACK):
   PUBLISH dev/{SN}/app = app_online (qos = 1, retain = true)

3. При штатном завершении работы (Shutdown):
   PUBLISH dev/{SN}/app = app_offline (qos = 1, retain = true)
   DISCONNECT
```

#### Сценарий для `extra_service` (Вспомогательный сервис):
```text
1. До подключения (CONNECT):
   will_topic   = dev/{SN}/svc
   will_payload = svc_offline
   will_qos     = 1
   will_retain  = true

2. После успешного ответа брокера (CONNACK):
   PUBLISH dev/{SN}/svc = svc_online (qos = 1, retain = true)

3. При штатном завершении работы (Shutdown):
   PUBLISH dev/{SN}/svc = svc_offline (qos = 1, retain = true)
   DISCONNECT
```

---

### 2.5. Спецификация топиков обмена

Все топики строго привязаны к серийному номеру `{SN}` терминала:

| Топик | Направление | QoS | Описание |
|---|---|---|---|
| `dev/{SN}/app` | Публикация | 1 (Retain) | Статус онлайн/оффлайн основного приложения (`app_online` / `app_offline`). |
| `dev/{SN}/svc` | Публикация | 1 (Retain) | Статус онлайн/оффлайн сервиса (`svc_online` / `svc_offline`). |
| `dev/{SN}/out` | Публикация | 0 / 1 | Исходящие асинхронные события и нотификации от терминала на сервер. |
| `dev/{SN}/evt` | Публикация | 1 | Телеметрия, события датчиков, инкассация, купюроприёмник. |
| `dev/{SN}/res` | Публикация | 1 | Ответы на RPC-команды от сервера. |
| `srv/{SN}/tsk` | Подписка | 1 | Входящие задачи и RPC-команды от сервера терминалу. |
| `srv/{SN}/rsp` | Подписка | 1 | Ответы сервера на синхронные запросы терминала. |
| `srv/{SN}/#`   | Подписка | 1 | Все входящие сообщения для данного терминала от облака. |

---

### 2.6. Примеры реализации MQTT-клиента

#### Пример на Python (`paho-mqtt`):
```python
import time
import requests
import paho.mqtt.client as mqtt

# 1. Получаем SN устройства от leo4proxy
def get_device_sn() -> str:
    while True:
        try:
            resp = requests.get("http://127.0.0.1:18443/_leo4/info", timeout=3)
            data = resp.json()
            if data.get("status") == "ready" and data.get("sn"):
                return data["sn"]
        except Exception:
            pass
        time.sleep(5)

SN = get_device_sn()

# 2. Настраиваем MQTT-клиент (роль: main_app)
client = mqtt.Client(client_id=f"main_app_{SN}", protocol=mqtt.MQTTv311)

# Обязательный Last Will (LWT)
client.will_set(topic=f"dev/{SN}/app", payload="app_offline", qos=1, retain=True)

def on_connect(cli, userdata, flags, rc):
    if rc == 0:
        print(f"Connected to Mosquitto. SN: {SN}")
        # Публикуем статус online
        cli.publish(f"dev/{SN}/app", payload="app_online", qos=1, retain=True)
        # Подписываемся на команды сервера
        cli.subscribe(f"srv/{SN}/#", qos=1)

def on_message(cli, userdata, msg):
    print(f"Received command: {msg.topic} -> {msg.payload.decode('utf-8')}")

client.on_connect = on_connect
client.on_message = on_message

client.connect("127.0.0.1", 1883, keepalive=60)
client.loop_start()

try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    # Корректное завершение
    client.publish(f"dev/{SN}/app", payload="app_offline", qos=1, retain=True)
    client.disconnect()
    client.loop_stop()
```

#### Пример на C# (.NET / MQTTnet):
```csharp
using System;
using System.Net.Http;
using System.Text.Json;
using System.Threading.Tasks;
using MQTTnet;
using MQTTnet.Client;

class Program
{
    static async Task Main(string[] args)
    {
        string sn = await GetDeviceSnAsync();
        var factory = new MqttFactory();
        using var mqttClient = factory.CreateMqttClient();

        var options = new MqttClientOptionsBuilder()
            .WithTcpServer("127.0.0.1", 1883)
            .WithClientId($"main_app_{sn}")
            .WithWillTopic($"dev/{sn}/app")
            .WithWillPayload("app_offline")
            .WithWillQualityOfServiceLevel(MQTTnet.Protocol.MqttQualityOfServiceLevel.AtLeastOnce)
            .WithWillRetain(true)
            .WithCleanSession()
            .Build();

        mqttClient.ConnectedAsync += async e =>
        {
            Console.WriteLine("MQTT Connected");
            await mqttClient.PublishStringAsync($"dev/{sn}/app", "app_online", 
                MQTTnet.Protocol.MqttQualityOfServiceLevel.AtLeastOnce, retain: true);
            await mqttClient.SubscribeAsync($"srv/{sn}/#");
        };

        await mqttClient.ConnectAsync(options);
        Console.ReadLine();

        // Штатное завершение
        await mqttClient.PublishStringAsync($"dev/{sn}/app", "app_offline", 
            MQTTnet.Protocol.MqttQualityOfServiceLevel.AtLeastOnce, retain: true);
        await mqttClient.DisconnectAsync();
    }

    static async Task<string> GetDeviceSnAsync()
    {
        using var http = new HttpClient();
        var resp = await http.GetStringAsync("http://127.0.0.1:18443/_leo4/info");
        using var doc = JsonDocument.Parse(resp);
        return doc.RootElement.GetProperty("sn").GetString()!;
    }
}
```

---

## 3. Интеграция внутреннего HTTP Backend сервиса

### 3.1. Принцип работы Reverse Proxy
`leo4proxy` слушает внешний HTTPS-порт `443` (mTLS) и выполняет роль защитного шлюза:
* Входящие защищённые запросы от облачного сервера расшифровываются с проверкой клиентского сертификата.
* Запрос транслируется на локальный сервис по адресу `http://127.0.0.1:8000`.

### 3.2. Требования к внутреннему сервису (порт 8000)
1. **Привязка к адресу**: Сервис должен слушать `127.0.0.1:8000` (или `0.0.0.0:8000`).
2. **Идентификационные заголовки**: `leo4proxy` передаёт метаданные вызывающего клиента в HTTP-заголовках:
   * `X-Client-Cert-DN` — Данные субъекта клиентского сертификата.
   * `X-Client-Cert-Serial` — Серийный номер сертификата.
   * `X-Real-IP` — Реальный IP-адрес облачного сервера.
   * `X-Forwarded-For` — Цепочка проксирования.
3. **Keep-Alive**: Рекомендуется поддержка HTTP/1.1 persistent connections.

---

## 4. Справочник REST API службы `leo4proxy` (порт 18443)

| Метод | URL | Описание |
|---|---|---|
| `GET` | `http://127.0.0.1:18443/_leo4/info` | Полная JSON-информация: статус сертификата, SN, SAN, порты, статистика трафика. |
| `GET` | `http://127.0.0.1:18443/_leo4/sn` | Быстрый текстовый ответ, содержащий только серийный номер устройства. |
| `GET` | `http://127.0.0.1:18443/_leo4/status` | Краткий статус: `ready` (работает) или `waiting_for_certificate` (нет сертификата). |
| `GET` | `http://127.0.0.1:18443/_leo4/health` | Проверка жизнеспособности прокси (возвращает HTTP 200 `{"status": "ok"}`). |

---

## 5. Рекомендации по устойчивости к сбоям

1. **Обработка ротации сертификата**:
   * При обновлении сертификата `leo4proxy` перезагружает его контекст на лету без остановки HTTP API.
   * Локальные MQTT-клиенты (`main_app`, `extra_service`) **не теряют соединение** с локальным брокером Mosquitto, так как рестарт моста происходит прозрачно.
2. **Обработка отсутствия связи**:
   * При обрыве связи с интернетом локальный брокер `Mosquitto` накапливает сообщения с QoS 1/2 в памяти. При восстановлении туннеля сообщения передаются в облако автоматически.
