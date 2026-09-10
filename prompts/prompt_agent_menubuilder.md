# PROMPT AGENT 2 — `MenuBuilder`: BFF WebSocket proxy, нормализация `key_event`, обогащение lease-контекстом и Video UI

Ты — Senior Full-Stack инженер (Python / FastAPI + React 19 / TypeScript). Работаешь автономно в репозитории `D:\repo\platerra\Public\etranprocessing`, каталоги `MenuBuilder/backend` и `MenuBuilder/frontend`.
Твоя цель — устранить контрактные расхождения в BFF WebSocket-прокси и Video UI: нормализовать трансляцию клавиатурного ввода `key` → `key_event`, обеспечить серверное обогащение команд контекстом активной трансляции (`desktop_id`, `stream_instance_id`), гарантировать корректную передачу координат мыши `0..65535` и проверить трансляцию событий смены состояния стрима (`stream_state`).

Соблюдай правила репозитория (`ProcessingBackend/GUIDELINES.md`, `AGENTS.md`):
- Backend: Python 3.14, FastAPI, SQLAlchemy 2.0 async, Pydantic v2. Линтеры:
  ```bash
  uv run ruff check --fix app
  uv run ruff format app
  uv run pyright app
  uv run pytest
  ```
- Frontend: React 19, TypeScript (strict), Vite, Ant Design v6. Сборка:
  ```bash
  npm --prefix MenuBuilder/frontend run build
  ```
- Не трогать каталоги `FRONT/`, `BACK/`, `sqlFileExample/`, `stored-procedures/`, `tools/`, `ProcessingBackend/`.

---

## 1. Контекст и выявленные расхождения

### 1.1 Backend BFF (`MenuBuilder/backend/app/routers/video_control.py`)
- В WebSocket-прокси `control_ws_proxy` (`/devices/{device_id}/control/ws/{lease_id}`):
  - Прием сообщения от браузера с типом `key`:
    ```python
    elif msg_type == "key":
        validated = WsInboundKey.model_validate(data)
    ```
    где `WsInboundKey` имеет `type: Literal["key"]`.
  - Затем отправка в `upstream_ws.send(validated.model_dump_json())`.
  - Сервис `app1` на `/ws/lease/{lease_id}` строго ожидает `type: "key_event"`. В результате `app1` падает с `ValidationError` и отсекает ввод с клавиатуры.
- В сообщениях ввода (`pointer_move`, `mouse_click`, `key`) из браузера отсутствуют поля `desktop_id` и `stream_instance_id`. Сервис `app1` требует совпадения этих полей с активной lease, иначе отклоняет команды как не привязанные к конкретному экрану/потоку.
- В REST fallback `/devices/{device_id}/control/events` тип события `key` должен корректно мапиться в `key_event`.

### 1.2 Frontend (`MenuBuilder/frontend/src/`)
- В `src/hooks/useRemoteControl.ts`: метод `sendKey` формирует `{ type: "key", kind, vk, text }`.
- В `src/api/video.ts`: типы `ControlWsInbound` и `ControlWsOutbound` должны поддерживать строгий дискриминатор `key_event` и обратную совместимость с `key`.
- Мышь (`sendMove`, `sendClick`): координаты должны быть целыми числами (`Math.round`) в диапазоне `0..65535`.

---

## 2. Задачи разработки

### 2.1 Backend: нормализация и обогащение в `video_control.py`
1. **Обновление моделей входящих сообщений**:
   - Разрешить прием как `type: "key"`, так и `type: "key_event"`:
     ```python
     class WsInboundKey(BaseModel):
         type: Literal["key", "key_event"]
         kind: Literal["down", "up", "press"]
         vk: int = Field(ge=0, le=255)
         text: str | None = Field(default=None, max_length=32)
         client_ref: str | None = Field(default=None, max_length=64)

         model_config = ConfigDict(extra="forbid")
     ```
2. **Обогащение и нормализация перед отправкой в `upstream_ws`**:
   - В цикле `browser_to_upstream`:
     - Для `pointer_move`, `mouse_click`, `key`/`key_event` извлечь из контекста активной lease `selected_desktop_id` и `stream_instance_id`.
     - При отправке клавиатурного события нормализовать поле `type` строго в `"key_event"`:
       ```python
       payload = validated.model_dump(exclude_none=True)
       if msg_type in ("key", "key_event"):
           payload["type"] = "key_event"
       if lease_info.get("selected_desktop_id"):
           payload.setdefault("desktop_id", lease_info["selected_desktop_id"])
       if lease_info.get("stream_instance_id"):
           payload.setdefault("stream_instance_id", str(lease_info["stream_instance_id"]))
       await upstream_ws.send(json.dumps(payload))
       ```
3. **Обработка событий от `upstream_ws`**:
   - Убедиться, что события `stream_state` (получаемые от `app1` в ответ на `stream_event` терминала) и `lease_revoked` корректно логируются и без искажений транслируются в браузерный `websocket.send_text(raw_msg)`.

### 2.2 Frontend: синхронизация типов и отправки событий
1. **`src/api/video.ts`**:
   - Обновить union-тип `ControlWsInbound`:
     ```typescript
     export interface ControlWsKey {
       type: "key_event" | "key";
       kind: "down" | "up" | "press";
       vk: number;
       text?: string;
       client_ref?: string;
     }
     ```
2. **`src/hooks/useRemoteControl.ts`**:
   - В методе `sendKey` отправлять `type: "key_event"`:
     ```typescript
     const sendKey = useCallback(
       (kind: "down" | "up" | "press", vk: number, text?: string) => {
         if (status !== "active") return false;
         return sendWsMessage({
           type: "key_event",
           kind,
           vk,
           text,
         });
       },
       [status, sendWsMessage]
     );
     ```
   - В `sendMove` и `sendClick` убедиться, что передаются округленные целые значения:
     ```typescript
     const clampedX = Math.max(0, Math.min(65535, Math.round(x)));
     const clampedY = Math.max(0, Math.min(65535, Math.round(y)));
     ```

### 2.3 Тестирование бэкенда (`MenuBuilder/backend/tests/test_video_control.py`)
- Добавить тесты для WebSocket-прокси:
  - Проверка нормализации `{"type": "key", ...}` в `{"type": "key_event", ...}` при отправке upstream.
  - Проверка обогащения `desktop_id` и `stream_instance_id` из active lease.
  - Проверка трансляции `stream_state` от upstream в браузер.
  - Запуск: `uv run pytest tests/test_video_control.py`.

---

## 3. Сборка и деплой

### 3.1 Сборка и линтинг
В каталоге `MenuBuilder/backend`:
```bash
uv run ruff check --fix app
uv run ruff format app
uv run pyright app
uv run pytest
```
В каталоге `MenuBuilder/frontend`:
```bash
npm run build
```
Убедиться, что сборка `dist/` завершается без ошибок компиляции TypeScript.

### 3.2 Доставка на прод-сервер `87.242.100.34`
1. **Frontend (Live Mount)**:
   Артефакты SPA монтируются контейнером `nginx-default` из `/home/user1/MenuBuilder/frontend/dist/`.
   Доставка артефактов из PowerShell:
   ```bash
   scp -i d:\.ssh\id_ed25519 -r MenuBuilder/frontend/dist/* user1@87.242.100.34:/home/user1/MenuBuilder/frontend/dist/
   ```
2. **Backend**:
   Перезапуск сервиса `menubuilder-backend` (через SSH с флагом `-n`):
   ```bash
   ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker restart menubuilder-backend"
   ```
3. **Проверка**:
   ```bash
   ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker compose -f /home/user1/compose.yaml ps menubuilder-backend"
   ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker logs --tail=50 menubuilder-backend"
   ```

---

## 4. Формат отчёта

По завершении сформируй Markdown-отчёт:
1. **Карта изменений** в `MenuBuilder/backend/` и `MenuBuilder/frontend/`.
2. **Результаты проверок**: статус ruff, pyright, pytest и `npm run build`.
3. **Подтверждение деплоя**: доставка файлов frontend, статус `menubuilder-backend` в Docker.
