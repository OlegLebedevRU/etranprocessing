# PROMPT AGENT — `MenuBuilder`: Устранение 404 keepalive, 409 stop idempotency, passive wheel listener и UX удаленного ввода

Ты — Senior Full-Stack инженер (Python 3.14 / FastAPI + React 19 / TypeScript). Работаешь автономно в репозитории `D:\repo\platerra\Public\etranprocessing`, каталоги `MenuBuilder/backend` и `MenuBuilder/frontend`.
Твоя цель — устранить ошибки в подсистеме видеотрансляции и удаленного управления: исправить 404 Not Found на эндпоинте keepalive (деплой бэкенда), сделать остановку стрима строго идемпотентной (ликвидировать 409 Conflict), исправить пассивный слушатель `wheel` в `RemoteControlOverlay` и исключить невалидную отправку координат мыши при неактивном desktop-режиме.

Соблюдай правила репозитория (`ProcessingBackend/GUIDELINES.md`, `AGENTS.md`):
- Backend: Python 3.14, FastAPI, SQLAlchemy 2.0 async, Pydantic v2.
  ```bash
  uv run ruff check --fix app
  uv run ruff format app
  uv run pyright app
  uv run pytest
  ```
- Frontend: React 19, TypeScript (strict), Vite, Ant Design v6.
  ```bash
  npm --prefix MenuBuilder/frontend run build
  ```
- Запрещено трогать каталоги `FRONT/`, `BACK/`, `sqlFileExample/`, `stored-procedures/`, `tools/`, `ProcessingBackend/`.

---

## 1. Контекст выявленных дефектов

### 1.1 Ошибка 404 на `POST /api/v1/video/devices/{device_id}/control/keepalive`
- **Проблема:** В браузере фиксируется:
  `POST https://dev.leo4.ru:3000/api/v1/video/devices/773/control/keepalive 404 (Not Found)`
- **Причина:** В коде `MenuBuilder/backend/app/routers/video_control.py:811` эндпоинт присутствует, однако запущенный на сервере `87.242.100.34` контейнер `menubuilder-backend` работает на старом образе, где этот роут отсутствовал.
- **Задача:** Проверить регистрацию роута, пересобрать и перезапустить контейнер `menubuilder-backend` на прод-сервере.

### 1.2 Ошибка 409 Conflict на `POST /api/v1/video/devices/{device_id}/stream/stop`
- **Проблема:** При попытке остановить поток (или при повторном клике / после завершения стрима) возвращается:
  `POST https://dev.leo4.ru:3000/api/v1/video/devices/773/stream/stop 409 (Conflict)`
- **Причина:** В `app/routers/video_control.py:764` исключение HTTPException с кодом 409 подавляется только если текст ошибки содержит `"unsupported"` или `"terminal_timeout"`. Если `app1` возвращает `409 Conflict` с `"no active stream for lease"` или `"stream already stopped"`, бэкенд выбрасывает ошибку в браузер.
- **Задача:** Сделать остановку стрима строго идемпотентной: при любых признаках уже остановленного потока возвращать `StreamStopResponse(result="stopped")`, очищать PIN/mountpoint и не отдавать 409 в UI.

### 1.3 Ошибка «Unable to preventDefault inside passive event listener invocation»
- **Проблема:** В консоли браузера:
  `vendor-antd-....js:32 Unable to preventDefault inside passive event listener invocation.`
- **Причина:** В `MenuBuilder/frontend/src/components/RemoteControlOverlay.tsx:255` используется JSX-проп `onWheel={(e) => e.preventDefault()}`. В современных браузерах обработчики `wheel` по умолчанию пассивные. Вызов `preventDefault()` в пассивном листнере запрещен спецификацией и вызывает runtime-ошибку.
- **Задача:** Заменить JSX-проп `onWheel` на явную подписку через `useEffect` и `ref.addEventListener("wheel", onWheel, { passive: false })`.

### 1.4 Ошибка «Pointer move only allowed in desktop mode» и валидация мыши
- **Проблема:** При наведении курсора на окно видео консоль и WebSocket спамятся ошибками `Pointer move only allowed in desktop mode`.
- **Причина:** Если трансляция запущена в режиме камеры (`usb-camera`) либо аренда еще не переведена в `stream_mode="desktop"`, бэкенд `app1` бракует события перемещения.
- **Задача:** В `RemoteControlOverlay.tsx` блокировать отправку `sendMove`, если `isCameraMode` активен или если режим стрима в данных терминала не равен `"desktop"`.

### 1.5 Цикл продления аренды (Keepalive Loop)
- **Проблема:** Агент на терминале завершает стрим через 20 секунд с `reason=lease_expired`, так как продление аренды не доходит.
- **Задача:** Убедиться, что на фронтенде в хуке управления или видеокомпоненте работает интервал `keepaliveControlLease`, который вызывается каждые 5–7 секунд (гарантированно чаще, чем TTL аренды 15–20 с) при активной сессии.

---

## 2. Задачи разработки

### 2.1 Бэкенд: Идемпотентность `stream/stop` в `video_control.py`
В функции `stop_device_stream`:
```python
except HTTPException as exc:
    err_detail = (
        str(exc.detail).lower()
        if isinstance(exc.detail, str)
        else str(exc.detail.get("code", "")).lower()
        if isinstance(exc.detail, dict)
        else ""
    )
    # Идемпотентная обработка: если поток уже остановлен или отсутствует
    if exc.status_code == status.HTTP_409_CONFLICT or (
        exc.status_code == status.HTTP_504_GATEWAY_TIMEOUT
        and any(c in err_detail for c in ("unsupported", "terminal_timeout", "already", "no active"))
    ):
        clear_mountpoint_pin(device_id)
        if body and body.destroy_mountpoint:
            await _destroy_janus_mountpoint(device_id)
        return StreamStopResponse(result="stopped")
```

### 2.2 Фронтенд: Исправление `RemoteControlOverlay.tsx`
1. Удалить `onWheel={(e) => e.preventDefault()}` из JSX контейнера.
2. Добавить `useEffect` с явным непассивным листнером:
   ```tsx
   useEffect(() => {
     const el = containerRef.current;
     if (!el) return;
     const handleWheel = (e: WheelEvent) => {
       e.preventDefault();
     };
     el.addEventListener("wheel", handleWheel, { passive: false });
     return () => {
       el.removeEventListener("wheel", handleWheel);
     };
   }, []);
   ```
3. В `handlePointerMove`:
   ```tsx
   const handlePointerMove = (e: React.PointerEvent<HTMLDivElement>) => {
     if (!active || isCameraMode) return;
     // Не отправлять перемещения, если стрим не в режиме desktop
     if (streamMode && streamMode !== "desktop") return;
     const coords = getNormalizedCoordinates(e);
     if (!coords) return;
     sendMove(coords.x, coords.y);
   };
   ```

### 2.3 Фронтенд: Регулярный Keepalive Loop
- Проверить интервал продления в `useRemoteControl.ts` / `video-surveillance.tsx`:
  - Интервал отправки keepalive должен составлять 5 секунд (при TTL 15–20 с).
  - При ошибке 404 или истечении сессии логировать причину и уведомлять пользователя, предотвращая внезапное замерзание видео.

---

## 3. Проверка, сборка и деплой

1. **Тестирование бэкенда**:
   ```bash
   cd MenuBuilder/backend
   uv run ruff check --fix app
   uv run ruff format app
   uv run pyright app
   uv run pytest tests/test_video.py tests/test_video_stream_permissions.py
   ```
2. **Сборка фронтенда**:
   ```bash
   cd MenuBuilder/frontend
   npm run build
   ```
3. **Деплой на прод-сервер `87.242.100.34`**:
   - Доставка фронтенда (live mount):
     ```bash
     scp -i d:\.ssh\id_ed25519 -r MenuBuilder/frontend/dist/* user1@87.242.100.34:/home/user1/MenuBuilder/frontend/dist/
     ```
   - Пересборка и перезапуск `menubuilder-backend`:
     ```bash
     ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker compose -f /home/user1/compose.yaml build menubuilder-backend && sudo docker compose -f /home/user1/compose.yaml up -d --no-deps menubuilder-backend"
     ```
   - Проверка наличия роута:
     ```bash
     ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "curl -i -X POST http://127.0.0.1:8000/api/v1/video/devices/773/control/keepalive"
     ```
     (Ожидается 401/403 авторизации, но НЕ 404 Not Found).
