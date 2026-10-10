# Changelog — l4media

## Unreleased

### Added

- Janus RTCP feedback is returned to the originating L4RTP/1 client as type `0x02` frames over the existing TCP/mTLS connection. Per-stream connected UDP sockets isolate feedback; bounded nonblocking queues preserve partial TCP frames and discard unsent feedback on route changes/stop.

- Consolidated the accepted 18D source-build/registry scripts and historical
  handoff into main without replacing newer ingress lifecycle code or rolling
  back pinned image digests. Optional image overrides come from private env;
  Janus rebuild remains an explicitly requested operation.

- Перенесены в репозиторий уже работавшие на сервере Redis-сохранение медиасессий и маршрутов, а также проверка существующего Janus mountpoint перед повторным использованием.
- Авторизованный сервисный `POST /api/v1/media/sessions/renew` продлевает TTL единственной активной медиасессии по SN после подтверждения lease keepalive.
- Авторизованный сервисный `POST /api/v1/media/sessions/stop` штатно удаляет активную медиасессию по SN после остановки потока.

### Fixed

- Watchdog отсчитывает TTL от последнего успешного продления; Redis-запись получает тот же срок и восстанавливает его после рестарта ingress. При прекращении keepalive маршрут и mountpoint удаляются автоматически.
- `/health.active_media_sessions` считает только ACTIVE/STARTING, как защищённый `/api/v1/media/metrics`; завершённые записи больше не выглядят активными.
- Ingress требует `L4MEDIA_SERVICE_TOKEN` и `JANUS_ADMIN_SECRET` из окружения и не запускается с резервными значениями из исходников.
