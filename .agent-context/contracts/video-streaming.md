# Video streaming: control, media, signaling

## Desktop compatibility correction — deployed 2026-10-26

Fresh authorized presence without video_contract_v2 uses retained CTL-v1 H264
for Auto/H264; explicit HEVC is unsupported. Native H264/HEVC descriptors retain
strict source/epoch/format/READY validation. A negative native negotiation alone
does not identify an old agent; a deliberate H264 selection may use the retained
adapter, but invalid/stale/mixed data or uncertain START never cause a retry.
Capability discovery is independent of delivery-port configuration; native START
still requires configured ports before prepare. Legacy browser API unknown can
negotiate H264 through SDP. Lease ownership/TTL, route-before-START, epoch binding
and camera isolation are unchanged. Owner-visible Chrome773 verified both codecs
and Auto at both profiles, native3000x2000 decode, renewals beyondTTL and clean
STOP/release. Old-binary E2E remains unavailable by agreement.
[Release and live evidence](../tasks/completed/2026-10-26-desktop-stream-recovery.md).

## Назначение
Различать запущенный процесс, доставку RTP и свежий декодированный кадр пользователя.

## Границы ответственности
MenuBuilder — viewing session/PIN/access, app1 — lease/control, l4desk — FFmpeg,
leo4proxy — transport, l4media/Janus — ingress/WebRTC. DB ownership см. компонентную задачу.

## Внешние контракты

OpenIPC через ESP32: [сквозной флоу камер](../../docs/etran_arch-openipc-camera-flow.md).
Панель использует camera=openipc, cam:ip000001, существующий video control plane
и отдельные RPC160..164 с gauge84. Питание, HTTP readiness и decoded frames
проверяются раздельно.

| Direction | Transport | Endpoint/topic | Main payload | Guarantees |
|---|---|---|---|---|
| UI → BFF | REST | /api/v1/video/devices/{device_id}/session | mountpoint_id, sn, janus_ws, session_ttl_sec, pin | авторизованный просмотр; PIN секрет |
| BFF → ingress | REST | /api/v1/media/sessions/start | session_id, sn, lease_expires_at | Авторизованный app1 deadline (Unix seconds) ограничивает существующий media watchdog; отдельные600с не продлевают аренду. |
| BFF → ingress | REST | /api/v1/media/sessions/renew | sn, lease_id, stream_instance_id?, lease_expires_at | Только после успешного video lease keepalive и совпадения эпохи; ID активной медиасессии должен совпасть с lease или stream ID. Истекшая сессия не оживляется. |
| BFF → ingress | REST | /api/v1/media/sessions/stop | sn, lease_id, stream_instance_id?, reason | после подтверждённой остановки потока; удаляет маршрут и mountpoint только если ID активной медиасессии совпадает с ID аренды или потока, повторный вызов идемпотентен |
| UI → BFF | REST | /api/v1/video/devices/{device_id}/status | streaming, rtp_packets, bytes, idle_sec | transport stats, не decoded frame |
| FFmpeg → proxy → ingress | RTP / L4RTP/1 через TLS | media plane | encoded H.264/RTP | sequence/route, не control lease |
| Janus ↔ browser | signaling + WebRTC | mountpoint/watch | SDP/ICE + video | успешный signaling не равен frame |

## Инварианты
- Route/mountpoint готов до stream_start; PIN привязан к lease/stream и не раскрывается.
- stream_instance_id не присутствует в L4RTP/1; не считать media заголовок ctl envelope.
- Для реального «В эфире» нужны свежие RTP и browser decode stats, не cumulative counter.
- lease_expired не восстанавливает FFmpeg; см. [lease](lease-lifecycle.md).
- Ingress expiry uses the existing ttl_sec/renewed_at pair, including Redis restore;
  authoritative app1 deadline is rounded down to whole Unix seconds (less than1s
  early), never extended by HTTP transit/setup. Legacy internal callers without
  a lease deadline retain their existing media TTL. Start/health also retire an
  already expired session before reuse/conflict checks, without evicting an
  unexpired session. No lease ownership moves into ingress.

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
Архитектура отмечает пробелы fresh-frame telemetry и E2E. 2026-10-04 оператор
подтвердил работающий видеопоток после Upgrade773 до tools 1.10.1 (ready/0,
RTP TLS valid). Это operator evidence, не измерение fresh-frame/decode stats
агентом и не полная stop/reconnect/late-join матрица.

## Источники и актуальность
- Authoritative docs: [E2E](../../docs/etran_arch-video-remote-desktop-e2e.md),
  [media architecture](../../docs/etran_arch-l4media-streaming-architecture.md).
- Code references: точки входа выше; media plane не аудитировался.
- Проверено: 2026-09-11, HEAD `63ce6a7`, документальная карточка.
- Обновить при: media framing, codec, session/PIN, routing/signaling или telemetry.

## HEVC P0 — 2026-10-22

Нормативный [contract 2.0.0](../../docs/etran_arch-hevc-contracts.md) и fixtures
зафиксированы TASK-00. [P0 handoff](../tasks/active/2026-10-22-hevc-contracts-p0.md)
содержит исходное offline evidence. Последующий
[TASK-40 handoff](../tasks/completed/2026-10-22-hevc-task40-server-ready.md)
фиксирует реальный deployed HEVC decode и H.264 regression. G0 неизменён;
новые HTTP projection не являются MQTT READY. Publisher context читается по
exact owner/session, terminal watch hints закрывают только совпадающий epoch;
новый START и live state остаются REST-authoritative. Разрешение независимых
viewer leases и native hardware probe серверной проверкой не расширялись.
