# PROMPT AGENT — `iot-rpc-rest-app` (`app1`): Продление аренды в MQTT, scope upgrade и статус агента

Ты — Senior Python / Backend инженер. Работаешь автономно в репозитории `D:\work\iot.leo4.ru\iot-rpc-rest-app` (сервис `app-service`, Docker-контейнер `app1`).
Твоя цель — устранить замерзание видеопотока из-за истечения локальной аренды терминала, обеспечить отправку команды продления аренды в MQTT-шину `srv/{SN}/ctl`, гарантировать установку `stream_mode="desktop"` при повышении аренды до `scope="input"` и защитить статус `online` агента от гонки LWT.

Соблюдай правила репозитория: `AGENTS.md`, `CONTRIBUTING.md`, `copilot-instructions.md` (Python 3.14, FastAPI, FastStream, RabbitMQ MQTT 5; тесты и линтеры: `uv run pytest`, `uv run ruff check .`, `uv run black --check .`).

---

## 1. Контекст выявленных дефектов

### 1.1 Замерзание видеопотока и локальный Watchdog в `l4desk`
- **Проблема:** В логах терминала:
  ```text
  11:51:25 stream_start / FFmpeg started
  11:51:45 Local lease expired (... + 5s). Fail-closed stop.
  11:51:45 stream_event state=stopped, reason=lease_expired
  ```
  Через 20 секунд после старта терминальный агент штатно глушит FFmpeg, и видео замерзает.
- **Причина:** При вызове keepalive в `app1` обновляется только in-memory `lease.expires_at`. В MQTT-топик `srv/{SN}/ctl` не публикуется команда обновления срока аренды для терминального агента. В результате таймер `l4desk` не продлевается.
- **Задача:** При каждом успешном keepalive (REST или WS) публиковать в `srv/{SN}/ctl` команду продления аренды (`lease_renew` или команду с обновленным `expires_at_ms` / `ttl_sec`).

### 1.2 Ошибка «Pointer move only allowed in desktop mode»
- **Проблема:** При движении мыши по видео WebSocket отбивает ошибку:
  `WsError(code="mode_conflict", message="Pointer move only allowed in desktop mode")`
- **Причина:** Если аренда создавалась со `scope="stream"` (`stream_mode=None`), а затем переведена в `scope="input"`, либо если старт стрима произошел после создания аренды, `lease.stream_mode` не обновляется до `"desktop"`.
- **Задача:** В `LeaseRegistry.acquire` и `upgrade_scope`: если `scope in ("stream", "input")` и режим стрима `"desktop"`, обязательно выставлять `lease.stream_mode = "desktop"`.

### 1.3 Рассинхрон статусов «В эфире», но «Агент офлайн» при рестарте l4desk
- **Проблема:** При падении или рестарте `l4desk` (например, `taskkill`) UI показывает, что агент офлайн, хотя l4desk переподключился и опубликовал `presence [online]`.
- **Причина:** Гонка сообщений на брокере: старое TCP-соединение разрывается, и брокер генерирует LWT `offline`, который доходит до `app1` после того, как новый процесс `l4desk` уже прислал `online`. Либо `app1` не обновляет `_SN_STATUS[sn].agent.online = True` при получении расширенного presence.
- **Задача:** В `mqtt_bridge.py`:
  - При получении `presence` с любым `online=True` или статусом `online` немедленно обновлять `_SN_STATUS[sn]`.
  - При получении `stream_event(state="stopped", reason=...)` сбрасывать состояние стрима в активной lease (`lease.stream_state = "stopped"`), чтобы не висел статус «В эфире» при мертвом стриме.

---

## 2. Задачи разработки

### 2.1 Публикация продления аренды в `srv/{SN}/ctl`
1. В `core/remote_input/schemas.py`:
   Определить исходящую модель команды продления аренды:
   ```python
   class CtlLeaseRenew(StrictBaseModel):
       v: Literal[1] = 1
       type: Literal["lease_renew"] = "lease_renew"
       cmd_id: UUID = Field(default_factory=uuid4)
       lease_id: UUID
       ttl_sec: int
       expires_at_ms: int
       timestamp: str
   ```
2. В `core/remote_input/service.py`:
   В методе `keepalive_lease(lease_id)`:
   - Продлить `lease.expires_at = now + ttl`.
   - Опубликовать команду `lease_renew` в топик `srv/{sn}/ctl`:
     ```python
     cmd = CtlLeaseRenew(
         lease_id=lease.lease_id,
         ttl_sec=lease.ttl_sec,
         expires_at_ms=int(lease.expires_at.timestamp() * 1000),
         timestamp=datetime.now(UTC).isoformat(),
     )
     await self.publisher.publish_control_command(lease.sn, cmd)
     ```

### 2.2 Фиксация `stream_mode` в `core/remote_input/leases.py`
- При создании аренды и при смене scope:
  ```python
  if scope in ("stream", "input"):
      lease.stream_mode = "desktop"
  ```
- Убедиться, что при валидации `WsPointerMove` в `remote_input.py` и `service.py`:
  если активен desktop-поток, событие не отклоняется.

### 2.3 Обработка сброса стрима по `stream_event` в `mqtt_bridge.py`
- При получении `CtlStreamEvent` со `state == "stopped"` или `state == "failed"`:
  - Найти активную lease для `sn`.
  - Установить `lease.stream_state = "stopped"`, `lease.stream_instance_id = None`.
  - Оповестить WebSocket-клиентов событием `stream_state`.

---

## 3. Тестирование и деплой

1. **Тестирование**:
   ```bash
   uv run ruff check .
   uv run black --check .
   uv run pytest tests/core/remote_input/
   ```
2. **Деплой на прод-сервер `87.242.100.34`**:
   - Синхронизация кода:
     ```bash
     ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "cd /home/user1/iot-rpc-rest-app && git fetch origin && git checkout master && git pull --ff-only"
     ```
   - Пересборка контейнера `app1`:
     ```bash
     ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "cd /home/user1 && sudo docker compose build app1 && sudo docker compose up -d --no-deps app1"
     ```
   - Проверка логов:
     ```bash
     ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker compose -f /home/user1/compose.yaml logs --tail=100 app1"
     ```
