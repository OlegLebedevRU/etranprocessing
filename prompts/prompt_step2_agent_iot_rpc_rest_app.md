# PROMPT AGENT 2.3 — `iot-rpc-rest-app` / `app1`: Идемпотентность управления потоком, синхронизация жизненного цикла стрима и деплой

Ты — Senior Python / Backend инженер. Работаешь автономно в репозитории `D:\work\iot.leo4.ru\iot-rpc-rest-app` (сервис `app-service`, Docker-контейнер `app1`).

Твоя цель — реализовать **идемпотентность операций управления стримом (`stream_start` / `stream_stop`)** (задача R5 / P1 из E2E-архитектуры) и **надежную синхронизацию жизненного цикла видеопотока** между событиями терминала (`stream_event`), активной арендой (`lease`) и присутствием (`presence`), покрыть изменения тестами и выполнить деплой обновленного `app1` на прод-сервер `87.242.100.34`.

Соблюдай правила репозитория: `AGENTS.md`, `CONTRIBUTING.md`, `copilot-instructions.md` (Python 3.14, FastAPI, FastStream, SQLAlchemy/asyncpg, Alembic, RabbitMQ MQTT 5; тесты и линтеры: `uv run pytest`, `uv run ruff check .`, `uv run black --check .`). Секреты — только `.env`; `app-service/.env` не выводить в лог/отчёт целиком. Другие репозитории (`etranprocessing`, MenuBuilder, `tools/`) **не изменяешь** — только читаешь для сверки контрактов.

---

## 1. Контекст и архитектурная проблема

### 1.1 Неидемпотентный `stream_start` и `stream_stop` (R5)
- **Проблема 1 (stream_start):** При повторном клике пользователя в UI, обновлении страницы или повторной отправке запроса из-за сетевого ретрая метод `RemoteInputService.stream_start()` генерирует новый `stream_instance_id = uuid4()`, отсылает новую команду на терминал и ждет ACK. Если терминал отвечает `already_running`, сервер меняет `stream_instance_id` в lease, что ломает валидацию команд ввода у уже подключенного клиента. Если же повторный запуск отклоняется терминалом, сервер падает с ошибкой 409.
- **Проблема 2 (stream_stop):** Если стрим уже остановлен (или упал сам), повторный вызов `stream_stop()` не должен возвращать ошибку 409 Conflict. Клиент должен получать успешный ответ со статусом `already_stopped`.

### 1.2 Рассинхронизация состояния lease при падении стрима
- **Проблема:** Когда терминальный агент `l4desk` присылает `stream_event` со статусом `stopped`, `failed`, `source_unavailable` или `session_unavailable`, в текущем коде `mqtt_bridge.py` обновляется объект `presence.stream`, но в активной аренде `lease.stream_instance_id` может оставаться заполненным старым идентификатором.
- В результате BFF считает, что трансляция активна, и пытается слать команды ввода к несуществующему процессу FFmpeg.

---

## 2. Задачи разработки

### 2.1 Идемпотентность в `app-service/core/remote_input/service.py`
1. **Идемпотентный `stream_start`**:
   - В методе `RemoteInputService.stream_start()`:
     - Проверить текущее состояние активной lease:
       ```python
       if lease.stream_instance_id is not None and lease.stream_mode == mode:
           # Если источник совпадает с текущим работающим источником:
           current_source = lease.selected_desktop_id if mode == "desktop" else None
           if current_source == source_id or not source_id:
               log.info(
                   "stream_start idempotent: stream already running for lease %s (instance %s)",
                   lease_id,
                   lease.stream_instance_id,
               )
               return StreamStartResponse(
                   stream_instance_id=lease.stream_instance_id,
                   result="already_running",
                   state="running",
               )
       ```
     - Если `mode` или `source_id` отличаются от текущего активного стрима — это операция переключения источника (sequential switch). В этом случае генерируется новый `stream_instance_id`, команда отсылается на терминал, и после ответа терминала обновляется `lease.stream_instance_id`.
2. **Идемпотентный `stream_stop`**:
   - В методе `RemoteInputService.stream_stop()`:
     - Если `lease.stream_instance_id is None`:
       ```python
       log.info("stream_stop idempotent: no active stream for lease %s", lease_id)
       return StreamStopResponse(
           result="already_stopped",
           state="stopped",
       )
       ```
     - Если стрим активен — послать `StreamStopCommand`, дождаться ACK, очистить поля стрима в lease (`lease.stream_instance_id = None`, `lease.stream_mode = None`, `lease.selected_desktop_id = None`) и вернуть ответ.

### 2.2 Синхронизация `stream_event` в `app-service/core/remote_input/mqtt_bridge.py`
1. При получении `CtlStreamEvent` от терминала:
   - Если `event.state in ("stopped", "failed", "source_unavailable", "session_unavailable")`:
     - Найти активную lease по `sn`:
       ```python
       active_lease = await self.leases.get_active_by_sn(sn)
       if active_lease and active_lease.stream_instance_id == event.stream_instance_id:
           active_lease.stream_instance_id = None
           active_lease.stream_mode = None
           active_lease.selected_desktop_id = None
       ```
     - Обновить состояние `presence.stream`.
     - Разослать событие смены состояния `WsStreamState` во все активные WebSocket-сессии этой lease, чтобы MenuBuilder UI немедленно узнал о завершении или падении потока.

### 2.3 Тестирование (`tests/core/remote_input/test_service.py`)
1. Добавить тесты на идемпотентность:
   - `test_stream_start_idempotent_when_already_running`:
     - Первый вызов `stream_start` запускает стрим и сохраняет `stream_instance_id`.
     - Второй вызов с теми же параметрами возвращает `result="already_running"` и тот же `stream_instance_id` без отправки повторной команды в MQTT.
   - `test_stream_stop_idempotent_when_already_stopped`:
     - Вызов `stream_stop` при отсутствующем стриме возвращает `result="already_stopped"` и HTTP 200 OK.
   - `test_stream_event_clears_lease_stream_instance`:
     - Проверка очистки `stream_instance_id` в lease при получении терминального события `stream_event(state="failed")`.

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
4. Сборка и перезапуск контейнера `app1`:
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
2. **Описание логики идемпотентности**: обработка `already_running` и `already_stopped`.
3. **Результаты тестов**: вывод `uv run pytest tests/core/remote_input/`.
4. **Результаты деплоя**: вывод `docker compose ps app1`, хеш коммита на сервере, проверка логов старта.
