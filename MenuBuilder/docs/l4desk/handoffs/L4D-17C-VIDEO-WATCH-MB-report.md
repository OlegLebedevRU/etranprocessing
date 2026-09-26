# H-L4D-17C-VIDEO-WATCH-MB-v1 — MenuBuilder watch consumer

```yaml
handoff_id: H-L4D-17C-VIDEO-WATCH-MB-v1
status: READY_FOR_BROWSER_VALIDATION
contract_kinds: [API, EVENT, DEPLOYMENT]
producer: H-L4D-17C-VIDEO-WATCH-IOT-01-v1
producer_contract_version: 1.0.0
producer_schema_revision: 2026-09-26-v1
consumer: MenuBuilder
consumer_commit: included_in_this_commit
browser_path: /api/v1/video/devices/{device_id}/watch/ws
upstream_path: /api/internal/v1/remote-input/ws/watch/{sn}
deployment_status: DEPLOYED_TO_DEV_LEO4_3000
next_gate: browser_stream_validation
```

## Task intake

- Цель: заменить регулярное чтение app1 presence/stream status видеовкладки на watch WS с REST resnapshot.
- Владелец producer: app1. Владелец BFF/UI: MenuBuilder. Миграции БД не требуются.
- Flow: app1 process-local presence/stream → внутренний WS с `X-Internal-Service-Key` и `X-Org-Id` → MenuBuilder BFF → браузерный WS `invalidate` → REST status BFF → UI.
- Инварианты: browser не получает внутренний ключ и сырой snapshot; BFF проверяет `video:view` и tenant; события не управляют lease, вводом или RTP; `stream_instance_id` старой эпохи не восстанавливает running.
- Ограничение producer: app1 watch требует `WEB_CONCURRENCY=1`; текущий runtime подтверждён на сервере.

## Изменения

- BFF `/api/v1/video/devices/{device_id}/watch/ws` проверяет cookie/permission/device ownership, подключается к app1 и пересылает только `invalidate`. URL token и чужой Origin отклоняются. Соединение переавторизуется после 60 секунд.
- Видеовкладка открывает watch для выбранного терминала, перечитывает REST status при подключении, invalidation и reconnect, игнорирует состояние другого `stream_instance_id`, закрывает socket при смене терминала или unmount.
- При недоступном WS временно работает пятисекундный REST status fallback. RTP status остаётся на пятисекундном таймере. Lease keepalive остаётся отдельным.
- Compose Nginx-конфиг MenuBuilder получил явный WS upgrade. В фактическом `nginx-default` маршруте `/api/v1/video/` WS upgrade уже был; `nginx-configs/port_3000.conf` теперь сохраняет порт в `Host`, чтобы same-origin проверка работала на `:3000`.

## Контракт

| Направление / владельцы | Канал | Schema / версия | QoS / retain | Duplicate | Timeout / TTL | Ошибка | Compatibility |
|---|---|---|---|---|---|---|---|
| app1 → MenuBuilder BFF | `/api/internal/v1/remote-input/ws/watch/{sn}` | `snapshot`, затем `invalidate`; v1 | N/A | Повторный сигнал вызывает REST resnapshot | send 5 с; BFF socket 60 с | 4403 auth/ownership, 1013 backlog | Старый REST status сохраняется |
| MenuBuilder BFF → браузер | `/api/v1/video/devices/{device_id}/watch/ws` | только `{type:"invalidate"}` | N/A | refresh coalesced | reconnect 1–15 с | 4401/4403/4404 без автоповтора | REST fallback при недоступном WS |
| Браузер → BFF | `/control/status`, `/stream/state`, `/session/status` | существующие REST DTO | N/A | повторное чтение безопасно | status fallback 5 с; RTP 5 с | существующие HTTP ошибки | без изменений |

## Проверено

- [x] BFF: Ruff check/format и Pyright — успешно, 0 ошибок.
- [x] BFF: `uv run pytest -q` — 469 passed; существующие предупреждения тестов.
- [x] Frontend: `npx tsc --noEmit --incremental false` — успешно.
- [x] Frontend: `npx vite build --configLoader runner --outDir .codex-dist-watch` — успешно. Этот режим сообщил предупреждение существующего Vite plugin о `__dirname`; сборка завершилась.
- [x] Frontend: `npx vitest run --configLoader runner src/tests/session-lifecycle.test.ts` — 6 passed.
- [x] Стандартный `npm run build` — успешно; предупреждение только о размере существующего vendor chunk.
- [x] На сервере MenuBuilder backend импортирует watch route; хеш `video_control.py` совпадает с локальной версией. Публичный frontend bundle содержит `/watch/ws`; главная страница отвечает 200, а анонимный запрос к watch route — 401.
- [x] Конфиг `port_3000.conf` доставлен из репозитория по разрешению пользователя. `nginx -t` и reload прошли; SHA-256 файла на хосте и в `nginx-default` совпадает с локальным: `bd5df5f187ec7432d5be90b3ad83be0b2d494e2ec31c5ad9216165fa804f4f48`.
- [x] Текущий runtime app1: `WEB_CONCURRENCY=1`.
- [ ] Браузер E2E с авторизованным WS upgrade и живой трансляцией: пользователь проверит на `https://dev.leo4.ru:3000`.

## Браузерная проверка

1. Открыть видеовкладку и выбрать доступный терминал. В Network должен появиться `101` для `/api/v1/video/devices/{id}/watch/ws`, затем `{type:"invalidate"}`.
2. Запустить трансляцию. По сигналам watch должны обновляться `control/status` и `stream/state`; `session/status` продолжает опрашивать RTP раз в 5 секунд.
3. Остановить трансляцию или отключить агент. UI должен перейти из running в stopped/idle без ожидания пятой секунды; reason виден там, где его возвращает REST.
4. Прервать WS и восстановить сеть: REST status работает во время разрыва, затем соединение повторяется и берётся новый snapshot.
5. Проверить viewer `video:view` и отказ для пользователя без права или чужой организации.

## Риски и следующий шаг

- Фактический Nginx после reload использует конфиг с хешем репозитория; `nginx -t` успешен.
- Vite dev proxy в имеющемся `vite.config.ts` не включает WS upgrade для `/api`; browser gate рассчитан на deployed Nginx.
- Работающие Janus кадры и RTP не подтверждаются watch и проверяются пользователем отдельно.
- В этой задаче не создано lease, тестового терминального ввода или удалённых ресурсов.
