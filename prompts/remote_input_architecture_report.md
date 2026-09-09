# Архитектурный отчёт: remote mouse control (Video UI → терминал) — основание для PROMPT 1 и PROMPT 2

Дата: 2026-09-09. Источник задания: `prompts/prompt_l4desk.md`. Результат: `prompts/prompt_1_iot-rpc-rest-app_remote_input.md`, `prompts/prompt_2_menubuilder_l4desk_remote_input.md`.

## 1. Подтверждённая конечная схема

```text
Browser /video (React)  ── HTTPS/WSS (JWT cookie) ──▶ nginx-default :3000 (auth_jwt, /api/v1/video/ → menubuilder-backend)
   │ WebRTC video ◀── /janus-ws ── l4media-janus                          │
   └ overlay: contentRect (object-fit) ∩ desktopRect (aspect из presence.screen) → x,y 0..65535
                                                                          ▼
menubuilder-backend (BFF): JWT/org/role(1–3)/device ownership → REST lease + WS-прокси
                                                                          │ http://app1:8000, X-Internal-Service-Key / X-Org-Id / X-Role / X-Role-Id / X-User-Id
                                                                          ▼
app1 (iot-rpc-rest-app): /api/internal/v1/remote-input/*  ── lease/pending/presence (memory, 1 worker) ──
   publish amq.topic srv.<SN>.ctl (QoS по подписке, expiration, no retain)   consume queue ctl ← dev.*.ctl (ack/nack/presence)
                                                                          │ RabbitMQ 4 Native MQTT (ACL ^srv.<SN>.* read / ^dev.<SN>.* write — уже покрывает ctl)
                                                                          ▼
Terminal: leo4proxy (mTLS, remote_clientid=<SN>) ← Mosquitto bridge (srv/<SN>/# in 1, dev/<SN>/# out 1) ← l4desk.exe (client_id svc_desk, localhost:1883)
   l4desk: validate → dedup → SendInput(ABSOLUTE|VIRTUALDESK) → ack/nack QoS1 → dev/<SN>/ctl; presence retained + LWT
   запуск: L4Superv (SYSTEM) → WTSQueryUserToken + CreateProcessAsUser (CREATE_NO_WINDOW) в активной консольной сессии
```

Video plane (RTP/L4RTP/Janus/WebRTC) и control plane (MQTT `ctl`) полностью разделены; `evt/eva`, RPC `tsk/req/rsp/res/cmt`, integration bus, `out`, `svc/app` не используются.

## 2. Матрица ответственности

| Компонент | Отвечает | Не отвечает |
|---|---|---|
| l4media/Janus/ingress | доставка видео в браузер | любые команды/координаты (не менять) |
| MenuBuilder frontend | явное включение управления, вычисление фактической области картинки и нормализация, throttle/latest-wins, UX ACK/NACK/timeout, release при Stop/unmount/logout | доступ к MQTT/RabbitMQ, знание топиков |
| MenuBuilder backend (BFF) | JWT/org/role/device ownership, вызовы Internal API, WS-прокси, маппинг ошибок, логирование | MQTT/AMQP, lease-state, формирование топиков |
| iot-rpc-rest-app (app1) | топология `ctl`, publish команд, consumer ACK/NACK/presence, lease (1 на SN), pending/ACK-корреляция, TTL, rate limit, tenant-проверка, audit-лог, Internal API | UI, nginx, терминальный код, retry click |
| RabbitMQ | Native MQTT, `amq.topic`, per-device topic ACL (regex уже покрывает `ctl`), очередь `ctl` | бизнес-логика |
| Mosquitto bridge (терминал) | локальный plain MQTT, проброс `srv/<SN>/#` in / `dev/<SN>/#` out с retain | ACL (allow_anonymous), никаких изменений не требуется |
| l4desk.exe | подписка `srv/<SN>/ctl`, валидация/dedup/TTL/SN/lease формат, SendInput, ACK/NACK, presence retained+LWT в `dev/<SN>/ctl` | TLS, keyboard/clipboard/shell, `svc/app/evt` |
| l4superv | запуск/перезапуск l4desk в активной interactive-сессии скрыто, остановка, статус | ffmpeg (запускается отдельно) |
| nginx-default (etranprocessing) | JWT на входе, инъекция identity-заголовков, WS upgrade для `/api/v1/video/` (правка по согласованию) | — |

## 3. Проверенные файлы и подтверждённые факты

iot-rpc-rest-app: `docs/mqtt_topic_rules.md`; `core/config.py` (`RabbitQXConfig`, `ApiInternalV1Prefix`, gunicorn workers `WEB_CONCURRENCY or cpu*2+1`); `core/topologys/declare.py` (`BINDINGS`, `topic_publisher`, `q_out durable=False`); `core/topologys/fs_queues.py` (subscribers, `out/app/svc` handlers); `core/topologys/fs_depends.py` (`Sn_dep` из routing key, `Corr_id_dep` из `correlationData`); `core/diagnostics/{mqtt_bridge,sessions,service}.py` (in-memory registry образец); `core/services/device_task_processing.py` (publish-паттерн с `expiration`/headers); `api/internal_v1/{internal_depends,__init__,diagnostics}.py`; `core/integrations/rmq_admin_api.py` (topic ACL `^dev.{client_id}.*` / `^srv.{client_id}.*`); `compose.yaml`, `docker-files/app-service/Dockerfile`, `run_main.py`, `docs/manual-app1-deploy-runbook.md` (сервер 87.242.100.34, `/home/user1/compose.yaml`, `build app1 && up -d --no-deps app1`); `pyproject.toml`, `tests/` структура; Redis отсутствует.

etranprocessing: `compose.yaml` (единый стек app1/rabbitmq/menubuilder-backend/nginx-default), `docs/ops_run-devops-runbook.md`, `nginx-configs/port_3000.conf` (`/api/v1/video/` без Upgrade; `/api/internal/v1/diagnostics/` с WS и инъекцией ключа), `MenuBuilder/backend/app/{routers/video.py,services/iot_client.py,auth.py,config.py,main.py}`, `.env.example` (`LEO4_INTERNAL_API_BASE_URL=http://app1:8000`), `MenuBuilder/frontend/src/{routes/video-surveillance.tsx,api/video.ts,api/client.ts,api/janusClient.ts,App.tsx,api/devices.ts}`, `docs/etran_arch-l4media-streaming-architecture.md` (ffmpeg gdigrab + pad внутри кадра), `tools/{l4con,leo4-simple-svc-mqtt}/README.md` и `src/mqtt_client.c` (zero-dependency MQTT, `<SN>_extra`, SN через `GET 127.0.0.1:18443/_leo4/sn`, presence в `dev/<SN>/svc`), `tools/l4superv/{README.md,src/mosquitto_conf.c,src/service_mgr.c}` (SYSTEM-служба, bridge `srv/<SN>/# in 1`, `dev/<SN>/# out 1`, `allow_anonymous true`), `tools/example_mosquitto.conf`, `tools/build_dist_win7.cmd` (`_WIN32_WINNT=0x0601`), `tools/leo4proxy/src/http_proxy.c` (`/_leo4/sn`), `AGENTS.md` §10.

## 4. Решения владельца (зафиксированы)

1. l4desk: `client_id=svc_desk`, presence только `dev/<SN>/ctl` (retained JSON + LWT), `svc` не трогать; `AGENTS.md` §10 дополнить типом `svc_desk`.
2. Канал MenuBuilder↔app1: WebSocket per lease (по образцу diagnostics_ws) + REST lifecycle; браузер → BFF → app1 (без прямого доступа к app1).
3. app1 state — memory-only при `WEB_CONCURRENCY=1`; в общем контексте всех агентов: «memory-only + N gunicorn воркеров → требует Redis в ближайшем будущем».
4. Координаты: фронтенд учитывает padding кадра ffmpeg через `presence.screen`; в документации отметить будущую передачу параметров запуска ffmpeg через control flow.
5. Запуск l4desk — оркестрация в l4superv (helper скрытых процессов в интерактивной сессии), не Task Scheduler.
6. nginx-конфиги принадлежат etranprocessing: правки при доработке app1 — только через эскалацию владельцу с конкретным diff.

## 5. Открытые допущения (не разрешаются исследованием)

- Фактическое `WEB_CONCURRENCY` в серверном `.env` app1 неизвестно (проверяется на pre-flight в PROMPT 1; при ≠1 — эскалация).
- Точное название/наличие WS-библиотеки в `MenuBuilder/backend` (`websockets`/`httpx-ws`) — определяется исполнителем по `uv.lock`.
- Retained presence не переигрывается в AMQP-очередь после рестарта app1 → компенсируется периодическим presence l4desk (30 с) и `stale` через 90 с.

## 6. Порядок реализации и деплоя

1. PROMPT 1: iot-rpc-rest-app — код, тесты, docs, деплой только `app1`; проверка очереди `ctl`, биндингов, Internal API smoke, presence через тестовый publish.
2. Проверка контракта: `GET /api/internal/v1/remote-input/devices/<SN>/status` из `menubuilder-backend`-сети; ACL/topic доступность.
3. PROMPT 2: MenuBuilder backend+frontend, nginx-правка по согласованию, `tools/l4desk`, `tools/l4superv`, `AGENTS.md`, docs; деплой `menubuilder-backend` + `dist/`; поставка `tools.zip` на тестовый терминал.
4. End-to-end: presence → move → click → ACK в UI; негативные сценарии (locked desktop, agent offline, busy, cross-tenant, viewer); контроль отсутствия `DeviceEvent`.
