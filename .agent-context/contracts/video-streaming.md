# Video streaming: control, media, signaling

## Назначение
Различать запущенный процесс, доставку RTP и свежий декодированный кадр пользователя.

## Границы ответственности
MenuBuilder — viewing session/PIN/access, app1 — lease/control, l4desk — FFmpeg,
leo4proxy — transport, l4media/Janus — ingress/WebRTC. DB ownership см. компонентную задачу.

## Внешние контракты
| Direction | Transport | Endpoint/topic | Main payload | Guarantees |
|---|---|---|---|---|
| UI → BFF | REST | /api/v1/video/{device_id}/session | mountpoint_id, sn, janus_ws, ttl, pin | авторизованный просмотр; PIN секрет |
| BFF → ingress | REST | /api/v1/media/sessions/renew | sn | только после успешного video lease keepalive; продлевает media TTL единственной активной сессии SN, истекшая сессия отклоняется |
| UI → BFF | REST | /api/v1/video/{device_id}/status | streaming, rtp_packets, bytes, idle_sec | transport stats, не decoded frame |
| FFmpeg → proxy → ingress | RTP / L4RTP/1 через TLS | media plane | encoded H.264/RTP | sequence/route, не control lease |
| Janus ↔ browser | signaling + WebRTC | mountpoint/watch | SDP/ICE + video | успешный signaling не равен frame |

## Инварианты
- Route/mountpoint готов до stream_start; PIN привязан к lease/stream и не раскрывается.
- stream_instance_id не присутствует в L4RTP/1; не считать media заголовок ctl envelope.
- Для реального «В эфире» нужны свежие RTP и browser decode stats, не cumulative counter.
- lease_expired не восстанавливает FFmpeg; см. [lease](lease-lifecycle.md).

## State machine
session/route ready → authorized stream_start → running → receiving RTP → decoding;
stop/error/expiry → terminal event → UI cleanup. Это этапы проверки, не общая enum.

## Ключевые исходники
- [video.py](../../MenuBuilder/backend/app/routers/video.py) — session/Janus/ingress.
- [video_control.py](../../MenuBuilder/backend/app/routers/video_control.py) — stream control.
- [ffmpeg_supervisor.c](../../tools/l4desk/src/ffmpeg_supervisor.c) — terminal lifecycle.
- [l4media](../../l4media) — media stack, точный handler искать по изменяемому flow.

## Проверка
Fresh-frame/first-frame timing, stop/reconnect/late join, duplicate start, lost route,
PIN/permission denial, supported H.264/SDP, no RTP vs no decode как разные случаи.

## Известные риски и незавершённые вопросы
mTLS termination не доказывает cert↔SN binding в media preamble; duplicate SN — не anti-clone.
Архитектура отмечает пробелы fresh-frame telemetry и E2E; runtime здесь не проверялся.

## Источники и актуальность
- Authoritative docs: [E2E](../../docs/etran_arch-video-remote-desktop-e2e.md),
  [media architecture](../../docs/etran_arch-l4media-streaming-architecture.md).
- Code references: точки входа выше; media plane не аудитировался.
- Проверено: 2026-09-11, HEAD `63ce6a7`, документальная карточка.
- Обновить при: media framing, codec, session/PIN, routing/signaling или telemetry.
