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
| UI → BFF → app1 | REST / WS | BFF control/keepalive → `/api/internal/v1/remote-input/lease/{lease_id}/keepalive` | lease_id, generation, wait_ack, owner context | wait_ack=1 ожидает terminal ACK; возврат renew_status |
| app1 → l4desk | MQTT | `srv/{SN}/ctl` | lease_renew, command_id (UUID), lease_id, expires_at_ms, ttl_sec | QoS 1, no retain, wire-поле command_id канонизировано |
| l4desk → app1/UI | MQTT → WS/status | `dev/{SN}/ctl` | ack (renewed/nack), applied_deadline_ms, stream_event с причиной | no retain; propagation в WS и UI координатор |

## Инварианты
- **lease_expired — terminal safety stop; recovery запрещён.**
- **unexpected_exit при валидной неистекшей lease — причина bounded recovery.**
- lease_id защищает от старых сессий; stream_instance_id — от старой эпохи стрима.
- Нормативно renew требует будущий expiry, совпадающий ID и running/restarting;
  строгая валидация expires_at_ms > now_ms (и stream_instance_id при наличии) внедрена в Step 4.
- Stop/release отменяет recovery; reconcile не оставляет orphan и ложный UI running.
- Отключение ввода (detach input) не удаляет общую видеотрансляцию (sharedLease в SessionLifecycleCoordinator).

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

## Устраненные дефекты и актуальное состояние (Шаг 4)
- **F1 / F2 (app1):** `CtlLeaseRenew` сериализует каноническое wire-поле `command_id` UUID;
  терминальный агент корректно распознает продления, ACK коррелируется со статусом `renew_status`.
- **F3 / F4 (MenuBuilder UI/BFF):** Реализован `SessionLifecycleCoordinator` с трекингом `generation`,
  безопасным `detachInput()` без сброса общей аренды и фильтрацией событий по `stream_instance_id`.
  BFF нормализовал 404 (`lease_not_found`) и поддержал `generation`.
- **F6 (l4desk):** Введена строгая валидация `expires_at_ms > now_ms` и верхнего предела эпохи;
  при невалидном времени или несоответствии `stream_instance_id` (если передан) возвращается NACK `invalid_payload` / `stream_mismatch`.
- **Остаточный этап:** Сквозная стендовая верификация (E2E) с реальным терминалом.

## Источники и актуальность
- Authoritative docs: [remote-input](../../docs/etran_arch-remote-input-control.md),
  [E2E architecture](../../docs/etran_arch-video-remote-desktop-e2e.md),
  [protocol specification](../../docs/ingress_iot/remote-input-protocol.md).
- Code references: ctl renew/dedup и supervisor update_lease/tick, ссылки выше.
- Актуализировано: 2026-09-11, реализация Шага 4, деплой на 87.242.100.34.
- Обновить при: проведении стендовой E2E-верификации.