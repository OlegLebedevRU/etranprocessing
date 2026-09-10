# PROMPT AGENT 1 — `iot-rpc-rest-app` / `app1`: Синхронизация контрактов `ctl v1`, схемы remote input, фикстуры и деплой

Ты — Senior Python / Backend инженер. Работаешь автономно в репозитории `D:\work\iot.leo4.ru\iot-rpc-rest-app` (сервис `app-service`, Docker-контейнер `app1`).
Твоя цель — устранить расхождения контрактов в подсистеме Remote Input / `ctl v1`, обеспечить строгую валидацию входящих сообщений от терминального агента (`l4desk`) и WebSocket-прокси (`MenuBuilder BFF`), покрыть контракт тестами-фикстурами и выполнить деплой `app1` на прод-сервер `87.242.100.34`.

Соблюдай правила репозитория: `AGENTS.md`, `CONTRIBUTING.md`, `copilot-instructions.md` (Python 3.14, FastAPI, FastStream, SQLAlchemy/asyncpg, Alembic, RabbitMQ MQTT 5; тесты и линтеры: `uv run pytest`, `uv run ruff check .`, `uv run black --check .`). Секреты — только `.env`; `app-service/.env` не выводить в лог/отчёт целиком. Другие репозитории (`etranprocessing`, MenuBuilder, `tools/`) **не изменяешь** — только читаешь для сверки контрактов.

---

## 1. Контекст и выявленные расхождения

### 1.1 Файлы подсистемы Remote Input (`app-service/core/remote_input/` и `api/internal_v1/`)
- `core/remote_input/schemas.py` — Pydantic-модели (StrictBaseModel с `extra="forbid"`):
  - Inbound от терминала: `CtlAck`, `CtlNack`, `CtlPresence`, `CtlStreamEvent`, дискриминатор `CtlInboundMessage`.
  - Inbound от WebSocket: `WsPointerMove`, `WsMouseClick`, `WsKeyEvent`, `WsKeepalive`, `WsRelease`, дискриминатор `WsInboundMessage`.
  - Outbound на терминал: `CtlInventoryGet`, `CtlStreamStart`, `CtlStreamStop`, `CtlPointerMove`, `CtlMouseClick`, `CtlKeyEvent`.
- `core/remote_input/service.py` — логика обработки команд, валидация lease, подстановка `desktop_id` и `stream_instance_id`, проверка режима `desktop`.
- `core/remote_input/publisher.py` — публикация envelope v1 в RabbitMQ / MQTT-топик `srv/<SN>/ctl`.
- `core/remote_input/mqtt_bridge.py` — прием сообщений из `dev/<SN>/ctl`, разбор через `CtlInboundAdapter`, маршрутизация в `pending` и `presence`.
- `core/remote_input/leases.py` — `LeaseRegistry` (in-memory, одна активная lease на SN, scopes: `console`, `view`, `stream`, `input`).
- `api/internal_v1/remote_input.py` — REST API (`/devices/{sn}/status`, `/devices/{sn}/lease`, `/lease/{id}/stream/start`, etc.) и `WS /ws/lease/{lease_id}`.

### 1.2 Критическое расхождение 1: Поле `sn` в `CtlStreamEvent`
- **Проблема:** Терминальный агент `l4desk` (`tools/l4desk/src/ctl_protocol.c`) отправляет событие смены состояния стрима в формате:
  ```json
  {"v":1,"type":"stream_event","sn":"<SN>","stream_instance_id":"<UUID>","state":"<STATE>","reason":"...","timestamp":"..."}
  ```
- В `app-service/core/remote_input/schemas.py` класс `CtlStreamEvent(StrictBaseModel)` наследует `extra="forbid"` и **не содержит** поля `sn` (полагая, что SN берется только из MQTT-топика `dev/<SN>/ctl`).
- **Следствие:** При получении `stream_event` от `l4desk` парсер Pydantic выдает ошибку `Extra inputs are not permitted [extra_forbidden]`, и событие дропается, ломая обновление состояния стрима на сервере.
- **Решение:** В `CtlStreamEvent` добавить опциональное поле `sn: str | None = None`. Если `sn` передан, `mqtt_bridge.py` должен сверить его с SN из топика (или пропустить, если они совпадают).

### 1.3 Критическое расхождение 2: Клавиатурный ввод через WebSocket (`key` vs `key_event`)
- **Проблема:** В `schemas.py` класс `WsKeyEvent` объявлен со строгим дискриминатором `type: Literal["key_event"] = "key_event"`. В то же время некоторые клиенты (legacy BFF proxy) шлют `type: "key"`.
- **Решение:** В `WsInboundMessage` разрешить либо оба литерала `Literal["key_event", "key"]`, либо поддержать alias `key` → `key_event`, чтобы Pydantic валидировал входящее сообщение без 422 ошибки. При этом поля `kind` (`"down" | "up" | "press"`), `vk` (целое `0..255` из `ALLOWED_VK_CODES`), `text` (строка до 32 символов) остаются строго валидируемыми.

### 1.4 Проверка координатной модели мыши
- `WsPointerMove` и `WsMouseClick` ожидают координаты `x: int = Field(..., ge=0, le=65535)` и `y: int = Field(..., ge=0, le=65535)`.
- Убедиться, что граничные значения `0` и `65535` проходят валидацию корректно.

---

## 2. Задачи разработки

### 2.1 Обновление схем (`core/remote_input/schemas.py`)
1. **`CtlStreamEvent`**:
   ```python
   class CtlStreamEvent(StrictBaseModel):
       v: Literal[1] = 1
       type: Literal["stream_event"] = "stream_event"
       sn: str | None = None  # Поддержка l4desk stream_event payload
       stream_instance_id: UUID | None = None
       state: StreamState
       reason: str | None = None
       timestamp: str
   ```
2. **`WsKeyEvent`**:
   Поддержать совместимость по типу `type`:
   ```python
   class WsKeyEvent(StrictBaseModel):
       type: Literal["key_event", "key"] = "key_event"
       kind: Literal["down", "up", "press"]
       vk: int = Field(..., ge=0, le=255)
       text: str | None = Field(default=None, max_length=32)
       desktop_id: str | None = None
       stream_instance_id: UUID | None = None
       client_ref: str | None = Field(default=None, max_length=64)
   ```
   Убедиться, что `WsInboundAdapter` корректно валидирует входящие сообщения как с `type: "key_event"`, так и с `type: "key"`.

### 2.2 Обработка в `mqtt_bridge.py` и `service.py`
1. При получении `CtlStreamEvent` в `mqtt_bridge.py`:
   - Если `event.sn` присутствует и не совпадает с `topic_sn`, залогировать предупреждение, но не ронять обработку, используя `topic_sn` как авторитетный источник.
   - Обновить `presence.stream` для данного SN.
   - Разослать событие подписчикам WebSocket активной lease (`stream_state`).
2. При передаче `key_event` в `service.py`:
   - Убедиться, что `desktop_id` и `stream_instance_id` автоматически обогащаются из активной lease, если клиент их опустил.
   - Если клиент передал несовпадающие `desktop_id` или `stream_instance_id` — вернуть понятный отказ (409 Conflict: `desktop_mismatch` / `stream_mismatch`).
   - Проверить, что ввод разрешен только при `lease.stream_mode == "desktop"`.

### 2.3 Добавление контрактных тестов-фикстур (`tests/core/remote_input/test_contract_fixtures.py`)
Создать новый файл тестов `app-service/tests/core/remote_input/test_contract_fixtures.py`:
- Тест 1: Валидация всех входящих сообщений от `l4desk`:
  - `presence` со всеми полями (`screen`, `inventory`, `stream`).
  - `stream_event` с полем `sn` и без него.
  - `ack` с результатами `started`, `switched`, `stopped`, `injected`.
  - `nack` с кодами ошибок (`lease_mismatch`, `desktop_mismatch`, `ffmpeg_missing` и т.д.).
- Тест 2: Валидация входящих WebSocket сообщений:
  - `pointer_move` с координатами 0, 65535, 32768.
  - `mouse_click` (left button, client_ref).
  - `key_event` с `type="key_event"` и `type="key"`, валидные `vk` из whitelist, отказ на `vk` вне whitelist.
- Тест 3: Сквозная проверка `mqtt_bridge.py` на прием `stream_event` со `sn`.

---

## 3. Деплой `app1` на прод-сервер

Хост: `user1@87.242.100.34`, ключ `d:\.ssh\id_ed25519`.
Сервис: `app1` в `/home/user1/compose.yaml`.
**Правило SSH из Windows PowerShell**: обязательно указывать флаг `-n` (`ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 ...`).

### 3.1 Pre-flight checks
```bash
ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "cd /home/user1 && sudo docker compose ps app1 && df -h / && free -m"
```
Убедись, что диск < 90 %, RAM свободно > 300 MiB.

### 3.2 Синхронизация кода и деплой
1. Зафиксируй локальные изменения, запусти линтеры и тесты:
   ```bash
   uv run ruff check .
   uv run black --check .
   uv run pytest
   ```
2. Отправь изменения в git (`git push origin master`).
3. На сервере:
   ```bash
   ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "cd /home/user1/iot-rpc-rest-app && git fetch origin && git checkout master && git pull --ff-only"
   ```
4. Сборка и перезапуск только контейнера `app1`:
   ```bash
   ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "cd /home/user1 && sudo docker compose build app1 && sudo docker compose up -d --no-deps app1"
   ```
5. Проверка статуса и логов:
   ```bash
   ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker compose -f /home/user1/compose.yaml logs --tail=100 app1"
   ```
   Убедись, что нет Traceback и сервис успешно подключился к RabbitMQ.

---

## 4. Формат отчёта

По завершении сформируй Markdown-отчёт:
1. **Список измененных файлов** в `app-service/core/remote_input/` и `tests/`.
2. **Точные JSON-схемы** обновленных сообщений (`stream_event`, `key_event`).
3. **Результаты тестов**: вывод `uv run pytest tests/core/remote_input/`.
4. **Результаты деплоя**: вывод `docker compose ps app1`, хеш коммита на сервере, проверка логов старта.
