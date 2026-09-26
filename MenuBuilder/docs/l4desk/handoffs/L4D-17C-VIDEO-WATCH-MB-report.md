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
- При недоступном WS временно работает пятисекундный REST status fallback. При открытом WS регулярных status HTTP-запросов нет; BFF переавторизует WS через 60 секунд и UI делает resnapshot при переподключении. Счётчик декодированных кадров берётся локально из WebRTC `getStats()` каждые 5 секунд. Lease keepalive остаётся отдельным.
- Compose Nginx-конфиг MenuBuilder получил явный WS upgrade. В фактическом `nginx-default` маршруте `/api/v1/video/` WS upgrade уже был; `nginx-configs/port_3000.conf` теперь сохраняет порт в `Host`, чтобы same-origin проверка работала на `:3000`.

## Контракт

| Направление / владельцы | Канал | Schema / версия | QoS / retain | Duplicate | Timeout / TTL | Ошибка | Compatibility |
|---|---|---|---|---|---|---|---|
| app1 → MenuBuilder BFF | `/api/internal/v1/remote-input/ws/watch/{sn}` | `snapshot`, затем `invalidate`; v1 | N/A | Повторный сигнал вызывает REST resnapshot | send 5 с; BFF socket 60 с | 4403 auth/ownership, 1013 backlog | Старый REST status сохраняется |
| MenuBuilder BFF → браузер | `/api/v1/video/devices/{device_id}/watch/ws` | только `{type:"invalidate"}` | N/A | refresh coalesced | reconnect 1–15 с | 4401/4403/4404 без автоповтора | REST fallback при недоступном WS |
| Браузер → BFF | `/control/status`, `/stream/state` | существующие REST DTO | N/A | повторное чтение безопасно | status fallback 5 с только без WS | существующие HTTP ошибки | `/session/status` больше не опрашивается UI |

## Проверено

- [x] BFF: Ruff check/format и Pyright — успешно, 0 ошибок.
- [x] BFF: `uv run pytest -q` — 469 passed; существующие предупреждения тестов.
- [x] Frontend: `npx tsc --noEmit --incremental false` — успешно.
- [x] Frontend: `npx vite build --configLoader runner --outDir .codex-dist-watch` — успешно. Этот режим сообщил предупреждение существующего Vite plugin о `__dirname`; сборка завершилась.
- [x] Frontend: `npx vitest run --configLoader runner src/tests/session-lifecycle.test.ts` — 6 passed.
- [x] Стандартный `npm run build` — успешно после перехода на локальную WebRTC статистику; предупреждение только о размере существующего vendor chunk.
- [x] На сервере MenuBuilder backend импортирует watch route; хеш `video_control.py` совпадает с локальной версией. Публичный frontend bundle содержит `/watch/ws`; главная страница отвечает 200, а анонимный запрос к watch route — 401.
- [x] Конфиг `port_3000.conf` доставлен из репозитория по разрешению пользователя. `nginx -t` и reload прошли; SHA-256 файла на хосте и в `nginx-default` совпадает с локальным: `bd5df5f187ec7432d5be90b3ad83be0b2d494e2ec31c5ad9216165fa804f4f48`.
- [x] Текущий runtime app1: `WEB_CONCURRENCY=1`.
- [ ] Браузер E2E с авторизованным WS upgrade и живой трансляцией: пользователь проверит на `https://dev.leo4.ru:3000`.

## Браузерная проверка

1. Открыть видеовкладку и выбрать доступный терминал. В Network должен появиться `101` для `/api/v1/video/devices/{id}/watch/ws`, затем `{type:"invalidate"}`.
2. Запустить трансляцию. По сигналам watch должны обновляться `control/status` и `stream/state`; регулярного `/session/status` в Network нет. Частота декодированных кадров берётся из WebRTC stats браузера.
3. Остановить трансляцию или отключить агент. UI должен перейти из running в stopped/idle без ожидания пятой секунды; reason виден там, где его возвращает REST.
4. Прервать WS и восстановить сеть: REST status работает во время разрыва, затем соединение повторяется и берётся новый snapshot.
5. Проверить viewer `video:view` и отказ для пользователя без права или чужой организации.

## Инцидент при первой браузерной проверке (2026-09-26)

- Авторизованный WS `/devices/773/watch/ws` был принят, `control/status` и `stream/state` отвечали `200`. `POST /devices/773/session` возвращал `500`: `Settings` в работающем образе не содержал `l4desk_session_orchestration_enabled`, хотя `video.py` обращался к нему. В том же конфиге отсутствовал `l4media_janus_url`.
- Источник расхождения — запущенный образ `sha256:7b5e4fe1b6e7c14f9c5c634a28b369436d90bda1653f64cf9844077cca7239e5`: его `app/config.py` отличается от репозиторного двумя отсутствующими полями и значением `remote_session_watchdog_ttl_sec=3600` вместо `600`.
- По разрешению пользователя создан производный образ от прежнего ID с единственной заменой `/workspace/MenuBuilder/backend/app/config.py` из коммита `0b60a53`. SHA-256 исходного и активного файла: `e4075f7316d676e3d97053fe4146cc0cbea4c61c7e62d6ecf4fbff2965027c86`. Образ `user1-menubuilder-backend:watch-config-0b60a53`, ID `sha256:295996258a458b4e4338767ee2a7755cc620d64ea7677c1e30c7aedd61aa5cdd`, назначен в `user1-menubuilder-backend` и поднят через Compose с `--no-deps --no-build --pull never`. Прежний образ сохранён как `user1-menubuilder-backend:before-watch-config-fix` для отката.
- После переключения: startup и schema compatibility прошли, оба поля доступны, watch WS принят, REST-статусы `200`. На терминале 773 в браузерной попытке два `POST /session`, два `POST /stream/start` и один `POST /stream/stop` вернули `200`; визуальное качество потока пользователь проверяет отдельно. Временный каталог сборки на сервере удалён, прежний образ сохранён для отката.
- Конфиг в Git и runtime теперь совпадают. Следующий управляемый выпуск MenuBuilder должен включать этот коммит или его эквивалент; текущий beta builder наблюдает `main`, а исправление пока в `l4desk/l4d-17d-media`. Выпуск из старого `main` вернёт расхождение. До интеграции ветки нельзя считать hotfix устойчивым к следующему релизу.

## Устранение оставшегося HTTP-опроса (2026-09-26)

- При первой работающей трансляции браузер продолжал запрашивать `/session/status` каждые 5 секунд для показателя кадров. Это не входило в app1 watch contract, но выглядело как сохраняющийся polling. Теперь показатель рассчитывается по `framesDecoded` из локального `RTCPeerConnection.getStats()`; регулярных HTTP-запросов этого маршрута нет. Удалён также 60-секундный REST timer при открытом watch; переавторизация BFF сама закрывает WS через 60 секунд, после reconnect выполняется resnapshot. Пятисекундный REST fallback действует только при недоступном WS.
- Код зафиксирован в `5fac8ed`; `npm run build` прошёл, `session-lifecycle.test.ts` — 6 passed. Assets доставлены до атомарной замены `index.html` в bind-mounted frontend на `dev.leo4.ru:3000`. SHA-256 `index.html` в локальной сборке и `nginx-default`: `5dddbf003d1b35d007765a0066d9a9b039ae71d4b9bd171b5efc7b3745a97a7a`; JS видеовкладки: `38c06a4411cc2dc4a0bd0b152f5efc72b8a19a83a057d10a67a05089a3e0cc79`. Публичные index и JS отвечают `200`, JS содержит watch и `getStats`, не содержит `/session/status`.
- Браузер после обновления страницы должен подтвердить отсутствие регулярного `/session/status` при живом watch; lease keepalive и WebSocket Janus остаются штатными. Предыдущий `index.html` сохранён на сервере как `index.html.before-5fac8ed` до пользовательской проверки.

## Подавление heartbeat и объединение resnapshot (2026-09-26)

- Живой лог показал, что app1 отправлял `invalidate` на неизменившийся presence heartbeat каждые 30 секунд. Исправление producer `d7b604a` в `iot-rpc-rest-app` не публикует сигнал, если изменился только `last_seen_at`; смена доступности рабочего стола и другие значимые поля продолжают вызывать invalidation. `app1` собран из Git и перезапущен отдельно, startup прошёл; трансляция после перезапуска восстановилась.
- После исправления producer в интервале между WS reconnect регулярных status GET не было. Оставались три близких чтения при штатном 60-секундном reconnect: на закрытии, во время короткого разрыва и после первого сигнала нового WS.
- MenuBuilder `dfb959b` убрал чтение на обычном закрытии и начинает REST fallback только после 5 секунд разрыва. На новом соединении начальный `invalidate` даёт один resnapshot. При отказе `4401`/`4403`/`4404` выполняется одно финальное чтение, затем повторные попытки и fallback выключаются. `npm run build` прошёл; новые assets доставлены до замены `index.html`. SHA-256 активного `index.html`: `56cca6dd19c3c23777988fe917861350a58d5d4567577464bbe970fa17e44dc4`, JS видеовкладки: `d8b661c4ceb6e4548964502024dacd88b306a3040215be25b833802d265b9de2`.
- После обновления браузерной вкладки живые логи `menubuilder-backend` подтвердили два полных цикла: WS принят в 09:47:29 и 09:48:31 UTC; каждый раз выполнено ровно по одному `GET /control/status` и `GET /stream/state` с `200`, без дополнительных GET между циклами и без `GET /session/status`. `POST /control/keepalive` каждые ~5 секунд остаётся частью lease/watchdog контракта и не является status polling. Визуальное качество трансляции подтверждает пользователь в браузере.

## Риски и следующий шаг

- Фактический Nginx после reload использует конфиг с хешем репозитория; `nginx -t` успешен.
- Vite dev proxy в имеющемся `vite.config.ts` не включает WS upgrade для `/api`; browser gate рассчитан на deployed Nginx.
- Работающие Janus кадры и RTP не подтверждаются watch и проверяются пользователем отдельно.
- В этой задаче не создано lease, тестового терминального ввода или удалённых ресурсов.
