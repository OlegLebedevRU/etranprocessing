# ЗАДАНИЕ СЕНЬОРУ-АРХИТЕКТОРУ: спроектировать и подготовить два изолированных промпта на внедрение remote mouse control от MenuBuilder Video UI до Windows-терминала

## 0. Роль и ожидаемый результат

Ты — сеньор software/system architect для экосистемы Leo4.

Нужно провести предметное архитектурное исследование и подготовить **два самостоятельных, высококачественных промпта для агентов-исполнителей**:
0. **Сохранить промпты в текущем репозитории в папке prompts**
1. **Промпт №1: для проекта `D:\work\iot.leo4.ru\iot-rpc-rest-app`**  
   Реализовать и развернуть необходимую MQTT/control-инфраструктуру для оперативного удалённого управления мышью терминала:
    - отдельный MQTT control topic pool;
    - RabbitMQ topology, bindings, consumers;
    - Internal API, предназначенный для MenuBuilder;
    - presence, lease, delivery, ACK/NACK, correlation, TTL, rate limiting;
    - документация;
    - деплой без нарушения существующих device RPC, event pipeline, definitions, acl, internal flows.

2. **Промпт №2: для проекта `etranprocessing/MenuBuilder`**  
   Реализовать и развернуть интеграцию Video UI с подготовленным Internal API `iot-rpc-rest-app`, а также создать/собрать локальный Windows-агент:
    - улучшение страницы `/video`;
    - UI-включение управления мышью;
    - backend MenuBuilder как BFF с tenant-проверками;
    - интеграция только через Internal API `iot-rpc-rest-app`, без прямого MQTT;
    - отдельный C-подпроект `tools/l4desk`;
    - сборка и поставка `l4desk.exe`;
    - запуск агента на терминале с локальным Mosquitto;
    - деплой MenuBuilder без затрагивания несвязанных сервисов.

Эти промпты должны быть готовы для непосредственной передачи разным агентам-разработчикам. Их нужно внедрять **строго по порядку**:

```text
1. Реализация + деплой iot-rpc-rest-app.
2. Проверка доступности control infrastructure и topic/ACL/API contracts.
3. Реализация + сборка + деплой MenuBuilder и tools/l4desk.
4. End-to-end проверка remote mouse click:
   Video UI → MenuBuilder → iot-rpc-rest-app → MQTT → local Mosquitto →
   l4desk.exe → Win32 SendInput → MQTT ACK → iot-rpc-rest-app → MenuBuilder UI.
```

---

## 1. Общий бизнес- и технический контекст

Уже реализован и работает видеопоток с удалённого Windows-терминала:

```text
ffmpeg desktop capture
  → RTP/RTCP UDP localhost
  → leo4proxy (L4RTP/1, mTLS TCP)
  → l4media Nginx :8443
  → l4media-ingress
  → Janus Streaming Plugin
  → WebRTC
  → MenuBuilder UI /video
```

На странице:

```text
https://dev.leo4.ru:3000/video
```

видео транслируется из Janus в браузер. Текущий тестовый терминал:

```text
device_id: 773
SN: a4b0000773c82116d210826
```

Этот SN/device_id использовать ТОЛЬКО как приёмочный пример. Запрещено фиксировать его в исходном коде, маршрутах, SQL или UI-конфигурации.

Новая задача: добавить **обратное управление мышью**, когда оператор кликает по области видео в браузере, а курсор и click выполняются на удалённом Windows desktop.

Приоритеты:

```text
1. Безопасность и tenant-изоляция.
2. Надёжное подтверждаемое выполнение click.
3. Корректные координаты при разных разрешениях, aspect ratio и multi-monitor desktop.
4. Малый объём нового терминального кода.
5. Отсутствие влияния на RTP/Janus/video media path.
6. Не требуется геймерская задержка или непрерывная передача положения мыши.
```

---

## 2. Жёсткое архитектурное решение, которое необходимо принять за основу

### 2.1. Video plane и control plane строго разделены

Запрещено передавать mouse-команды:

- через RTP/L4RTP;
- через `leo4proxy` stream tunnel;
- через Janus Streaming plugin;
- через Janus DataChannel;
- через WebRTC video signalling;
- через обычный MQTT RPC lifecycle `tsk → req → rsp → res → cmt`;
- через generic device events `dev/<SN>/evt`;
- через `integration.commands` / `integration.topic` из RFC integration bus.

Правильный путь:

```text
┌──────────────────── Browser ────────────────────────┐
│ MenuBuilder /video                                   │
│ • WebRTC video from Janus                             │
│ • explicit remote-control UI                          │
│ • pointer/click only inside actual video image area   │
└───────────────────────┬──────────────────────────────┘
                        │ HTTPS, JWT
                        ▼
┌────────────────── MenuBuilder Backend ──────────────┐
│ BFF: user auth + org/device access check             │
│ Calls iot-rpc-rest-app Internal API                  │
│ Never accesses RabbitMQ directly                     │
└───────────────────────┬──────────────────────────────┘
                        │ HTTP only, Internal API credentials
                        ▼
┌────────────────── iot-rpc-rest-app ─────────────────┐
│ Owner of MQTT/RabbitMQ infrastructure                 │
│ • remote input leases                                │
│ • MQTT ctl publish / subscribe                        │
│ • presence and ACK/NACK correlation                   │
│ • rate limits / schema validation                     │
└───────────────────────┬──────────────────────────────┘
                        │ existing MQTT v5 device plane
                        ▼
┌──────────────────── Terminal ───────────────────────┐
│ RabbitMQ Native MQTT                                  │
│ ← leo4proxy mTLS MQTT                                 │
│ ← local Mosquitto bridge (plain localhost MQTT)       │
│ ← l4desk.exe                                          │
│ • validate command                                    │
│ • Win32 SendInput                                     │
│ • ACK/NACK and retained presence                      │
└──────────────────────────────────────────────────────┘
```

### 2.2. Использовать готовую MQTT-инфраструктуру

Infrastructure owner: `iot-rpc-rest-app`.

На терминале уже работает:

```text
l4desk.exe → plain MQTT → local Mosquitto bridge
           → leo4proxy mTLS MQTT
           → RabbitMQ Native MQTT
```

Не создавать:
- новый MQTT broker;
- отдельный server-side WebSocket control gateway;
- отдельный mTLS control socket;
- прямой AMQP/MQTT client в MenuBuilder;
- входящие сетевые порты на терминале.

---

## 3. MQTT control protocol: обязательные требования к анализу и финальным промптам

### 3.1. Topic naming rules

Существующие обязательные правила:

```text
dev/<SN>/<suffix> — device → server
srv/<SN>/<suffix> — server → device
```

`<SN>` — CN клиентского сертификата терминала.

Ввести новый трёхсимвольный suffix:

```text
ctl
```

Новый отдельный control topic pool:

```text
srv/<SN>/ctl  — operational remote-input commands: server → terminal
dev/<SN>/ctl  — ACK/NACK and l4desk presence: terminal → server
```

Пример:

```text
srv/a4b0000773c82116d210826/ctl
dev/a4b0000773c82116d210826/ctl
```

Уточнить в `iot-rpc-rest-app` фактические policy/ACL и корректно обновить документацию, topology и consumer bindings.

### 3.2. Почему нельзя использовать `evt`

Существующий поток:

```text
dev/<SN>/evt
  → event consumer
  → DeviceEvent/БД
  → дедупликация
  → webhook/integration publish
  → потенциальный srv/<SN>/eva
```

НЕ использовать для:
- `pointer_move`;
- click ACK/NACK;
- input-agent presence/heartbeat;
- иных высокочастотных control-сообщений.

Причины:
- загрязнение DeviceEvent-истории;
- нагрузка на БД, webhooks и integrations;
- неверная корреляция (`dev_event_id`/timestamp вместо `command_id`);
- ненужный EVA traffic;
- риск засорения очередей событиями курсора.

### 3.3. Почему нельзя использовать device RPC и integration bus

Не использовать:

```text
srv/<SN>/tsk
dev/<SN>/req
srv/<SN>/rsp
dev/<SN>/res
srv/<SN>/cmt
```

Причина: remote input — не задача с TTL/workflow, а low-latency command/ack flow.

Не использовать:

```text
integration.commands
integration.topic
```

Причина: это AMQPS integration plane доменных серверных приложений. Он создаёт обычные `DeviceTask` и переводит их в существующий RPC lifecycle. MenuBuilder — не tenant AMQP client, а внутренний сервис, который обязан работать через Internal API `app1`.

### 3.4. QoS и retain

Обязательные правила:

| Направление / тип | MQTT topic | QoS | Retain | Правило |
|---|---|---:|---:|---|
| Pointer movement | `srv/<SN>/ctl` | 0 | Нет | best-effort, latest-wins |
| Left mouse click | `srv/<SN>/ctl` | 1 | **Нет** | TTL ≤ 5 секунд, ACK/NACK |
| ACK / NACK | `dev/<SN>/ctl` | 1 | Нет | корреляция по `command_id` |
| Presence online/offline | `dev/<SN>/ctl` | 1 | Да | последнее состояние агента |

Никогда не устанавливать `retain=true` для:

```text
pointer_move
mouse_click
ack
nack
```

Для local Mosquitto bridge требуется сохранить retain в направлении terminal → cloud:

```conf
bridge_outgoing_retain true
```

Архитектор должен:
- проверить реальную схему Mosquitto bridge / topic mappings;
- убедиться, что `ctl` передаётся по bridge в обоих направлениях;
- описать необходимые конфигурационные изменения в промпте для MenuBuilder/l4desk;
- не делать предположений, если фактический bridge config лежит в другом проекте: обозначить точный файл/место исследования и сформулировать безопасный шаг изменения.

### 3.5. Формат сообщений

Нужен versioned JSON envelope, допустимый в MQTT v5.

#### Server → terminal: move

```json
{
  "v": 1,
  "type": "pointer_move",
  "command_id": "UUID",
  "lease_id": "UUID",
  "sn": "terminal-sn",
  "x": 32768,
  "y": 16384,
  "issued_at_ms": 1788805978123,
  "expires_at_ms": 1788805980123
}
```

#### Server → terminal: click

```json
{
  "v": 1,
  "type": "mouse_click",
  "command_id": "UUID",
  "lease_id": "UUID",
  "sn": "terminal-sn",
  "x": 32768,
  "y": 16384,
  "button": "left",
  "issued_at_ms": 1788805978123,
  "expires_at_ms": 1788805983123
}
```

#### Terminal → server: ACK

```json
{
  "v": 1,
  "type": "ack",
  "command_id": "UUID",
  "lease_id": "UUID",
  "sn": "terminal-sn",
  "result": "injected",
  "terminal_time_ms": 1788805978301
}
```

#### Terminal → server: NACK

```json
{
  "v": 1,
  "type": "nack",
  "command_id": "UUID",
  "lease_id": "UUID",
  "sn": "terminal-sn",
  "code": "interactive_desktop_unavailable",
  "message": "Interactive desktop is locked or inaccessible",
  "terminal_time_ms": 1788805978301
}
```

#### Terminal → server: presence

```json
{
  "v": 1,
  "type": "presence",
  "agent": "l4desk",
  "status": "online",
  "desktop_available": true,
  "screen": {
    "virtual_x": 0,
    "virtual_y": 0,
    "virtual_width": 4920,
    "virtual_height": 2000
  },
  "timestamp": "2026-09-08T12:00:00Z"
}
```

Для MQTT Last Will agent обязан публиковать retained `presence` offline.

### 3.6. Идемпотентность и доставка

QoS 1 даёт **at-least-once**, а не exactly-once.

`l4desk.exe` обязан:
- хранить bounded in-memory LRU/cache последних `command_id` не менее TTL команды;
- не выполнять повторно уже обработанный click;
- при повторной доставке вернуть сохранённый результат ACK/NACK;
- отклонять команду с истёкшим `expires_at_ms`;
- отклонять команду с чужим `sn`;
- отклонять неправильный/неактивный `lease_id`.

`iot-rpc-rest-app` обязан:
- не выполнять автоматический retry `mouse_click`, если нет ACK: действие могло сработать, но ACK потерялся;
- вернуть MenuBuilder статус `unconfirmed` / `timeout`;
- rate-limit:
    - `pointer_move`: максимум 10/секунду на active lease;
    - `mouse_click`: например максимум 5/секунду;
- контролировать размер JSON payload;
- логировать `command_id`, `lease_id`, `org_id`, `device_id`, `SN`, type, результат и latency.

---

## 4. Coordinate model и Win32 requirements

### 4.1. Frontend coordinates

Нельзя пересылать browser/CSS-пиксели как координаты Windows.

Frontend обязан:
1. определить реальный прямоугольник изображения внутри video element;
2. корректно исключить letterbox/pillarbox/padding при `object-fit: contain`;
3. не отправлять command при клике по чёрному полю вне фактического изображения;
4. нормализовать координаты:

```text
x: 0..65535
y: 0..65535
```

### 4.2. Terminal coordinates

`l4desk.exe` обязан инжектировать pointer position с:

```text
SendInput()
MOUSEEVENTF_ABSOLUTE
MOUSEEVENTF_VIRTUALDESK
```

и учитывать virtual desktop:

```text
SM_XVIRTUALSCREEN
SM_YVIRTUALSCREEN
SM_CXVIRTUALSCREEN
SM_CYVIRTUALSCREEN
```

Это требуется для multi-monitor окружений.

### 4.3. Alpha command scope

В первом внедрении разрешить только:

```text
pointer_move
mouse_click (button = left)
```

Не реализовывать:
- keyboard input;
- clipboard;
- shell, process execution;
- файлы;
- right click;
- scroll;
- drag-and-drop;
- `mouse_down`/`mouse_up`.

### 4.4. Event UX

Игровая реакция не нужна.

Frontend должен:
- throttle pointer_move до 5–10 сообщений/с;
- применять latest-wins: не накапливать старые движения;
- перед click отправлять final pointer position и затем click с теми же координатами;
- показывать ACK/NACK/timeout пользователю.

---

## 5. Lease и security model

Управление нельзя включать автоматически при открытии video page.

UI должен содержать явную кнопку:

```text
Включить управление
```

С предупреждением:

```text
Вы управляете мышью удалённого терминала.
Действия ограничены мышью, подтверждаются агентом и журналируются.
```

### 5.1. Lease model

`iot-rpc-rest-app` — owner remote-input lease state.

Минимальные поля:

```text
lease_id
org_id
device_id
device_sn
issued_to / caller identity or owner reference
created_at
expires_at
last_keepalive_at
```

Правила:
- у одного terminal SN только один active control lease;
- lease ограничен по времени, например 60 секунд;
- MenuBuilder поддерживает его keepalive каждые 15 секунд;
- Stop / смена устройства / logout / закрытие страницы / timeout → revoke;
- без lease команды не публикуются;
- команда обязана содержать lease_id;
- lease связан с доступным терминалом и активным video-control UI, но video stream не является доказательством права управления;
- `iot-rpc-rest-app` проверяет `X-Org-Id` и binding устройства к организации;
- MenuBuilder также проверяет права пользователя до вызова Internal API (defence in depth).

### 5.2. Active interactive desktop

`l4desk.exe` не должен быть обычной Session 0-only Windows Service.

`SendInput` действует в interactive user session. Агент обязан:
- запускаться в активной пользовательской desktop session;
- определять, доступен ли `WinSta0\Default`;
- корректно сообщать status `desktop_available`;
- возвращать NACK, если экран заблокирован, доступен Secure Desktop/UAC или input injection невозможна.

Архитектор обязан исследовать реально применяемый способ запуска терминальных tools (`l4superv`, Task Scheduler, login scripts и т.д.) и в промпте №2 предложить минимально надёжный путь запуска `l4desk.exe` именно в interactive session.

---

## 6. Обязательное исследование проектов

Перед подготовкой промптов изучить только релевантные файлы и архитектуру.

### 6.1. `D:\work\iot.leo4.ru\iot-rpc-rest-app`

Цель: понять **фактическую** реализацию, а не ограничиваться приложенными документами.

Минимальный круг исследования:

```text
docs/mqtt_topic_rules.md
docs/mqtt-rpc-protocol.md
docs/mqtt-rpc-correlation-matrix.md
docs/event-protocol-mqtt.md                 # если существует
docs/internal-api-contract-v1.md
docs/rfc-integration-bus.md

app-service/core/topologys/declare.py
app-service/core/topologys/fs_queues.py
app-service/core/topologys/internal_bus.py
app-service/core/integrations/rmq_admin_api.py
app-service/core/services/device_events_collect.py
app-service/core/services/device_tasks.py
app-service/core/config.py
app-service/create_api_app.py
```

Также найти и изучить:
- регистрацию FastAPI routers;
- текущую authentication dependency для `/api/internal/v1/*`;
- применяемые FastStream/RabbitMQ patterns;
- реальные definitions `req/ack/evt/res/out` queue bindings;
- способы получения device/org binding;
- подход к in-memory/Redis state (если уже есть);
- текущий compose/deploy/service structure;
- локальный bridge/ACL provisioning, если находится в проекте.

Результат исследования должен ответить:
1. Где корректно объявлять новые `ctl` queues/bindings?
2. Как consumer получает topic/routing key, MQTT v5 metadata и payload?
3. Каким способом надёжно publish в device MQTT plane `amq.topic`?
4. Как избежать пересечения с RPC `ack`/`evt` consumers?
5. Где реализовать lease state и ожидание ACK?
6. Есть ли подходящий persistence/Redis или для alpha допустимо memory-only?
7. Как Internal API аутентифицируется и как MenuBuilder уже подключается к `app1`?
8. Как deploy/build/test выполняются без перезапуска несвязанных сервисов?

### 6.2. `etranprocessing/MenuBuilder`

Минимально изучить:

```text
MenuBuilder/backend/:
- main/create app и подключение routers;
- auth dependencies, JWT/org_id/roles;
- существующие calls в app1 Internal API;
- devices/terminals router/services;
- config/.env/compose/deploy.

MenuBuilder/frontend/:
- route /video и Video UI;
- существующий device/terminal list API;
- auth/session context;
- frontend API client conventions;
- layout/navigation;
- current Janus player and stream session code.

tools/:
- l4superv / l4install / leo4proxy conventions;
- build scripts, README, packaging conventions;
- способ поставки Mosquitto на terminal, если он документирован.
```

Не изучать и не менять другие проекты без необходимости.

---

## 7. Требования к промпту №1: `iot-rpc-rest-app`

Промпт должен быть автономным и не ссылаться на «контекст чата». Он обязан содержать:

### 7.1. Scope и ограничения

- Работать только в `iot-rpc-rest-app`.
- Не менять MenuBuilder и tools.
- Не ломать MQTT RPC, device tasks, `evt/eva`, integrations и existing ACL.
- Явно запретить отправку remote-input через `evt`, RPC или integration bus.
- Учитывать, что terminal MQTT username/client id/SN связан с CN сертификата.
- Не хардкодить device 773/SN, использовать только в optional acceptance test.

### 7.2. Полный backend/MQTT scope

Обязательно включить:
- документированное добавление `ctl` в topic rules;
- topology declaration:
  ```text
  remote_input_commands: srv.*.ctl
  remote_input_events:   dev.*.ctl
  ```
  или реальные эквивалентные имена согласно code style;
- schemas Pydantic/dataclasses для command/ack/nack/presence;
- strict topic/payload/SN validation;
- manager/service:
    - device presence state;
    - active lease lifecycle;
    - ACK correlation via `command_id`;
    - bounded pending-command registry;
    - TTL;
    - timeout result;
    - rate limit;
    - audit/structured logging;
- Internal API paths с требованиями `X-Internal-Service-Key`, `X-Org-Id`, role semantics;
- device-to-org validation;
- конкретные JSON request/response contracts;
- обработку ACK/NACK не как DeviceEvent и без БД потока pointer movements;
- политика хранения:
    - pointer movements — никогда не сохранять;
    - presence — state/memory, optional transition logs;
    - click/ACK/NACK — structured logs или отдельный limited audit механизм, без изменения общего DeviceEvent flow;
- описание обновления MQTT ACL/provisioning:
    - оценить, распространяются ли текущие regex `^dev.<SN>.*` / `^srv.<SN>.*` на `ctl`;
    - если да — не усложнять provisioning;
    - если нет — точное минимальное изменение.

### 7.3. Deployment

Промпт №1 обязан включать полный безопасный deploy:
- target server / реальная схема compose — определить по проекту;
- build, migrations если нужны;
- только нужный сервис `iot-rpc-rest-app` и его непосредственные контейнеры;
- никакого restart RabbitMQ, PostgreSQL, MenuBuilder, l4media или processing-сервисов;
- проверки topology/queues/bindings/consumer readiness;
- internal API smoke calls;
- безопасный способ проверить publish/ACK не выполняя реальный click, например presence/status validation;
- логи и rollback instructions.

### 7.4. Acceptance criteria

Как минимум:
1. Existing RPC and `evt` flows unchanged.
2. `srv/<SN>/ctl` разрешён доставке нужному terminal client.
3. `dev/<SN>/ctl` принимается отдельным consumer’ом, не попадает в event persistence.
4. Valid retained presence отображается по Internal API.
5. `POST event` с invalid lease получает отказ.
6. Click command получает `unconfirmed` при отсутствии agent ACK, без automatic retry.
7. ACK c valid `command_id` правильно завершает ожидание.
8. Cross-tenant request получает `403`.
9. Ограничения QoS/retain documented and enforced in publish API.

---

## 8. Требования к промпту №2: `etranprocessing/MenuBuilder` + `tools/l4desk`

Промпт должен предполагать, что prompt №1 уже внедрён и предоставил готовый Internal API.

### 8.1. Scope и ограничения

Разрешённый scope:

```text
MenuBuilder/backend/
MenuBuilder/frontend/
tools/l4desk/             # новый isolated C/Windows subproject
```

При необходимости разрешить минимальные изменения в:
- MenuBuilder compose/deploy конфигурации;
- терминальном packaging/supervisor config, только если это необходимо для запуска `l4desk.exe`.

Запреты:
- не менять `iot-rpc-rest-app` (считать Internal API готовым контрактом);
- не менять l4media/Janus/video ingress;
- не менять leo4proxy;
- не вводить RabbitMQ credentials в MenuBuilder;
- не подключать браузер к MQTT;
- не хардкодить device 773/SN.

### 8.2. MenuBuilder backend

Реализовать BFF endpoints, которые:
- проверяют JWT, `org_id`, роли и device ownership;
- вызывают Internal API `iot-rpc-rest-app` строго с:
  ```text
  X-Internal-Service-Key
  X-Org-Id
  X-Role
  ```
- не формируют MQTT topics напрямую;
- не выдают Internal Service Key фронтенду.

Ожидаемые публичные/BFF возможности:

```text
POST   /api/v1/video/devices/{device_id}/control/lease
POST   /api/v1/video/devices/{device_id}/control/keepalive
DELETE /api/v1/video/devices/{device_id}/control/lease
GET    /api/v1/video/devices/{device_id}/control/status
POST   /api/v1/video/devices/{device_id}/control/events
```

Endpoint events принимает только normalized coordinates и allowed type/button; API `mouse_click` возвращает ACK/NACK/unconfirmed. Pointer movement должен быть asynchronous/best-effort, без долгого ожидания ACK.

### 8.3. MenuBuilder frontend

Расширить существующий `/video`:
- слева список устройств остаётся динамическим, без SN/device hardcode;
- справа — текущий Janus video player;
- явная кнопка `Включить управление`;
- показывать agent presence/desktop availability/control lease status;
- безопасная обработка pointer:
    - только в actual video content rect;
    - exclude black bars;
    - normalize `0..65535`;
    - throttle movement;
    - final move + click;
- при Stop/change device/unmount/logout: release lease, stop timers, abort pending requests;
- UX:
    - «Управление недоступно: агент offline»;
    - «Экран терминала заблокирован»;
    - «Клик выполнен»;
    - «Клик не подтверждён — повторите вручную»;
    - «Управление занято другим оператором».

### 8.4. Новый `tools/l4desk`

Создать отдельный, минимальный, изолированный C/Win32 project:

```text
tools/l4desk/
```

Результат:

```text
l4desk.exe
```

Функции:
- plain MQTT connection только к localhost Mosquitto;
- локальный client_id = `svc_desk` # сверить с референсом mqtt-клиентов, при необходимости расширить референс
- subscribe `srv/<own-sn>/ctl`;
- publish `dev/<own-sn>/ctl`;
- SN — получить безопасно и детерминированно:
    - приоритетно из того же certificate source / cert store, что используется в leo4proxy;
    - архитектор должен выбрать способ после анализа существующих tool conventions;
- MQTT LWT retained offline presence;
- retained online presence после подключения;
- периодическое обновление `desktop_available` / geometry с разумной частотой (не high-frequency);
- ограниченный parser JSON:
    - schema v1;
    - pointer_move;
    - left click;
    - reject unknown fields/types safely;
- validate:
    - SN;
    - lease_id format;
    - `command_id`;
    - coordinate range;
    - TTL;
    - duplicate command;
- `SendInput` / absolute virtual desktop;
- ACK/NACK;
- bounded dedup cache;
- robust reconnect к local Mosquitto;
- stdout/file logs;
- working in interactive user session, NOT Session 0.

Требования сборки:
- Windows x86 и x64;
- Windows 7 SP1 compatibility, если это требование текущей terminal toolchain;
- existing MSVC build conventions;
- dependency strategy for `libmosquitto` or `paho`:
    - сначала изучить, есть ли уже легально поставляемая `paho` для l4con, если нет, то смотреть `mosquitto.dll/libmosquitto`;
    - не дублировать и не скачивать неконтролируемые DLL;
    - если нужен runtime DLL — включить точную packaging/inventory процедуру;
- README, `build.cmd`, example config/start command;
- без OpenSSL/TLS внутри l4desk: локальный MQTT plain, защищённый upstream обеспечивают Mosquitto bridge + leo4proxy mTLS.

### 8.5. Терминальный запуск

Промпт должен содержать проверенный способ:
- установить/запустить l4desk в active interactive session;
- не запускать его только как обычную Session 0 service;
- предусмотреть login/autostart/Task Scheduler или существующий supervisor pattern;
- не ломать Mosquitto/leo4proxy/l4superv;
- предложить варианты инсталляции и оркестрации в том числе с помощью l4superv.

### 8.6. Deployment

Включить:
- развёртывание MenuBuilder на сервере без перезапуска l4media/RabbitMQ/processing;
- конфиг Internal API URL и service key только backend-side;
- build frontend/backend;
- deployment l4desk на тестовую Windows-машину;
- нужные bridge topic rules/configuration changes, если они требуются;
- smoke test сначала presence, потом real left-click;
- logs to inspect across MenuBuilder, app1, Mosquitto, l4desk.

---

## 9. Обязательная форма ответа архитектора

Перед двумя промптами предоставить краткий архитектурный отчёт:

1. **Подтверждённая конечная схема** всех компонентов.
2. **Матрица ответственности**:
    - l4media/Janus;
    - MenuBuilder frontend;
    - MenuBuilder backend;
    - iot-rpc-rest-app;
    - RabbitMQ;
    - Mosquitto bridge;
    - l4desk.exe.
3. **Список проверенных файлов** и фактов, подтверждённых реальным кодом.
4. **Открытые допущения** — только если их нельзя разрешить исследованием.
5. **Порядок реализации и деплоя**.
6. Далее два отдельных промпта:
    - `PROMPT 1 — iot-rpc-rest-app`;
    - `PROMPT 2 — MenuBuilder + tools/l4desk`.

Каждый финальный промпт должен:
- быть самодостаточным;
- не требовать чтения чата;
- содержать конкретный scope;
- содержать точные файлы/модули после исследования;
- описывать API и message contracts;
- содержать deploy plan;
- содержать приёмочные критерии;
- содержать запреты против архитектурных ошибок;
- не содержать placeholder-решений вида «сделать по аналогии» без указания, с чем именно;
- использовать устройство `773` только как alpha acceptance test, не в коде.

## 10. Критерий качества работы

Работа архитектора успешна, только если после выполнения обоих будущих промптов получится:

```text
1. Оператор с доступом к устройству открывает MenuBuilder /video.
2. Видит WebRTC-трансляцию устройства.
3. Явно включает remote control.
4. Кликает в фактической области картинки.
5. MenuBuilder валидирует доступ и вызывает app1 Internal API.
6. app1 публикует QoS 1 non-retained command в srv/<SN>/ctl.
7. l4desk получает его через существующий local Mosquitto bridge.
8. l4desk выполняет SendInput в interactive Windows desktop.
9. l4desk публикует QoS 1 non-retained ACK в dev/<SN>/ctl.
10. app1 сопоставляет ACK с command_id.
11. UI показывает подтверждённый результат.
12. Никакие pointer_move/ACK не создают DeviceEvent в БД и не нагружают evt/eva pipeline.
13. Другой tenant не может открыть control lease или послать command чужому устройству.
```