# l4media и siplite: media contract 2.0.0

TASK-00, 2026-10-22. Это контракт интеграции; реализация siplite не исследована
и не включена. Нормативная [общая спецификация](etran_arch-hevc-contracts.md),
[schema/fixtures](../contracts/hevc-v2/README.md) и их manifest имеют один revision
`hevc-contract/2.0.0`. API из этого документа в P0 не реализованы.

## Полномочия и идентичности

Камера кодирует; siplite передаёт encoded RTP; ingress маршрутизирует;
Janus обслуживает watchers. Перекодирования нет. MFT — требование Windows
l4capture, а не камеры. Один SN — один publisher, источник и video track.

BFF/IoT проверяет tenant, lease, источник и выбор кодека. Service-authenticated
media API создаёт route/mountpoint до первого RTP. Камера и siplite не получают
Janus admin credential или полномочия создавать mountpoints.
`publisher={sn,stream_instance_id}`, `descriptor={descriptor_revision,source_type,
source_id,geometry_generation,profile_id,tracks}`. Идентичности publisher, lease,
media session, operation и watcher subscription разделены.

## API

| Маршрут | Метод / schema definition | Поведение |
|---|---|---|
| /api/v2/media/capabilities | GET / media_capabilities | service auth, фактическая готовность зависимостей |
| /api/v2/media/sessions/start | POST / media_start → media_response | prepare до START producer,201 при создании,200 при replay |
| /api/v2/media/sessions/renew | POST / media_renew → media_response | только совпадающая identity и авторитетный lease deadline |
| /api/v2/media/sessions/stop | POST / media_stop → media_response | идемпотентное завершение только своей эпохи |
| /api/v2/media/sessions/{session_id} | GET / media_health | service auth; query sn,lease_id,stream_instance_id,descriptor_revision |

Health содержит transport.connected,rtp_packets,bytes,last_rtp_at_ms,rtp_fresh
и сохранённый descriptor/state без обещания decode. До первого RTP last_rtp_at_ms=null.
Не смешивать service response с viewer PIN/watch credentials. Авторизованная BFF
выдача Janus watch data сохраняет существующую модель доступа.
Service start допускает прежний optional `pin` вне descriptor. При его отсутствии
media создаёт непустой криптографически случайный watch PIN и сохраняет прежнюю
проверку доступа Janus; отсутствие PIN в fixture не разрешает анонимный mountpoint.
Service response содержит publisher/lease/operation identity, mountpoint_id,
rtp_port,rtcp_port,janus_ws (относительный путь) и optional pin только для
авторизованного BFF. PIN не передаётся producer и отсутствует в fixtures/logs.
Маршрут Janus RTP/RTCP внутренний; terminal START.delivery указывает на штатный
локальный proxy transport, а не открывает терминалу прямой доступ к Janus admin/RTP.
У всех HTTP ответов действует словарь ошибок из общей спецификации. Неподдерживаемый
track —422; malformed payload —400. Health чужой identity не раскрывает descriptor.
Повтор STOP завершённой известной identity —200,state=stopped; неизвестная/stale
identity —409,lease_mismatch, без изменения текущего publisher.

`media_capabilities` объявляет versions, codecs, formats (codec/profile/packetization/
clock), max_video_tracks=1,audio_supported=false,reverse_rtcp,server_revision,
dependencies_ready,hevc_runtime_verified. H265 объявляется только при подтверждённой
runtime поддержке Janus. Код/версия контейнера сами по себе её не доказывают.
Уровень/размер режима камеры дополнительно сверяется с matrix producer и Janus.

Golden запрос — [media_start](../contracts/hevc-v2/fixtures/media_start.json),
ответ — [media_response](../contracts/hevc-v2/fixtures/media_response.json).
Эти файлы содержат wrapper для verifier; HTTP body — только вложенный payload.
Пример содержит синтетический desktop source; камера заменяет source_type на camera
и source_id на фактический ID с новым descriptor revision до начала потока.
Профиль и bitstream обязаны входить в утверждённую матрицу; camera Main10/HDR/4K
не объявляются поддержанными этим выпуском.

## Совместимость и lifecycle

Прежние `/api/v1/media/sessions/*` и response shapes остаются H.264.
Внутри TASK-20 нужен общий lifecycle с v1 adapter, не отдельный независимый тракт.
V2 сохраняет полный descriptor в Redis и проверяет codec/fmtp при reuse Janus.
Равные session/operation/stream с другим descriptor —409; другой publisher на SN —409.
Нельзя destroy/recreate чужой живой mountpoint ради удовлетворения запроса.
Ошибка ensure mountpoint не превращается в active/replay success.
V2 Redis без descriptor invalid; legacy без версии нормализуется только как H.264.

Prepare → route → producer ready → RTP → browser decode. Отказ producer завершает
его попытку и уведомляет watchers. Producer остаётся доступен control plane,
если способен; ошибка камеры не называется hardware MFT failure.
Окончательный failed не разрешает автоматический retry, смену кодека или новый START.
Watcher join/leave не меняет publisher codec/deadline. Admission остаётся прежним:
Janus fan-out не создаёт право на независимый IoT lease.

## RTP и feedback

L4RTP/1, certificate/SN binding, reverse type0x02 RTCP и watchdog не меняются.
90kHz timestamp, marker последнего packet access unit, корректная sequence,
bounded queues, согласованный PT96..127. H264: SPS/PPS/IDR; HEVC: VPS/SPS/PPS/IDR,
RFC7798 Single NAL/FU, без DONL/interleaving/PACI. Камерный AP требует отдельной
conformance проверки в TASK-20, поддержка AP не обещана одним прозрачным ingress.
Параметрические наборы и точка входа нужны при старте и позднем viewer join.

PLI/FIR capability отражает фактическую способность siplite доставить feedback
камере и получить IDR. При отсутствии feedback обязательны периодические точки
входа и измерение позднего подключения. Конкретная реализация TASK-20 должна
коалесцировать запросы (не более одного upstream PLI/IDR request за500ms на stream)
и сохранить bounded queue/drop policy. RTCP/keepalive не обновляют RTP freshness.

## Будущий голос и доставка команде siplite

В этом revision audio/дополнительные tracks/directions отвергаются. Track identity
не выводится из SN/camera ID, codec/PT/clock принадлежат track. Будущий voice требует
отдельных прав, направления, transport/demux и версии контракта. Обратный RTCP
нельзя занять под звук. Video failure не является будущей командой stop voice.

Сейчас передаются schemas, JSON fixtures, ошибки и последовательности.
OpenAPI runtime и H264/H265 RTP sample replay выдаёт TASK-20, после реализации;
это не выполненные артефакты P0. Ответный siplite handoff должен описать реальный
camera codec/profile/level, mode switch, keyframe cadence, feedback, MTU/очереди,
TLS reconnect и certificate/SN binding. RTP counters не заменяют decoded frames.
