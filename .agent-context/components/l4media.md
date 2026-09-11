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
Framing, invalid/duplicate SN, route absent, stale counters, first frame, browser ICE/decode,
reconnect/stop и недоступность management извне. Подробно [video contract](../contracts/video-streaming.md).

## Известные риски и незавершённые вопросы
Cert↔SN binding и fresh RTP telemetry требуют отдельной проверки; не подтверждены этой карточкой.

## Источники и актуальность
- Authoritative docs: [E2E](../../docs/etran_arch-video-remote-desktop-e2e.md),
  [media](../../docs/etran_arch-l4media-streaming-architecture.md).
- Code references: навигационные ссылки выше, media runtime не исследован.
- Проверено: 2026-09-11, HEAD `63ce6a7`, документальная карта ответственности.
- Обновить при: ingress protocol/routes, Janus/ICE, TLS trust boundary или deployment.