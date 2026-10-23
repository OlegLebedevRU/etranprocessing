# HEVC: фиксированный контракт 2.0.0 (TASK-00)

Source-selection extension,2026-10-24: see [hevc-source-probe/1.0](hevc-selected-display-probe.md). The frozen2.0.0 package remains unchanged.

Дата: 2026-10-22. Revision: `hevc-contract/2.0.0`. Уровень доказательства:
static + offline conformance, без integration/deployed/real-hardware.
Нормативная машинная форма — [пакет](../contracts/hevc-v2/README.md),
его [manifest](../contracts/hevc-v2/manifest.json) и CI pin.
[Общий план](etran_arch-hevc-rollout-plan.md) определяет этапы;
[media/siplite](etran_arch-hevc-media-siplite-contract.md) — внешний producer flow.

## Task intake

- Цель/тип: P0, спецификация и исполняемые контрактные примеры. Только TASK-00.
- Границы: docs, JSON Schema, golden vectors и их offline CI в трёх репозиториях.
  Product code, MQTT/HTTP подключения к стенду, migrations, deploy и capture не меняются.
- Владельцы: native/IPC — l4tools; CTL/lease — IoT; media API/Janus — l4media;
  пользовательская оркестрация/проекция — MenuBuilder; сквозной пакет — координатор.
- Producer → transport → consumer: BFF → service HTTP → IoT → MQTT CTL → l4desk;
  producer → L4RTP/1 → ingress → Janus → браузер.
- Инварианты: immutable codec per stream, прежний lease admission, stop on HEVC failure,
  отсутствие automatic retry/fallback, транскодирования, audio и новых прав.
- Проверка: строгие schemas, негативные vectors, неизменённый IoT v1 reader,
  stateful сценарии, equality/hash пакетов, документационные ссылки.
- MQTT type `svc_desk` подтверждён пользователем; клиент в P0 не изменяется.

## Исходная база и владельцы

| Repo | Ветка/полный SHA плана и текущая исследованная база | Delta исходников к плану |
|---|---|---|
| etranprocessing | main `4d4be68d45a197b4390af6e0c29e7bb6225c0789` | нет |
| iot-rpc-rest-app | master `36814a1705216520bb841b6e52e57bdde07f9de7` | нет |
| l4tools | main `8fb1e730779d0544b97ba8fb3c1ebebe2bd0ca06` | нет |

Рабочие копии P0 созданы отдельно от пользовательских деревьев. Исходный локальный
etranprocessing HEAD `b0a4545816ccf8cc0267719e8d4bb7f005d1c007` отставал на один
коммит; исходный IoT HEAD `8cd723367c09f5aa45b71c98dc4d3a89ac69612d` тоже отставал.
Исследована актуальная база из таблицы. Копии native в etranprocessing не используются.
Immutable ссылки и список точек интеграции — [handoff](../.agent-context/tasks/active/2026-10-22-hevc-contracts-p0.md).

## Имена и версии

CTL использует только `v:2`; media service — только `contract_version:2`.
Поля `schema_version`, `resolved_codec:auto`, `desktop-medium` не используются.
Исходные 01/02 содержали эскизы: здесь окончательно выбрано вложенное поле
`descriptor` вместо дублирования source/track параметров на каждом уровне.

`descriptor` содержит `descriptor_revision` (UUID), `source_type`, `source_id`,
`geometry_generation`, `profile_id`, `tracks`. В CTL источник — desktop/usb-camera;
media дополнительно допускает camera/test. `publisher` в media — только `sn` и
`stream_instance_id`. Session ID, lease ID, operation ID и descriptor revision
не взаимозаменяемы. `descriptor_revision` — идентификатор immutable содержимого,
а не доказательство авторизации и не криптографический hash.

Codec policy существует только до подготовки: `auto|h264|h265`.
Исполняется только concrete `resolved_codec:h264|h265`, совпадающий с треком.
Новый BFF запрос с явно переданным `codec_policy` выбирает расширенную проекцию;
отсутствие поля сохраняет прежний ответ и означает H.264 **при создании** потока.
Viewer join не запускает producer и наследует его формат независимо от policy.

## Bootstrap, CTL и корреляция

1. Агент публикует прежний v1 presence с добавленной строкой `video_contract_v2`
   в существующем `capabilities[]`. Никаких новых полей v1 и rich retained presence.
2. Авторизованный сервер, обнаружив строку, отправляет `$defs.inventory_get` v2
   с новым UUID `command_id`, SN, `issued_at_ms`, `expires_at_ms`.
   Это read-only inventory, поэтому lease_id отсутствует; право опроса SN обязательно.
3. Ответ `$defs.capabilities_ack` — v2 ACK `result:inventory`, тот же command_id/SN,
   вложенный `video_capabilities`. Принимать только ответ на живой pending request
   того же SN и boot epoch; unsolicited/retained ACK не является negotiation.
4. По timeout, смене boot ID, reconnect с неподтверждённым состоянием, отключению
   feature gate или откату сервера negotiation/cache инвалидируются. Повторный probe
   только по новому запросу с cooldown; v1 presence продолжается всегда.
5. Старый сервер не шлёт v2. Новый агент не посылает ему v2. H.264 старого запроса
   использует прежние v1 START/ACK/stop/events и IPC; extended H.264 допустим только
   после явного opt-in. HEVC никогда не проходит через v1 START или IPC v1.

| Направление/владельцы | Канал | Schema | QoS/retain | Идемпотентность | Срок | Ошибка/совместимость |
|---|---|---|---|---|---|---|
| IoT → l4desk | `srv/{SN}/ctl` | inventory_get/start v2 | 1/false | command_id + owner/stream + descriptor | команда ≤30 s | v2 только negotiated |
| l4desk → IoT | `dev/{SN}/ctl` | capabilities_ack/ready/error v2 | 1/false | pending command / stream epoch | probe freshness ≤60 s | v1 unsolicited presence |
| BFF → l4media | `/api/v2/media/sessions/start` | media_start | N/A | operation/session/stream + descriptor | lease deadline | HTTP отказ; v1 отдельно |
| BFF → l4media | `/api/v2/media/sessions/renew`, `/stop` | media_renew/media_stop | N/A | SN+session+lease+stream+revision | lease deadline | старая эпоха не меняет новую |
| BFF ← l4media | `/api/v2/media/capabilities` | media_capabilities | N/A | read-only | без кеширования ready через restart | service auth обязателен |
| producer → ingress | TLS/L4RTP/1 | неизменённый framing | N/A | одна объявленная RTP session | прежние watchdog | кодек только в descriptor |
| Janus → watcher | WebRTC | SDP согласованного track | N/A | subscription отдельно от publisher | доступ watcher | decode проверяет браузер |

## Capability и лимиты

Новые v2 объекты строго запрещают неизвестные поля на каждом уровне. Все поля
schema обязательны, если не указано иначе; дополнительных default или null нет.
Исключение — обязательный `reason_code:null` при ready. Legacy модели не ужесточаются.
JSON — UTF-8, не более 32768 bytes до разбора, повторяющиеся object keys запрещены.
UUID — canonical 36 символов; SN — 1..64 ASCII `[A-Za-z0-9._-]`, source ID — 1..63,
revision/environment strings — 1..128; человекочитаемая ошибка — максимум160.
Массив video — 1..4, profiles — 0..8, tracks — ровно1, server formats — 1..2.
Количество serialized bytes проверяется отдельно от JSON Schema. Глубина фиксирована
схемой; неограниченных словарей и произвольной строки SDP/fmtp нет.

Времена `*_ms` — целые Unix milliseconds, положительные, ≤5000000000000.
Media `lease_expires_at` — Unix seconds, округлённые вниз из авторитетного deadline.
TTL команды ≤30 s; `issued_at_ms <= now < expires_at_ms <= lease_expires_at_ms`
для START; просроченное или будущее сообщение не исполняется. Прежние v1 допуски
времени сохраняются. Probe: бюджет5000ms, cooldown30000ms, freshness максимум60000ms,
`checked_at_ms <= now < valid_until_ms`, интервал ≤60000ms. Эти новые бюджеты —
нормативный выбор P0, а не заявление о существующем hard timeout в драйверном вызове.
TASK-50 обязан обеспечить внешний watchdog probe-процесса.

Capability привязана к agent_instance_id, process_arch, adapter_id, driver_revision,
component_revision, interactive_session_id, source_id, geometry_generation и
конкретному проверенному profile/track. Меняется любое из них — старый ready
не годится. Положительный probe не резервирует MFT и не гарантирует runtime init.
Для Windows HEVC требуются backend=media_foundation, hardware=true,
status=ready, probe_evidence=encoded_sequence и точный проверенный режим.
Сведения экрана/его содержимое в capability не передаются. Camera capability
не обязана использовать Windows MFT; выдаётся отдельным producer adapter.

Статусы и reason codes перечислены в schema. После HEVC runtime failure:
temporarily_unavailable до нового успешного probe; агент остаётся online.
Auto при неизвестном/stale/unsupported HEVC или неподтверждённой серверной цепочке
выбирает H.264 **до** commit descriptor. Explicit HEVC получает отказ. Auto после
выбора HEVC не повторяет START с H.264, даже при suggested_action=retry_same_codec.

## Профили, track и SDP

| profile_id | Реальный UI/источник | Native request ID v1 | Верхний бюджет v2 |
|---|---|---|---|
| low | Medium | 1 | ≤1920×1080, ≤15fps, только проверенный capability raster |
| default | HD | 3 | ≤1920×1080, ≤25fps |

Доказательства: SourceSelector.tsx; l4capture_adapter.c:profile_to_id;
video_profile.h:L4C_PROFILE_REQ_*; main.c native raster overrides.
Старые alias `480p`/`540p` и wire2 сохраняются в v1, но не добавлены в v2.
Имена внутренних actual_id (`base_480p`,720p) не заменяют requested profile_id;
При сверке выявлено расхождение: README описывает Medium≤1280×960, а main.c
без quality max_width override допускает1920×1080. В P0 сохранён фактический
предел кода и обязательный точный probe, как требует план сохранения размеров;
новое ограничение по одному тексту README не введено. Вопрос вынесен пользователю,
после отсутствия уточнения применён этот вариант без изменения product code.
low не означает обязательные854×480. Пропорции и малый native raster сохраняются,
нечётные размеры округляются вниз до чётных, увеличение запрещено.
Проверенный width/height/fps передаётся явно; bitrate HEVC не наследуется как обещание
качества H.264 и не фиксируется без замеров.

Трек video-main: 8-bit420,90kHz, dynamic PT96..127, B frames=0,
keyframe interval250..5000ms, repeat_parameter_sets=true. HEVC: Main, profile_space0,
profile_id1, tier0, level_id из schema (максимум120), RFC7798, DON diff0.
H.264 extended v2: baseline, RFC6184, packetization-mode1, profile_level_id из SPS.
Допустимость level для фактического raster/rate и совпадение реальных SPS/VPS
обязательно проверяются producer и READY; schema не заменяет bitstream parser.
Выбранные максимумы — предел релизного контракта, не обещание поддержки каждого GPU.

SDP строится из whitelist полей выбранного трека. Для HEVC не копируются
H.264 profile-level-id/packetization-mode; clock/PT не определяют codec сами по себе.
HEVC RTP описан [RFC7798](https://www.rfc-editor.org/rfc/rfc7798.html),
H.264 — [RFC6184](https://www.rfc-editor.org/rfc/rfc6184.html).
HEVC Single NAL/FU без DONL/PACI/interleaving; camera AP требует отдельной conformance
проверки. VPS/SPS/PPS и IDR доступны первому и позднему viewer. RTCP остаётся feedback,
его нельзя использовать для голоса. Audio и второй track всегда отвергаются.

## Prepare, START, READY и partial failure

1. BFF проверяет пользователя, tenant, video:view/stream scope, policy, lease owner,
   source и boot/probe freshness, а также server capabilities/runtime verification.
2. До запуска фиксирует concrete descriptor, operation_id и новый stream_instance_id.
   Несовместимый existing publisher не заменяется. Prepare ограничен lease deadline.
3. Создаёт media route/mountpoint с тем же descriptor через media_start.
4. Передаёт START с теми же identity/descriptor; командный срок не продлевает lease.
5. READY (`stream_event`,state=running,confirmation=agent_ready) включает полный
   actual_descriptor. Любое несовпадение — descriptor_mismatch и остановка своей
   попытки, никакой silent adjustment. READY не утверждает поступление RTP/decode.
6. ingress_rtp_seen и browser_first_frame_decoded — отдельные факты. Browser проверяет
   рост framesDecoded. webrtcup и cumulative RTP count не доказывают изображение.

Отказ media prepare: START не отправляется, освобождается своё reservation.
Отказ/timeout START после успешного route: направить bounded STOP своей эпохе,
закрыть свою media session; ambiguous ACK не разрешает новый автоматический START.
Cleanup должен быть идемпотентным; запоздалый результат не трогает новый поток.
Release input относится только к связанному producer; console/files не закрываются
по одному SN. После failed поток terminal; capability не равна состоянию агента.

Идемпотентность проверяется после auth/owner/lease. Сравниваются operation/session,
stream, agent epoch, source, geometry, profile, codec, все track параметры и descriptor
revision; равная revision с другим содержимым — conflict. Одинаковый повтор возвращает
тот же результат без нового процесса. Иной active publisher на SN — conflict/busy.
Текущие unsigned служебные fingerprint/hash не являются правами доступа.

## Ошибки и legacy projection

Error сохраняется без потерь adapter → l4desk → IoT → BFF → UI.
scope=stream,current_stream_terminated=true,auto_restart_allowed=false,attempt=0.
recovery_class/retry_same_codec — информация о возможной новой ручной попытке,
которая создаёт новый stream UUID и проходит авторизацию. Ошибки capture/conversion/
transport не объявляются отказом GPU. Потерянный producer без события — producer_lost
с unknown reason/domain=producer; не выдумывать hardware evidence.

Для старого stream_event выдаются только прежние v,type,sn,stream_instance_id,state,
reason,timestamp. Для старого REST start — stream_instance_id,result,state по
существующей модели; stop — result,state. Сохраняются существующие optional/null
правила, но error,actual_descriptor,descriptor_revision и новые null поля не добавляются.
Для v1 ack/presence/inventory используется прежний writer; v2 объект не фильтруется
глобальным extra=ignore. Golden legacy формы и actual model snapshots находятся
в `contracts/hevc-v2/legacy`.

| Service code | HTTP | Семантика |
|---|---|---|
| invalid_payload | 400 | schema, unknown field, bounds, лишний/audio track |
| unauthorized / forbidden | 401 / 403 | нет service auth / нет tenant/scope/source права |
| lease_invalid / expired | 409 / 410 | нет живой аренды / deadline истёк |
| lease_mismatch, conflict, busy | 409 | чужая identity или занятый publisher |
| stale_capability, capability_mismatch | 409 | probe/epoch устарел или не относится к запросу |
| unsupported | 422 | codec/profile недоступен; нет fallback на исполнении |
| codec_mismatch, descriptor_mismatch | 409 | фактический формат не согласован |
| janus_unavailable / capacity_exhausted | 502 / 503 | prepare не выполнен, START запрещён |
| producer_lost | 502 | producer потерян, причина железа неизвестна |

Это v2 service dictionary. Legacy HTTP/NACK codes сохраняются прежними;
в частности legacy conflict/busy не получает новый response body.

## Publisher и watcher admission

IoT leases.py (in-memory acquire и Redis acquire) хранит одну active lease на SN.
Idempotent reacquire допустим для того же user/org/device **и owner_session_id**;
другой пользователь/сессия получает lease_taken. Files имеет отдельный drain guard.
Нельзя заимствовать publisher lease, продлевать её из watcher или ослаблять input
exclusivity. В P0 не проектируется новый endpoint независимой viewer аренды.

Watcher subscription identity отдельно от publisher stream_instance_id. Разрешённая
подписка связывается с реальным stream UUID, не строкой viewer. Leave закрывает
только её PeerConnection; явный owner stop останавливает publisher. Текущее событие
failed/stopped завершает все его подписки; событие другой эпохи игнорируется.
Три Janus подписки под разрешённой текущей сессией проверяют media fan-out,
но не доказывают допуск трёх независимых пользователей. Follow-up: отдельный дизайн
viewer admission с tenant/owner/session authorization до реализации новых leases.

## Хранение, совместимость и rollback

Redis v2 хранит version, полный descriptor, publisher/session/lease identities,
deadline и terminal error. Legacy запись без версии нормализуется в H.264 только
через legacy adapter. V2 без descriptor — invalid, никогда implicit H.264.
Watchdog использует прежний authoritative lease deadline; media renew его не создаёт.
STOP/renew старой эпохи не затрагивает новую; повтор STOP безопасен.

| Сочетание | Результат |
|---|---|
| старые server/agent/client | прежний H.264 v1 |
| новый server, старый agent, Auto | H.264, capability неизвестна |
| новый agent, старый server | v1 presence/CTL/IPC, никаких unsolicited v2 |
| новый server+agent, старый start | H.264 и прежний ответ |
| negotiated ready + explicit HEVC | один concrete HEVC descriptor |
| unsupported/stale + explicit HEVC | отказ до START |
| runtime HEVC failure | terminal stop, temporarily_unavailable, без retry |
| watcher join/leave | наследует формат / detach своей подписки |

Rollout: dual readers → UI с feature gate → G-SERVER-READY → новые агенты.
Rollback: запретить новые v2 prepare, завершить HEVC publisher и дождаться teardown,
отдельно сохранить/очистить v2 Redis state, инвалидировать negotiated capability,
потом откатывать readers. Нельзя давать старому reader живую HEVC запись или
считать rollback контейнера достаточным. IPC v1 CMD_START остаётся ровно70bytes;
IPC v2 и probe/info wire layout фиксируются в TASK-50, не внедряются здесь.

## G0 и отложенное

G0 спецификации означает один идентичный пакет во всех трёх repos, CI digest pin,
успешные offline vectors и legacy reader. Реальные handlers пока v1; выполнение
тех же vectors против dual readers/writers обязательно в TASK-10/20/30.
Это не G-SERVER-READY. Новые API и capabilities из schema пока не реализованы.
Отложены: independent viewer admission, audio, multi-camera, automatic recovery,
zero-copy guarantees, hardware matrix, RTP sample replay/Janus runtime acceptance,
IPC v2 layout и аппаратные значения bitrate/quality. Никаких release exemptions
или новых прав P0 не предоставляет.
