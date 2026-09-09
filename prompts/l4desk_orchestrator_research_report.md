# Отчёт по исследованию репозитория: видеотрансляция, Remote Control, Console, lease, права, l4desk/l4superv/FFmpeg

Дата: 2026-09-09. Основание: `prompts/prompt_for_l4desk_orchestrator.md`.
Результат: изолированные промпты, порядок выполнения — `prompts/prompt_3_1_iot_rpc_rest_app_unified_lease.md` (смежный репозиторий `iot-rpc-rest-app`/`app1`, под ключ с деплоем — **первым**), затем `prompts/prompt_3_2_menubuilder_video_permissions.md` (MenuBuilder BFF + frontend), `prompts/prompt_4_1_tools_l4superv_ffmpeg_deployment.md` (tools-супервизор, Job Object, дистрибуция и инсталлятор FFmpeg) и `prompts/prompt_4_2_tools_l4desk_ffmpeg_orchestration.md` (интерактивный агент l4desk, инвентаризация, ввод и оркестрация FFmpeg).

---

## 1. Что изучено

| Область | Расположение | Технологии |
|---|---|---|
| Tenant/admin портал (BFF, Video UI, Remote Control, права) | `MenuBuilder/backend`, `MenuBuilder/frontend` | FastAPI, SQLAlchemy async, React 19 + Ant Design |
| Общие ORM-модели | `shared/etranprocessing_db` (`models/auth.py`: `User.permissions`) | SQLAlchemy 2.0 |
| Alembic-миграции (единственный владелец) | `ProcessingBackend/backend/alembic/versions/` (head: `026_add_terminal_gauge_states.py`; права viewer: `025_add_user_permissions_and_role4.py`) | Alembic |
| IoT-платформа `app1` (MQTT/RPC, remote input, diagnostics) | внешний репозиторий `D:\work\iot.leo4.ru\iot-rpc-rest-app\app-service` (в `compose.yaml` — сервис `app1`, `context: ./iot-rpc-rest-app`) | FastAPI, aiomqtt |
| Медиа-контур | `l4media/` (`compose.yaml`, `ARCHITECTURE.md`, `ingress/`, `janus/`, `nginx/`) + `docs/etran_arch-l4media-streaming-architecture.md` | Nginx stream mTLS, C-ingress, Janus streaming plugin |
| Nginx фронта | `MenuBuilder/nginx.conf`, `nginx-configs/port_3000.conf` (`/janus-ws`, `/api/internal/v1/diagnostics/`) | nginx |
| Терминальные утилиты | `tools/l4desk`, `tools/l4superv`, `tools/leo4proxy`, `tools/dist`, правила в `AGENTS.md` (раздел «Key Rules for Tools Development», MQTT-типы клиентов `main_app`/`extra_service`/`svc_desk`) | C/Win32, MSVC `/MT`, x86+x64 |
| Документация архитектуры | `docs/etran_arch-remote-input-control.md`, `iot-rpc-rest-app/docs/remote-input-protocol.md`, `remote-diagnostics-protocol.md`, `docs/terminal-tools-user-guide.md` | Markdown |
| Дистрибутив FFmpeg | `D:\ffmpeg` — FFmpeg **9.0.1 full_build gyan.dev, shared, x64-only** (`bin/ffmpeg.exe` + `avcodec-63.dll`, `avdevice-63.dll`, `avfilter-12.dll`, `avformat-63.dll`, `avutil-61.dll`, `swresample-7.dll`, `swscale-10.dll`, `ffprobe.exe`, `ffplay.exe`; ~239 МБ `bin/`; `include/`, `lib/`, `doc/`, `presets/` в поставку не нужны) | — |

Каталоги `FRONT/`, `BACK/`, `sqlFileExample/`, `stored-procedures/` не рассматривались (правила проекта).

---

## 2. Карта существующих реализаций

### 2.1 Терминальная сторона (`tools/`)

- **`tools/l4superv/src/`** — Windows-служба-супервизор.
  - `orchestrator.c` — цикл watchdog: находит активную консольную сессию (`sp_get_active_console_session`), запускает `l4desk.exe` (`<base>\l4desk\l4desk.exe`, fallback `\x86\`, `\x64\`) с аргументами `--run --presence-interval 30` (переопределяемо `cfg->l4desk_args`), перезапускает с backoff (`g_l4desk_backoff_sec`), останавливает при отсутствии/смене сессии через именованное событие `Global\L4Desk_Stop_<SN>` + `sp_stop(..., 3000 ms)`. Есть `orchestrator_get_l4desk_status(pid, session)`.
  - `session_proc.c` — `WTSQueryUserToken` + `CreateProcessAsUserW` (`winsta0\default`, `CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_BREAKAWAY_FROM_JOB`), `sp_stop` = событие → ожидание → `TerminateProcess`. **Job Object не используется** — дочерние процессы l4desk (будущий ffmpeg) сейчас не гарантированно завершаются.
  - `installer_main.c` (`l4install`) — распаковка `tools.zip` в `C:\l4tools` через `zip_extract_all(zip, dest, arch)`, создание `l4desk\log`, регистрация службы; `zip_extractor.c` — miniz, при распаковке пропускает каталог противоположной архитектуры и срезает сегмент `x86|x64` из пути.
  - `pack_zip.cmd` — staging `obj\staging\<tool>\{x86,x64}\...` → `bin\tools.zip` → копии в `tools\l4superv\tools.zip`, `tools\tools.zip`, `tools\dist\tools.zip`; рядом `tools\dist\l4install_x86.exe|x64.exe` + `.cmd`.
  - `config.c`, `state_mgr.c`, `mosquitto_conf.c`, `proxy_client.c`, `hardware_fingerprint.c` — конфиг, состояние, локальный Mosquitto, работа с leo4proxy.
- **`tools/l4desk/src/`** — агент в пользовательской сессии (MQTT-клиент `svc_desk`, собственная минимальная реализация MQTT 3.1.1 `mqtt_protocol.c` + `mqtt_client.c`, локальный Mosquitto `127.0.0.1:1883`).
  - `main.c` — single-instance mutex `Local\L4Desk_SingleInstance`, stop-event, наблюдатель `Global\L4Desk_Stop_<SN>`, `mqtt_client_run(&config, stop_event)`.
  - `ctl_protocol.c/.h` — envelope v1 для `srv/<SN>/ctl` → `dev/<SN>/ctl`: `presence` (`status`, `desktop_available`, `ScreenMetrics` virtual screen), `ack`/`nack` (`command_id`, `lease_id`, `code`, `message`, `terminal_time_ms`), `ctl_handle_command`. Команды: `pointer_move`, `mouse_click`. `dedup_cache.c` — идемпотентность по `command_id`.
  - `desktop_state.c/.h` — `desktop_is_interactive_available`, `desktop_get_screen_metrics` (только виртуальный экран целиком), `desktop_get_current_session_id`.
  - `input_inject.c/.h` — `input_inject_move/click` (SendInput по абсолютным координатам виртуального экрана).
  - `config.h` — `L4DeskConfig` (`mqtt_host/port`, `proxy_http_port 18443`, `client_id svc_desk`, `sn`, интервалы, `log_file C:\l4tools\l4desk\log\l4desk.log`, `run_mode`, `console_mode`), `sn_discovery.c`, `log.c`, `json_min.c`.
  - **Нет**: inventory дисплеев/камер, запуска ffmpeg, Job Object, per-display захвата, проверки клавиатуры (только мышь).
- **`tools/leo4proxy`** — mTLS-прокси терминала; `--rtp-tunnel` принимает RTP/RTCP на UDP `127.0.0.1:5004/5005` и туннелирует в `l4media-nginx:8443` (L4RTP/1). Примеры ручного запуска ffmpeg: `examples/ffmpeg_rtp_tunnel_example.cmd` (`-f dshow ... -c:v libx264 -preset ultrafast -tune zerolatency -b:v 800k -f rtp rtp://127.0.0.1:5004?rtcpport=5005`), `ffmpeg_stream_example.cmd`. Ищут ffmpeg в `PATH`/`D:\ffmpeg`/`C:\ffmpeg` — **запрещённые для целевого решения источники**.
- Правила `AGENTS.md`: каждый tool — изолированный подкаталог `C:\l4tools\<tool>`, статическая сборка `/MT` x86+x64 через `build.cmd`, обязательная стадия в `pack_zip.cmd` и обработка в `installer_main.c`; MQTT-клиент `l4desk` = тип `svc_desk` (presence `dev/{SN}/ctl`, LWT).

### 2.2 Медиа-контур (`l4media/`)

Цепочка: `ffmpeg.exe` (терминал) → RTP UDP 5004/5005 → `leo4proxy --rtp-tunnel` → mTLS `l4media-nginx:8443` → `l4media-ingress` (:9000 данные, :9100 Control API `GET/PUT/DELETE /routes/{sn}`, `/stats`) → Janus `janus.plugin.streaming` RTP mountpoint (UDP 6000/6001+) → WebRTC → браузер через `/janus-ws` (nginx `port_3000.conf` → `l4media-janus:8188`).
Один mountpoint на устройство (`get_device_ports(device_id)` в `MenuBuilder/backend/app/routers/video.py`, слоты `video_port_base + 2*slot`).

### 2.3 Серверная сторона MenuBuilder (BFF)

- `app/routers/video.py` — `POST /api/v1/video/devices/{device_id}/session` (создаёт ingress-route и Janus mountpoint, возвращает `janus_ws`, `mountpoint_id`), `GET .../status` (статистика ingress). Доступ: `require_tenant_context` + проверка org → **любая роль 1–4**, без права `video:*`, без lease.
- `app/routers/video_control.py` — Remote Control: `GET /devices/{id}/control/status`, `POST .../control/lease`, `POST .../control/keepalive`, `DELETE .../control/lease/{lease_id}`, `POST .../control/events`, `WS /devices/{id}/control/ws/{lease_id}` (прокси в app1). Роли захардкожены `role_id in (1,2,3)` (`require_remote_control_user`, `get_ws_user`).
- `app/services/iot_client.py` — `IotPlatformClient.remote_input_*` (status/acquire/keepalive/release/move/click/ws_url) → внутренний API app1 `/api/internal/v1/remote-input/...` с заголовками `X-User-Id`, `X-Role`, `X-Org-Id`.
- `app/security/permissions.py` — роли (`ROLE_SUPERUSER=1, ROLE_ADMIN=2, ROLE_USER=3, ROLE_VIEWER=4`), коды прав (`monitoring:view`, `reports:*`, `billing:view`, `settings:terminals:view`), `ALL_PERMISSIONS`/`VALID_PERMISSION_CODES`, зависимости `require_permission(code)`, `require_readonly_guard` (запрет мутаций для роли 4), `require_tenant_admin`.
- `app/routers/settings_users.py` — role 3 управляет пользователями role 4 (`POST/PUT /settings/users`, валидация `permissions` по `VALID_PERMISSION_CODES`).
- `app/user_store.py` — `UserRecord.permissions`, `app/auth.py` — JWT, `get_current_user`, `require_tenant_context`, `resolve_org_id`.
- Тесты: `tests/test_video_control.py`, `tests/test_admin_users.py`, `tests/test_user_auth_and_sessions.py`.
- Console (Diagnostics) **не проходит через BFF**: браузер подключается к `wss://.../api/internal/v1/diagnostics/ws/devices/{sn}?org_id=` напрямую в app1 (nginx `location /api/internal/v1/diagnostics/`), `getDiagnosticsWsUrl` в `frontend/src/api/devices.ts`.

### 2.4 Frontend MenuBuilder

- `src/routes/video-surveillance.tsx` — Video UI: выбор устройства, `createVideoSession`, `JanusStreamingClient` (`src/api/janusClient.ts`, `src/api/video.ts`), опрос статуса, встроенный Remote Control (`useRemoteControl`, `RemoteControlOverlay`), проверка роли захардкожена (`role_id === 4` → без RC).
- `src/hooks/useRemoteControl.ts`, `src/components/RemoteControlOverlay.tsx` — lease UX (acquire/keepalive/release, WS, coordinate mapping по `object-fit`; см. `docs/etran_arch-remote-input-control.md` §3–4).
- `src/routes/devices/DeviceConsoleTab.tsx` — Console (WS в app1, доступна только superuser по правилам app1).
- `src/utils/permissions.ts` — `PERMISSION_*`, `ALL_PERMISSIONS`, `PERMISSION_LABELS`, `hasPermission`, `getDefaultRouteForViewer`; `src/App.tsx` — `ViewerGuard`, маршрут `video` без permission-guard.
- `src/routes/settings/UserSettingsPage.tsx` — UI назначения прав viewer'ам (галочки по `ALL_PERMISSIONS`).

### 2.5 IoT-платформа `app1` (`D:\work\iot.leo4.ru\iot-rpc-rest-app\app-service`)

- `core/remote_input/leases.py` — `LeaseRegistry` (in-memory, `LeaseRegistryProtocol`: `acquire(org_id, device_id, sn, owner_user_id, owner_role, ttl_sec)`, `touch`, `revoke`, `get_active(sn)`, `cleanup_expired`, revocation-подписки, `mark_ws_connected/disconnected`; `LeaseConflictError`). **Одна lease на SN** — готовая база для единой interactive-lease.
- `core/remote_input/service.py`, `publisher.py`, `mqtt_bridge.py`, `pending.py`, `presence.py`, `rate_limit.py`, `schemas.py` — публикация envelope v1 в `srv/<SN>/ctl`, ожидание ack/nack из `dev/<SN>/ctl`, presence-реестр (stale 90 с), rate limiting.
- `api/internal_v1/remote_input.py` — REST + `WS /ws/lease/{lease_id}`; роль из `X-Role`/`X-Role-Id`, viewer отклоняется (4403).
- `core/diagnostics/` (`sessions.py` `DiagnosticsSessionRegistry`, `service.py`, `commands.py`, `mqtt_bridge.py`) и `api/internal_v1/diagnostics.py` (`WS /ws/devices/{sn}`, только superuser, без владельца/эксклюзивности) — Console. **Второй независимый механизм сессий**, не связан с `LeaseRegistry`.
- Ограничение: state in-memory ⇒ `WEB_CONCURRENCY=1` (см. `docs/remote-input-protocol.md` §6–7).
- Тесты: `tests/core/remote_input/*`, `tests/core/diagnostics/*`, `tests/api/v1/test_remote_input_api.py`, `test_diagnostics_ws_auth.py`.

---

## 3. Что переиспользуется (решения утверждены заказчиком 2026-09-09)

1. **Канал управления трансляцией** — расширение существующего control-plane `srv/<SN>/ctl` / `dev/<SN>/ctl` (envelope v1, `command_id`, ack/nack, dedup). RPC `tsk/req/rsp` (l4con) **не используется**. Новые типы сообщений: `inventory_get`, `stream_start`, `stream_stop`, расширенный `presence` (inventory display/camera, состояние стрима, `streamInstanceId`, `selectedDesktopId`, `selectedSessionId`).
2. **Единая lease** — `LeaseRegistry` app1 становится единой «interactive lease» терминала (scope: `console | stream | input`). Console и стрим **взаимно исключены**: активная Console блокирует получение stream/input lease и наоборот. Diagnostics WS проверяет lease через `X-User-Id`; Console остаётся на прямом WS в app1 (не переносится в BFF).
3. **Право `Видеонаблюдение`** — строгий вариант (a): код `video:view` даёт viewer доступ к Video UI и статусу/просмотру **уже запущенной** трансляции только через lease в режиме `view-only`; не даёт start/stop/switch, Remote Control, Console. Это означает, что viewer без запущенного оператором потока видео не увидит — зафиксировано как бизнес-правило.
4. **FFmpeg** — отдельный артефакт `ffmpeg.zip` рядом с `tools.zip` в `tools/dist`, структура `ffmpeg/x64/...` и `ffmpeg/x86/...` (совместимо с `zip_extract_all`), установка в `C:\l4tools\ffmpeg`, минимальный набор файлов (`ffmpeg.exe` + необходимые DLL, без `ffplay`, `doc`, `include`, `lib`). Нужны x64 **и** x86.

---

## 4. Риски, пробелы, противоречия

| # | Риск / пробел | Где | Влияние |
|---|---|---|---|
| R1 | Нет Job Object в `l4superv`/`l4desk`; `CREATE_BREAKAWAY_FROM_JOB` при запуске l4desk | `session_proc.c` | Orphan `ffmpeg.exe` при аварии l4desk/l4superv, logout, session switch |
| R2 | `desktop_get_screen_metrics` — только виртуальный экран; нет `EnumDisplayMonitors`/`EnumDisplayDevices`, нет стабильного display id | `desktop_state.c` | Невозможно выбрать один display, отрицательные координаты не учтены |
| R3 | Ввод не привязан к display/streamInstance; проверяется только `lease_id` | `ctl_protocol.c`, `input_inject.c` | Ввод в «чужой» desktop при смене источника |
| R4 | `/janus-ws` в nginx открыт без JWT; mountpoint_id детерминирован (`device_id`) | `nginx-configs/port_3000.conf`, `video.py` | Обход монопольности просмотра при знании mountpoint |
| R5 | Console (diagnostics) — отдельный реестр сессий, только superuser, без владельца | app1 `core/diagnostics` | Console и Remote Control могут работать параллельно у разных пользователей |
| R6 | Роли в `video_control.py` захардкожены (1–3); `video.py` доступен роли 4 без права | MenuBuilder | Viewer может создавать video session |
| R7 | app1 state in-memory, `WEB_CONCURRENCY=1` | app1 | Lease не переживает рестарт app1; горизонтальное масштабирование невозможно (вне scope) |
| R8 | `D:\ffmpeg` — 9.0.1 gyan.dev shared **x64**, требует Windows 10 / UCRT; официальных win32-сборок нет (gyan.dev/BtbN — только win64) | `D:\ffmpeg` | Терминалы x86 / Win7 Embedded / POSReady 7 не покрываются текущим дистрибутивом |
| R9 | ffmpeg-примеры ищут бинарь в `PATH`/`D:\ffmpeg`/`C:\ffmpeg` | `tools/leo4proxy/examples/*.cmd` | Противоречит требованию запуска только из `C:\l4tools\ffmpeg` |
| R10 | Один mountpoint/route на device; переключение источника требует перезапуска ffmpeg с тем же RTP-таргетом | `video.py`, `l4media` | Необходим controlled switch: stop → подтверждённый exit → start |
| R11 | `l4desk` использует только мышь (`move/click`); клавиатура отсутствует | `input_inject.h` | Требование «мышь/клавиатура» частично покрыто; клавиатура — расширение |

Решение по R8: для x86 использовать (в порядке предпочтения) сборку BtbN `FFmpeg-Builds` с целью `win32` (docker `./build.sh win32 gpl`), либо community-сборки `defisym/FFmpeg-Builds-Win32`, либо архивные Zeranoe win32 static 4.4.x (без UCRT, совместимы с Win7). Точный выбор и проверка на целевой ОС — задача tools-агента с явной фиксацией версии и SHA-256 в артефактах.

---

## 5. Открытые вопросы бизнес-правил (зафиксировано)

- Многопользовательский read-only просмотр **исключён**: любая трансляция монопольно принадлежит одному браузерному пользователю (держателю lease); viewer с `video:view` может присоединиться только как `view-only` держатель, если lease свободна и поток уже запущен. Если требуется одновременный просмотр несколькими пользователями — это отдельное решение продукта.
- Клавиатурный ввод — включён в scope tools-агента как расширение `ctl`-протокола (`key_event`), серверный агент добавляет соответствующий проброс.
