# Lease, watchdog и recovery

## Назначение
Не смешивать продление аренды, безопасное истечение и восстановление FFmpeg.

## Границы ответственности
app1 — server lease/owner/scope; MenuBuilder — авторизованный keepalive;
l4desk — локальный expiry, FFmpeg и освобождение ввода. Схема app1 внешняя,
её миграции не следует автоматически относить к ProcessingBackend.

## Внешние контракты
| Direction | Transport | Endpoint/topic | Main payload | Guarantees |
|---|---|---|---|---|
| UI → BFF → app1 | REST | BFF control/keepalive → `/api/internal/v1/remote-input/lease/{lease_id}/keepalive` | lease_id, доверенный owner context | endpoint успех ≠ terminal renew |
| app1 → l4desk | MQTT | `srv/{SN}/ctl` | lease_renew, command_id, lease_id, expires_at_ms | QoS 1, no retain, локальная проверка |
| l4desk → app1/UI | MQTT → WS/status | `dev/{SN}/ctl` | ack/nack или stream_event с причиной | no retain; propagation требует E2E |

## Инварианты
- **lease_expired — terminal safety stop; recovery запрещён.**
- **unexpected_exit при валидной неистекшей lease — причина bounded recovery.**
- lease_id защищает от старых сессий; stream_instance_id — от старой эпохи стрима.
- Нормативно renew требует будущий expiry, совпадающий ID и running/restarting;
  текущие ограничения проверки ниже не следует считать допустимой нормой.
- Stop/release отменяет recovery; reconcile не оставляет orphan и ложный UI running.

## State machine
Концептуальная lease: `created → active → renewed → expired → stopped`.
`renewed` — действие, не утверждение о literal enum сервера.

| State | Entered by | Exit conditions | External event |
|---|---|---|---|
| running | успешный start | renew сохраняет state; stop/expiry/exit | running / started или recovered |
| restarting | unexpected_exit | действующая lease + bounded retry; stop/expiry прерывает | restarting / unexpected_exit |
| stopped | stop, lease_expired | только новый авторизованный start | stopped / lease_expired |
| stopped после agent restart | reconcile orphan | новый авторизованный start | stopped / agent_restart_reconcile |
| failed | restart limit | новое разрешённое действие | failed / restart_limit |

Код также имеет session_unavailable/source_unavailable; не своди все причины к error.

## Ключевые исходники
- [ctl_protocol.c](../../tools/l4desk/src/ctl_protocol.c): expiration до dedup;
  lease_renew/stream_renew проверяет state/ID и вызывает update_lease.
- [ffmpeg_supervisor.c](../../tools/l4desk/src/ffmpeg_supervisor.c): update_lease только
  увеличивает expiry; tick проверяет watchdog (+5 с grace) перед recovery;
  лимит 5 попыток за 600 с, затем failed/restart_limit.
- [iot_client.py](../../MenuBuilder/backend/app/services/iot_client.py): remote_input_keepalive.

## Проверка
ACK + наблюдаемое увеличение локального expiry + FFmpeg жив дольше исходного окна;
stop keepalive → lease_expired без restart. Отдельно controlled unexpected exit,
wrong lease, missing/zero/past expiry, duplicate до/после TTL, expiry во время backoff,
restart агента и доставка события в UI. [Полная матрица](../operations/validation-matrix.md).

## Известные риски и незавершённые вопросы
- **Код:** отсутствующий/нулевой expires_at_ms может дать ACK без продления; общий
  expiry-check допускает 2000 мс tolerance. Это не строгая валидация будущего срока.
- **Код:** expiration проверяется до dedup; после TTL повтор может вернуть expired,
  а не исходный ответ. Некоторые NACK renew не кладутся в cache.
- **Код:** watchdog действует только при expiry > 0 и имеет grace +5 с; это слабее
  строгого требования «recovery только до expiry». Граничные случаи не проверены runtime.
- **Документ:** app1 публикует renew и очищает stream по event; внешний код не проверен.
  Локальная архитектура явно оставляет E2E этап незавершённым.

## Источники и актуальность
- Authoritative docs: [remote-input](../../docs/etran_arch-remote-input-control.md),
  [E2E architecture](../../docs/etran_arch-video-remote-desktop-e2e.md).
- Code references: ctl renew/dedup и supervisor update_lease/tick, ссылки выше.
- Проверено: 2026-09-11, HEAD `63ce6a7`, статическое чтение, без запуска и исправлений C.
- Обновить при: таймерах, renew validation, retry budget, state/reason или reconcile.