---
name: lease-and-watchdog-safety
description: Безопасные изменения lease_id, expires_at, keepalive, watchdog, FFmpeg supervisor, recovery и restart reconcile в remote-control/video flow.
---

# Lease and watchdog safety

1. Прочитай [карточку lease](../../../.agent-context/contracts/lease-lifecycle.md).
   Разделяй lease сервера, локальный watchdog, TTL команды и фактический stream state.
2. `lease_expired` — штатная fail-closed остановка, recovery запрещён.
   `unexpected_exit` разрешает bounded recovery только при валидной действующей lease;
   проверяй её перед каждой попыткой, stop/release должны отменять recovery.
3. На renew проверь running или допустимый restarting, совпадение lease_id,
   валидный будущий expiry и безопасный повтор. ACK без изменения expiry не доказывает renew.
4. Сохраняй корреляцию ACK/NACK: wire `command_id`, lease_id, timestamp.
   Сверяй cache TTL и порядок expiry/dedup; отрицательный ответ тоже требует политики повторов.
5. Принудительная локальная остановка публикует terminal event с причиной;
   события и ACK/NACK не retained. Не меняй presence `svc_desk` на `svc`.
6. При restart агента выполни reconcile только принадлежащих ему процессов,
   освободи ввод и синхронизируй `stopped / agent_restart_reconcile` с сервером/UI.
7. Проверь границы времени, гонки renew/stop/restart, потерю MQTT и истечение lease
   во время backoff. Не обещай гарантию по одному чтению кода.

## Результат
- State machine, ограничения recovery и владельцы таймеров.
- [Матрица сценариев](../../../.agent-context/operations/validation-matrix.md)
  с фактом выполнения и evidence для каждого применимого случая.
- Оставшиеся gaps и обновлённая карточка; требования безопасности не выдаются за реализацию.