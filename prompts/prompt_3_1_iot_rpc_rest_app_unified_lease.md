# Промпт 3.1 (под ключ, с деплоем): `iot-rpc-rest-app` / `app1` — единая монопольная lease, расширение control-plane `ctl`, Console под lease

Ты — Senior Backend инженер. Работаешь автономно в репозитории `D:\work\iot.leo4.ru\iot-rpc-rest-app` (сервис `app-service`, контейнер `app1`). Это **первый** этап: твой результат — контракт, на который потом опираются BFF (MenuBuilder) и терминальный агент (`l4desk`). Ты должен довести задачу **до продакшн-деплоя `app1`** и подтверждённой проверки.

Соблюдай правила репозитория: `AGENTS.md`, `CONTRIBUTING.md`, `copilot-instructions.md` (Python 3.14, FastAPI, FastStream, SQLAlchemy/asyncpg, Alembic, RabbitMQ MQTT 5; `uv run pytest`, `uv run ruff check .`, `uv run black --check .`). Секреты — только `.env`; `app-service/.env` не выводить в лог/отчёт целиком. Другие репозитории (`etranprocessing`, MenuBuilder, `tools/`) **не изменяешь** — только читаешь для контекста.

---

## 1. Контекст: что уже существует (изучи в первую очередь)

- `app-service/core/remote_input/leases.py` — `Lease`, `LeaseConflictError`, `LeaseRegistryProtocol`, `LeaseRegistry` (in-memory; `acquire(org_id, device_id, sn, owner_user_id, owner_role, ttl_sec)`, `touch`, `revoke`, `get`, `get_active(sn)`, `cleanup_expired`, `subscribe_revocation/unsubscribe_revocation`, `mark_ws_connected/disconnected`). **Одна активная lease на SN** — база единой lease.
- `app-service/core/remote_input/` — `service.py`, `publisher.py` (публикация envelope v1 в `srv/<SN>/ctl`), `mqtt_bridge.py` (приём `ack`/`nack`/`presence` из `dev/<SN>/ctl`), `pending.py` (ожидание ack по `command_id`), `presence.py` (stale 90 c), `rate_limit.py`, `schemas.py` (`pointer_move`, `mouse_click`, `ack`, `nack`, `presence`).
- `app-service/api/internal_v1/remote_input.py` — REST `GET devices/{sn}/status`, `POST lease`, `POST keepalive`, `DELETE lease/{id}`, `POST pointer/move`, `POST mouse/click`, `WS /ws/lease/{lease_id}`; роль из `X-Role`/`X-Role-Id` (`extract_caller_role`), пользователь из `X-User-Id` (`extract_caller_user_id`), `verify_ws_internal_auth`; viewer → 4403.
- `app-service/api/internal_v1/internal_depends.py` — `verify_internal_service_auth` (`X-Internal-Service-Key` / Bearer, `settings.auth.internal_service_key`, `settings.api_keys`), `is_request_superuser`.
- `app-service/core/diagnostics/` — `sessions.py` (`DiagnosticSession`, `DiagnosticsSessionRegistry`: `register`, `get`, `mark_closing`, `remove`, `remove_all_for_sn`, `route_output`, `cleanup_expired`, `list_active`; глобальный `registry`), `service.py`, `commands.py`, `mqtt_bridge.py`, `schemas.py`; `api/internal_v1/diagnostics.py` — `WS /ws/devices/{sn}` (только superuser, `org_id` из query/заголовков, **без владельца и эксклюзивности**). Это Console. Браузер MenuBuilder подключается к нему **напрямую** через nginx (`location /api/internal/v1/diagnostics/`), минуя BFF.
- Документы: `docs/remote-input-protocol.md` (envelope v1, QoS, lease model, **`WEB_CONCURRENCY=1`** из-за in-memory state), `docs/remote-diagnostics-protocol.md`, `docs/remote-diagnostics-implementation-plan.md`, `docs/manual-app1-deploy-runbook.md`, `docs/deploy-plan.md`.
- Тесты: `app-service/tests/core/remote_input/{test_leases,test_schemas,test_service,test_mqtt_bridge}.py`, `tests/core/diagnostics/*`, `tests/api/v1/test_remote_input_api.py`, `tests/api/v1/test_diagnostics_ws_auth.py`, `tests/core/topologys/test_remote_input_topology.py`.
- Терминальный агент `l4desk` (другой репозиторий, только чтение: `D:\repo\platerra\Public\etranprocessing\tools\l4desk\src\ctl_protocol.c`) сейчас понимает `pointer_move`, `mouse_click`, публикует `presence` (`status`, `desktop_available`, `screen{virtual_x,virtual_y,virtual_width,virtual_height}`), `ack`, `nack`. Новые типы он получит позже — твоя реализация должна быть **обратно совместима** с текущим presence и `ack/nack` без новых полей.

### Проблемы, которые ты закрываешь
- Console и Remote Control — независимые механизмы → два пользователя одновременно работают с терминалом.
- Нет команд управления трансляцией и inventory источников.
- Ввод не привязан к display / экземпляру трансляции.
- Нет освобождения lease по владельцу (logout) и нет идентификатора браузерной сессии.

---

## 2. Утверждённые архитектурные решения (не пересматривать)

1. Канал управления трансляцией — **существующий** control-plane `srv/<SN>/ctl` / `dev/<SN>/ctl`, envelope v1, существующие `ack`/`nack`/dedup по `command_id`. Новые MQTT-топики не вводить; RPC `tsk/req/rsp` не использовать.
2. `LeaseRegistry` — **единая** interactive lease терминала: одна активная lease на SN для Console, трансляции и Remote Control. Console и stream/input **взаимно исключены**. Console остаётся на прямом WS браузер→app1, но обязана проходить через lease.
3. Scope lease: `console` | `view` | `stream` | `input`. `view` — только просмотр уже запущенного потока (для viewer с правом «Видеонаблюдение», право проверяет BFF; app1 проверяет роль/scope по заголовкам).
4. Сервер передаёт на терминал только логические идентификаторы: `source_id` (`disp:…` | `cam:…`), `profile`, `stream_instance_id` (генерирует app1, uuid4). Никаких аргументов ffmpeg, device name, путей, session id.
5. State остаётся in-memory, `WEB_CONCURRENCY=1`; Redis не внедрять, но `*Protocol`-интерфейсы должны допускать это позже.

---

## 3. Контракт протокола `ctl` (envelope v1) — реализовать в `schemas.py`, `publisher.py`, `service.py`, `mqtt_bridge.py`, `presence.py`

Поля — snake_case, как в существующих сообщениях. Все новые поля во входящих сообщениях **опциональны** (старый `l4desk` продолжает работать).

### 3.1 Terminal → Server (`dev/<SN>/ctl`)
- `presence` (расширение; старые поля сохраняются):
  ```json
  {"v":1,"type":"presence","agent":"l4desk","status":"online","desktop_available":true,
   "session_id":1,"timestamp":"...",
   "screen":{"virtual_x":0,"virtual_y":0,"virtual_width":1920,"virtual_height":1080},
   "inventory":{
     "displays":[{"desktop_id":"disp:<hex>","name":"\\\\.\\DISPLAY1","primary":true,
                  "x":0,"y":0,"width":1920,"height":1080,"session_id":1,"policy":"input|view|denied"}],
     "cameras":[{"camera_id":"cam:<hex>","name":"USB Camera","available":true}]},
   "stream":{"state":"stopped|starting|running|stopping|restarting|failed|source_unavailable|session_unavailable",
             "mode":"desktop|usb-camera|stopped","source_id":"disp:...|cam:...|null",
             "stream_instance_id":"uuid|null","profile":"default","reason":"...|null",
             "ffmpeg_pid":1234,"started_at":"...","restart_count":0}}
  ```
  `presence.py` хранит последнее `inventory` и `stream` по SN и отдаёт их в `status`.
- `ack` — существующие поля + опциональные `result` (`started|already_running|switched|stopped|already_stopped`), `stream_instance_id`, `state`, `inventory`.
- `nack.code` — существующие + `lease_mismatch`, `desktop_mismatch`, `stream_mismatch`, `source_not_allowed`, `source_unavailable`, `session_unavailable`, `busy_transition`, `ffmpeg_missing`, `ffmpeg_integrity`, `input_not_allowed_in_camera_mode`, `invalid_profile`.
- `stream_event` (новый, без ответа): `{"v":1,"type":"stream_event","stream_instance_id":"...","state":"...","reason":"...","timestamp":"..."}` → обновляет `presence.stream` и рассылается подписчикам WS lease (`{"type":"stream_state", ...}`).

### 3.2 Server → Terminal (`srv/<SN>/ctl`)
- `inventory_get {command_id, lease_id?}` → ждать `ack.inventory` (timeout 5 c).
- `stream_start {command_id, lease_id, mode, source_id, profile, stream_instance_id}` → ждать `ack.result ∈ {started, already_running, switched}` (timeout ≥ 15 c — controlled switch на терминале). При успехе сохранить в lease `stream_instance_id`, `stream_mode`, `selected_desktop_id` (если `mode=desktop`), `selected_session_id` (из inventory display).
- `stream_stop {command_id, lease_id, stream_instance_id?}` → `ack.result ∈ {stopped, already_stopped}`. Поток считается остановленным **только** по `ack` или `stream_event.state=stopped`.
- `pointer_move` / `mouse_click` — добавить поля `desktop_id`, `stream_instance_id` (берутся из lease, клиент их не задаёт произвольно: сервер подставляет из lease и отклоняет запрос 409 `stream_mismatch`/`desktop_mismatch`, если клиент передал другие).
- `key_event {command_id, lease_id, desktop_id, stream_instance_id, kind:"down|up|press", vk:int, text?:str}` — новый; серверный whitelist `vk` (0x08 Backspace, 0x09 Tab, 0x0D Enter, 0x1B Esc, 0x20 Space, 0x25–0x28 стрелки, 0x2E Delete, 0x30–0x39, 0x41–0x5A, 0x70–0x7B F1–F12); `text` ≤ 32 символов; rate limit как для `pointer_move`.

Любая команда `stream_*`/ввод отклоняется **до публикации**, если `lease_id` не принадлежит вызывающему (`X-User-Id` + `X-Session-Id`), scope недостаточен, или `desktop_id`/`stream_instance_id` не совпадают с lease. Ввод запрещён, если `lease.stream_mode != desktop` → 409 `input_not_allowed_in_camera_mode`.

---

## 4. Модель единой lease (`core/remote_input/leases.py`)

Расширить `Lease`: `scope: Literal["console","view","stream","input"]`, `owner_session_id: str` (заголовок `X-Session-Id`), `selected_desktop_id: str | None`, `selected_session_id: int | None`, `stream_instance_id: UUID | None`, `stream_mode: str | None`, `issued_at`, `expires_at`, `heartbeat_at`, `ws_connected`, `ws_disconnected_at`.

Правила `LeaseRegistry`:
- `acquire(..., scope, owner_session_id)` под `asyncio.Lock`. Активная lease **другого** `owner_user_id` или другой `owner_session_id` любого scope → `LeaseConflictError` с публичным телом `{code:"lease_taken", owner_role, owner_user_id_masked, scope, expires_at}`. Тот же владелец+сессия → идемпотентно та же lease; смена scope — `upgrade(lease_id, scope)` с проверкой роли (см. §5).
- `scope=view`: выдаётся только если `presence.stream.state == "running"` для SN, иначе `409 stream_not_running`. Владельцу `view` доступны только keepalive/release/status/WS-подписка на `stream_state`.
- `scope=console`: только superuser. Пока активна — `stream`/`input` другим (и тому же пользователю через другую сессию) отклоняются; и наоборот: активная `stream|input|view` блокирует `console`.
- Освобождение: `release`; TTL без heartbeat; разрыв WS → grace `ws_disconnect_grace_sec` (по умолчанию 10) → revoke; `DELETE /leases/by-owner` (logout из BFF); `cleanup_expired` фоновая задача (уже есть — расширить).
- При `revoke` lease со scope `stream|input`, если `stream_instance_id` задан и поток не `stopped` — опубликовать `stream_stop` (best-effort, свой `command_id`, логировать результат). При `revoke` `console` — `DiagnosticsSessionRegistry.remove_all_for_sn(sn)` + закрытие WS.
- Гонки: два одновременных `acquire` → ровно один успешный (тест с `asyncio.gather`).
- Настройки в `config` (`settings.remote_input.*`): `lease_ttl_sec`, `ws_disconnect_grace_sec`, `stream_start_timeout_sec`, `inventory_timeout_sec`; значения по умолчанию задокументировать.

---

## 5. Внутренний API (`api/internal_v1/remote_input.py`, `diagnostics.py`)

Авторизация — как сейчас: `verify_internal_service_auth` + заголовки `X-User-Id`, `X-Role`/`X-Role-Id`, `X-Org-Id`, **новый обязательный `X-Session-Id`** (для WS — заголовок или query `session_id`). Матрица по роли (`extract_caller_role`): `superuser|admin|user` → любой scope кроме `console` (он только `superuser`); `viewer` → только `view`.

Эндпоинты (префикс существующий `/api/internal/v1/remote-input`):
- `GET /devices/{sn}/status` — расширить: `lease{scope, owner_role, owner_masked, expires_at, stream_instance_id, selected_desktop_id}`, `presence.inventory`, `presence.stream`, `online/stale`.
- `POST /devices/{sn}/lease` `{scope, ttl_sec?}` → 201 lease | 409 `lease_taken` | 409 `stream_not_running` | 403 `scope_not_allowed`.
- `POST /lease/{lease_id}/scope` `{scope}` — апгрейд/даунгрейд владельцем.
- `POST /lease/{lease_id}/keepalive`, `DELETE /lease/{lease_id}` — как сейчас.
- `DELETE /leases/by-owner` `{user_id, session_id?}` — освободить все lease владельца (logout).
- `GET /devices/{sn}/inventory?refresh=0|1` — из presence; при `refresh=1` и lease владельца — `inventory_get`.
- `POST /lease/{lease_id}/stream/start` `{mode, source_id, profile}` → генерирует `stream_instance_id`, публикует `stream_start`, возвращает `{stream_instance_id, result, state}`; ошибки терминала пробрасываются как 409 с `nack.code`; таймаут → 504 `terminal_timeout`.
- `POST /lease/{lease_id}/stream/stop` → `{result}`.
- `POST /lease/{lease_id}/pointer/move`, `/mouse/click` — как сейчас + подстановка/проверка `desktop_id`, `stream_instance_id`, проверка `stream_mode=desktop`.
- `POST /lease/{lease_id}/key` — `key_event`.
- `WS /ws/lease/{lease_id}` — как сейчас + события `stream_state`, `lease_revoked{reason}`; для scope `view` — только приём событий.
- `diagnostics.py` `WS /ws/devices/{sn}`: принимать `lease_id` (query) и `X-User-Id`/`X-Session-Id` (или query), проверять, что lease активна, scope `console`, владелец совпадает; иначе `close(4409)` с причиной. При закрытии WS → `mark_ws_disconnected` → grace → revoke. Обратная совместимость: если `lease_id` не передан — попытаться `acquire(scope=console)` неявно для superuser и вернуть `lease_id` первым сообщением (`{"type":"lease","lease_id":...}`), чтобы текущий фронт не сломался до обновления BFF; зафиксировать это как переходный режим с флагом `settings.diagnostics.implicit_console_lease` (по умолчанию `true`, выключить после обновления MenuBuilder).

---

## 6. Порядок работы

1. Изучи файлы §1, `AGENTS.md`, `docs/remote-input-protocol.md`, `docs/remote-diagnostics-protocol.md`, `docs/manual-app1-deploy-runbook.md`. Составь карту изменений.
2. Реализуй §3 (схемы, publisher, bridge, presence), §4 (lease), §5 (API, diagnostics).
3. Обнови документацию: `docs/remote-input-protocol.md` (новые типы, коды `nack`, модель lease со scope, диаграмма состояний, `X-Session-Id`), `docs/remote-diagnostics-protocol.md` (Console под lease, переходный режим), `README.md` (ссылки), CHANGELOG если есть.
4. Тесты (§8), `uv run ruff check .`, `uv run black --check .`, `uv run pytest` — всё зелёное.
5. Коммит в `master` (или ветку по правилам репозитория) с понятным сообщением; **деплой** (§7); проверка после деплоя; отчёт (§9).

---

## 7. Деплой `app1` (обязателен, по `docs/manual-app1-deploy-runbook.md`, основной сценарий — сборка на хосте)

Хост: `user1@87.242.100.34`, ключ `d:\.ssh\id_ed25519`; проект на хосте `/home/user1/iot-rpc-rest-app`, стек `/home/user1/compose.yaml` (сервис `app1`, `env_file: ./iot-rpc-rest-app/app-service/.env`). Из PowerShell всегда `ssh -n …` для одиночных команд; `sudo` для docker. Только `app1`: `--no-deps`, **никогда** не `docker compose up -d` целиком.

1. Pre-flight: `ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "cd /home/user1 && sudo docker compose ps app1 && df -h / && free -m"`. Убедись, что диск < 90 %, RAM свободно > 300 MiB.
2. Backup `.env`: `cd /home/user1/iot-rpc-rest-app && cp app-service/.env app-service/.env.backup-$(date +%Y%m%d-%H%M%S)`.
3. Зафиксируй текущий коммит для rollback: `git -C /home/user1/iot-rpc-rest-app log -1 --oneline`.
4. Синхронизация: `git fetch origin && git checkout master && git pull --ff-only && git log -1 --oneline` (в `/home/user1/iot-rpc-rest-app`).
5. Проверь, что `WEB_CONCURRENCY=1` в `app-service/.env` (не печатай остальное содержимое: `grep -E '^WEB_CONCURRENCY=' app-service/.env`); при отсутствии — добавь.
6. Сборка и перезапуск: `cd /home/user1 && sudo docker compose build app1 && sudo docker compose up -d --no-deps app1 && sudo docker compose ps app1`.
7. Миграции выполняет `prestart.sh` (`alembic upgrade head`) — вручную не запускать; проверь в логах, что прошли.
8. Проверки: `sudo docker compose logs --tail=200 app1` без traceback; HTTP-проверка внутреннего API с ключом из `.env` (не выводя ключ): `GET /api/internal/v1/remote-input/devices/<тестовый SN>/status` → 200 и новые поля; `docs`/`openapi.json` содержат новые маршруты; WS `diagnostics` в переходном режиме отвечает `{"type":"lease",...}`; существующий Remote Control (MenuBuilder → app1) продолжает работать: `POST lease` без `scope` → трактуется как `input` (обратная совместимость до обновления BFF, зафиксируй).
9. Rollback при проблемах: `git checkout <previous_sha>` → `sudo docker compose build app1 && sudo docker compose up -d --no-deps app1`; восстановить `.env` из backup при необходимости.
10. В отчёте — вывод `docker compose ps app1`, короткий фрагмент логов старта, результаты HTTP-проверок (без секретов), SHA коммита.

---

## 8. Требования к тестированию (pytest, существующие фикстуры и стиль)

- `tests/core/remote_input/test_leases.py`: scope, идемпотентный re-acquire, конфликт другого владельца и другой сессии того же пользователя, `upgrade` по ролям, `view` только при `running`, console ⟂ stream/input в обе стороны, гонка acquire, TTL/heartbeat, ws-disconnect grace, `revoke` → `stream_stop` опубликован (мок publisher), `revoke console` → закрытие diagnostics-сессий, `by-owner`.
- `tests/core/remote_input/test_schemas.py`: все новые типы/поля/коды, обратная совместимость со «старым» presence и ack без новых полей.
- `tests/core/remote_input/test_service.py`, `test_mqtt_bridge.py`: `inventory_get`, `stream_start/stop` (ack результаты, nack пробрасывается, таймаут), `stream_event` обновляет presence и рассылается в WS, `key_event` whitelist, подстановка `desktop_id`/`stream_instance_id`, запрет ввода при `usb-camera`.
- `tests/api/v1/test_remote_input_api.py`: матрица ролей/scope, `X-Session-Id` обязателен, 409/403/504 тела, `by-owner`, WS события.
- `tests/api/v1/test_diagnostics_ws_auth.py`: WS с lease/без lease (переходный режим), отказ при чужой lease, отказ при активной stream-lease, revoke при разрыве.
- `tests/core/topologys/test_remote_input_topology.py` — обновить при изменении топологии.
- Ручная проверка после деплоя — §7 п. 8.

---

## 9. Формат отчёта

Markdown, разделы:
1. **Карта изменений** — файлы и назначение.
2. **Контракт `ctl` и внутреннего API** — итоговые JSON-схемы всех сообщений, коды `nack`, HTTP-коды/тела ошибок, заголовки; таблица обратной совместимости (что работает со старым `l4desk` и старым MenuBuilder). Этот раздел — спецификация для команд BFF и терминала.
3. **Модель lease** — поля, правила, диаграмма состояний, таймауты/настройки по умолчанию.
4. **Тесты** — перечень, результаты `pytest`/`ruff`/`black`.
5. **Деплой** — выполненные шаги §7, SHA коммита до/после, вывод `ps`, проверки, время; что делать для rollback.
6. **Известные ограничения и риски** — in-memory/`WEB_CONCURRENCY=1`, переходный режим Console, что обязано сделать BFF (передавать `X-Session-Id`, `scope`, вызывать `by-owner` при logout, включить `pin`/защиту просмотра) и терминал (новые типы сообщений).
