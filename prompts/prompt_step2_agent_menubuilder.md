# PROMPT AGENT 2.1 — `MenuBuilder`: Порядок Route-before-Start, привязка сессии и устранение потери начальных кадров WebRTC

Ты — Senior Full-Stack инженер (Python / FastAPI + React 19 / TypeScript). Работаешь автономно в репозитории `D:\repo\platerra\Public\etranprocessing`, каталоги `MenuBuilder/backend` и `MenuBuilder/frontend`.

Твоя цель — реализовать архитектурный паттерн **Route-before-Start** (задача R6 / P1 из E2E-архитектуры) для устранения задержки старта видео (до 2 с) и черного экрана. Маршрут в `l4media-ingress` (`PUT /routes/{sn}`) и точка вещания Janus Streaming Mountpoint должны быть полностью настроены **до** отправки команды запуска `stream_start` на терминал. Когда FFmpeg на терминале начинает передачу по L4RTP/1, его самые первые IDR/SPS/PPS пакеты должны немедленно попадать в готовый Janus mountpoint, не теряясь в `unrouted`.

Соблюдай правила репозитория (`ProcessingBackend/GUIDELINES.md`, `AGENTS.md`):
- Backend: Python 3.14, FastAPI, SQLAlchemy 2.0 async, Pydantic v2. Линтеры и проверки:
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

## 1. Контекст и архитектурная проблема

### 1.1 Текущий порядок вызовов (проблема задержки и `unrouted`)
В текущей реализации UI (`MenuBuilder/frontend/src/routes/video-surveillance.tsx`):
```text
1. acquireControlLease (scope="stream") -> аренда получена.
2. startDeviceStream -> отправляет stream_start в app1 -> l4desk -> процесс FFmpeg запускается немедленно и шлет RTP в ingress.
3. [ОКНО ПОТЕРИ КАДРОВ]: первые RTP пакеты приходят на l4media-ingress, где еще НЕТ маршрута для этого SN. Ingress дропает пакеты и увеличивает unrouted_packets.
4. createVideoSession -> UI только сейчас шлет запрос в BFF: BFF регистрирует PUT /routes/{sn} в ingress и создает mountpoint в Janus.
5. Janus подключается, но начальный IDR-кадр (ключевой) уже потерян! WebRTC-плеер ждет следующего IDR-кадра до 2 секунд (при GOP=2s), показывая черный экран.
```

### 1.2 Целевой порядок (Route-before-Start)
```text
1. acquireControlLease (scope="stream" или "view") -> аренда получена.
2. createVideoSession -> BFF конфигурирует PUT /routes/{sn}?rtp=...&rtcp=... в ingress и создает mountpoint в Janus с PIN. Маршрут и Janus ГОТОВЫ слушать UDP-порты.
3. startDeviceStream -> терминал получает команду stream_start, запускает FFmpeg.
4. Первые же RTP/RTCP-дейтаграммы от FFmpeg через leo4proxy и ingress мгновенно пробрасываются в Janus.
5. Браузер через JanusStreamingClient подключается к mountpoint и сразу получает первый IDR-кадр без задержки.
```

---

## 2. Задачи разработки

### 2.1 Backend: Жизненный цикл сессии и PIN в `MenuBuilder/backend/app/routers/video.py`
1. **Привязка PIN mountpoint к `lease_id`**:
   - В функции `get_or_create_mountpoint_pin`:
     - Сейчас кэш PIN завязан на `stream_instance_id`. При раннем создании сессии `stream_instance_id` еще не сгенерирован (`None`).
     - Модифицировать логику кэширования PIN: привязать его к `device_id` и `lease_id` (или сохранить PIN для устройства, пока активна текущая аренда).
     - Если `create_video_session` вызывается повторно в рамках той же активной аренды (например, зрителем или при переподключении), должен возвращаться уже созданный PIN без уничтожения существующего mountpoint в Janus.
2. **Идемпотентность `_ensure_janus_mountpoint`**:
   - Если mountpoint для `device_id` уже существует в Janus с правильными портами и PIN, переиспользовать его (`reusing`), не уничтожая (`destroy`) без необходимости.
3. **Уничтожение/очистка сессии**:
   - В эндпоинте остановки / освобождения аренды обеспечить удаление временного PIN и возможность опционального вызова Janus `destroy` mountpoint при окончательном освобождении устройства.
4. **Улучшение телеметрии в `_get_ingress_status`**:
   - В ответе `/stats` от `l4media-ingress` учитывать поле `last_rtp_at` / `last_activity`, если оно возвращается, для точного определения активности трансляции.

### 2.2 Frontend: Реорганизация запуска в `MenuBuilder/frontend/src/routes/video-surveillance.tsx`
1. **Перестановка шагов в `handleOperatorStart`**:
   - **Шаг 1**: Запросить аренду (`acquireControlLease(selectedDevice.device_id, "stream")`).
   - **Шаг 2**: Подготовить сессию просмотра **до** запуска FFmpeg:
     ```typescript
     setStatusText("Подготовка медиаканала (маршрутизация)...");
     const sessionData = await createVideoSession(selectedDevice.device_id);
     ```
   - **Шаг 3**: Отправить команду запуска видеопотока на терминал:
     ```typescript
     setStatusText("Запуск видеопотока на терминале...");
     const startRes = await startDeviceStream(selectedDevice.device_id, {
       mode: mode as "desktop" | "usb-camera",
       source_id: source_id || "0",
       profile: selectedProfile,
       lease_id: leaseRes.lease_id,
     });
     ```
   - **Шаг 4**: Подключить WebRTC-клиент Janus:
     ```typescript
     setStatusText("Подключение к медиасерверу...");
     const wsUrl = getJanusWsUrl(sessionData.janus_ws);
     const client = new JanusStreamingClient({
       wsUrl,
       mountpointId: sessionData.mountpoint_id,
       pin: sessionData.pin,
       // ... callbacks
     });
     await client.start();
     janusClientRef.current = client;
     ```
2. **Обработка ошибок и откат при сбое старта**:
   - Если `startDeviceStream` возвращает ошибку (таймаут терминала, `source_unavailable`, `offline` и т.д.):
     - Перехватить исключение в блоке `catch`.
     - Вызвать аварийную очистку: остановить клиента Janus (`janusClientRef.current?.stop()`), освободить аренду (`releaseControlLease`), очистить состояние плеера.
     - Отобразить пользователю понятное сообщение об ошибке через `formatVideoError`.
3. **Синхронизация с режимом Viewer (`handleViewerConnect`)**:
   - В `handleViewerConnect` (для роли наблюдателя) поток на терминале уже запущен оператором.
   - Проверить, что `createVideoSession` корректно возвращает существующий mountpoint и PIN активной трансляции, и подключение зрителя происходит без прерывания трансляции оператора.

### 2.3 Тестирование бэкенда (`MenuBuilder/backend/tests/`)
- Добавить/расширить тесты в `tests/test_video.py`:
  - Проверка создания сессии при наличии активной lease со scope `stream` до вызова `stream_start`.
  - Проверка стабильности PIN при повторном вызове `create_video_session` в рамках одной lease.
  - Проверка вызова `_ensure_ingress_route` с корректными RTP/RTCP портами устройства.

---

## 3. Сборка, верификация и деплой

### 3.1 Локальная проверка
1. Backend (`MenuBuilder/backend`):
   ```bash
   uv run ruff check --fix app
   uv run ruff format app
   uv run pyright app
   uv run pytest
   ```
2. Frontend (`MenuBuilder/frontend`):
   ```bash
   npm run build
   ```
   Убедись в отсутствии ошибок TypeScript и успешной сборке бандла `dist/`.

### 3.2 Доставка на прод-сервер `87.242.100.34`
1. **Frontend (Live Mount)**:
   Артефакты SPA монтируются контейнером `nginx-default` из `/home/user1/MenuBuilder/frontend/dist/`.
   Доставка артефактов из PowerShell:
   ```bash
   scp -i d:\.ssh\id_ed25519 -r MenuBuilder/frontend/dist/* user1@87.242.100.34:/home/user1/MenuBuilder/frontend/dist/
   ```
2. **Backend**:
   Перезапуск сервиса `menubuilder-backend` (SSH с флагом `-n`):
   ```bash
   ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker restart menubuilder-backend"
   ```
3. **Проверка работы**:
   ```bash
   ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker compose -f /home/user1/compose.yaml ps menubuilder-backend"
   ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker logs --tail=50 menubuilder-backend"
   ```

---

## 4. Формат отчёта

По завершении сформируй Markdown-отчёт:
1. **Карта изменений**: какие файлы изменены в `MenuBuilder/backend/` и `MenuBuilder/frontend/`.
2. **Описание обновленного потока старта**: как реализован Route-before-Start и откат ошибок.
3. **Результаты тестов и линтеров**: вывод ruff, pyright, pytest и `npm run build`.
4. **Статус деплоя**: доставка файлов фронтенда и логи перезапуска `menubuilder-backend`.
