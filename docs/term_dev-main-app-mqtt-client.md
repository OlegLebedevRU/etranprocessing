# Краткая инструкция: Внутренний MQTT-клиент main_app (C#)

Документ содержит только ключевые технические требования для быстрой интеграции основного C#-приложения терминала (`main_app`) с локальным брокером MQTT.

---

## 1. Подключение к брокеру

Терминальное приложение подключается **строго к локальному брокеру** (внешний mTLS-туннель в облако обслуживается системным мостом `leo4proxy` прозрачно).

| Параметр | Значение | Примечание |
|---|---|---|
| **Хост** | `127.0.0.1` (`localhost`) | Только локальный loopback |
| **Порт** | `1883` | Plain TCP (без SSL/TLS) |
| **Протокол** | MQTT v3.1.1 (или v5.0) | Стандартный стек MQTT |
| **Client ID** | `main_app_{SN}` | Обязателен префикс роли и серийный номер |
| **Clean Session** | `true` | Очистка сессии при старте |
| **Keep Alive** | `60` сек | Интервал пинга брокера |
| **Аутентификация**| Анонимная | Без логина и пароля |

---

## 2. Получение серийного номера (`SN`)

Единственным источником истины по серийному номеру является локальная служба `leo4proxy` (порт `18443`). Читать реестр или сертификаты напрямую **запрещено**.

- **Быстрый эндпоинт (рекомендуется)**: `GET http://127.0.0.1:18443/_leo4/sn` (возвращает чистую строку SN в UTF-8).
- **Информационный эндпоинт**: `GET http://127.0.0.1:18443/_leo4/info` (возвращает JSON `{"sn": "...", "status": "ready"}`).

> **Правило готовности**: При старте терминала сертификат может инициализироваться несколько секунд. Клиент должен опрашивать `GET /_leo4/sn` в цикле с паузой 2–3 секунды до получения непустого значения SN. До получения SN подключение к MQTT **не выполняется**.

---

## 3. Регламент присутствия и LWT (Last Will and Testament)

Для роли `main_app` регламентирован строгий сценарий отслеживания статуса:

```text
1. До вызова CONNECT (настройка Will):
   will_topic   = dev/{SN}/app
   will_payload = app_offline
   will_qos     = 1
   will_retain  = true

2. После успешного CONNACK:
   PUBLISH dev/{SN}/app = app_online (QoS 1, retain = true)

3. При штатной остановке приложения (Graceful Shutdown):
   PUBLISH dev/{SN}/app = app_offline (QoS 1, retain = true)
   DISCONNECT

4. При аварийном завершении (Crash / Kill / Socket drop):
   Брокер автоматически рассылает Will: dev/{SN}/app = app_offline (retain = true)
```

**Критичные требования**:
- Топик и полезная нагрузка строго фиксированы: `dev/{SN}/app` и `app_online` / `app_offline` (запрещено использовать суффиксы `svc`, `status`, `state`).
- Флаг `retain = true` обязателен и для Will, и для публикации статусов.

---

## 4. Соглашение по топикам

Формат всех топиков строго иерархический: `{направление}/{SN}/{суффикс}`. Ad-hoc топики запрещены.

### Исходящие от терминала на сервер (`dev/{SN}/...`):

| Топик | QoS | Retain | Назначение |
|---|---|---|---|
| `dev/{SN}/app` | 1 | **true** | Статус присутствия `main_app` (`app_online` / `app_offline`). |
| `dev/{SN}/evt` | 1 | false | Бизнес-события: транзакции, купюроприемник, инкассация, алерты. |
| `dev/{SN}/res` | 1 | false | Ответы на RPC-задачи/команды от сервера. |
| `dev/{SN}/req` | 0 / 1 | false | Запрос расширенных параметров задачи по `correlationData`. |
| `dev/{SN}/out` | 0 / 1 | false | Потоковый вывод/логи (при необходимости). |

> `dev/{SN}/svc` зарезервирован за вспомогательным сервисом диагностики (`l4con`). Основное приложение `main_app` его **не использует**.

### Входящие от сервера к терминалу (`srv/{SN}/...`):

| Топик | QoS | Назначение |
|---|---|---|
| `srv/{SN}/#`   | 1 | Рекомендуемая маска подписки на все входящие сообщения для терминала. |
| `srv/{SN}/tsk` | 1 | Анонсы задач и управляющих команд от облака. |
| `srv/{SN}/rsp` | 1 | Ответы сервера / полезная нагрузка RPC-команд. |
| `srv/{SN}/cmt` | 1 | Подтверждения фиксации обработки задач. |

---

## 5. Эталонный пример на C# (.NET / MQTTnet)

Минимальный рабочий класс клиента с соблюдением всех правил (используется пакет `MQTTnet`):

```csharp
using System;
using System.Net.Http;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using MQTTnet;
using MQTTnet.Client;
using MQTTnet.Protocol;

public class TerminalMqttClient : IAsyncDisposable
{
    private IMqttClient _client;
    private string _sn;

    public async Task StartAsync(CancellationToken ct = default)
    {
        // 1. Получаем SN устройства у leo4proxy
        _sn = await WaitForDeviceSnAsync(ct);

        // 2. Инициализируем клиент
        var factory = new MqttFactory();
        _client = factory.CreateMqttClient();

        // 3. Формируем опции с обязательным Will (LWT)
        var options = new MqttClientOptionsBuilder()
            .WithTcpServer("127.0.0.1", 1883)
            .WithClientId($"main_app_{_sn}")
            .WithCleanSession(true)
            .WithKeepAlivePeriod(TimeSpan.FromSeconds(60))
            .WithWillTopic($"dev/{_sn}/app")
            .WithWillPayload("app_offline")
            .WithWillQualityOfServiceLevel(MqttQualityOfServiceLevel.AtLeastOnce)
            .WithWillRetain(true)
            .Build();

        // 4. Обработчик успешного подключения
        _client.ConnectedAsync += async e =>
        {
            // Публикуем app_online СТРОГО после CONNACK с retain = true
            await _client.PublishStringAsync(
                $"dev/{_sn}/app",
                "app_online",
                MqttQualityOfServiceLevel.AtLeastOnce,
                retain: true,
                cancellationToken: ct);

            // Подписываемся на входящие команды сервера
            await _client.SubscribeAsync($"srv/{_sn}/#", MqttQualityOfServiceLevel.AtLeastOnce, ct);
        };

        // 5. Обработчик входящих сообщений
        _client.ApplicationMessageReceivedAsync += e =>
        {
            string topic = e.ApplicationMessage.Topic;
            string payload = Encoding.UTF8.GetString(e.ApplicationMessage.PayloadSegment);
            // Обработка бизнес-логики входящего топика...
            return Task.CompletedTask;
        };

        await _client.ConnectAsync(options, ct);
    }

    public async Task StopAsync()
    {
        if (_client == null || !_client.IsConnected) return;

        // Штатная остановка: явно отправляем app_offline с retain = true
        await _client.PublishStringAsync(
            $"dev/{_sn}/app",
            "app_offline",
            MqttQualityOfServiceLevel.AtLeastOnce,
            retain: true);

        await _client.DisconnectAsync();
    }

    private static async Task<string> WaitForDeviceSnAsync(CancellationToken ct)
    {
        using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(3) };
        while (!ct.IsCancellationRequested)
        {
            try
            {
                var sn = (await http.GetStringAsync("http://127.0.0.1:18443/_leo4/sn", ct)).Trim();
                if (!string.IsNullOrEmpty(sn)) return sn;
            }
            catch { /* Ожидание готовности leo4proxy */ }

            await Task.Delay(2000, ct);
        }
        throw new OperationCanceledException("Не удалось получить SN терминала.");
    }

    public async ValueTask DisposeAsync()
    {
        await StopAsync();
        _client?.Dispose();
    }
}
```
