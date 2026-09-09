# Промпт 3.2: MenuBuilder (BFF + frontend) — Video UI трансляций, единая lease через app1, право «Видеонаблюдение»

Ты — Senior Full-stack инженер. Работаешь автономно в репозитории `D:\repo\platerra\Public\etranprocessing`: `MenuBuilder/backend`, `MenuBuilder/frontend`, при необходимости `shared/etranprocessing_db` и Alembic в `ProcessingBackend/backend/alembic/versions/` (единственное место миграций; текущий head — `026_add_terminal_gauge_states.py`; права viewer вводились в `025_add_user_permissions_and_role4.py`). Смежный сервис IoT-платформы `app1` (`iot-rpc-rest-app`) **уже обновлён и задеплоен** — его внутренний API считаешь готовым контрактом (§3); код `app1` не меняешь, только читаешь `D:\work\iot.leo4.ru\iot-rpc-rest-app\docs\remote-input-protocol.md` и `app-service/api/internal_v1/remote_input.py`, `diagnostics.py` для сверки. Терминальную часть (`tools/`) не трогаешь.

Правила: `AGENTS.md`, `ProcessingBackend/GUIDELINES.md`; Python 3.14, `uv`, `uv run ruff check --fix`, `uv run ruff format`, `uv run pyright`, `uv run pytest` в `MenuBuilder/backend`; frontend — TypeScript strict, `npm run build`. Не трогай `FRONT/`, `BACK/`, `tools/`, `sqlFileExample/`, `stored-procedures/`. Секреты — только `.env`. Деплой — только по явному указанию (в отчёте опиши шаги).

---

## 1. Контекст: что уже существует (изучи в первую очередь)

### 1.1 Медиа-цепочка
`ffmpeg.exe` (терминал) → RTP UDP `127.0.0.1:5004/5005` → `leo4proxy --rtp-tunnel` (mTLS) → `l4media-nginx:8443` → `l4media-ingress` (Control API :9100 `GET/PUT/DELETE /routes/{sn}`, `/stats`) → Janus `janus.plugin.streaming` RTP mountpoint → WebRTC → браузер через nginx `location /janus-ws` (`nginx-configs/port_3000.conf`, upstream `l4media-janus:8188`, **без авторизации**). Документы: `l4media/ARCHITECTURE.md`, `docs/etran_arch-l4media-streaming-architecture.md`, `docs/etran_arch-remote-input-control.md`.

### 1.2 Backend (`MenuBuilder/backend/app`)
- `routers/video.py` — `POST /api/v1/video/devices/{device_id}/session` (`_ensure_ingress_route`, `_ensure_janus_mountpoint`, `get_device_ports(device_id)` — mountpoint детерминирован по `device_id`), `GET .../status` (`_get_ingress_status`). Доступ: `require_tenant_context` + `_verify_device_access` — **любая роль 1–4, без права и без lease**.
- `routers/video_control.py` — Remote Control BFF: `GET .../control/status`, `POST .../control/lease`, `POST .../control/keepalive`, `DELETE .../control/lease/{lease_id}`, `POST .../control/events` (`ControlEventRequest`), `WS .../control/ws/{lease_id}` (прокси в app1). Роли захардкожены `role_id in (1,2,3)` (`require_remote_control_user`, `get_ws_user`, `control_ws_proxy`).
- `services/iot_client.py` — `IotPlatformClient.remote_input_status/acquire_lease/keepalive/release/move/click/ws_url`, `_get_headers(org_id, user=...)` (передаёт `X-User-Id`, `X-Role`, `X-Org-Id`), `_handle_app1_http_error`.
- `security/permissions.py` — `ROLE_SUPERUSER=1, ROLE_ADMIN=2, ROLE_USER=3, ROLE_VIEWER=4`; `PERMISSION_*` (`monitoring:view`, `reports:*`, `billing:view`, `settings:terminals:view`), `ALL_PERMISSIONS`, `VALID_PERMISSION_CODES`; `require_permission(code)` (роли 1–3 всегда проходят, роль 4 — по `user["permissions"]`), `require_readonly_guard`, `require_tenant_admin`.
- `routers/settings_users.py` — role 3 управляет пользователями role 4 и их `permissions` (валидация по `VALID_PERMISSION_CODES`).
- `routers/auth.py` (login/logout/refresh), `auth.py` (`get_current_user`, `require_tenant_context`, `resolve_org_id`, `create_access_token`), `user_store.py` (`UserRecord.permissions`, `UserSession`), модель `User.permissions` — `shared/etranprocessing_db/models/auth.py`.
- Тесты: `tests/test_video_control.py`, `tests/test_admin_users.py`, `tests/test_user_auth_and_sessions.py`.

### 1.3 Frontend (`MenuBuilder/frontend/src`)
- `routes/video-surveillance.tsx` — Video UI (маршрут `video` в `App.tsx`, **без permission-guard**): выбор устройства, `createVideoSession` (`api/video.ts`), `JanusStreamingClient` (`api/janusClient.ts`), опрос статуса, Remote Control (`hooks/useRemoteControl.ts`, `components/RemoteControlOverlay.tsx`), проверка роли захардкожена (`role_id === 4` → без RC).
- `routes/devices/DeviceConsoleTab.tsx` — Console: WS **напрямую** в app1 `wss://.../api/internal/v1/diagnostics/ws/devices/{sn}?org_id=` (`getDiagnosticsWsUrl` в `api/devices.ts`).
- `utils/permissions.ts` — `PERMISSION_*`, `ALL_PERMISSIONS`, `PERMISSION_LABELS`, `hasPermission`, `getDefaultRouteForViewer`; `App.tsx` — `ViewerGuard`.
- `routes/settings/UserSettingsPage.tsx`, `api/settingsUsers.ts` — галочки прав для viewer'ов.
- `session/SessionContext` — текущий пользователь/JWT.

### 1.4 Проблемы, которые ты закрываешь
- Viewer может создавать video-session; права на видео нет; роли захардкожены.
- Console минует lease на стороне BFF (app1 теперь требует lease scope `console`).
- Нет UI выбора источника (display/камера), start/stop/switch, состояния трансляции.
- `/janus-ws` открыт; mountpoint предсказуем.
- Lease не освобождается при logout; app1 теперь требует `X-Session-Id`.

---

## 2. Утверждённые архитектурные решения (не пересматривать)

1. Единая монопольная lease терминала живёт в app1 (`LeaseRegistry`, scope `console|view|stream|input`). BFF **не** хранит свою копию lease и не создаёт параллельный механизм; он только проксирует, проверяет права и передаёт идентичность (`X-User-Id`, `X-Role`, `X-Org-Id`, `X-Session-Id`).
2. Console и трансляция/Remote Control взаимоисключены (обеспечивает app1); BFF обязан получать console-lease перед подключением Console и показывать явный отказ.
3. Право «Видеонаблюдение» — код `video:view`, строгий вариант: даёт viewer (role 4) доступ к Video UI, статусу и просмотру **уже запущенной** трансляции через lease scope `view`; не даёт start/stop/switch, Remote Control, Console, ввод. Multi-viewer co-viewing исключён.
4. Просмотр через Janus должен быть доступен только держателю lease: mountpoint защищается `pin`, выдаваемым в ответе `POST .../session` только держателю lease.
5. Никаких проверок «только в UI»: каждая операция защищена зависимостью FastAPI на backend и/или проверкой app1.

---

## 3. Контракт app1 (готов, использовать как есть)

Базовый префикс внутреннего API `/api/internal/v1/remote-input`, авторизация `X-Internal-Service-Key` (как сейчас в `IotPlatformClient`) + заголовки `X-User-Id`, `X-Role`/`X-Role-Id`, `X-Org-Id`, **`X-Session-Id`** (обязателен; для WS — заголовок или query `session_id`).

| Метод | Назначение | Ответы |
|---|---|---|
| `GET /devices/{sn}/status` | `lease{scope, owner_role, owner_masked, expires_at, stream_instance_id, selected_desktop_id}`, `presence.inventory{displays[],cameras[]}`, `presence.stream{state,mode,source_id,stream_instance_id,profile,reason,restart_count}`, `online/stale` | 200 |
| `POST /devices/{sn}/lease {scope, ttl_sec?}` | получить lease | 201; 409 `lease_taken{owner_role, owner_masked, scope, expires_at}`; 409 `stream_not_running`; 403 `scope_not_allowed` |
| `POST /lease/{id}/scope {scope}` | смена scope владельцем | 200/403/409 |
| `POST /lease/{id}/keepalive`, `DELETE /lease/{id}` | как сейчас | 200/404 |
| `DELETE /leases/by-owner {user_id, session_id?}` | освободить все lease владельца (logout) | 200 |
| `GET /devices/{sn}/inventory?refresh=0|1` | inventory из presence / запрос у терминала | 200/504 |
| `POST /lease/{id}/stream/start {mode:"desktop"|"usb-camera", source_id, profile}` | старт/переключение | 200 `{stream_instance_id, result: started|already_running|switched, state}`; 409 с `nack.code` (`source_not_allowed`, `source_unavailable`, `session_unavailable`, `busy_transition`, `ffmpeg_missing`, `invalid_profile`…); 504 `terminal_timeout` |
| `POST /lease/{id}/stream/stop` | стоп | 200 `{result: stopped|already_stopped}` |
| `POST /lease/{id}/pointer/move`, `/mouse/click` | ввод (только `stream_mode=desktop`) | 200/409 `input_not_allowed_in_camera_mode`/`desktop_mismatch`/`stream_mismatch` |
| `POST /lease/{id}/key {kind, vk, text?}` | клавиатура | 200/409/422 |
| `WS /ws/lease/{id}` | события `ack/nack`, `stream_state`, `lease_revoked{reason}` | — |
| `WS /api/internal/v1/diagnostics/ws/devices/{sn}?lease_id=&session_id=` | Console; требует lease scope `console` того же владельца | close `4409` при конфликте |

Роли в app1: `superuser|admin|user` → `view|stream|input`; `console` → только `superuser`; `viewer` → только `view`. Фактические схемы сверь с `docs/remote-input-protocol.md` в репозитории app1 и с `openapi.json` работающего `app1`; при расхождении — следуй реальному API и зафиксируй в отчёте.

---

## 4. Права: `video:view` («Видеонаблюдение»)

Backend:
- `security/permissions.py`: `PERMISSION_VIDEO_VIEW = "video:view"` → `ALL_PERMISSIONS`, `VALID_PERMISSION_CODES`.
- Миграция не требуется (`User.permissions` — список строк); проверь `shared/etranprocessing_db/models/auth.py`. Если добавляешь таблицы (например, аудит действий с трансляцией) — миграция `027_*` в `ProcessingBackend/backend/alembic/versions/`, рестарт `menubuilder-backend` после применения — описать в отчёте.
- Матрица (реализовать зависимостями FastAPI):

| Действие | role 1/2/3 | role 4 + `video:view` | role 4 без права |
|---|---|---|---|
| Video UI, `GET .../status`, `GET .../inventory`, `GET .../stream/state` | ✔ | ✔ | ✘ 403 |
| `POST .../session` (Janus mountpoint + `pin`) | ✔ при lease любого scope ≥ `view` | ✔ только при собственной lease `view` | ✘ |
| Lease `view` | ✔ | ✔ (app1 выдаст только при `running` и свободной lease) | ✘ |
| Lease `stream`, `POST .../stream/start|stop` | ✔ | ✘ 403 | ✘ |
| Lease `input`, `control/events`, `control/ws`, `key` | ✔ (app1 дополнительно требует `mode=desktop`) | ✘ | ✘ |
| Console lease / `DeviceConsoleTab` | только superuser | ✘ | ✘ |
| Назначение `video:view` (`settings_users.py`) | role 3 (как для остальных прав) | — | — |

- Заменить захардкоженные `role_id in (1,2,3)` в `video_control.py` (`require_remote_control_user`, `get_ws_user`, `control_ws_proxy`) на явные зависимости по матрице; в `video.py` — `require_permission(PERMISSION_VIDEO_VIEW)` + проверка lease через app1 `status`.
- `settings_users.py` — логика без изменений; тест, что `video:view` принимается/возвращается.

Frontend:
- `utils/permissions.ts`: `PERMISSION_VIDEO_VIEW`, метка `"Видеонаблюдение"`, в `ALL_PERMISSIONS`, учесть в `getDefaultRouteForViewer` (`/video`).
- `App.tsx`: маршрут `video` → `ViewerGuard permission={PERMISSION_VIDEO_VIEW}`.
- `UserSettingsPage.tsx`: галочка из `ALL_PERMISSIONS` + tooltip «Доступ к разделу Видеонаблюдение и просмотр запущенной трансляции. Не даёт управлять трансляцией и терминалом».
- Убрать захардкоженные проверки роли в `video-surveillance.tsx`; опираться на `hasPermission` + ответы backend.

---

## 5. Backend BFF: маршруты и сервисы

`services/iot_client.py`: добавить методы под все строки таблицы §3 (`remote_input_acquire_lease(scope=...)`, `remote_input_change_scope`, `remote_input_release_by_owner`, `remote_input_inventory`, `remote_input_stream_start/stop`, `remote_input_key`), `_get_headers` — добавить `X-Session-Id` (идентификатор сессии пользователя: `jti`/`session_id` из JWT — см. `auth.py`/`user_store.UserSession`; если в токене нет — добавить claim при выдаче токена, сохранив обратную совместимость). `_handle_app1_http_error` — пробрасывать 409/403/504 с телом `{code, ...}` без потери `code`.

`routers/video_control.py` (развивать существующий префикс `/api/v1/video/devices/{device_id}`):
- `GET .../control/status` — расширенный статус (lease, inventory, stream) — роли по матрице.
- `POST .../control/lease {scope}` — 201 | 409 `{code:"lease_taken", owner_role, owner_masked, scope, expires_at}` | 409 `stream_not_running` | 403. Без неявного захвата/частичного подключения.
- `POST .../control/scope {scope}`, `POST .../control/keepalive`, `DELETE .../control/lease/{lease_id}` — как сейчас + новые.
- `GET .../inventory?refresh=`; `POST .../stream/start {mode, source_id, profile}` → `{stream_instance_id, result, state}`; `POST .../stream/stop`; `GET .../stream/state` (presence.stream + существующий `_get_ingress_status`).
- `POST .../control/events` — существующие move/click + новый тип `key` (`kind, vk, text`); `WS .../control/ws/{lease_id}` — прокси событий `stream_state`, `lease_revoked`.
- Console: `POST .../control/lease {scope:"console"}` (только superuser) — используется фронтом перед WS в app1; `DeviceConsoleTab` передаёт `lease_id` и `session_id` в query WS.

`routers/video.py`:
- `POST .../session` — требует `require_permission(PERMISSION_VIDEO_VIEW)` и активную lease вызывающего (проверка через app1 `status`: `lease.owner` = текущий user/session). В `_ensure_janus_mountpoint` задавать `pin` (случайный, хранить в памяти процесса per mountpoint/`stream_instance_id`, обновлять при новой lease `stream`), возвращать `pin` в `VideoSessionResponse` только держателю lease; `JanusStreamingClient` использует его в `watch`. Исследовать `janus.plugin.streaming` (`l4media/janus/janus.plugin.streaming.jcfg`) — `pin` поддерживается для mountpoint; если mountpoint уже создан без pin — пересоздать (`destroy`+`create`) при первом запросе новой lease. Остаточный риск `/janus-ws` без auth зафиксировать; при возможности добавить в `nginx-configs/port_3000.conf` ограничение (например, `auth_request` в BFF) — только если это не ломает существующий поток, иначе описать как follow-up.
- `GET .../status` — роли по матрице.

`routers/auth.py`: при `logout` и инвалидации сессии → `remote_input_release_by_owner(user_id, session_id)` (best-effort, с логированием).

---

## 6. Frontend: Video UI и Console

`routes/video-surveillance.tsx`, `api/video.ts`, `hooks/useRemoteControl.ts`, `components/RemoteControlOverlay.tsx`, `api/janusClient.ts`:
- Панель источников из inventory: дисплеи (имя, разрешение, primary, policy `input/view/denied`), камеры (имя, доступность); radio-выбор **одного** источника; кнопки «Запустить», «Остановить», «Переключить» (переключение = `stream/start` с новым `source_id`, показывать стадии `stopping → starting → running`); профиль (`default|low`).
- Индикатор состояния (`state`, `reason`, `restart_count`); баннеры `source_unavailable`, `session_unavailable`, `failed`, `ffmpeg_missing`, `terminal_timeout`.
- Lease UX: явный отказ «Терминал занят: <owner_role/owner_masked>, до <expires_at>» без автоповтора; keepalive; release при `pagehide`/`beforeunload` и при уходе со страницы; обработка `lease_revoked` (остановить плеер, показать причину).
- Remote Control: доступен только при lease scope `input` и `mode=desktop`; передавать события через BFF (desktop_id/stream_instance_id подставляет сервер); при `usb-camera` overlay отключён с пояснением; клавиатура — захват клавиш при фокусе на видео (`keydown/keyup` → `key`), экранная подсказка.
- Viewer с `video:view`: только просмотр; берёт lease `view` при выборе устройства; при `stream_not_running` — «Трансляция не запущена оператором»; при `lease_taken` — отказ.
- `JanusStreamingClient.watch` с `pin`.

`routes/devices/DeviceConsoleTab.tsx`, `api/devices.ts`: перед WS — `POST .../control/lease {scope:"console"}`; `getDiagnosticsWsUrl(sn, orgId, leaseId, sessionId)`; при `409` — причина; при закрытии вкладки/размонтировании — release; keepalive на время открытой Console.

`api/settingsUsers.ts`, `UserSettingsPage.tsx` — см. §4.

---

## 7. Порядок работы

1. Изучи §1 и контракт §3 (документация app1 + `openapi.json` работающего `app1`). Составь карту изменений.
2. Backend: права (§4), `IotPlatformClient` + `X-Session-Id`, маршруты (§5), Janus `pin`, logout-release.
3. Frontend (§4, §6).
4. Тесты (§9), `ruff`/`pyright`/`pytest`, `npm run build`.
5. Документация: `docs/etran_arch-remote-input-control.md` (lease scope, режимы, Console под lease), `docs/etran_arch-l4media-streaming-architecture.md` (pin, кто создаёт mountpoint), `MenuBuilder/README` при необходимости.
6. Отчёт (§10). Деплой — только по указанию; описать шаги (сборка frontend `npm run build` → bind-mount `frontend/dist`; `menubuilder-backend` — rebuild/restart контейнера; при новой миграции — `alembic upgrade head` в `processing-backend`, затем `docker restart menubuilder-backend`).

---

## 8. Критерии готовности

- Второй браузерный пользователь получает явный отказ на lease любого scope, start/stop/switch, ввод, Console, создание Janus-сессии — покрыто тестами BFF (мок `IotPlatformClient` возвращает 409) и проверено вручную с реальным `app1`.
- Матрица §4 реализована зависимостями и покрыта тестами; viewer без права — 403 на все video-эндпоинты и redirect в UI; viewer с правом — Video UI, `view`-lease, просмотр running-потока, никаких управляющих кнопок и 403 на управляющие эндпоинты.
- `POST .../session` доступен только держателю lease и возвращает `pin`; плеер подключается с `pin`.
- Logout/закрытие вкладки → release; `lease_revoked` корректно обрабатывается UI.
- Console работает только после console-lease; при активной трансляции — отказ с причиной.
- `ruff check`, `ruff format --check`, `pyright` чисто; `uv run pytest` зелёный; `npm run build` успешен; старые права/роли работают без миграции данных.

---

## 9. Требования к тестированию

- `tests/test_video_control.py` + новый `tests/test_video_stream_permissions.py`: матрица §4 для всех эндпоинтов и WS (`create_access_token` для ролей 1–4 с/без `video:view`), формат 409 `lease_taken`/`stream_not_running`, проброс `nack.code`, `X-Session-Id` в заголовках к app1 (проверка мока), logout → `release_by_owner`, `pin` в ответе только держателю lease, console-lease только superuser.
- `tests/test_admin_users.py`/settings users: `video:view` сохраняется/возвращается; неизвестный код — ошибка как сейчас.
- `tests/test_user_auth_and_sessions.py`: claim/идентификатор сессии присутствует в токене и совместим со старыми токенами.
- Frontend: `npm run build`; при наличии vitest/jest — unit-тесты `permissions.ts` и редьюсера состояния трансляции.
- Ручной сценарий (с реальным `app1`, описать в отчёте): оператор берёт lease `stream`, стартует desktop; второй оператор — отказ на lease/start/Console; viewer с `video:view` видит поток после release оператора, не видит кнопок; переключение desktop→camera; RC в camera-mode отключён; закрытие вкладки → lease освобождена (`status`).

---

## 10. Формат отчёта

Markdown, разделы: 1) карта изменений (файлы/назначение); 2) матрица прав и где проверяется (backend/WS/frontend); 3) интеграция с app1 (используемые эндпоинты, заголовки, обработка ошибок, расхождения с §3); 4) UI (экраны/состояния, отказы); 5) тесты и результаты (`pytest`, `ruff`, `pyright`, `npm run build`); 6) ручная проверка; 7) известные ограничения и риски (`/janus-ws`, in-memory lease в app1, переходные режимы); 8) миграции и шаги деплоя.
