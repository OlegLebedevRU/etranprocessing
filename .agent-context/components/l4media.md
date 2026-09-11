# l4media

## Назначение
Медиаконтур приёма терминального видеопотока и доставки в browser через Janus/WebRTC.

## Границы ответственности
Nginx media TLS, ingress routing/decapsulation, Janus mountpoints/signaling.
Не владеет terminal lease, RPC shell или server permissions MenuBuilder.
Изменения схемы общей БД не подразумеваются этим компонентом.

## Внешние контракты
| Direction | Transport | Endpoint/topic | Main payload | Guarantees |
|---|---|---|---|---|
| Terminal proxy → ingress | TLS/L4RTP | media ingress | SN/RTP | framing/route, cert binding отдельно |
| BFF → media/Janus | management API | route/mountpoint | device/stream mapping | внутренний контур, не public API |
| Janus → browser | WebRTC | watch/mountpoint | SDP/ICE/video | проверять decoded frames |

## Инварианты
Management API не публикуется наружу; секреты/PIN не попадают в логи.
Keepalive транспорта не равен свежему кадру; control-plane и media-plane независимы.

## State machine
route configured → transport connected → RTP receiving → viewer decoding;
disconnect/idle/stop требует раздельной оценки lease, ingress и viewer state.

## Ключевые исходники
- [l4media](../../l4media) — исходники и конфигурация стека.
- [video.py](../../MenuBuilder/backend/app/routers/video.py) — BFF integration.
- [compose.yaml](../../compose.yaml) — локальная orchestration reference, не доказательство runtime.

## Проверка
C unit-тесты (`tests/test_ingress_unit.c`) исполняются автоматически в Dockerfile (`make test`).
Python regression suite `tests/test_ingress_regression.py` (6 сценариев).
Подробно [video contract](../contracts/video-streaming.md).

## Актуальный статус реализации (Шаг 4)
- В ingress разделены метки `last_rtp_time` и `last_activity` (RTCP/keepalive не обновляют RTP).
- Реализован порог свежести `RTP_STALE_DEADLINE_SEC = 10`, эпоха `connection_epoch` и enum `media_state`.
- Добавлены эндпоинты `/stats` и `/stats/<sn>`.
- Уровень отладки Janus понижен до `debug_level = 3` (исключена утечка PIN).
- Логи Nginx направлены в `/dev/stdout` и `/dev/stderr`. Стек задеплоен на 87.242.100.34.

## Источники и актуальность
- Authoritative docs: [E2E](../../docs/etran_arch-video-remote-desktop-e2e.md),
  [media](../../docs/etran_arch-l4media-streaming-architecture.md).
- Code references: l4media_ingress.c, ingress/Makefile, janus.jcfg, nginx.conf.
- Актуализировано: 2026-09-11, реализация Шага 4, деплой на 87.242.100.34.
- Обновить при: ingress protocol/routes, Janus/ICE, TLS trust boundary или deployment.