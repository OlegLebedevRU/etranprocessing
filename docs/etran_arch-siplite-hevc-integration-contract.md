# siplite ↔ l4media: интеграция камер с основным режимом H.265

Редакция документа: `siplite-hevc/1.1`, 2026-10-23.
Статус: контракт на реализацию siplite и согласованное расширение серверных схем для камер.
Базовый реализованный wire contract: `hevc-contract/2.0.0`, `contract_version=2`.
Машинное расширение camera-схем зафиксировано отдельно как `siplite-camera/1.0.0`: [package и negotiation](../contracts/siplite-camera-v1/README.md). Это контракт к реализации readers/writers, не свидетельство их развёртывания. Frozen revision не изменён.

## 1. Принятые решения и граница работ

**H.265 — основной режим новой интеграции.** H.264 сохраняется как явно выбираемый режим совместимости и как прежний рабочий путь существующих установок. Новая версия не переводит действующий H.264 publisher на другой кодек без отдельного действия владельца. По уточнению заказчика camera Auto автоматически выбирает H.264 только **до START** при подтверждённой недоступности H.265 и готовом H.264 тракте. Явный запрос H.265 не подменяется; после START runtime-ошибка H.265 не вызывает автоматический переход на H.264 или новый START.

Камеры уже способны выдавать H.265. Камера кодирует изображение, siplite принимает готовый поток и передаёт его в существующий медиатранспорт. siplite не должен реализовывать encoder, транскодирование, Media Foundation, GPU probe или изменение качества через повторное кодирование. Допускаются разбор RTP/параметрических наборов и необходимая перепакетизация без изменения закодированного изображения.

siplite имеет средства управления некоторыми настройками камеры. Какие именно настройки читать, изменять и проверять обратным чтением, исполнитель определяет по фактическому API/firmware камеры. Этот документ не требует обязательного применения предустановки, конкретного bitrate, raster, FPS, GOP или vendor-specific команды.

**Схемы можно расширять для реальных потоков камер, сохраняя совместимость desktop-трансляторов l4capture.** Поля descriptor должны честно описывать поток; нельзя подставить подходящий серверу профиль вместо фактического.

Текущая задача — документ и переносимые машинные источники. Она не меняет firmware, MQTT-клиент, серверы, frozen package или конфигурацию камер.

## 2. Владельцы и взаимодействие

| Участник | Ответственность |
|---|---|
| Камера | Готовый H.265/H.264 bitstream и фактические настройки encoder |
| siplite | Камерный адаптер, проверка формата, relay, при наличии — управление камерой и feedback |
| MenuBuilder BFF | Пользователь, tenant, права, разрешённый просмотр и оркестрация |
| IoT/app1 | Control lease, publisher identity, команды и состояние источника |
| l4media | Service API, validation, route, lease-bounded lifecycle, Redis, Janus mountpoint |
| Janus и браузер | Согласование SDP/WebRTC и фактическое декодирование |
| ProcessingBackend | Прежний сертификатный flow; новых миграций этим контрактом не вводится |

| Направление | Канал / версия | Identity / повторы | Срок / результат |
|---|---|---|---|
| Камера → siplite | Существующий камерный RTP transport | Один выбранный source; PT не определяет codec | Фактический битстрим и доступность камеры |
| BFF → l4media | Service HTTP `/api/v2/media/*` | session + operation + publisher + lease + immutable descriptor | Авторитетный lease deadline; HTTP response |
| Control plane → siplite | Существующий камерный control flow, адаптация при реализации | Сохранять командную корреляцию и stream epoch | Командный ACK отдельно от готовности потока |
| siplite → ingress | Существующий TLS/L4RTP/1 | SN/certificate boundary, одна активная RTP session | Прежние transport/lease watchdog |
| ingress → Janus → viewer | RTP → WebRTC | Publisher отдельно от watcher subscription | Fresh RTP отдельно от framesDecoded |
| Janus → siplite → камера | Reverse RTCP, если поддержан | Feedback только текущему publisher | Bounded queue; PLI/IDR ≤1 за 500 мс на stream |

QoS/retain неприменимы к HTTP/RTP. Новые MQTT topics и envelopes этим документом не вводятся. При реализации MQTT-клиента необходимо отдельно определить его тип по правилам проекта: Windows `svc_desk` не является типом siplite по умолчанию. Камерную source identity нельзя подменять desktop для прохождения валидации.

Существующая камерная интеграция использует `camera=openipc` и локальный source `cam:ip000001`; конкретное соответствие source/device/SN подтверждается в siplite. `device_id`, certificate SN и `source_id` — разные идентификаторы. Существующие RPC управления камерой не считаются автоматически расширенными на HEVC.

## 3. Машинные источники и приоритет

Рядом сохранены неизменённые снимки реализованного сервера:

- [hevc.schema.json](contracts/siplite-hevc/hevc.schema.json) — `$defs` frozen `hevc-contract/2.0.0`;
- [openapi-v2.json](contracts/siplite-hevc/openapi-v2.json) — реализованный service API;
- [media-start-camera-h265.json](contracts/siplite-hevc/media-start-camera-h265.json) — синтетический HTTP payload камеры в рамках текущей схемы;
- [provenance.json](contracts/siplite-hevc/provenance.json) — происхождение и SHA-256 файлов.

Нормативный aggregate SHA-256 полного исходного frozen package:
`f065b967cd10c2c709fb394ef9e6c2ecf74f77de060d88c9a821cc9069766ac4`.
Снимки — подмножество источников, не новый полный frozen package и не его перевыпуск.
Server source: `dcd3ea1c04a6be1e3fe3047f3d5a3bec8747081d`.

Для реализованного API точные типы, обязательность, enum и запрет неизвестных полей задают schema/OpenAPI. Для будущей camera-ветки требования раздела 5 должны сначала получить машинную форму и серверную реализацию. Этот текст не расширяет возможности уже запущенного сервера.

В исходных fixtures обёртка содержит `definition`, `valid`, `payload`; по сети передаётся только `payload`. Приложенный camera-example уже без обёртки. Его UUID/SN синтетические, deadline исторический: перед настоящим START нужны новые IDs и актуальный lease deadline.

## 4. Готовый поток камеры и существующие ограничения сервера

При подготовке siplite/камерный адаптер определяет фактические codec, profile/tier/level, размеры, FPS, bit depth/chroma, B-frames, parameter sets, GOP и RTP packetization. Данные camera configuration/readback и битстрима сопоставляются; наличие H.265 в перечне настроек ещё не доказывает текущий выход H.265.

Текущий frozen v2 использует общую ограниченную матрицу, исторически связанную с desktop-выпуском:

| Поле | Что сейчас принимает schema 2.0.0 |
|---|---|
| Video tracks | Ровно один `video-main`, `producer_to_viewers`; audio нет |
| H.265 | Main, `profile_space=0`, `profile_id=1`, `tier_flag=0` |
| HEVC level_id | 30, 60, 63, 90, 93, 120 |
| Pixel format | 8-bit, 4:2:0 |
| Width / height | Чётные, 2..1920 / 2..1080 |
| FPS | Целое 1..25; descriptor profile `low` ограничивает до 15 |
| GOP | `max_b_frames=0`, keyframe interval 250..5000 мс, repeated parameter sets |
| H.265 RTP | RFC7798, DON diff 0; Single NAL/FU, AP отдельно проверяется |
| H.264 v2 | Baseline, RFC6184, packetization mode 1; SPS-derived profile_level_id |

Это границы **текущего серверного validator**, а не требования к encoder внутри siplite и не универсальные пределы камер. Существующий H.264 v1 не ужесточается до ограничений H.264 v2.

Если реальный поток выходит за эту матрицу, исполнитель выбирает по фактической камере: согласованная настройка камеры, её подходящий готовый поток либо расширение camera-схем и принимающей цепочки по разделу 5. До расширения несовместимый запрос отклоняется явно. Обрезать объявленные width/FPS, скрыть B-frames, переименовать Main10 в Main или занизить level в descriptor нельзя. Перезапись VPS/SPS не входит в базовое требование relay.

### Проверенный набор как пример

TASK-40 подтвердил готовый поток камеры HEVC Main, 8-bit/4:2:0, 1920×1080@20, без B-кадров, с VPS/SPS/PPS и IDR каждую секунду; descriptor profile — `default`. Тестовый адаптер передавал видео без перекодирования, но отдельно нормализовал metadata Level 4.1 → 4.0 с проверкой неизменности VCL. Это особенность того теста, а не разрешение молча менять metadata в siplite. Native camera output Level 4.1 честно передаётся только после соответствующего расширения и проверки цепочки либо после согласованной настройки самой камеры.

Приложенный синтетический пример показывает уже соответствующий v2 поток Level 4.0 и не является записью камеры. Ни этот пример, ни набор TASK-40 не задают обязательные настройки для остальных камер. Bitrate и качество подбираются и измеряются отдельно; desktop MFT results не переносятся на камеры.

## 5. Расширение схем для камер

Нормативная новая revision — `siplite-camera/1.0.0`, wire остаётся `contract_version=2`. [hevc.schema.json](../contracts/siplite-camera-v1/hevc.schema.json) и [OpenAPI](../contracts/siplite-camera-v1/openapi-v2.json) задают отдельную camera-ветку. Запрос `X-L4-Camera-Contract: siplite-camera/1.0.0` явно согласует projection; capabilities сообщают camera_contract_revisions/camera_formats. Расширенный descriptor содержит camera_contract_revision. Без negotiation сохраняется frozen v2.

Начальная machine matrix представляет native2592×1944/Main8/420/level153 и ограниченный H.264 baseline/main/high; это допустимое описание, не подтверждение runtime decode. Архивный SPS камеры имеет level153 (5.1), а не примерный level123 (4.1). Никакая metadata normalization не разрешена. Для неподтверждённых форматов runtime_verified=false и START не допускается.

### Разделение camera и desktop

При расширении descriptor валидируется по `source_type`. Для `desktop` и `usb-camera` сохраняются существующие схемы, значения, обязательность, profile mapping, probe и encoder policy l4capture. Camera-ветка получает собственные definitions/правила; расширять общий track и тем самым случайно разрешать новые desktop-режимы нельзя. Значение `test` не является обходом camera admission.

Camera descriptor сохраняет общую структуру и identity. На первом этапе можно сохранить `profile_id=default` как совместимый идентификатор выбранного режима; параметры камеры при этом определяются её собственным track. Если нужны дополнительные имена профилей, они вводятся только в camera-ветке и согласуются с control plane. Desktop `low/default` и их смысл не меняются.

Camera-схема должна представлять фактические camera profile/level/tier, bit depth/chroma, raster/FPS, GOP/B-frames и поддерживаемую packetization независимо от desktop-лимитов. Конкретные допустимые значения и конечные пределы формируются по реальным камерам и проверкам принимающего тракта при реализации. Никакие новые максимумы, поддержка Main10/HDR/4K, B-frames или AP этим документом не объявляются уже работающими.

Сохраняются ограниченные JSON types/ranges и ограничения размера запроса. Если конкретная камера требует дробного FPS, иной структуры fmtp или дополнительных полей, для них сначала вводятся точные wire types/units и отрицательные fixtures. Нельзя отправлять произвольный vendor JSON или свободную SDP-строку в descriptor.

### Admission и согласование

Необходимо различать три факта: камера выдаёт формат; siplite способен передать его; сервер/Janus/целевой браузер способен принять и декодировать его. Успех любого одного звена не означает готовность остальных.

Существующий `GET /api/v2/media/capabilities` недостаточен для сообщения произвольной camera-матрицы. Расширение должно дать consumer явный способ определить поддержку camera-ветки и её форматов. Точное имя/место новых capability-полей закрепляется в новой schema revision и OpenAPI вместе с request/response examples до изменения writer. Догадываться о поддержке по HTTP200, версии контейнера или наличию `h265` нельзя.

Для нерасширенного сервера camera writer использует только текущий валидный v2 descriptor либо получает явный отказ. Запросы с новыми полями/значениями не отправляются старому validator. Старые l4capture продолжают прежние v1/v2 запросы и ответы без обязательных новых camera-полей. Rich camera capability не заполняется выдуманными Windows adapter/driver/MFT сведениями: нужен явный camera producer adapter и его собственная binding/проверка готового потока.

### Выпуск расширения

1. Зафиксировать фактические camera outputs и желаемую delta к текущей схеме.
2. Назначить новую согласованную schema revision, определить negotiation, обновить schema/OpenAPI и связанные manifest/pins. Frozen `2.0.0` не менять задним числом.
3. Обновить camera validators/adapters в l4media, IoT и BFF, включая Redis restore, health, renew/stop, actual descriptor и whitelist SDP/fmtp. Janus/browser поддержку проверить отдельно.
4. Проверить, что desktop definitions и прежние запросы/ответы l4capture сохраняют совместимость. Если потребуется смена wire version, оставить dual readers старой версии.
5. Развернуть совместимые readers до camera writers; включать новый camera-формат только после negotiation и decode-проверки. Новая firmware не должна зависеть от неразвёрнутой схемы без явного отказа.

Новая schema revision и точная machine delta находятся в отдельном camera package; wire2 и frozen2.0.0 сохранены. Развёртывание readers, camera writers и реальная приёмка учитываются отдельно в release handoff.

## 6. Service API и descriptor

API вызывается доверенным серверным orchestrator с service auth (`Bearer` либо `X-Media-Service-Token`), а не браузером или камерой. siplite получает только необходимые transport/stream параметры; Janus admin credential и viewer PIN ему не выдаются.

| Path | Метод / definition | Результат |
|---|---|---|
| `/api/v2/media/capabilities` | GET / `media_capabilities` | Фактические service formats/dependencies/runtime verification |
| `/api/v2/media/sessions/start` | POST / `media_start` → `media_response` | 201 создание, 200 идемпотентный replay |
| `/api/v2/media/sessions/renew` | POST / `media_renew` → `media_response` | Продление своей эпохи после control lease renew |
| `/api/v2/media/sessions/stop` | POST / `media_stop` → `media_response` | Остановка своей эпохи; известный stopped replay — 200 |
| `/api/v2/media/sessions/{session_id}` | GET / `media_health` | Query `sn`, `lease_id`, `stream_instance_id`, `descriptor_revision` |

Структура START: `contract_version`, `session_id`, `operation_id`, `publisher={sn,stream_instance_id}`, `lease_id`, `lease_expires_at`, `descriptor`. В descriptor находятся `descriptor_revision`, `source_type=camera`, фактический `source_id`, `geometry_generation`, `profile_id`, `tracks`. Не дублировать source внутри publisher или tracks в корне запроса.

Все identity UUID различают lease, operation, media session, stream и descriptor revision. `lease_expires_at` — Unix seconds из авторитетного deadline, с округлением вниз; CTL `*_ms` — milliseconds. `geometry_generation` — поколение конфигурации источника, не требование наличия desktop geometry. Смена источника/формата создаёт новый descriptor и stream, а не правит действующий объект.

Для примера `$defs.media_start` см. [camera payload](contracts/siplite-hevc/media-start-camera-h265.json). Optional `pin` относится только к service API; без него сервер создаёт защищённый PIN, а не открытый mountpoint. Service response несёт сохранённый descriptor, identities, route generation, state и внутренние данные маршрута. BFF выдаёт watch data только авторизованным viewer.

Renew передаёт `contract_version`, `sn`, `session_id`, `lease_id`, `stream_instance_id`, `descriptor_revision`, `lease_expires_at`; STOP — ту же identity и `reason`, без deadline. Полные определения — в schema. Старые STOP/renew не затрагивают новую эпоху. Тот же operation/session с другим descriptor или другой живой publisher на SN — конфликт, без destroy/recreate чужого mountpoint.

HTTP errors читать по реализованному OpenAPI и body `rejection`: 400 malformed/schema payload, 401 service auth, 409 identity/conflict, 410 expired, 422 unsupported, 502 Janus failure, 503 capacity. Конкретные code/status pairs сверять с handler/OpenAPI; не переносить legacy HTTP ошибки в новый словарь автоматически. Отказ ensure на повторном START не считается успешным replay.

JSON: UTF-8, ≤32768 bytes, duplicate keys и неизвестные поля запрещены. UUID canonical, source ID ≤63 символов, SN соответствует schema. Секреты, camera credentials и URL с auth не входят в descriptor/logs/fixtures.

## 7. Запуск, смена настроек и прекращение потока

1. Авторизованный новый camera запрос по умолчанию использует Auto с приоритетом H.265. До media prepare/START выбрать H.265 при свежей готовности камеры, relay, server/Janus и браузера; при подтверждённой недоступности H.265 и готовом H.264 выбрать H.264 с видимой причиной. Unknown/auth/timeout не являются несовместимостью. Явный H.265 не подменять. Старые legacy H.264 и desktop Auto сохраняют семантику.
2. Камерный адаптер определяет текущий/подготавливаемый выход камеры и поддержку relay. При необходимости настройки применяются по реально доступному camera API с readback; изменять активную камеру под зрителями нельзя.
3. Orchestrator проверяет lease, source, формат и server capability; фиксирует concrete descriptor. Для H.265 путь не продолжается через implicit H.264 v1. Auto selection и причина фиксируются до START; resolved codec всегда concrete и совпадает с actual descriptor. Автоматическое H.264 решение не выдаётся за успешный explicit H.265.
4. Service API готовит route/mountpoint; затем producer получает START той же эпохи. Если камера уже отправляет UDP, relay до разрешённой эпохи не публикует поток на новый route.
5. Адаптер подтверждает actual format. READY означает готовность пересылать заявленный поток; fresh RTP и browser decode подтверждаются отдельно. Для siplite не вводится требование Windows `encoded_sequence` probe.
6. Ошибка после media prepare вызывает scoped compensation/STOP. Повторы не создают второго publisher. Lease renew не меняет descriptor и не заменяется RTP keepalive.
7. STOP, expiry, producer/transport failure закрывают текущую попытку и подписки её эпохи. После разрыва TLS новый запуск требует актуальной авторизации и ручного действия; автоматического START или codec fallback нет.

Если камера может выдавать одновременно main/substream, выбор всё равно даёт один source и один video track на SN. Multi-camera и дополнительный track требуют отдельной задачи.

Изменение codec/profile/размеров/FPS/других согласованных параметров требует прекращения старой эпохи и нового согласования. Незапрошенная смена формата камерой не маскируется обновлением descriptor задним числом; текущая попытка завершается с соответствующей причиной.

## 8. RTP, transport и feedback

L4RTP/1 не меняется: preamble `4c345254 01 00 <SN byte length: BE16> <SN UTF-8>`; далее `<type:u8><reserved:0><length:BE16><packet>`. Type 1 — RTP, type 2 — RTCP; обратный type 2 также занят RTCP. Сохраняется существующая TLS/certificate↔SN граница, plain ingress не становится публичным.

Для H.265 используется согласованный RFC7798, для H.264 — прежний согласованный RFC6184. Clock video — 90 kHz; PT согласуется по descriptor, не означает codec сам по себе. У частей access unit общий timestamp, marker завершает AU, sequence и fragmentation корректны. Перепакетизация сохраняет закодированное изображение и согласованный выходной PT/SSRC/порядок.

Параметрические наборы и точка входа доступны первому и позднему viewer: VPS/SPS/PPS + IDR для базового H.265 режима, SPS/PPS + IDR для H.264. Если камера использует иной random-access режим, он отдельно описывается и проверяется при расширении, а не объявляется эквивалентным IDR без доказательства. AP, DONL, PACI/interleaving требуют отдельной проверки/согласования; прозрачный relay не доказывает их поддержку браузером.

MTU и очереди определяются существующим транспортом и измерениями камеры; лимит native l4capture не навязывается siplite. Очереди ограничены; при смене поколения старые пакеты не доставляются в новый stream. RTCP/keepalive не обновляют RTP freshness.

Feedback `pli/fir=true` означает, что siplite действительно может принять запрос и добиться соответствующего действия камеры. Если этого нет — `false`, периодические точки входа и измеренный late join. Сервер коалесцирует keyframe requests ≤1/500 мс на publisher; очередь ограничена. Количество viewer не создаёт N publisher или N перезапусков камеры.

## 9. Ошибки, наблюдатели и совместимость

Camera failure относится к `camera`, forward/network — к `transport`/`producer` по фактической причине, не к Media Foundation. Используются согласованные коды, включая `codec_mismatch`, `network_failure`, `producer_lost`; если нужен новый camera-specific code, он добавляется в новую schema revision, а не отправляется неизвестным enum.

Сохраняются признаки terminal HEVC outcome: `scope=stream`, `current_stream_terminated=true`, `auto_restart_allowed=false`, `attempt=0`. `recovery_class` описывает условия новой ручной попытки. siplite может оставаться online для управления камерой после остановки media. Способ адаптации существующих camera events к серверному состоянию необходимо реализовать и проверить; Windows CTL envelope не приписывается прошивке без исследования.

Watcher join/leave не меняет publisher codec/deadline и не создаёт независимую control lease. Viewer наследует codec активного потока; leave закрывает только его подписку. Terminal outcome закрывает подписки соответствующего stream; поздняя ошибка A не закрывает новый B. Авторизация watcher проверяется отдельно от media fan-out.

| Сочетание | Требуемое поведение |
|---|---|
| Новые server + siplite, запрос нового camera потока | По умолчанию H.265, честный descriptor, проверенный decode |
| Явный H.264 / прежняя установка | Рабочий H.264 путь и прежний legacy wire сохраняются |
| H.265 недоступен / camera extension не согласован | Explicit H.265 — отказ; Auto может выбрать готовый H.264 только до START по согласованной policy |
| Runtime HEVC failure | Завершение текущего stream; ручной новый START |
| Старый/новый l4capture | Прежние desktop schemas, profiles, v1/v2 negotiation и encoder policy |
| Old server + camera writer с новыми полями | Новые поля не посылаются без negotiation; текущая схема либо отказ |

Redis сохраняет полный descriptor/identity/deadline. Восстановление v2 без descriptor не превращается в H.264. Перед откатом readers с новой camera-схемой новые publisher блокируются, активные расширенные потоки завершаются, storage readers сохраняются совместимыми. Recreate BFF/app1 не считается проверенным способом бесшовного отката живых H.264: TASK-60 зафиксировал прерывания.

Голос/audio, новые права viewer и автоматическое восстановление не входят в эту версию.

## 10. Приёмка и передача исполнителю siplite

Серверный TASK-40 подтвердил HEVC decode и fan-out на тестовом камерном источнике. TASK-60 передал схемы/OpenAPI, но не проверил siplite firmware и не приложил реальный обезличенный camera RTP sample. Native release blockers не являются доказательством отказа camera relay, а server-ready не является приёмкой siplite.

Исполнитель должен представить camera model/firmware и siplite revision, actual settings/bitstream, schema revision, server image/source revisions, target browser и результаты:

- H.265 как основной режим: один publisher, настоящий рост decoded frames и отсутствие re-encode.
- Явный H.264 и существующая H.264 установка: start/renew/stop и decode без регрессии; для l4capture — прежние запросы/ответы и корректная desktop validation.
- Если camera-схема расширена: реальная камера за пределами прежней матрицы, положительные/отрицательные schema vectors, capability negotiation; desktop отрицательные случаи остаются отрицательными.
- Первый кадр, 1/3 разрешённых watcher, late join, detach одного без остановки остальных; отдельная проверка view-role authority и outcome трёх полных UI sessions.
- Runtime camera failure, RTP silence, TLS loss, lease expiry: bounded stop, причина в UI, отсутствие автоматического START/fallback.
- Идемпотентный START, конфликт другого publisher, stale STOP/renew/READY/error A после ручного запуска B; B продолжает декодироваться.
- Restore/cleanup route, Janus и Redis; owner исчез без браузерного cleanup; старые эпохи не оживают и не блокируют новый разрешённый START.
- Проверка фактических AP/FU/DONL, parameter-set/keyframe cadence, MTU/очередей и feedback; неподдержанный feedback честно выключен.
- Согласованный обезличенный RTP sample с параметрами и checksum, без auth/credentials. Синтетические пакеты и счётчики не заменяют sample/decode.

Критерии измерения latency/quality и выбранные настройки камеры фиксируются по факту реализации; desktop бюджеты не переносятся на камеры автоматически. Существующие security/lease deadlines при этом сохраняются. Результаты разделяются на schema/static, integration и реальный camera/browser E2E; NOT RUN не считается PASS.

## 11. Источники и оставшаяся реализация

Camera RPC/gauge84 и закрытый credential flow — upstream IoT `docs/siplite-camera-control.md`. Постоянный local API для tool l4siplite задан [local schema](../contracts/siplite-camera-v1/local-camera.schema.json); камера подключается только к physical port2 в switch/ports. Дополнительные сеты управления отложены. Cloud credentials в descriptor/media route не попадают.

- Локальный пакет исследования: `D:/.codex/hevc-plan/siplite-task60/` (README, frozen package, OpenAPI); immutable server source указан в разделе 3.
- Результаты: `D:/.codex/hevc-plan/TASK-40-HANDOFF.md`, `TASK-60-HANDOFF.md`, `TASK-60-ACCEPTANCE.md`; evidence датировано 2026-10-22/23, live-проверка в этой задаче не выполнялась.
- [Существующий camera flow](etran_arch-openipc-camera-flow.md) и [панель камер](menu_video-openipc-cameras.md) описывают H.264 базу; этот документ задаёт её развитие с основным H.265 и не утверждает, что firmware уже изменена.

Следующая реализация: исследовать siplite camera adapters; выбрать реальные camera settings; определить минимальную camera schema delta; реализовать совместимые серверные readers и negotiation; затем relay/writer и целевую приёмку. Пакет `02-L4MEDIA-SIPLITE-CONTRACT.md` в старом HEVC плане остаётся историческим эскизом и не используется как wire specification.
