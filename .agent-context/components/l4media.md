# l4media

## 2026-10-23 siplite camera extension

[Contract](../../docs/etran_arch-siplite-hevc-integration-contract.md) и [machine package](../../contracts/siplite-camera-v1/README.md) вводят negotiated camera1.0.0 отдельно от frozen2.0.0. Native camera SPS:2592×1944, Main8/420, level153. Camera Auto допускает H.264 только до START, explicit H.265 не подменяется. Это contract/offline validation; новые runtime readers/writers и decode на4623 ещё не проверены.

## 2026-10-22: HEVC server readiness

G-SERVER-READY passed on the target MenuBuilder with a real1920×1080@20 camera
through the existing proxy/TLS/L4RTP path: H.265 decoded frames, readable optical
table, three authorized same-owner subscriptions, late join/detach, shared failure,
no auto restart and stale-error isolation. Old agent/old request H.264 and new
Auto fallback passed. Exact revisions/digests, runtime flag overrides, cleanup,
rollback evidence and limits are in the
[TASK-40 handoff](../tasks/completed/2026-10-22-hevc-task40-server-ready.md).
Test camera exceptions are cleared; native hardware readiness remains TASK-50.

## 2026-10-11: lease-bounded media watchdog (deployed and verified)

MenuBuilder forwards authoritative app1 lease_expires_at on media start/renew.
Ingress stores the deadline with existing ttl_sec/renewed_at fields; Redis and
lease ownership are unchanged. Deadline-aware renew checks lease/stream identity;
expired sessions cannot be renewed and are stopped before start/health reuse.
Unexpired conflicting sessions remain protected. Legacy internal callers keep
their existing TTL semantics. Local C/backend tests, live4619 expiry→new session,
45-second renew beyond initial lease, and90-second browser stop/restart/unmount
passed. Source a05eb95; exact immutable digests and limitations in the
[expiry handoff](../tasks/active/2026-10-11-media-lease-expiry.md).

## 2026-10-10: downstream RTCP (local implementation)

Ingress now sends upstream RTCP through a lazily created per-stream connected
UDP socket and receives Janus feedback from that configured endpoint. Reverse
L4RTP/1 is type0x02/flags0/BE16-length with no preamble. Epoch/generation-tagged
epoll events avoid stale client/fd reuse. A bounded 128KiB feedback queue drops
new packets at saturation; route changes/stop discard untouched frames and
finish any already-started TCP frame. Disconnect destroys all feedback state.
Feedback never refreshes RTP freshness or transport idle time. Legacy upward-only
clients keep working but cannot request IDR through feedback until updated.
WSL Linux warning-clean build, C unit groups, real-socket production-code tests,
ASan/UBSan, six live regressions and reverse-feedback event-loop integration
passed. Real Janus/browser E2E and deployment are coordinated separately by
release owner; these local checks do not establish browser decode.
See [RTCP handoff](../tasks/completed/2026-10-10-l4media-rtcp-downstream.md).

## 2026-10-04: server TLS invariant

Media требует CA-issued `dev.leo4.ru` (serverAuth, сроки, соответствие ключа).
Compose монтирует canonical cert/key из iot-rpc-rest-app, без копии media/crt.
Entrypoint и pre-deploy gate отклоняют неверную пару; self-signed генерация удалена.
После обновления файлов используется `l4media/deploy/reload-media.sh`: gate
перед пересозданием только nginx, чтобы обновить file bind mounts.
Production CA-пара и guarded image выпущены; gate/nginx -t и native TLS
MQTT/HTTPS/RTP прошли. CRL/OCSP и E2E видео этой проверкой не подтверждены.
См. [handoff выпуска](../tasks/completed/2026-10-04-media-tls.md).

## 2026-10-01: consolidation of accepted18D build flow

Source-built Janus Dockerfile/private entrypoint, registry scripts, immutable
baseline and historical18D handoff restored into main. Newer ingress lifecycle
code/current digest defaults retained; optional image overrides use env.
Routine builds reuse the frozen Janus digest; a rebuild requires explicit scope.
Image-layer scanner rejects unrecognized layouts instead of claiming success.
Linux C unit/build, script syntax, scanner tests and quality checks passed;
actual image smoke was not rerun (local Docker engine unavailable). No deploy.
See [consolidation handoff](../tasks/completed/2026-10-01-server-stack-consolidation.md).

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

## HEVC P0 — 2026-10-22

[Media/siplite contract](../../docs/etran_arch-hevc-media-siplite-contract.md) фиксирует descriptor и совместимость. Это specification/offline evidence, не реализация /api/v2 и не подтверждение Janus HEVC runtime. [Handoff](../tasks/active/2026-10-22-hevc-contracts-p0.md).
