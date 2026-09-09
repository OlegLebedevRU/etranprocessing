# PROMPT 1 — `iot-rpc-rest-app`: Remote Input control plane (`ctl` topic pool, lease, ACK/NACK, Internal API)

> Автономный промпт для агента-исполнителя. Не требует чтения чата. Все факты ниже подтверждены исследованием кода проекта на момент подготовки (2026-09-09).
> Проект: `D:\work\iot.leo4.ru\iot-rpc-rest-app` (контейнер `app1`, Python/FastAPI/FastStream/RabbitMQ 4 Native MQTT).

---

## 0. Роль и результат

Ты — senior backend-разработчик `iot-rpc-rest-app`. Нужно реализовать, протестировать, задокументировать и развернуть **инфраструктуру удалённого управления мышью терминала (remote input)**:

- новый MQTT control topic pool `srv/<SN>/ctl` / `dev/<SN>/ctl`;
- RabbitMQ topology (очередь + binding) и отдельный FastStream consumer;
- in-memory state: presence агента `l4desk`, remote-input lease, pending-команды с корреляцией ACK/NACK по `command_id`;
- Internal API для MenuBuilder (REST lifecycle + WebSocket командный канал);
- rate limiting, TTL, structured audit logging;
- документацию;
- деплой только `app1` без перезапуска RabbitMQ/PostgreSQL/MenuBuilder/l4media/processing.

Потребитель API — сервис `menubuilder-backend` (этот же docker-стек, обращается по `http://app1:8000`). Терминальный агент `l4desk.exe` реализуется другим промптом; здесь считай его контрактом (раздел 3).

---

## 1. Scope и жёсткие ограничения

**Разрешено менять только** `iot-rpc-rest-app` (`app-service/**`, `docs/**`, тесты, при необходимости `compose.yaml`/`.env.example`).

**Запрещено:**
- менять MenuBuilder, `tools/`, l4media, leo4proxy, nginx-конфиги (nginx принадлежит проекту `etranprocessing`; если правка nginx понадобится — **остановись и эскалируй владельцу с точным diff**, не меняй сам);
- ломать/менять поведение MQTT RPC (`tsk/req/rsp/res/cmt/rac`), `evt/eva`, `ack`, `out`, `app/svc` presence, integrations (`integration.commands/topic`), webhooks, billing, provisioning, существующие ACL;
- отправлять remote-input через `evt`, RPC lifecycle или integration bus;
- создавать `DeviceEvent`/`DeviceTask` из pointer/click/ACK/NACK/presence;
- сохранять pointer movements в БД;
- ставить `retain` серверным сообщениям (AMQP → MQTT в RabbitMQ не поддерживает retain — не пытаться эмулировать);
- автоматически повторять `mouse_click` при отсутствии ACK;
- хардкодить device `773` / SN `a4b0000773c82116d210826` в коде, конфиге, SQL, тестах-фикстурах по умолчанию (допустимо только как параметр ручного acceptance-теста);
- перезапускать `rabbitmq`, `pg`, `menubuilder-backend`, `nginx-default`, `l4media-*`, `processing-backend`.

Терминальный MQTT username / client id / SN = `CN` клиентского сертификата (см. `docs/mqtt_topic_rules.md`).

---

## 2. Факты о проекте (подтверждены кодом) — используй именно эти точки расширения

| Что | Где | Факт |
|---|---|---|
| Настройки RMQ | `app-service/core/config.py` → `class RabbitQXConfig` (строки ~249–310) | queue names `req/ack/res/evt/out/app/svc`, routing keys через `RoutingKey("dev","*","<sfx>")`, `def_queue_args={"x-message-ttl":600000}`, `prefix_srv="srv"`, суффиксы server→device (`suffix_task`, `suffix_response`, `suffix_event_ack`, `suffix_commited`) |
| Декларация топологии | `app-service/core/topologys/declare.py` | `RabbitQueue(...)` + список `BINDINGS: list[(queue, routing_key, exchange)]`, exchange `amq.topic` (`topic_exchange`, `declare=False`), `topic_publisher = fs_router.publisher(exchange=topic_exchange)`; `q_out` объявлена `durable=False` — образец для volatile-очереди |
| Consumers | `app-service/core/topologys/fs_queues.py` | `@fs_router.subscriber(q_xxx)` под guard `_ensure_single_registration()`; deps `Session_dep`, `Sn_dep` (SN из routing key), `Corr_id_dep` (из header `correlationData`) — `core/topologys/fs_depends.py`. Образец volatile-потока без БД: `diagnostics_output(msg, sn)` → `core.diagnostics.mqtt_bridge.handle_device_output_message(routing_key, payload)` |
| Publish в устройство | `app-service/core/services/device_task_processing.py` (`send_tsk/send_rsp/send_eva/send_cmt`) | `await topic_publisher.publish(routing_key="srv.<SN>.<sfx>", message=<dict|BaseModel>, correlation_id=..., expiration=<ms>, headers={"correlationData": str(id), ...})`; `expiration` транслируется RabbitMQ в MQTT Message Expiry |
| In-memory registry образец | `app-service/core/diagnostics/sessions.py`, `service.py`, `schemas.py` | dataclass + `asyncio.Lock`, TTL, `cleanup_expired`, module-level singleton `registry`; Pydantic-схемы с discriminated union (`BrowserMessageAdapter`) |
| Internal API deps | `app-service/api/internal_v1/internal_depends.py` | `Internal_Auth_dep` (`X-Internal-Service-Key` == `settings.auth.internal_service_key`), `Internal_Org_dep` (`X-Org-Id`; superuser по `X-Role`/`X-Role-Id`/`X-User-Id` через `is_request_superuser(request_or_websocket)`), `Session_dep` |
| Регистрация роутеров | `app-service/api/internal_v1/__init__.py` | `router.include_router(<x>_router, include_in_schema=False)`, префикс `settings.api.internal_v1.prefix` = `/internal/v1` под `settings.api.prefix` = `/api`; под-префиксы объявляются в `class ApiInternalV1Prefix` (`config.py` ~78–90, напр. `diagnostics = "/diagnostics"`) |
| WS-образец | `app-service/api/internal_v1/diagnostics.py` → `@router.websocket("/ws/devices/{sn}")` | org из заголовков, проверка устройства `DeviceRepo.get_device_id(session, sn, org_id)`, форвардинг очереди сессии в `websocket.send_json`, cleanup в `finally` |
| Device↔org | `app-service/core/crud/device_repo.py` | `DeviceRepo.get_device_id(session, sn, org_id)` → `int | None`; `DeviceRepo.get_org_id_by_device_id(session, device_id)` |
| Терминальный ACL | `app-service/core/integrations/rmq_admin_api.py` `_topic_permission_payload` | `write ^dev.{client_id}.*`, `read ^srv.{client_id}.*` на `amq.topic` → **`ctl` уже разрешён, provisioning менять не нужно** |
| Запуск | `docker-files/app-service/Dockerfile` → `prestart.sh` (alembic upgrade head) → `appup` → `run_main.py` → gunicorn `UvicornWorker`, `workers = WEB_CONCURRENCY or cpu*2+1` (`core/config.py` ~33–42) | **In-memory state корректен только при `WEB_CONCURRENCY=1`** (см. §6) |
| Тесты/линт | `pyproject.toml` (`testpaths=["app-service/tests"]`), `AGENTS.md` | `uv run pytest`, `uv run ruff check .`, `uv run black --check .`; образцы: `tests/core/diagnostics/test_*.py`, `tests/api/v1/test_diagnostics_ws_auth.py`, `tests/core/topologys/test_lwt_subscribers.py` |
| Docs | `docs/mqtt_topic_rules.md`, `docs/internal-api-contract-v1.md`, `docs/remote-diagnostics-protocol.md`, `docs/manual-app1-deploy-runbook.md` | обновить/дополнить (раздел 8) |
| Deploy | `docs/manual-app1-deploy-runbook.md` | сервер `87.242.100.34`, `ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34`, compose `/home/user1/compose.yaml`, исходники `/home/user1/iot-rpc-rest-app`, `sudo docker compose build app1 && sudo docker compose up -d --no-deps app1` |

Redis/иного распределённого state в проекте **нет**. Alpha — memory-only (решение владельца), см. §6.

---

## 3. MQTT control protocol (обязательный контракт с `l4desk.exe`)

### 3.1. Топики

```text
srv/<SN>/ctl   server → terminal : pointer_move, mouse_click            (routing key srv.<SN>.ctl)
dev/<SN>/ctl   terminal → server : ack, nack, presence (retained)       (routing key dev.<SN>.ctl)
```

Правила `dev/<SN>/<sfx>` / `srv/<SN>/<sfx>` и трёхсимвольный суффикс сохраняются. Локальный Mosquitto bridge на терминале уже прокидывает `srv/<SN>/# in 1` и `dev/<SN>/# out 1` — со стороны терминала ничего менять не нужно.

### 3.2. QoS / retain (enforce в publish-коде и задокументировать)

| Тип | Topic | QoS (подписка/публикация агента) | Retain | Server TTL (`expiration`) |
|---|---|---:|---|---|
| `pointer_move` | `srv/<SN>/ctl` | 0 (best-effort, latest-wins) | нет | `remote_input.move_ttl_ms` = 2000 |
| `mouse_click` | `srv/<SN>/ctl` | 1 | нет | `remote_input.click_ttl_ms` = 5000 |
| `ack` / `nack` | `dev/<SN>/ctl` | 1 | нет | — |
| `presence` | `dev/<SN>/ctl` | 1 | **да** (retained + LWT offline) | — |

Сервер публикует через AMQP (`amq.topic`) — QoS доставки определяется подпиской агента (QoS 1), retain невозможен. `expiration` обязателен для каждой команды.

### 3.3. Envelope v1 (JSON, UTF-8, ≤ 1024 байт для команд, ≤ 2048 для presence)

Server → terminal:

```json
{"v":1,"type":"pointer_move","command_id":"<uuid4>","lease_id":"<uuid4>","sn":"<SN>","x":32768,"y":16384,"issued_at_ms":1788805978123,"expires_at_ms":1788805980123}
{"v":1,"type":"mouse_click","command_id":"<uuid4>","lease_id":"<uuid4>","sn":"<SN>","x":32768,"y":16384,"button":"left","issued_at_ms":1788805978123,"expires_at_ms":1788805983123}
```

Terminal → server:

```json
{"v":1,"type":"ack","command_id":"<uuid4>","lease_id":"<uuid4>","sn":"<SN>","result":"injected","terminal_time_ms":1788805978301}
{"v":1,"type":"nack","command_id":"<uuid4>","lease_id":"<uuid4>","sn":"<SN>","code":"interactive_desktop_unavailable","message":"Interactive desktop is locked or inaccessible","terminal_time_ms":1788805978301}
{"v":1,"type":"presence","agent":"l4desk","status":"online","desktop_available":true,"screen":{"virtual_x":0,"virtual_y":0,"virtual_width":4920,"virtual_height":2000},"timestamp":"2026-09-08T12:00:00Z"}
```

Presence `status:"offline"` (LWT) содержит `agent`, `status`, `timestamp`; `desktop_available=false`, `screen` может отсутствовать. Агент переиздаёт presence периодически (каждые 30 с) — сервер обязан считать presence устаревшим, если `received_at` старше `remote_input.presence_stale_sec` = 90.

`x`, `y` — нормализованные координаты `0..65535` виртуального рабочего стола. `button` в alpha — только `"left"`. NACK codes (закрытый список для валидации): `interactive_desktop_unavailable`, `lease_invalid`, `expired`, `duplicate`, `invalid_sn`, `invalid_payload`, `unsupported`, `inject_failed`.

### 3.4. Идемпотентность и доставка

- QoS 1 = at-least-once. Сервер **никогда** не ретраит `mouse_click`; при отсутствии ACK за `click_ack_timeout_ms` (5000) возвращает `unconfirmed`.
- Повторный ACK/NACK для уже завершённого `command_id` — игнорировать (debug-log).
- Сообщение из `dev/<SN>/ctl`, у которого `payload.sn != SN` из routing key, — отбрасывать с warning.
- Rate limit на активный lease: `pointer_move` ≤ 10/с, `mouse_click` ≤ 5/с (token bucket). Превышение → команда не публикуется, WS-ответ `error{code:"rate_limited"}` / REST `429`.

---

## 4. Реализация: конфигурация, топология, consumer, модуль `core/remote_input`

### 4.1. `core/config.py`

В `RabbitQXConfig` добавить (по аналогии с `out`):

```python
suffix_control: str = "ctl"                      # core -> dev: srv.<SN>.ctl
ctl_queue_name: str = "ctl"                      # dev -> core queue
ctl_queue_args: dict = {"x-message-ttl": 15000}  # volatile control traffic
routing_key_dev_control: str = str(RoutingKey("dev", "*", "ctl"))
```

Новый блок настроек `class RemoteInputConfig(BaseModel)` и поле `remote_input: RemoteInputConfig = RemoteInputConfig()` в `Settings` (env-префикс по существующей схеме pydantic-settings проекта — посмотри, как объявлены `webhook`/`billing`/`ttl_job`):

```python
lease_ttl_sec: int = 60
lease_keepalive_sec: int = 15
click_ack_timeout_ms: int = 5000
click_ttl_ms: int = 5000
move_ttl_ms: int = 2000
move_rate_per_sec: int = 10
click_rate_per_sec: int = 5
max_command_payload_bytes: int = 1024
max_inbound_payload_bytes: int = 2048
pending_max_per_lease: int = 32
pending_max_total: int = 1024
presence_stale_sec: int = 90
```

В `ApiInternalV1Prefix` добавить `remote_input: str = "/remote-input"`.

### 4.2. `core/topologys/declare.py`

```python
q_ctl = RabbitQueue(name=topology.ctl_queue_name, durable=False, arguments=topology.ctl_queue_args)
BINDINGS += [(q_ctl, topology.routing_key_dev_control, topic_exchange)]
```

Никаких изменений существующих очередей/биндингов. Очередь `evt` **не** должна получать `dev.*.ctl` (binding `dev.*.evt` уже точный — проверить тестом).

### 4.3. `core/topologys/fs_queues.py`

```python
@fs_router.subscriber(q_ctl)
async def remote_input_ctl(msg: RabbitMessage, sn: Sn_dep):
    routing_key = getattr(msg, "routing_key", None) or f"dev.{sn}.ctl"
    await handle_device_ctl_message(routing_key=routing_key, payload=msg.body)
```

Без `Session_dep`, без billing, без `DeviceEventsCollect`. Импорт `q_ctl` рядом с `q_out`.

### 4.4. Новый пакет `app-service/core/remote_input/`

```text
core/remote_input/
  __init__.py
  schemas.py      # Pydantic v2: PointerMoveCommand, MouseClickCommand, CtlAck, CtlNack, CtlPresence,
                  #   CtlInboundAdapter (discriminated union по "type"), WS/REST DTO (раздел 5)
  presence.py     # PresenceRegistry: sn -> (CtlPresence, received_at); get(sn) -> PresenceView
                  #   (online = status=="online" and age < presence_stale_sec); подписка на изменения (asyncio.Queue per listener)
  leases.py       # Lease dataclass (lease_id, org_id, device_id, sn, owner_user_id, owner_role, created_at,
                  #   expires_at, last_keepalive_at, revoked_reason); LeaseRegistry: acquire (единственный active на SN,
                  #   иначе LeaseConflictError с owner), touch, revoke, get_active(sn), get(lease_id), cleanup_expired
  pending.py      # PendingCommandRegistry: command_id -> PendingCommand(lease_id, sn, type, issued_at, future);
                  #   bounded (pending_max_*), resolve(command_id, result), expire()
  rate_limit.py   # TokenBucket per (lease_id, type)
  publisher.py    # send_ctl_command(sn, command) -> topic_publisher.publish(routing_key=f"{prefix_srv}.{sn}.{suffix_control}",
                  #   message=command.model_dump(mode="json"), correlation_id=command.command_id,
                  #   expiration=<ttl_ms>, headers={"correlationData": str(command_id), "ctl_type": command.type})
  mqtt_bridge.py  # extract_sn_from_ctl_routing_key, decode_ctl_payload, handle_device_ctl_message
  service.py      # RemoteInputService: acquire_lease/keepalive/release/status, pointer_move (fire-and-forget),
                  #   mouse_click (await ACK|NACK|timeout), проверки lease/org/rate/size, audit-log
```

Требования:
- `handle_device_ctl_message`: SN из routing key; размер ≤ `max_inbound_payload_bytes`; `CtlInboundAdapter.validate_json`; `payload.sn` (для ack/nack) обязан совпадать с SN топика; `presence` → `PresenceRegistry.update(sn, presence)` + INFO-лог только при смене `status`/`desktop_available`; `ack/nack` → `PendingCommandRegistry.resolve`. Любая ошибка — warning + drop, без исключений наружу.
- Все registries — module-level singletons, `asyncio.Lock`, без БД. Фоновая задача cleanup (lease expiry → `lease_revoked{reason:"expired"}` подписчикам; pending expiry → `unconfirmed`), запускается в lifespan приложения там же, где стартуют существующие фоновые задачи (посмотри `create_api_app.py` / `main.py`).
- `RemoteInputService.mouse_click`: проверить lease активен и принадлежит `org_id`; rate limit; сформировать команду (`issued_at_ms=now`, `expires_at_ms=now+click_ttl_ms`); зарегистрировать pending **до** publish; publish; `await asyncio.wait_for(future, click_ack_timeout_ms/1000)`; результат `injected | nack(code,message) | unconfirmed`; latency_ms; **без retry**.
- `pointer_move`: проверки те же, publish без ожидания, без pending. Не логировать каждое движение (только счётчик в DEBUG/метрике).
- Lease привязан к `(org_id, device_id, sn)`; `device_id` получать через `DeviceRepo.get_device_id(session, sn, org_id)` — если `None` → 403/404 (cross-tenant не должен раскрывать существование устройства: возвращай `403 {"detail":"device not available for organization"}` единообразно).

### 4.5. Роли вызывающего

Заголовки от MenuBuilder: `X-Internal-Service-Key`, `X-Org-Id`, `X-Role` (`superuser|admin|user|viewer` или `1..4`), `X-Role-Id`, `X-User-Id`. Управление разрешено ролям `superuser/admin/user` (1–3); `viewer` (4) → `403 {"detail":"role not allowed for remote input"}`. Owner lease = `X-User-Id` (строка), сохраняется для UI («занято другим оператором»). Superuser с `org_id` query — по правилам `get_internal_org_id`.

---

## 5. Internal API contract (новый роутер `api/internal_v1/remote_input.py`, prefix `/api/internal/v1/remote-input`)

Все эндпоинты: `Internal_Auth_dep` + `Internal_Org_dep` (для WS — эквивалентная проверка вручную через `websocket.headers`, **обязательно включая service key**, в отличие от `diagnostics_ws`). Регистрация в `api/internal_v1/__init__.py` с `include_in_schema=False`.

### 5.1. REST

| Метод и путь | Назначение | Ответы |
|---|---|---|
| `GET /devices/{sn}/status` | presence + lease | `200 StatusResponse` |
| `POST /devices/{sn}/lease` | выдать lease (единственный на SN) | `201 LeaseResponse`; `409 {"detail":"lease busy","owner_user_id":"…","expires_at":"…"}`; `403` (org/role) |
| `POST /lease/{lease_id}/keepalive` | продлить `expires_at = now + lease_ttl_sec` | `200 LeaseResponse`; `404 lease not found/expired`; `403` чужой org |
| `DELETE /lease/{lease_id}` | revoke | `204`; `404` |
| `POST /lease/{lease_id}/pointer-move` | best-effort move | `202 {"accepted":true}`; `429`; `409 lease inactive`; `422` |
| `POST /lease/{lease_id}/mouse-click` | click, ждёт ACK | `200 ClickResult`; `429`; `409`; `422` |

DTO:

```json
StatusResponse  {"sn":"…","agent":{"online":true,"desktop_available":true,"screen":{"virtual_x":0,"virtual_y":0,"virtual_width":1920,"virtual_height":1080},"last_seen_at":"2026-09-09T00:00:00Z","stale":false},
                 "lease":{"active":true,"lease_id":"…","owner_user_id":"12","expires_at":"…"}}
LeaseRequest    {"owner_user_id":"12","owner_role":"user"}           // opционально; иначе из заголовков X-User-Id/X-Role
LeaseResponse   {"lease_id":"…","sn":"…","device_id":773,"org_id":5,"owner_user_id":"12","created_at":"…","expires_at":"…","keepalive_sec":15,"ws_path":"/api/internal/v1/remote-input/ws/lease/{lease_id}"}
MoveRequest     {"x":32768,"y":16384}                                 // int 0..65535
ClickRequest    {"x":32768,"y":16384,"button":"left","client_ref":"optional-string<=64"}
ClickResult     {"command_id":"…","client_ref":"…","result":"injected|nack|unconfirmed","code":null,"message":null,"latency_ms":184}
```

### 5.2. WebSocket командный канал (основной путь для операторской сессии MenuBuilder)

`WS /api/internal/v1/remote-input/ws/lease/{lease_id}` — lease создаётся REST'ом заранее (BFF), WS **присоединяется** к нему. Проверки при connect: service key, `X-Org-Id` == `lease.org_id`, роль 1–3, `X-User-Id` == `lease.owner_user_id` (superuser может присоединиться к любому lease своей/указанной org). Нарушение → `close(4403)`; lease не найден/истёк → `close(4404)`; уже есть активное WS-подключение к этому lease → `close(4409)`.

Inbound (BFF → app1), JSON:

```json
{"type":"pointer_move","x":32768,"y":16384}
{"type":"mouse_click","x":32768,"y":16384,"button":"left","client_ref":"c-17"}
{"type":"keepalive"}
{"type":"release"}
```

Outbound (app1 → BFF):

```json
{"type":"hello","lease_id":"…","sn":"…","expires_at":"…","keepalive_sec":15,"limits":{"move_per_sec":10,"click_per_sec":5}}
{"type":"presence","online":true,"desktop_available":true,"screen":{…},"last_seen_at":"…","stale":false}
{"type":"click_result","command_id":"…","client_ref":"c-17","result":"injected|nack|unconfirmed","code":null,"message":null,"latency_ms":184}
{"type":"error","code":"rate_limited|invalid_message|lease_inactive|payload_too_large","message":"…","client_ref":"c-17"}
{"type":"lease_revoked","reason":"expired|released|replaced|server_shutdown"}
```

Поведение: сразу после accept отправить `hello` и текущий `presence`; далее push `presence` при изменениях; любое inbound-сообщение = implicit keepalive; `mouse_click` обрабатывается конкурентно (не блокирует движения), результат приходит `click_result` с `client_ref`; `pointer_move` — latest-wins: если предыдущий move ещё не опубликован, заменить его. При закрытии WS любым способом → `release` lease (`reason:"released"`) и отмена pending futures (`unconfirmed`). При `lease_revoked` — сервер закрывает WS `1000`.

---

## 6. Multi-worker / WEB_CONCURRENCY — обязательное условие корректности (общий контекст для всех агентов)

Lease/pending/presence — **memory-only внутри одного процесса**. FastStream-consumer `ctl` и WS/REST-обработчики должны работать в одном процессе: при `WEB_CONCURRENCY>1` ACK может прийти в другой воркер и click получит ложный `unconfirmed`, а presence/lease будут разъезжаться (эта же зависимость уже существует у `core/diagnostics`).

Обязательно:
1. Проверить на сервере фактическое значение `WEB_CONCURRENCY` в `/home/user1/iot-rpc-rest-app/app-service/.env` (`grep -n WEB_CONCURRENCY`); если не задано или `>1` — **эскалируй владельцу** перед деплоем (не меняй .env молча); целевое значение для alpha `WEB_CONCURRENCY=1`.
2. Добавить в `.env.example`, `docs/manual-app1-deploy-runbook.md` и новый `docs/remote-input-protocol.md` явный блок: «memory-only state (diagnostics, remote_input) требует одного gunicorn-воркера; при масштабировании требуется вынос lease/pending/presence в Redis (roadmap)».
3. При старте приложения писать WARNING, если `settings.gunicorn.workers > 1` и remote_input включён.
4. Спроектировать `LeaseRegistry/PendingCommandRegistry/PresenceRegistry` за узким интерфейсом (Protocol/ABC), чтобы будущая Redis-реализация не трогала сервис и API.

---

## 7. Хранение, логирование, безопасность, тесты

### 7.1. Политика хранения
- pointer movements — не сохранять никогда;
- presence — только память; переходы online/offline/desktop_available — INFO-лог;
- click/ACK/NACK/unconfirmed, lease acquire/keepalive/release/conflict — structured log (`setup_module_logger(__name__, "remote_input.log")`) с полями `command_id`, `lease_id`, `org_id`, `device_id`, `sn`, `type`, `result`, `code`, `latency_ms`, `owner_user_id`. Без `DeviceEvent`, без БД-таблиц (alembic-миграций в этой задаче нет).

### 7.2. Валидация и лимиты
- строгие Pydantic-схемы (`extra="forbid"` для входящих от терминала и от BFF), `x,y` `0..65535`, `button == "left"`, `v == 1`, UUID-формат `command_id/lease_id`;
- размер входящего payload из MQTT ≤ 2048 байт; исходящей команды ≤ 1024 байт (assert перед publish);
- никогда не публиковать без активного lease; `lease_id` обязателен в каждой команде.

### 7.3. Тесты (pytest, каталог `app-service/tests/`, стиль существующих)
- `tests/core/remote_input/test_schemas.py` — валидные/невалидные envelope, лишние поля, границы координат;
- `tests/core/remote_input/test_mqtt_bridge.py` — SN mismatch drop, presence update, ack resolves pending, duplicate ack ignored, oversized payload drop;
- `tests/core/remote_input/test_leases.py` — единственный lease на SN, conflict, expiry, keepalive, cross-org get;
- `tests/core/remote_input/test_service.py` — click → injected / nack / unconfirmed (без ретрая), rate limit, publisher вызван с `routing_key="srv.<sn>.ctl"`, `expiration`, без retain;
- `tests/core/topologys/test_remote_input_topology.py` — `q_ctl` в `BINDINGS` с `dev.*.ctl`, существующие биндинги неизменны, `evt` не матчит `ctl`;
- `tests/api/v1/test_remote_input_api.py` — REST: 403 без ключа, 403 cross-tenant, 403 viewer, 409 busy, 201/200/204 happy path (мок `DeviceRepo.get_device_id`); WS: 4403/4404/4409, hello+presence, click_result, release при disconnect.
- Прогнать `uv run pytest`, `uv run ruff check .`, `uv run black --check .` — всё зелёное. Существующие тесты не трогать.

---

## 8. Документация (обязательно)

1. `docs/mqtt_topic_rules.md`: добавить строки `ctl` в обе таблицы (dev→srv: «ACK/NACK и presence агента удалённого ввода `l4desk` (retained presence)»; srv→dev: «оперативные команды удалённого ввода: pointer_move/mouse_click, QoS 1, без retain»); обновить примечание о том, что remote input **не** идёт через RPC/`out`.
2. Новый `docs/remote-input-protocol.md`: схема плоскостей (video vs control), топики, QoS/retain таблица, envelope v1, NACK codes, идемпотентность, lease model, rate limits, политика хранения, WEB_CONCURRENCY-ограничение, Roadmap (Redis; передача параметров запуска ffmpeg через control flow — зарезервировано для будущих версий, в alpha не реализуется).
3. `docs/internal-api-contract-v1.md`: новый раздел `3.x Remote Input (/api/internal/v1/remote-input)` с таблицей маршрутов, DTO и WS-протоколом из §5.
4. `docs/manual-app1-deploy-runbook.md`: подраздел «Remote input: проверки после деплоя» (§9.4) и требование `WEB_CONCURRENCY=1`.
5. `README.md`/`AGENTS.md` проекта — одна строка-ссылка на новый протокол, если там есть список документов.

---

## 9. Deploy (только `app1`)

Следуй `docs/manual-app1-deploy-runbook.md` (основной сценарий — сборка на сервере). Все SSH-команды из PowerShell — с `-n`.

### 9.1. Pre-flight
```powershell
ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "cd /home/user1 && sudo docker compose ps app1 rabbitmq && grep -n '^WEB_CONCURRENCY' iot-rpc-rest-app/app-service/.env || echo 'WEB_CONCURRENCY not set'"
```
Если `WEB_CONCURRENCY` не равен `1` — остановись и эскалируй (§6). Сделать backup `.env` (runbook §2.3).

### 9.2. Sync + build + restart только app1
- Синхронизировать исходники в `/home/user1/iot-rpc-rest-app` способом из runbook §3.1 (git pull/scp — как принято);
- `sudo docker compose build app1`;
- `sudo docker compose up -d --no-deps app1`;
- Запрещено: `docker compose up -d` без `--no-deps app1`, `restart rabbitmq|pg|menubuilder-backend|nginx-default`.

### 9.3. Проверки topology / consumer
```powershell
ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker compose -f /home/user1/compose.yaml logs --no-color --tail=100 app1 | grep -E 'Migrations applied|Subscribers registered|топологи|ctl'"
ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker exec rabbitmq rabbitmqctl list_queues name messages consumers | grep -E '^(ctl|evt|out|ack|req|res)\b'"
ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker exec rabbitmq rabbitmqctl list_bindings | grep -E 'dev\.\*\.(ctl|evt)'"
```
Ожидается: очередь `ctl` с `consumers=1`, binding `amq.topic → ctl : dev.*.ctl`, остальные очереди/биндинги без изменений.

### 9.4. Smoke Internal API (внутри контейнера, ключ брать из `.env` на сервере, не печатать его в логи)
```bash
# из ssh на сервере
KEY=$(grep -E '^INTERNAL_SERVICE_KEY=' /home/user1/iot-rpc-rest-app/app-service/.env | cut -d= -f2-)
SN=<sn тестового терминала, задаётся оператором>; ORG=<org_id этого терминала>
sudo docker exec app1 curl -s -o /dev/null -w '%{http_code}\n' -H "X-Internal-Service-Key: $KEY" -H "X-Org-Id: $ORG" -H "X-Role: admin" http://127.0.0.1:8000/api/internal/v1/remote-input/devices/$SN/status   # 200
sudo docker exec app1 curl -s -o /dev/null -w '%{http_code}\n' -H "X-Org-Id: $ORG" http://127.0.0.1:8000/api/internal/v1/remote-input/devices/$SN/status                                    # 403 (нет ключа)
sudo docker exec app1 curl -s -o /dev/null -w '%{http_code}\n' -H "X-Internal-Service-Key: $KEY" -H "X-Org-Id: 999999" -H "X-Role: user" http://127.0.0.1:8000/api/internal/v1/remote-input/devices/$SN/status   # 403 (cross-tenant)
```
Безопасная проверка приёма `dev/<SN>/ctl` без реального клика: опубликовать тестовый `presence` через RabbitMQ Management API/`rabbitmqadmin publish exchange=amq.topic routing_key=dev.$SN.ctl payload='{"v":1,"type":"presence","agent":"l4desk","status":"online","desktop_available":false,"timestamp":"..."}'` и убедиться, что `GET …/status` показывает `agent.online=true`, а таблица `device_events` не получила новых строк для этого устройства (`SELECT count(*) …` до/после). Реальный `mouse_click` в этом промпте не выполнять — его проверяет промпт №2.

### 9.5. Rollback
Runbook §5: пересобрать предыдущий коммит / вернуть предыдущий `IMAGE_TAG` и `sudo docker compose up -d --no-deps app1`. Схема БД не менялась — откат безопасен. Очередь `ctl` (non-durable) можно оставить или удалить `rabbitmqctl delete_queue ctl`.

---

## 10. Acceptance criteria

1. Существующие RPC/`evt`/`eva`/`ack`/`out`/`app`/`svc`/webhook/billing потоки не изменены (все прежние тесты зелёные, биндинги те же).
2. `srv/<SN>/ctl` публикуется через `amq.topic` с `expiration`, без retain, с `correlationData=command_id`; терминальный ACL допускает подписку (проверено regex `^srv.<SN>.*`).
3. `dev/<SN>/ctl` потребляется отдельной очередью `ctl`; сообщения не попадают в `evt`/`DeviceEvent`/webhooks.
4. Retained/периодический presence отражается в `GET …/status` (и `stale=true` после 90 с тишины).
5. Команда с невалидным/чужим/истёкшим lease отклоняется (`409/403/404`, WS `error lease_inactive`), в MQTT ничего не публикуется.
6. `mouse_click` без ACK → `unconfirmed` ровно через `click_ack_timeout_ms`, без повторной публикации.
7. ACK с валидным `command_id` завершает ожидание с `injected` и `latency_ms`; NACK → `nack` + `code`.
8. Cross-tenant запрос (org без этого устройства) → `403`; viewer → `403`; без service key → `403`.
9. Второй lease на тот же SN → `409` с owner; WS-подключение к чужому lease → `4403`.
10. QoS/retain/TTL-правила задокументированы (`docs/remote-input-protocol.md`, `docs/mqtt_topic_rules.md`) и enforce'ятся тестами publisher'а.
11. `WEB_CONCURRENCY` проверен/зафиксирован; предупреждение при `workers>1` реализовано; ограничение задокументировано.
12. `uv run pytest`, `uv run ruff check .`, `uv run black --check .` — без ошибок; деплой выполнен только для `app1`, проверки §9.3–9.4 пройдены; результаты и логи приложены к отчёту.

