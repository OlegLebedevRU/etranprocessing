# PROMPT 2 — `etranprocessing`: MenuBuilder Video UI remote mouse control + `tools/l4desk` + запуск через `l4superv`

> Автономный промпт для агента-исполнителя. Не требует чтения чата. Факты подтверждены исследованием кода репозитория `etranprocessing` и проекта `iot-rpc-rest-app` (2026-09-09).
> Предусловие: **PROMPT 1 уже внедрён и задеплоен** — `iot-rpc-rest-app` (контейнер `app1`) предоставляет Internal API `/api/internal/v1/remote-input/*` и MQTT control pool `srv/<SN>/ctl` / `dev/<SN>/ctl`. Контракт зафиксирован в разделе 2 — считать его готовым, `iot-rpc-rest-app` не менять.

---

## 0. Роль и результат

Ты — senior full-stack/Win32 инженер экосистемы Leo4. Реализовать, собрать, протестировать и развернуть:

1. **MenuBuilder backend (BFF)** — эндпоинты `/api/v1/video/devices/{device_id}/control/*` (REST + WebSocket-прокси) с проверкой JWT/org/roles/device ownership, вызывающие только Internal API `app1`.
2. **MenuBuilder frontend** — расширение страницы `/video`: кнопка «Включить управление», статус агента/lease, безопасная обработка pointer/click внутри фактической области картинки, нормализация `0..65535`, UX-сообщения.
3. **`tools/l4desk`** — новый изолированный C/Win32 агент `l4desk.exe` (plain MQTT к localhost Mosquitto, `SendInput`, ACK/NACK, retained presence, dedup), сборка x86/x64 + Win7 SP1.
4. **`tools/l4superv`** — оркестрация `l4desk` в **активной интерактивной сессии** (не Session 0) через helper для скрытых консольных процессов; включение в `tools.zip`.
5. **Guidelines** — дополнить `AGENTS.md` §10 третьим типом MQTT-клиента (`svc_desk`).
6. **Деплой** MenuBuilder без перезапуска l4media/RabbitMQ/app1/processing; развёртывание l4desk на тестовый терминал; end-to-end проверка.

Порядок E2E: `Video UI → MenuBuilder BFF → app1 Internal API (WS) → MQTT srv/<SN>/ctl → leo4proxy → local Mosquitto → l4desk.exe → Win32 SendInput → dev/<SN>/ctl ACK → app1 → BFF → UI`.

---

## 1. Scope и запреты

**Разрешённый scope**
```text
MenuBuilder/backend/**            (роутер, iot_client, config, тесты)
MenuBuilder/frontend/**           (страница /video, api/video.ts, новые компоненты/хуки)
tools/l4desk/**                   (новый подпроект)
tools/l4superv/**                 (оркестрация l4desk, pack_zip.cmd), tools/build_dist*.cmd (добавить l4desk)
nginx-configs/port_3000.conf      (ТОЛЬКО блок /api/v1/video/ — см. §4.3; правка требует подтверждения владельца)
AGENTS.md §10                     (дополнение guidelines)
docs/**                           (новая документация)
```

**Запрещено**
- менять `iot-rpc-rest-app` (Internal API — готовый контракт); при несоответствии контракта — остановиться и эскалировать владельцу;
- менять l4media/Janus/ingress, `tools/leo4proxy`, `tools/mosquitto`, генерацию `mosquitto.conf` (bridge уже прокидывает `srv/<SN>/#` in и `dev/<SN>/#` out);
- вводить RabbitMQ/MQTT-клиент или credentials в MenuBuilder; подключать браузер к MQTT/RabbitMQ;
- отдавать `X-Internal-Service-Key` фронтенду; формировать MQTT-топики в MenuBuilder;
- передавать команды мыши через RTP/L4RTP, Janus DataChannel, WebRTC signalling, `evt`, RPC `tsk/req/rsp/res`, integration bus;
- хардкодить device `773` / SN `a4b0000773c82116d210826` (только как параметры ручного acceptance-теста);
- реализовывать keyboard/clipboard/shell/right click/scroll/drag/`mouse_down`/`mouse_up`;
- запускать `l4desk` как обычную Session 0 службу; открывать входящие сетевые порты на терминале; использовать OpenSSL/TLS внутри l4desk;
- скачивать/вкладывать сторонние DLL (`mosquitto.dll`, `paho`) — референсные клиенты zero-dependency (§2.5);
- перезапускать `rabbitmq`, `app1`, `pg`, `l4media-*`, `processing-backend`; менять другие location'ы nginx.

Roles MenuBuilder: `1 superuser`, `2 admin`, `3 user`, `4 viewer`. Управление разрешено только ролям 1–3; viewer — только просмотр статуса.

---

## 2. Подтверждённые факты и готовые точки расширения

### 2.1. Internal API `app1` (контракт из PROMPT 1, base `http://app1:8000`, префикс `/api/internal/v1/remote-input`)

Заголовки на каждый вызов: `X-Internal-Service-Key`, `X-Org-Id`, `X-Role` (имя роли или `1..4`), `X-Role-Id`, `X-User-Id`.

| Метод и путь | Ответ |
|---|---|
| `GET /devices/{sn}/status` | `200 {"sn","agent":{"online","desktop_available","screen":{"virtual_x","virtual_y","virtual_width","virtual_height"},"last_seen_at","stale"},"lease":{"active","lease_id","owner_user_id","expires_at"}}` |
| `POST /devices/{sn}/lease` body `{"owner_user_id","owner_role"}` | `201 {"lease_id","sn","device_id","org_id","owner_user_id","created_at","expires_at","keepalive_sec":15,"ws_path":"/api/internal/v1/remote-input/ws/lease/{lease_id}"}`; `409 {"detail":"lease busy","owner_user_id","expires_at"}`; `403` |
| `POST /lease/{lease_id}/keepalive` | `200 LeaseResponse`; `404`; `403` |
| `DELETE /lease/{lease_id}` | `204`; `404` |
| `POST /lease/{lease_id}/pointer-move` `{"x","y"}` | `202`; `429`; `409` |
| `POST /lease/{lease_id}/mouse-click` `{"x","y","button":"left","client_ref"}` | `200 {"command_id","client_ref","result":"injected|nack|unconfirmed","code","message","latency_ms"}` |
| `WS /ws/lease/{lease_id}` | inbound: `{"type":"pointer_move","x","y"}`, `{"type":"mouse_click","x","y","button":"left","client_ref"}`, `{"type":"keepalive"}`, `{"type":"release"}`; outbound: `hello`, `presence`, `click_result`, `error{code: rate_limited|invalid_message|lease_inactive|payload_too_large}`, `lease_revoked{reason}`; close codes `4403/4404/4409` |

Координаты — `int 0..65535` виртуального рабочего стола. Lease TTL 60 с, keepalive ≤ 15 с (любое inbound WS-сообщение = keepalive). Закрытие WS со стороны BFF = release lease.

### 2.2. MenuBuilder backend (`MenuBuilder/backend/app`)

| Что | Где | Факт |
|---|---|---|
| Видео-роутер | `routers/video.py` | `router = APIRouter(prefix="/api/v1/video")`; `_verify_device_access(device_id, user, db) -> Terminal` (404 / 403 по `terminal.org_id != resolve_org_id(user)`, superuser — везде); `terminal.sn`; `Depends(require_tenant_context)`, `Depends(get_db)`; подключён в `main.py:126` `app.include_router(video.router)` |
| Auth | `auth.py` | `get_current_user` → dict `{sub, org_id, role, role_id, is_superuser, permissions,…}`; `require_tenant_context` (org_id>0); роли 1..4 (`ROLE_VIEWER=4`); JWT из cookie `accessToken` или Bearer; nginx также инжектирует `X-User-Id/X-Org-Id/X-User-Role/X-Role-Id` |
| Клиент app1 | `services/iot_client.py` `IotPlatformClient` | `settings.internal_api_base_url` (`LEO4_INTERNAL_API_BASE_URL=http://app1:8000`), `settings.internal_service_key_value`, `settings.iot_rpc_timeout_seconds`; `_get_headers(org_id)` ставит `X-Internal-Service-Key`, `X-Org-Id` (роль/user **не** передаёт — добавить); «simulated» режим при пустом base_url |
| Config | `config.py` `Settings` (`env_file=".env"`, `extra="ignore"`) | `l4media_ingress_url`, `l4media_janus_url`, `video_port_*` — рядом добавить настройки control |
| Тесты | `tests/` (pytest, `conftest.py`, `test_route_ownership.py`, `test_multi_tenancy.py`, `test_nginx_header_contract.py`) | `uv run pytest`, `uv run ruff check --fix app`, `uv run ruff format app`, `uv run pyright app` |
| Deploy | `docs/ops_run-devops-runbook.md` | сервер `87.242.100.34` (`ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34`), compose `/home/user1/compose.yaml`: `scp -r MenuBuilder/backend/* user1@…:/home/user1/MenuBuilder/backend/` → `sudo docker compose -f /home/user1/compose.yaml build menubuilder-backend && sudo docker compose -f /home/user1/compose.yaml up -d --no-deps menubuilder-backend`; frontend: `npm run build` → `scp -r dist/* …:/home/user1/MenuBuilder/frontend/dist/` (без рестарта); nginx: `scp nginx-configs/port_3000.conf …:/home/user1/nginx-configs/` → `sudo docker exec nginx-default nginx -t && … nginx -s reload` |

### 2.3. MenuBuilder frontend (`MenuBuilder/frontend/src`, React 19 + TS + antd 6 + axios)

| Что | Где | Факт |
|---|---|---|
| Страница | `routes/video-surveillance.tsx` (route `video` в `App.tsx:195`) | слева динамический список `getDevices(orgId)` (`api/devices.ts`, `DeviceListItem{device_id, sn, status}`), справа `<video ref={videoRef} style={{objectFit:"contain"}}>` в контейнере `position:relative; height:560px; background:#000`; `handleStart/stopSession` (Janus), `useEffect` cleanup при unmount, `handleSelectDevice` останавливает сессию; `useSession()` → `user.org_id` (роль — см. `session/SessionContext`) |
| API | `api/video.ts` | `client` (axios, `baseURL "/api"`, cookie/bearer, ошибки → `Error(detail)`), пути `/v1/video/devices/{id}/session`, `/session/status`; `getJanusWsUrl()` строит `wss://host/…` |
| Janus | `api/janusClient.ts` | `JanusStreamingClient({wsUrl, mountpointId, onRemoteTrack})` — не трогать |

### 2.4. nginx (`nginx-configs/port_3000.conf`, владелец — etranprocessing)

- `location /api/v1/video/` → `$menubuilder_upstream` с `auth_jwt_location COOKIE=accessToken`, инъекцией `X-User-Id/X-Org-Id/X-User-Role/X-Role-Id`, **без** `Upgrade`/`Connection` и без длинных таймаутов → WebSocket через BFF сейчас **не пройдёт**;
- образцы WS-блоков: `location /api/internal/v1/diagnostics/` и `location /janus-ws` (`proxy_http_version 1.1; proxy_set_header Upgrade $http_upgrade; proxy_set_header Connection "upgrade"; proxy_read_timeout 3600s; proxy_send_timeout 3600s`).

### 2.5. Терминальные tools (`tools/`)

| Что | Где | Факт |
|---|---|---|
| Референс MQTT-клиента с подпиской | `tools/l4con` (`src/mqtt_client.c`, `mqtt_protocol.c`, `config.c`, `service_mgr.c`, `build.cmd`, `README.md`) | zero-dependency C: MQTT 3.1.1 поверх WinSock2, `SUBSCRIBE srv/{SN}/tsk` QoS1, publish `dev/{SN}/out|res`, LWT/presence в `dev/{SN}/svc`, default client_id `<SN>_extra`, reconnect loop, `/MT` static, x86/x64 |
| Референс минимального клиента | `tools/leo4-simple-svc-mqtt` | тот же стек; SN через WinHTTP `GET http://127.0.0.1:18443/_leo4/sn` (fallback `DEVICE_SN`, `--sn` только для `--console`) |
| SN источник | `tools/leo4proxy/src/http_proxy.c:504-516` | `GET /_leo4/sn` (plain text), `GET /_leo4/info?format=json` на `127.0.0.1:18443` |
| Local broker | `tools/l4superv/src/mosquitto_conf.c` | `listener 1883 127.0.0.1`, `allow_anonymous true`, без ACL; bridge `platerra-upstream` → `127.0.0.1:18883` (leo4proxy), `bridge_protocol_version mqttv50`, `topic dev/<SN>/# out 1`, `topic srv/<SN>/# in 1` → `ctl` проходит в обе стороны, retain сохраняется (Mosquitto default `bridge_outgoing_retain true`). **Ничего не менять** |
| Супервизор | `tools/l4superv` (`src/orchestrator.c`, `service_mgr.c`, `config.c`, `state_mgr.c`, `supervisor_main.c`, `installer_main.c`, `pack_zip.cmd`, `README.md`) | служба `L4Superv` под `NT AUTHORITY\SYSTEM`, watchdog SCM-служб `Leo4Proxy/Mosquitto/L4Con`, конфиг `C:\l4tools\l4superv.json` (`services.{leo4proxy,mosquitto,l4con}.auto_start`), `state.json` (sn, thumbprint), опрос `GET /_leo4/info`; `l4install.exe` + `tools.zip` (каталоги `leo4proxy/ mosquitto/ l4con/ l4pin/ l4superv/`) |
| Сборка дистрибутива | `tools/build_dist.cmd`, `tools/build_dist_win7.cmd` (`/D_WIN32_WINNT=0x0601 /SUBSYSTEM:CONSOLE,6.01`, x86), `tools/dist_win7_sp1/` | добавить l4desk по образцу l4con |
| Видеозахват | `docs/etran_arch-l4media-streaming-architecture.md §4.3` | ffmpeg `-f gdigrab -i desktop` (весь virtual desktop) с `scale=W:H:force_original_aspect_ratio=decrease,pad=W:H:(ow-iw)/2:(oh-ih)/2` → **чёрные поля могут быть внутри кадра** |
| Guidelines | `AGENTS.md` §10 «MQTT Client Development Rules» | типы `main_app` (`dev/{SN}/app`) и `extra_service` (`dev/{SN}/svc`) — `svc` занят l4con: last-wins retained → l4desk **не должен** публиковать в `svc` |

---

## 3. MenuBuilder backend — BFF (`MenuBuilder/backend/app`)

### 3.1. Config (`config.py`)
```python
remote_control_enabled: bool = True
remote_control_ws_connect_timeout_sec: float = 5.0
remote_control_click_timeout_sec: float = 7.0     # > app1 click_ack_timeout (5 с)
```
`.env.example`: `REMOTE_CONTROL_ENABLED=true`. Internal API URL/ключ — только backend-side (`LEO4_INTERNAL_API_BASE_URL`, `INTERNAL_SERVICE_KEY`), фронтенду не отдавать.

### 3.2. `services/iot_client.py`
- `_get_headers(org_id, *, user=None)` — дополнить `X-Role` (`user["role"]`), `X-Role-Id` (`user["role_id"]`), `X-User-Id` (`user["sub"]`) при наличии `user`.
- Новые методы: `remote_input_status(sn, org_id, user)`, `remote_input_acquire_lease(sn, org_id, user)` (409 → `HTTPException(409, detail={"detail":"lease busy","owner_user_id":…,"expires_at":…})`), `remote_input_keepalive(lease_id, …)`, `remote_input_release(lease_id, …)` (404 игнорировать), `remote_input_click(lease_id, x, y, client_ref, …)`, `remote_input_move(lease_id, x, y, …)`; `remote_input_ws_url(lease_id) -> "ws://app1:8000/api/internal/v1/remote-input/ws/lease/{lease_id}"` (схема `http→ws`, `https→wss`). Без base_url — «simulated»: status `agent.online=false`, lease → 503 `remote control unavailable`.
- Ошибки app1 маппить: 403 → 403 «Доступ к управлению устройством запрещён», 409 → 409, 429 → 429, 5xx/RequestError → 502.

### 3.3. Роутер `routers/video_control.py` (новый, `prefix="/api/v1/video"`, подключить в `main.py` рядом с `video.router`)

Общая зависимость `require_remote_control_user`: `require_tenant_context` + `role_id in (1,2,3)` (viewer → 403 «Управление доступно только операторам»); `_verify_device_access` импортировать из `routers/video.py` (не дублировать).

| Метод и путь | Действие |
|---|---|
| `GET  /devices/{device_id}/control/status` | доступ (любая роль 1–4) → `iot_client.remote_input_status(terminal.sn)` → `ControlStatusResponse{agent, lease:{active, mine, owner_user_id, expires_at}}` (`mine = owner_user_id == user.sub`) |
| `POST /devices/{device_id}/control/lease` | роль 1–3 → acquire → `ControlLeaseResponse{lease_id, expires_at, keepalive_sec, ws_path:"/api/v1/video/devices/{device_id}/control/ws/{lease_id}"}` |
| `POST /devices/{device_id}/control/keepalive` body `{lease_id}` | keepalive → 200 |
| `DELETE /devices/{device_id}/control/lease/{lease_id}` | release → 204 |
| `POST /devices/{device_id}/control/events` body `{type:"pointer_move"|"mouse_click", x, y, button?:"left", client_ref?}` + `lease_id` | REST-fallback: move → 202; click → `ClickResult` (ждать ≤ `remote_control_click_timeout_sec`) |
| `WS   /devices/{device_id}/control/ws/{lease_id}` | WebSocket-прокси к app1 (§3.4) |

Pydantic: `x,y: int = Field(ge=0, le=65535)`, `button: Literal["left"] = "left"`, `type: Literal["pointer_move","mouse_click"]`, `client_ref: str | None = Field(max_length=64)`; `extra="forbid"`.

### 3.4. WebSocket-прокси
- Аутентификация WS: `get_current_user` не работает с `WebSocket` напрямую — реализовать `get_ws_user(websocket)`: JWT из cookie `accessToken` (или `Authorization` header / `?token=` только если `VITE_AUTH_TRANSPORT=bearer`), `decode_token`, те же правила ролей/org, что и в `get_current_user`; при ошибке → `close(4401)`.
- Проверить `_verify_device_access` и роль 1–3; lease должен принадлежать этому пользователю (`status.lease.owner_user_id == user.sub`) → иначе `close(4403)`.
- Открыть upstream WS к app1 (`websockets`/`httpx-ws` — выбрать библиотеку, уже присутствующую в `uv.lock`/pyproject; если нет — добавить `websockets` с фиксацией версии) с заголовками `_get_headers(org_id, user=user)`. Двунаправленный relay (`asyncio.gather` двух задач): browser→app1 пропускать только валидные `pointer_move|mouse_click|keepalive|release` (ревалидировать Pydantic, всё остальное → `error{code:"invalid_message"}` браузеру, upstream не трогать); app1→browser пересылать как есть.
- Любое закрытие (браузер ушёл, upstream закрылся, `lease_revoked`) → закрыть вторую сторону; на выходе из хендлера — `remote_input_release(lease_id)` best-effort (idempotent).
- Логи: `lease_id`, `device_id`, `sn`, `org_id`, `user`, причина закрытия, количество click/результаты (без координат движений).

### 3.5. Тесты (`tests/test_video_control.py`, стиль `test_route_ownership.py`/`test_multi_tenancy.py`, моки `iot_client` через `monkeypatch`/`dependency_overrides`)
- 403 cross-tenant (терминал другой org), 403 viewer на lease/events, 200 viewer на status;
- 409 lease busy проброшен с `owner_user_id`; 202 move; click → injected/unconfirmed;
- WS: 4401 без токена, 4403 чужой lease, relay happy-path с fake upstream (мок `websockets.connect`), release вызывается при disconnect;
- `X-Internal-Service-Key` никогда не попадает в ответ/лог фронту (assert по телу ответа).
- Прогнать `uv run pytest`, `uv run ruff check --fix app`, `uv run ruff format app`, `uv run pyright app` в `MenuBuilder/backend`.

---

## 4. MenuBuilder frontend (`MenuBuilder/frontend/src`)

### 4.1. API (`api/video.ts` — дополнить)
`getControlStatus(deviceId)`, `acquireControlLease(deviceId)`, `releaseControlLease(deviceId, leaseId)`, `keepaliveControlLease(deviceId, leaseId)`, `getControlWsUrl(wsPath)` (по образцу `getJanusWsUrl`). Типы: `ControlStatus`, `ControlLease`, `ControlWsOutbound` (`hello|presence|click_result|error|lease_revoked`), `ControlWsInbound`.

### 4.2. Хук `hooks/useRemoteControl.ts` (новый)
Состояние: `idle | acquiring | active | busy(owner) | agent_offline | desktop_locked | error`; `presence`, `lease`, `lastClickResult`. Методы: `enable()` (acquire lease → open WS → ждать `hello`), `disable()` (send `release`, close WS, `DELETE lease` best-effort), `sendMove(x,y)` (throttle 100 мс = ≤10/с, latest-wins: хранить только последнее значение и отправлять по таймеру), `sendClick(x,y)` (сначала финальный `pointer_move` теми же координатами, затем `mouse_click` с `client_ref`; ожидание `click_result` по `client_ref` с таймаутом 7 с → `unconfirmed`). Keepalive: если 10 с не было inbound-сообщений — отправить `{"type":"keepalive"}`. Автоматический `disable()` при: Stop видео, смене устройства, unmount, logout (`notifySessionEvent` `logout`), `lease_revoked`, закрытии WS. Все таймеры и pending-промисы отменяются в cleanup.

### 4.3. Компонент overlay `components/RemoteControlOverlay.tsx`
Абсолютно позиционированный слой поверх `<video>` внутри существующего контейнера (`position:relative`). Вычисление **фактического прямоугольника картинки**:
1. `contentRect` от `object-fit: contain`: по `video.videoWidth/videoHeight` vs `getBoundingClientRect()` контейнера (letterbox/pillarbox исключить).
2. **Внутренний padding кадра ffmpeg**: если известен `presence.screen` (`virtual_width/height`), внутри `contentRect` вычислить центрированный прямоугольник с аспектом virtual desktop (`force_original_aspect_ratio=decrease` + центрированный `pad`) — это и есть `desktopRect`. Если `screen` неизвестен — считать `desktopRect = contentRect` и показывать предупреждение «Геометрия экрана неизвестна — точность клика снижена».
3. Клик/движение за пределами `desktopRect` — игнорировать (не отправлять). Внутри: `nx = round((px - desktopRect.left) / desktopRect.width * 65535)`, аналогично `ny`, clamp `0..65535`.
4. `pointermove` → `sendMove`; `click` (левая кнопка, без модификаторов) → `sendClick`; правая кнопка/контекстное меню/колесо — `preventDefault`, ничего не отправлять; курсор `crosshair` только при `active`.
5. Отрисовать тонкую рамку `desktopRect` (debug-toggle) для визуальной проверки.

### 4.4. Изменения `routes/video-surveillance.tsx`
- Кнопка **«Включить управление»** (иконка `ControlOutlined`) рядом с Старт/Стоп: активна только при `isSessionActive && presence.online && desktop_available && role ∈ {superuser, admin, user}`; для viewer — скрыта; при нажатии — antd `Modal.confirm` с текстом: «Вы управляете мышью удалённого терминала. Действия ограничены мышью, подтверждаются агентом и журналируются.» → `enable()`. В активном состоянии — «Отключить управление» (danger).
- Индикаторы (Badge/Tag) в строке статуса: агент `online/offline/stale`, `desktop_available`, lease `активно до …`/`занято другим оператором (#owner)`. Опрос `getControlStatus` каждые 5 с вместе с существующим `getVideoSessionStatus` пока WS не открыт; при открытом WS — по `presence`-сообщениям.
- Сообщения (antd `message`/`Alert`): «Управление недоступно: агент offline», «Экран терминала заблокирован», «Клик выполнен» (`injected`, с `latency_ms`), «Клик не подтверждён — повторите вручную» (`unconfirmed`), «Клик отклонён агентом: …» (`nack`), «Управление занято другим оператором», «Слишком частые действия» (`rate_limited`).
- `stopSession()` и `handleSelectDevice()` вызывают `disable()` до остановки Janus; unmount cleanup — тоже.
- Список устройств остаётся динамическим; никаких SN/device_id в коде.

### 4.5. Roadmap-заметка (в `docs/` и в комментарии хука)
В будущих версиях флоу «Включить управление» может передавать параметры запуска ffmpeg (разрешение/fps/без `pad`) через control-канал; в alpha не реализуется, координатная модель уже учитывает `presence.screen`.

### 4.6. nginx `port_3000.conf` — обязательная правка для WS через BFF (**согласовать с владельцем до применения**)
Показать владельцу diff и применить только после подтверждения: в `location /api/v1/video/` добавить
```nginx
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_read_timeout 3600s;
        proxy_send_timeout 3600s;
```
(остальные директивы блока и другие location'ы не менять). Обновить `tests/test_nginx_header_contract.py`, если он проверяет этот блок. Применение: `scp` → `sudo docker exec nginx-default nginx -t && sudo docker exec nginx-default nginx -s reload`.

### 4.7. Проверка
`npm run build` без ошибок TS; ручная проверка в браузере: overlay рамка совпадает с картинкой при разных размерах окна; клик по чёрному полю не отправляется; Stop/смена устройства освобождают lease (проверить `GET control/status` → `lease.active=false`).

---

## 5. `tools/l4desk` — терминальный агент удалённого ввода (новый изолированный C/Win32 подпроект)

### 5.1. Структура (по образцу `tools/l4con`)
```text
tools/l4desk/
  src/main.c            # CLI, режимы --console/-f, --run (скрытый режим под l4superv), --version
  src/config.c/.h       # аргументы: --host 127.0.0.1 --port 1883 --proxy-port 18443 --sn (только console) --client-id svc_desk
                        #           --presence-interval 30 --keepalive 30 --reconnect 5 --log <file> --verbose
  src/sn_discovery.c/.h # WinHTTP GET http://127.0.0.1:18443/_leo4/sn с бесконечными ретраями (как l4con); DEVICE_SN/--sn только в console
  src/mqtt_client.c/.h  # MQTT 3.1.1 поверх WinSock2 (скопировать/адаптировать из l4con: CONNECT с LWT, SUBSCRIBE, PUBLISH QoS0/1, PUBACK, PINGREQ, reconnect)
  src/mqtt_protocol.c/.h
  src/json_min.c/.h     # ограниченный парсер/сериализатор JSON (только плоские объекты, строки/числа/bool, schema v1); неизвестные поля → reject
  src/ctl_protocol.c/.h # валидация envelope, формирование ack/nack/presence
  src/input_inject.c/.h # SendInput MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK|MOUSEEVENTF_MOVE, LEFTDOWN/LEFTUP; virtual screen metrics
  src/desktop_state.c/.h# доступность interactive desktop: OpenInputDesktop / SwitchDesktop test, сравнение имени desktop с "Default" (GetUserObjectInformation UOI_NAME), сессия != 0
  src/dedup_cache.c/.h  # bounded LRU (256 записей) command_id -> результат, TTL ≥ expires_at
  src/log.c/.h          # stdout + ротируемый файл (как в l4con)
  res/l4desk.rc, res/resource.h
  build.cmd             # MSVC cl.exe, /MT, x86 + x64 + universal, флаги как в l4con/build.cmd; поддержка WIN7 (см. tools/build_dist_win7.cmd)
  CMakeLists.txt        # по образцу l4con
  README.md, CHANGELOG.md
  l4desk_console.cmd    # запуск в консоли для отладки (--console --sn <SN>)
```
Zero-dependency: WinSock2, WinHTTP, User32 (`SendInput`, `GetSystemMetrics`, `OpenInputDesktop`), Wtsapi32 (`ProcessIdToSessionId`). Без OpenSSL/TLS, без DLL. Не использовать код/бинарники libmosquitto/paho.

### 5.2. MQTT-поведение
- Подключение только к `127.0.0.1:1883` (локальный Mosquitto), `client_id = svc_desk` (локальный идентификатор, наружу не выходит — bridge использует `remote_clientid=<SN>`), `username` не обязателен (`allow_anonymous true`), `keepalive 30`.
- **LWT**: topic `dev/<SN>/ctl`, payload `{"v":1,"type":"presence","agent":"l4desk","status":"offline","desktop_available":false,"timestamp":"<UTC ISO>"}`, `retain=1`, `qos=1`.
- После CONNACK rc=0: `SUBSCRIBE srv/<SN>/ctl` QoS 1 → затем `PUBLISH dev/<SN>/ctl` presence `online` (`retain=1, qos=1`) с текущими `desktop_available` и `screen`.
- Периодически (`--presence-interval 30` с) и при изменении `desktop_available`/геометрии — republish presence (retain=1, qos=1). Не чаще 1 раза в 5 с.
- Штатное завершение (Ctrl+C / `--run` получил stop-сигнал от l4superv через именованное событие `Global\L4Desk_Stop_<SN>` или закрытие консоли): publish presence `offline` retain=1 → `DISCONNECT` → закрыть сокет.
- **Никогда** не публиковать в `dev/<SN>/svc`, `dev/<SN>/app`, `dev/<SN>/evt`, `dev/<SN>/out`, `dev/<SN>/res`.
- Reconnect loop с backoff 5→60 с; после reconnect — заново SUBSCRIBE и presence online.
- Single-instance: именованный мьютекс `Local\L4Desk_SingleInstance` (в пределах сессии).

### 5.3. Обработка команд `srv/<SN>/ctl`
Порядок проверок (любая неудача → NACK с кодом, кроме случаев, где явно указано «молча»):
1. размер ≤ 1024 байт, валидный JSON-объект, `v == 1` → иначе NACK `invalid_payload` (если `command_id` извлечь нельзя — drop молча с warning);
2. `type ∈ {pointer_move, mouse_click}` → иначе NACK `unsupported`;
3. `sn == own SN` → иначе NACK `invalid_sn`;
4. `command_id`, `lease_id` — UUID-формат (36 символов, дефисы) → иначе `invalid_payload`;
5. `expires_at_ms` > now (локальное UTC время в мс; допуск рассинхронизации +2 с) → иначе NACK `expired`;
6. `x,y ∈ [0..65535]`; для click `button == "left"` → иначе `invalid_payload`/`unsupported`;
7. dedup: `command_id` уже в кеше → повторно отправить сохранённый ACK/NACK, **не** выполнять;
8. `desktop_available == false` (locked/Secure Desktop/UAC/не interactive) → NACK `interactive_desktop_unavailable`;
9. выполнить inject; `SendInput` вернул 0 → NACK `inject_failed` (с `GetLastError` в message).

`pointer_move`: только `MOUSEEVENTF_MOVE|ABSOLUTE|VIRTUALDESK` с `dx=x, dy=y` (нормализованные 0..65535 уже соответствуют virtual desktop — `SM_XVIRTUALSCREEN/SM_YVIRTUALSCREEN/SM_CXVIRTUALSCREEN/SM_CYVIRTUALSCREEN` использовать для presence `screen` и для проверки, что virtual desktop не пустой). **ACK на `pointer_move` не отправлять** (best-effort), кроме NACK при `invalid_sn`/`expired` — тоже не отправлять (drop с debug-логом), чтобы не грузить канал.
`mouse_click`: move (те же координаты) → `LEFTDOWN` → `LEFTUP` одним `SendInput` из 3 `INPUT`; затем ACK `{"v":1,"type":"ack","command_id","lease_id","sn","result":"injected","terminal_time_ms"}` QoS 1, retain 0. Результат → dedup-кеш.
Не реализовывать: keyboard, clipboard, shell, файлы, right click, scroll, drag, `mouse_down/up`.

### 5.4. Interactive desktop
- Агент работает в пользовательской сессии (запуск через l4superv, §6). При старте проверить `ProcessIdToSessionId == 0` → лог WARNING «running in session 0 — input injection will fail», presence `desktop_available=false`.
- `desktop_available` = (`OpenInputDesktop(0,FALSE,GENERIC_READ)` успешен И имя desktop == `Default` И `GetForegroundWindow()!=NULL` не обязательно) — пересчитывать каждые 5 с и перед каждым click; при `Winlogon`/Secure Desktop → false.
- Не создавать окон/консоли в режиме `--run` (см. §6 helper): `FreeConsole()` не требуется, если процесс запущен с `CREATE_NO_WINDOW`; логи — в файл `C:\l4tools\l4desk\log\l4desk.log`.

### 5.5. Сборка и поставка
- `build.cmd` — как `tools/l4con/build.cmd` (MSVC `cl.exe`/`rc.exe`, `/MT`, `bin\x86\l4desk.exe`, `bin\x64\l4desk.exe`, `bin\l4desk.exe`); Win7 SP1 — добавить шаг в `tools/build_dist_win7.cmd` (`/D_WIN32_WINNT=0x0601 /SUBSYSTEM:CONSOLE,6.01`, x86) и копирование в staging `l4desk\x86\l4desk.exe` по образцу l4con; добавить в `tools/build_dist.cmd`.
- `tools/l4superv/pack_zip.cmd` — включить каталог `l4desk/` в `tools.zip` (структура §2.5).
- README: назначение, протокол, CLI, security notes (без TLS локально; upstream защищён Mosquitto bridge + leo4proxy mTLS), пример запуска `l4desk.exe --console --sn <SN> --verbose`, инвентарь поставки (только `l4desk.exe`, без DLL).

---

## 6. `tools/l4superv` — оркестрация `l4desk` в активной интерактивной сессии

Служба `L4Superv` работает под SYSTEM — это единственный компонент с правом запускать процесс в чужой сессии. Реализовать **helper скрытых консольных процессов** и интегрировать его в orchestrator/watchdog:

### 6.1. `src/session_proc.c/.h` (новый)
- `DWORD sp_get_active_console_session()` → `WTSGetActiveConsoleSessionId()`; `0xFFFFFFFF`/`0` → «нет интерактивной сессии».
- `BOOL sp_start_in_session(DWORD session_id, const wchar_t* exe, const wchar_t* cmdline, const wchar_t* workdir, PROCESS_INFORMATION* out)`:
  `WTSQueryUserToken(session_id, &hUserToken)` → `DuplicateTokenEx(TOKEN_ALL_ACCESS, SecurityImpersonation, TokenPrimary)` → `CreateEnvironmentBlock(&env, hToken, FALSE)` → `STARTUPINFOW si = {.lpDesktop = L"winsta0\\default", .dwFlags = STARTF_USESHOWWINDOW, .wShowWindow = SW_HIDE}` → `CreateProcessAsUserW(hToken, exe, cmdline, NULL, NULL, FALSE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_BREAKAWAY_FROM_JOB, env, workdir, &si, out)`. Освобождать env/токены. Не показывать окна, не воровать фокус у киоск-приложения (никаких `SetForegroundWindow`).
- `BOOL sp_is_alive(HANDLE hProcess)`; `void sp_stop(PROCESS_INFORMATION*, const wchar_t* stop_event_name, DWORD grace_ms)` → `SetEvent(OpenEvent(stop_event))`, ждать `grace_ms` (3000), затем `TerminateProcess`.
- Требуемые привилегии SYSTEM: `SE_TCB_NAME` (есть у SYSTEM), `SE_ASSIGNPRIMARYTOKEN_NAME`, `SE_INCREASE_QUOTA_NAME` — включить через `AdjustTokenPrivileges` при старте службы (логировать неудачу).
- Win7-совместимость: все используемые API доступны с Vista; линковать `wtsapi32.lib userenv.lib advapi32.lib`.

### 6.2. Интеграция в orchestrator/watchdog
- `l4superv.json`: `"services": { …, "l4desk": { "auto_start": true, "mode": "user_session", "args": "--run --presence-interval 30" } }`; `config.c` — распарсить; `installer_main.c` — генерировать по умолчанию.
- В цикле watchdog (`watchdog_interval_sec`): если `l4desk.auto_start` и есть SN (state `ACTIVE`, как условие запуска L4Con) и активная консольная сессия найдена → убедиться, что процесс жив (`sp_is_alive`) и запущен в **текущей** активной сессии (`ProcessIdToSessionId(pid) == active`); если сессия сменилась (logoff/logon, switch user) → `sp_stop` старого и запуск в новой; если сессии нет — не запускать, лог DEBUG.
- Рестарт при падении с backoff 5/10/30 с (как SCM recovery у других служб). При stop службы `L4Superv` / `--stop` → `sp_stop(l4desk)`; при смене SN (как для L4Con) → перезапуск l4desk.
- `l4superv --status` показывает `l4desk: RUNNING (pid, session N)` / `STOPPED (no interactive session)`.
- `README.md` l4superv: новый раздел «Процессы пользовательской сессии (l4desk)», обновить дерево `C:\l4tools\` (добавить `l4desk\`), таблицу конфига, CHANGELOG.
- Тест на dev-машине: `l4superv --console` при залогиненном пользователе запускает `l4desk` в сессии ≠ 0 (проверить `tasklist /v /fi "imagename eq l4desk.exe"` — колонка Session#), после `logoff/logon` процесс пересоздан; `l4desk.log` фиксирует `desktop_available=true`.

---

## 7. Guidelines: дополнить `AGENTS.md` §10

Добавить третий тип клиента (и учесть его в вопросе «Какой тип MQTT-клиента создаётся: main_app, extra_service или svc_desk?»):

```text
### 3. Сценарий для `svc_desk` (Агент удалённого ввода l4desk)
Client CONNECT (только localhost Mosquitto, client_id = svc_desk):
  will_topic   = dev/{SN}/ctl
  will_payload = {"v":1,"type":"presence","agent":"l4desk","status":"offline","desktop_available":false,"timestamp":"<UTC>"}
  will_retain  = true, will_qos = 1
After CONNACK:
  SUBSCRIBE srv/{SN}/ctl (qos 1)
  PUBLISH dev/{SN}/ctl = presence status=online (retain=true, qos=1), затем каждые 30 с
Normal shutdown:
  PUBLISH dev/{SN}/ctl = presence status=offline (retain=true) → DISCONNECT
Запреты: не публиковать в dev/{SN}/svc|app|evt|out|res; команды/ACK/NACK — без retain; только pointer_move/mouse_click(left).
```
Обновить «Общие требования» (§10.3 → §10.4): `svc_desk` нельзя заменять на `extra_service`, так как `dev/{SN}/svc` занят l4con (retained last-wins). Добавить ссылку на `docs/remote-input-protocol.md` (создаётся в PROMPT 1 в репо iot-rpc-rest-app) и на новую `docs/etran_arch-remote-input-control.md` (§8).

---

## 8. Документация (`docs/`)
1. Новый `docs/etran_arch-remote-input-control.md`: схема video plane vs control plane, матрица ответственности (l4media/Janus, frontend, BFF, app1, RabbitMQ, Mosquitto bridge, l4desk, l4superv), координатная модель (object-fit + padding ffmpeg + virtual desktop), lease/UX-состояния, ограничения alpha, roadmap (Redis для app1 state при `WEB_CONCURRENCY>1`; передача параметров ffmpeg через control flow), troubleshooting (какие логи смотреть: `menubuilder-backend`, `app1` `remote_input.log`, `C:\l4tools\mosquitto\log\mosquitto.log`, `C:\l4tools\l4desk\log\l4desk.log`, `l4superv`).
2. `docs/ops_run-devops-runbook.md`: подраздел деплоя remote control (backend, frontend, nginx-правка по согласованию, `tools.zip`).
3. `tools/USER_GUIDE.md` / `tools/dist_win7_sp1/terminal-tools-user-guide.md`: раздел «l4desk — удалённое управление мышью» (как убедиться, что агент online; что делать при «Экран заблокирован»).
4. `tools/l4desk/README.md`, `tools/l4superv/README.md`, CHANGELOG'и.

---

## 9. Deploy и E2E

### 9.1. Серверная часть (`87.242.100.34`, ключ `d:\.ssh\id_ed25519`, все `ssh` из PowerShell с `-n`)
1. Pre-flight: `sudo docker compose -f /home/user1/compose.yaml ps menubuilder-backend app1 nginx-default`; убедиться, что `app1` отвечает `GET /api/internal/v1/remote-input/devices/<SN>/status` (PROMPT 1 внедрён) — иначе остановиться.
2. Backend: `scp -i d:\.ssh\id_ed25519 -r MenuBuilder/backend/* user1@87.242.100.34:/home/user1/MenuBuilder/backend/` → `sudo docker compose -f /home/user1/compose.yaml build menubuilder-backend && sudo docker compose -f /home/user1/compose.yaml up -d --no-deps menubuilder-backend`. `.env` менять только добавлением `REMOTE_CONTROL_ENABLED=true` (после backup).
3. Frontend: `npm run build` → `scp -r dist/* …:/home/user1/MenuBuilder/frontend/dist/` (без рестарта контейнера).
4. nginx (только после подтверждения владельцем diff §4.6): `scp nginx-configs/port_3000.conf …:/home/user1/nginx-configs/port_3000.conf` → `sudo docker exec nginx-default nginx -t && sudo docker exec nginx-default nginx -s reload`.
5. Запрещено: `docker compose up -d` без `--no-deps`, рестарт `app1|rabbitmq|pg|l4media-*|processing-backend`.
6. Проверки: `sudo docker logs --tail=50 menubuilder-backend`; `curl -k https://dev.leo4.ru:3000/api/v1/video/devices/<id>/control/status` с cookie оператора → 200; WS handshake `wss://dev.leo4.ru:3000/api/v1/video/devices/<id>/control/ws/<lease>` → 101.

### 9.2. Терминал (тестовая Windows-машина, SN задаёт оператор; device `773` — только как acceptance-пример)
1. Собрать `tools/l4desk` (`build.cmd`), `tools/l4superv` (`build.cmd`, `pack_zip.cmd`) и при необходимости `tools/build_dist_win7.cmd`.
2. Доставить `l4desk\l4desk.exe` в `C:\l4tools\l4desk\`, обновить `C:\l4tools\l4superv\l4superv.exe`, добавить блок `l4desk` в `C:\l4tools\l4superv.json`; `l4superv_restart.cmd`. Mosquitto/leo4proxy/L4Con не трогать и не перезапускать.
3. Проверка presence: `tasklist /v | findstr l4desk` (Session# ≠ 0); в `mosquitto.log` — CONNECT `svc_desk`, SUBSCRIBE `srv/<SN>/ctl`; на сервере `GET …/remote-input/devices/<SN>/status` → `agent.online=true, desktop_available=true, screen заполнен`.
4. Убедиться, что `dev/<SN>/svc` presence l4con не изменился (`svc_online` остался).

### 9.3. End-to-end
1. Оператор роли user/admin открывает `https://dev.leo4.ru:3000/video`, выбирает устройство, «Старт», видит трансляцию.
2. «Включить управление» → подтверждение → статус «Управление активно», рамка `desktopRect` совпадает с картинкой.
3. Движение мыши по видео → курсор на терминале двигается (без ACK, ≤10/с).
4. Клик по элементу интерфейса терминала → на терминале выполнен левый клик, UI показывает «Клик выполнен (NN мс)». В логах: `menubuilder-backend` (lease/click), `app1 remote_input.log` (`command_id`, `injected`, latency), `l4desk.log` (`ack injected`).
5. Негатив: клик по чёрному полю — запрос не уходит; заблокировать экран терминала (Win+L) → presence `desktop_available=false`, UI «Экран терминала заблокирован», клик → NACK `interactive_desktop_unavailable`; остановить `l4desk` → LWT offline, UI «Управление недоступно: агент offline».
6. Второй оператор той же org → «Управление занято другим оператором»; оператор другой org → устройство отсутствует в списке / 403 на прямой вызов; viewer → кнопка скрыта, `POST lease` → 403.
7. Stop/смена устройства/закрытие вкладки → `GET control/status` показывает `lease.active=false` в течение ≤ 60 с (немедленно при корректном закрытии WS).
8. В БД app1 `device_events` для устройства не появилось новых записей за время теста (проверить `count(*)` до/после); очередь `evt` без роста.

### 9.4. Rollback
Backend: предыдущая версия каталога + `build/up -d --no-deps menubuilder-backend`; frontend: предыдущий `dist/`; nginx: предыдущий `port_3000.conf` + `nginx -s reload`; терминал: удалить блок `l4desk` из `l4superv.json`, `l4superv_restart.cmd`, `l4desk.exe` остановится (retained presence offline).

---

## 10. Acceptance criteria

1. Пользователь с доступом к устройству видит WebRTC-трансляцию и явно включает управление кнопкой с предупреждением; управление никогда не включается автоматически.
2. Клики отправляются только внутри фактической области картинки (с учётом object-fit и внутреннего padding кадра), координаты нормализованы `0..65535`; движение ≤ 10/с, latest-wins; перед click — финальный move.
3. MenuBuilder проверяет JWT, org, роль (viewer → 403), device ownership и вызывает только Internal API `app1` с `X-Internal-Service-Key/X-Org-Id/X-Role/X-Role-Id/X-User-Id`; ключ не покидает backend; MQTT-топики в MenuBuilder не формируются.
4. WS-канал браузер → BFF → app1 работает через nginx `/api/v1/video/` (после согласованной правки); закрытие любого звена освобождает lease.
5. `l4desk.exe`: zero-dependency, x86/x64 (+ Win7 SP1 x86), client_id `svc_desk`, подписка `srv/<SN>/ctl`, ACK/NACK QoS 1 без retain, presence retained + LWT в `dev/<SN>/ctl`, `svc/app/evt/out/res` не затронуты; dedup по `command_id`; отклонение чужого SN, истёкших, невалидных команд; NACK при заблокированном экране; `SendInput` с `ABSOLUTE|VIRTUALDESK`.
6. `l4superv` запускает `l4desk` скрыто в активной интерактивной сессии (Session ≠ 0), пересоздаёт при смене сессии/падении, корректно останавливает; киоск-приложение не теряет фокус; Mosquitto/leo4proxy/L4Con не затронуты.
7. `AGENTS.md` §10 дополнен типом `svc_desk`; документация §8 создана.
8. E2E §9.3 пройден полностью; pointer/ACK не создают `DeviceEvent` и не грузят `evt/eva`; cross-tenant не может получить lease или отправить команду.
9. `uv run pytest`/`ruff`/`pyright` в `MenuBuilder/backend` и `npm run build` во `frontend` — без ошибок; серверный деплой затронул только `menubuilder-backend`, `frontend/dist`, (по согласованию) `nginx-default`.

