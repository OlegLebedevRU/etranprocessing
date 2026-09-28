
# Архитектурная записка: замена FFmpeg на нативный модуль `l4capture`

**Статус:** Предшествующая альфа-редакция (заменена финальным архитектурным релизом `l4capture_arch_final.md`)  
**Цель документа:** предварительная модель замены FFmpeg в desktop-трансляции L4Desk. Утверждённый архитектурный релиз с каскадом агентов и регламентом контроллера см. в [l4capture_arch_final.md](l4capture_arch_final.md).

---

## 1. Резюме решения

Предлагается заменить FFmpeg в подавляющем большинстве сценариев захвата рабочего стола отдельным узкоспециализированным нативным Windows-процессом:
```
text
l4capture.exe
```
Модуль отвечает только за:
```
text
захват выбранного desktop/display
→ захват и отрисовку курсора
→ масштабирование и конвертацию pixel format
→ H.264 encoding
→ RTP packetization
→ отправку RTP/RTCP на существующий localhost ingress
```
`l4capture` не должен становиться новым универсальным мультимедийным фреймворком или повторной реализацией FFmpeg.

Существующая orchestration-обвязка агента должна быть сохранена с минимальными изменениями:
```
text
stream_start / stream_stop / stream_renew
lease и fail-closed stop
single active stream
source/session validation
state machine
health-check и ограниченный restart
process ownership
MQTT ACK/NACK и stream events
```
На время rollout сохраняется backend-выбор:
```
text
backend=l4capture | ffmpeg
```
По умолчанию на поддерживаемых desktop-сценариях выбирается `l4capture`; FFmpeg остаётся временным rollback/fallback-путём для несовместимого агента, непокрытого сценария или обнаруженного production-дефекта. После подтверждения покрытия FFmpeg может быть исключён из основного пакета, но не до завершения compatibility acceptance.

---

## 2. Проблема и мотивация

Текущая desktop-трансляция использует отдельный процесс FFmpeg для:
```
text
GDI desktop capture
→ software H.264 encode
→ RTP localhost output
```
Такой подход функционален, но имеет системные недостатки:

1. **Избыточный runtime.** Для одного заранее известного pipeline поставляется крупный универсальный multimedia-инструмент.
2. **Непрозрачность lifecycle.** Мониторинг строится вокруг PID, pipe progress и текстового вывода вместо типизированных метрик и кодов ошибок.
3. **Ограниченный контроль capture backend-а.** Невозможно тонко и одинаково контролировать DXGI/GDI fallback, курсор, screen topology и аппаратное кодирование.
4. **CPU-нагрузка.** Software H.264 encoding может быть тяжёлым для слабых терминалов.
5. **Лицензионная и supply-chain площадь.** FFmpeg является широким набором библиотек и компонентов, хотя требуемая функция существенно уже.
6. **Необходимость поддержки heterogeneous Windows estate.** Одновременно нужны современный GPU-path и гарантированный legacy-path для Windows 7, VM и устройств без доступного encoder GPU.

Цель замены — не максимальная мультимедийная универсальность, а предсказуемая и ограниченная реализация единственной продуктовой функции: **захват выбранного desktop/display и выдача low-latency H.264 over RTP в существующий media ingress**.

---

## 3. Целевые архитектурные принципы

1. **Узкая специализация.** `l4capture` реализует только desktop-to-H.264/RTP; не включает контейнеры, запись файлов, звук, RTSP server, arbitrary filters или универсальный transcoding.
2. **Совместимый внешний lifecycle.** Существующий агент сохраняет команды запуска/остановки, lease, states, события и ограничения одной активной сессии.
3. **RTP ingress не меняется.** `l4media` и последующая WebRTC-цепочка получают совместимый RTP/H.264 поток на тех же локальных endpoint-ах и по согласованному media contract.
4. **GPU — оптимизация, не обязательное требование.** Отсутствие GPU encoder не должно само по себе блокировать basic desktop streaming.
5. **Windows 7 — поддерживаемая legacy-платформа.** На Windows 7 используется GDI capture и software H.264 либо явно проверенный legacy hardware path.
6. **Runtime capability probing.** Выбор pipeline выполняется по фактическому успешному созданию capture/encoder, а не по названию GPU.
7. **Fail closed для session/desktop policy.** При lock screen, secure desktop, смене сессии, недоступности выбранного display или истечении lease поток останавливается штатным контролируемым образом.
8. **Один владелец lifecycle.** `l4capture` не создаёт независимый watchdog и не управляет MQTT/session policy; ими продолжает владеть существующий агент/supervisor.
9. **Наблюдаемость как часть контракта.** В telemetry и stream state должны быть видны выбранные capture/encoder backend, fps, frame drops, error reason и факт аппаратного ускорения.
10. **Постепенный rollout и быстрый rollback.** В ранних релизах `l4capture` и FFmpeg сосуществуют за feature flag/backend policy.

---

## 4. Целевая схема компонентов
```
text
Remote session orchestration
    │
    ├─ validates session/source/profile/lease
    ├─ owns state machine and restart policy
    ├─ publishes ACK/NACK and stream events
    └─ starts exactly one child media process
    │
    ▼
l4capture.exe
    │
    ├─ validates startup parameters received from parent
    ├─ binds to designated interactive user session
    ├─ opens selected display/capture region
    │
    ├─ Capture layer
    │   ├─ DXGI Desktop Duplication backend, preferred
    │   └─ GDI BitBlt backend, legacy fallback
    │
    ├─ Cursor layer
    │   ├─ obtains cursor shape/position
    │   └─ composites exactly one visible cursor into frame
    │
    ├─ Frame pipeline
    │   ├─ crop selected display/region
    │   ├─ downscale according to selected profile
    │   ├─ frame pacing and drop policy
    │   └─ BGRA → NV12/I420 conversion
    │
    ├─ H.264 encoder layer
    │   ├─ Media Foundation hardware H.264 MFT, preferred
    │   ├─ optional vendor backend: NVENC / QSV / AMF
    │   └─ software H.264 fallback, subject to license decision
    │
    ├─ RTP/RTCP layer
    │   ├─ H.264 RTP packetization under RFC 6184
    │   ├─ SPS/PPS and IDR handling
    │   ├─ UDP RTP sender
    │   └─ minimal RTCP sender/receiver as required by ingress
    │
    └─ typed local status/metrics IPC
    │
    ▼
127.0.0.1 RTP/RTCP ingress
    │
    ▼
l4media → existing transport/media pipeline → WebRTC/browser
```
`l4capture` рекомендуется реализовать как **отдельный executable**, а не как DLL внутри основного агента.

Причины:

- сбой capture/encoder-кода не должен завершать MQTT/control agent;
- сохраняется хорошо понятная модель дочернего процесса;
- сохраняется process ownership через Job Object;
- можно независимо контролировать x86/x64 builds;
- проще сопоставить PID, start time, stream ID и crash diagnostics;
- fallback на FFmpeg может быть реализован без изменения внешнего жизненного цикла;
- проще ограничить IPC и привилегии.

---

## 5. Граница ответственности `l4capture`

### 5.1. В scope первой версии

| Функция | Обязательность |
|---|---|
| Захват выбранного display или прямоугольника desktop | Да |
| Windows 7-compatible GDI capture | Да |
| DXGI Desktop Duplication для современных Windows | Да |
| Захват курсора и его корректная отрисовка | Да |
| Frame pacing и drop late frames | Да |
| Resize/downscale в профильные разрешения | Да |
| BGRA → NV12/I420 | Да |
| H.264 encoding | Да |
| H.264 Baseline-compatible режим при необходимости ingress | Да |
| Отсутствие B-frames в low-latency режиме | Да |
| Управляемый GOP/IDR | Да |
| Ограничение bitrate | Да |
| RTP/H.264 packetization | Да |
| RTP на loopback ingress | Да |
| Минимально необходимый RTCP | Да |
| Typed progress/error reporting в parent process | Да |
| Защита от очереди кадров без ограничения | Да |
| Остановка по IPC/parent process termination | Да |

### 5.2. Явно вне scope первой версии

| Возможность | Решение |
|---|---|
| Аудио capture/encoding | Не реализуется |
| Запись в MP4/MKV/файл | Не реализуется |
| RTSP server/client | Не реализуется |
| SRT, RTMP, HLS, DASH | Не реализуются |
| Поддержка кодеков кроме H.264 | Не реализуется |
| Arbitrary video filters/effects | Не реализуются |
| Сцены, overlays, composition engine | Не реализуются |
| Универсальный video transcoding | Не реализуется |
| Захват защищённого контента | Не реализуется |
| Обход secure desktop/UAC/lock screen | Запрещён |
| Camera capture | Отдельный последующий scope; не является blocker desktop replacement |
| Мультиэкранный GPU composite | Не нужен для первой версии |
| Прямой WebRTC в агенте | Не реализуется: сохраняется RTP ingress |

---

## 6. Совместимость orchestration и обратный rollout

### 6.1. Что должно остаться совместимым

Внешняя control plane семантика не зависит от того, запущен `ffmpeg.exe` или `l4capture.exe`.

Должны сохраниться:
```
text
stream_instance_id
lease_id
source_id
profile
selected session
selected desktop/display rectangle
one-stream-per-terminal policy
start/stop/restart lifecycle states
source unavailable semantics
session unavailable semantics
lease expiry fail-closed behavior
process reconcile after agent restart
stream health checks
existing MQTT command and event flows
```
Внешние состояния должны остаться как минимум:
```
text
stopped
starting
running
stopping
restarting
failed
source_unavailable
session_unavailable
```
Можно добавить additive internal/telemetry reasons:
```
text
capture_backend_failed
encoder_unavailable
encoder_initialization_failed
rtp_sender_failed
rtcp_error
unsupported_profile
unsupported_display_topology
capture_access_lost
hardware_fallback
software_fallback
```
Новые причины не должны требовать изменения уже существующего consumer contract. Если причина не распознаётся старым consumer, она должна быть безопасно представлена через существующее поле `reason`.

### 6.2. Adapter вместо переименования всей control plane

На первом этапе нежелательно механически переименовывать все сущности `ffmpeg_*`. Рекомендуемый подход:
```
text
current orchestration API
    → media backend adapter
        → ffmpeg backend
        → l4capture backend
```
Пример логических операций adapter-а:
```
text
media_backend_start(...)
media_backend_stop(...)
media_backend_poll_health(...)
media_backend_get_process_identity(...)
media_backend_collect_metrics(...)
media_backend_reconcile(...)
```
Так можно:

1. сохранить текущие внешние вызовы;
2. подключить `l4capture` за feature flag;
3. сохранить FFmpeg как fallback;
4. позднее переименовать внутренние сущности в нейтральные `media_supervisor` только отдельным рефакторингом.

### 6.3. Режимы rollout
```
text
mode=ffmpeg_only
mode=l4capture_preferred
mode=l4capture_required
mode=ffmpeg_fallback_only
```
Рекомендуемый rollout:
```
text
1. ffmpeg_only — baseline.
2. l4capture_preferred — выбранные тестовые/новые терминалы.
3. l4capture_preferred + automatic one-way fallback to FFmpeg for approved failures.
4. l4capture_required — только после acceptance покрытых desktop-сценариев.
5. ffmpeg_fallback_only — FFmpeg не включён в default deployment, но доступен по отдельному recovery artifact до завершения периода стабилизации.
```
**Важно:** fallback с `l4capture` на FFmpeg разрешён только до объявления успешного запуска сессии либо при явном controlled restart. Нельзя одновременно держать два sender-а на одном RTP endpoint.

---

## 7. Совместимость с Windows-терминалами

### 7.1. Capture backend matrix

| Среда | Предпочтительный backend | Fallback | Ожидание |
|---|---|---|---|
| Windows 10/11, WDDM-compatible GPU | DXGI Desktop Duplication | GDI | Основной современный путь |
| Windows 8/8.1 | DXGI при успешном probe | GDI | Best effort DXGI |
| Windows 7 SP1 | GDI BitBlt | Нет второго обязательного backend-а | Обязательный legacy путь |
| VM с virtual adapter | GDI или успешный DXGI | Software H.264 | Проверять по факту |
| RDP/disconnected session | Не считать рабочим desktop capture по умолчанию | Controlled stop | Зависит от утверждённой политики |
| Lock screen / secure desktop | Capture запрещён/недоступен | Controlled stop | Не обходить |
| Multi-monitor | Захват одного выбранного display | GDI crop при невозможности DXGI mapping | Без GPU composite в v1 |

DXGI Desktop Duplication следует применять как оптимизированный путь на современных Windows. Он использует нативный графический стек Windows и может предоставить кадры в GPU memory; FFmpeg `ddagrab` является обёрткой над тем же направлением Desktop Duplication API. [[1]](https://trac.ffmpeg.org/wiki/Capture/Desktop)

### 7.2. Нормативная стратегия capture fallback
```
text
if Windows supports DXGI path and selected output initializes successfully:
    use DXGI Desktop Duplication
else if interactive target desktop is available:
    use GDI BitBlt
else:
    return session_unavailable or source_unavailable
```
Нельзя определять выбор только по версии Windows или названию GPU. Требуется инициализационный probe для конкретного selected display/session.

### 7.3. Требования Windows 7

Windows 7 — не второстепенный деградированный случай, а явно поддерживаемая платформа с другим техническим путем:
```
text
GDI capture
→ CPU resize/color conversion
→ software H.264 или проверенный доступный encoder
→ RTP/H.264
```
Ограничения Windows 7 должны быть видимы в capability telemetry:
```
text
capture_backend=gdi
hardware_acceleration=false|true
profile_ceiling=base|premium_1
```
Не следует обещать максимальный premium-профиль для Windows 7 без стендового подтверждения CPU, encoder и стабильности трансляции.

---

## 8. H.264 encoder architecture

### 8.1. Общий подход

`l4capture` должен иметь внутренний интерфейс `IEncoderBackend`, независимый от capture backend:
```
text
initialize(profile, width, height, fps, bitrate, gop, low_latency)
encode(frame, pts)
force_idr()
flush()
get_metrics()
shutdown()
```
Capture layer должен отдавать кадр либо как GPU texture, либо как CPU frame. Encoder backend выбирается по способности принять этот тип кадра.

### 8.2. Предлагаемый порядок выбора encoder-а
```
text
1. Media Foundation hardware H.264 MFT
2. Optional vendor-specific backend:
   - NVIDIA NVENC
   - Intel Quick Sync / oneVPL
   - AMD AMF
3. Software H.264 backend
4. Diagnostic failure: encoder_unavailable
```
### 8.3. Media Foundation как основной Windows-native backend

Media Foundation H.264 MFT рекомендуется рассматривать как базовый аппаратный путь:

- Windows-native API;
- не требует отдельного универсального media runtime;
- может использовать доступный аппаратный encoder через установленный драйвер;
- позволяет избежать обязательной прямой зависимости от SDK каждого производителя;
- совместим с D3D11-oriented архитектурой;
- пригоден как основной путь на современных Intel/NVIDIA/AMD терминалах после runtime probe.

Фактическое наличие аппаратного encoder-а должно быть подтверждено запуском transform и тестовым encode, а не inferred из vendor/device name.

### 8.4. Vendor-specific backend-ы

| Backend | Назначение | Решение для v1 |
|---|---|---|
| NVENC | Оптимальный контролируемый путь на NVIDIA | Опционально после PoC и license review |
| Intel QSV / oneVPL | Intel iGPU/CPU estate | Опционально после PoC и compatibility review |
| AMD AMF | AMD GPU estate | Опционально после PoC и compatibility review |
| Media Foundation MFT | Универсальный Windows-native hardware path | Предпочтительная основа |
| OpenH264 / иной software codec | Universal CPU fallback | Обязателен после license decision |

В первой поставке необязательно одновременно поддержать все vendor SDK. Рациональный порядок:
```
text
Phase 1: GDI + software H.264 + RTP
Phase 2: DXGI + Media Foundation hardware H.264
Phase 3: vendor-specific backend только там, где доказан выигрыш
```
Так обеспечивается функциональная замена FFmpeg до усложнения GPU matrix.

---

## 9. Software H.264 и лицензирование

### 9.1. Требование

Для Windows 7, VM и терминалов без usable hardware encoder необходим software H.264 fallback. Без него `l4capture` не сможет заменить FFmpeg в подавляющем большинстве реальных desktop-сценариев.

### 9.2. Кандидаты

| Вариант | Техническая оценка | Лицензионный статус для коммерческой поставки |
|---|---|---|
| OpenH264 source integration | Подходит для real-time H.264, ограниченная функциональная площадь | Source имеет двухпунктовую BSD-лицензию; H.264 patent obligations должны проверяться отдельно |
| Cisco-provided OpenH264 binary | Возможна отдельная загружаемая зависимость | Условия патентного покрытия Cisco зависят от конкретной модели распространения и не равны простому «положить DLL в installer» |
| x264 | Высокая зрелость и качество software encoding | GPL либо коммерческая лицензия; не включать в proprietary artifact без отдельного лицензионного решения |
| Коммерческий H.264 SDK | Предсказуемая contractual support модель | Оценить стоимость, территорию, redistribution и поддержку Windows 7 |
| Только Media Foundation | Минимум внешних зависимостей | Не гарантирует universal software fallback и не покрывает все terminal environments |

### 9.3. Предварительная рекомендация

Архитектурно заложить software encoder как сменный backend и не фиксировать финальный third-party codec до legal review.

Предпочтительный кандидат для технического PoC — OpenH264, но его использование нельзя считать автоматически юридически закрытым только на основании BSD-лицензии исходного кода.

Cisco указывает, что исходный код OpenH264 лицензирован по Two-Clause BSD, а предоставляемые Cisco бинарники имеют отдельные условия AVC/H.264 Patent Portfolio License и ограничения модели распространения. [[2]](https://www.openh264.org/faq.html)

### 9.4. Обязательное licencing decision до production

Юридическая/лицензионная проверка должна ответить на вопросы:

1. Встраивается ли исходный код OpenH264, поставляется ли собственная сборка или бинарник загружается непосредственно от Cisco?
2. На каких рынках будет распространяться агент?
3. Кто несёт H.264 patent obligations при выбранной модели поставки?
4. Допустимо ли поставлять third-party notices и license texts рядом с продуктом?
5. Достаточно ли условий поставщика для SaaS/коммерческой трансляции и удалённого управления?
6. Нужна ли коммерческая лицензия на x264 или альтернативный codec SDK?
7. Нужен ли SBOM с версией, source URL, license, SHA-256 и способом поставки компонента?
8. Разрешается ли динамическая загрузка codec DLL и как она обновляется/проверяется?
9. Требуется ли отдельное пользовательское уведомление или переключатель codec backend-а?
10. Какие security/SBOM/vulnerability scanning требования применяются к third-party native binaries?

До принятия решения software codec должен быть архитектурно отделён через интерфейс, чтобы замена реализации не меняла RTP contract и основной capture pipeline.

---

## 10. RTP/H.264 contract с `l4media`

### 10.1. Принцип

`l4capture` должен быть совместим с существующим RTP/H.264 ingress. Замена FFmpeg не должна требовать неявного изменения media server, Janus-цепочки, signalling или browser-side playback.

Контракт должен быть зафиксирован как отдельный versioned immutable artifact между `tools`/agent и `l4media`.

### 10.2. Минимальный RTP/H.264 contract

| Область | Требование |
|---|---|
| Transport | UDP loopback до утверждённого ingress endpoint |
| Media | H.264/AVC |
| Packetization | RFC 6184 |
| RTP version | 2 |
| Payload type | Фиксированный согласованный dynamic PT либо параметр route |
| SSRC | Стабилен в пределах одного stream instance; новый stream/restart может иметь новый SSRC |
| Sequence number | Монотонно увеличивается внутри SSRC |
| Timestamp clock | 90 kHz |
| Timestamp policy | Производится от media PTS и выбранного FPS, не от wall-clock на каждый пакет |
| MTU | Консервативный payload budget, согласованный с tunnel/ingress; не отправлять oversized UDP datagrams |
| Fragmentation | FU-A для NAL units больше допустимого RTP payload |
| SPS/PPS | Доступны при подключении ingest и перед IDR по согласованной политике |
| IDR | Первый кадр stream — IDR; последующие не реже согласованного GOP; форсируются после recovery/PLI при поддержке |
| B-frames | Отключены в interactive low-latency profiles |
| Pixel format | 8-bit 4:2:0, например NV12/I420 до encoder-а |
| Profile/level | Фактически согласованные H.264 profile-level-id/constraints; не предполагать, что любой hardware encoder даёт нужный Baseline bitstream |
| RTCP | Минимально: Sender Reports при необходимости; поддержка PLI/FIR должна быть определена contract-ом |
| Audio | Не передаётся в рамках `l4capture` v1 |

### 10.3. SPS/PPS и recovery

Обязательная политика:
```
text
- Первый access unit после start содержит IDR.
- SPS/PPS должны быть доступны ingress до или вместе с первым IDR.
- При controlled restart поток должен получить новый initialization sequence.
- При reconnect/PLI, если RTCP-feedback поддержан ingress, l4capture делает force-IDR.
- При отсутствии RTCP feedback IDR генерируется с ограниченным GOP interval.
```
### 10.4. RTP compatibility acceptance

До включения `l4capture` необходимо подтвердить:

1. ingress принимает RTP от FFmpeg и `l4capture` одинаково;
2. downstream корректно получает SPS/PPS;
3. поток появляется после start без ручной реконфигурации;
4. stream восстанавливается после restart;
5. packet loss не создаёт бессрочного black frame;
6. H.264 profile/level принимаются следующим звеном;
7. video/cursor/input coordinate geometry сохраняются;
8. stop освобождает RTP endpoint и не оставляет sender;
9. `l4capture` не отправляет RTP после expiry lease/stop;
10. packetization проверяется golden PCAP/fixture-тестами.

---

## 11. Профили качества и ресурсные ограничения

### 11.1. Общая политика

Профиль является частью server-approved session policy, а не клиентской произвольной настройкой.

`l4capture` получает разрешённый profile и применяет его как верхнюю границу. При ограниченных ресурсах модуль может деградировать внутри этого профиля по заранее определённой политике, но не увеличивает bitrate/FPS/разрешение самостоятельно.

Во всех профилях:
```
text
codec: H.264/AVC
low latency: enabled
B-frames: disabled
cursor: enabled when available
GOP: ориентир 1–2 секунды
RTP: existing l4media-compatible contract
audio: disabled
```
### 11.2. Рекомендуемые стартовые профили

| Профиль | Назначение | Output resolution | FPS | Target bitrate | Максимальный bitrate | Типовой backend |
|---|---|---:|---:|---:|---:|---|
| `base_480p` | Стандартный для большинства терминалов | `854×480` | `10` | `500 kbit/s` | `700 kbit/s` | GDI/DXGI + software/hardware H.264 |
| `premium_540p` | Повышенное качество для подходящих терминалов | `960×540` | `15` | `700 kbit/s` | `900 kbit/s` | Предпочтительно DXGI + hardware H.264 |
| `premium_720p` | Максимальный профиль первой версии | `1280×720` | `10–15` | `800 kbit/s` | `1000 kbit/s` | DXGI + hardware H.264, software only after acceptance |

Указанный диапазон для максимального профиля соответствует целевому ограничению:
```
text
1280×720, 10–15 FPS, 600–1000 kbit/s
```
Для практической эксплуатации рекомендуется считать:
```
text
600 kbit/s — нижняя допустимая граница для статичного/умеренно меняющегося desktop;
800 kbit/s — целевая точка premium_720p;
1000 kbit/s — верхняя граница premium_720p.
```
### 11.3. Почему 480p является default

`base_480p` должен быть профилем по умолчанию, поскольку:

- существенно легче для старых CPU;
- применим на Windows 7;
- переносим по нестабильным uplink-каналам;
- достаточен для базового наблюдения и многих kiosk-сценариев;
- даёт более стабильную задержку, чем попытка кодировать 720p/1080p на слабом CPU;
- обеспечивает реалистичный software fallback.

### 11.4. Политика деградации

Разрешённая последовательность деградации:
```
text
1. Drop late captured frames, не накапливая очередь.
2. Снизить фактический FPS до минимального значения профиля.
3. Снизить bitrate в пределах profile minimum/maximum.
4. Если policy допускает — перейти на lower approved profile.
5. Если поток не соответствует минимальному health threshold — завершить с diagnostic failure.
```
Запрещённая последовательность:
```
text
- Увеличивать очередь необработанных frames.
- Сохранять старые frames и повышать end-to-end latency.
- Самостоятельно увеличивать bitrate сверх policy.
- Молча менять stream geometry без публикации актуального состояния.
- Автоматически повышать profile при появлении GPU.
```
---

## 12. Cursor, monitor geometry и remote input

`l4capture` не управляет вводом, но обязан сохранить геометрический контракт между транслируемым кадром и remote input.

Инвариант:
```
text
capture crop rectangle
= encoded frame source rectangle
= display rectangle, используемый для преобразования remote input coordinates
```
Требования:

1. Курсор должен быть либо уже встроен в передаваемый frame, либо не встроен вовсе; двойная отрисовка запрещена.
2. Для selected display нужно учитывать его virtual-desktop offset, включая отрицательные координаты.
3. Если video downscaled, input coordinates должны интерпретироваться относительно исходного capture rectangle, а не encoded pixel dimensions.
4. При изменении display topology, resolution, rotation или выбранного monitor identity поток должен controlled-stop/restart-иться.
5. Secure desktop, Winlogon desktop, lock screen и недоступный input desktop не обходятся.
6. При смене interactive session capture должен быть остановлен; старые buffered frames не могут быть опубликованы как текущий экран.

---

## 13. Процесс, IPC и безопасность

### 13.1. Startup contract

Parent/supervisor должен передавать `l4capture` только типизированные, уже валидированные значения:
```
text
stream_instance_id
lease_id
session_id
source_id
capture rectangle
selected profile
RTP destination address and port
RTCP destination address and port
backend policy
allowed fallback policy
correlation_id
```
`l4capture` не должен принимать из сети:
```
text
arbitrary executable paths
arbitrary command lines
unrestricted RTP destinations
arbitrary display handles
unvalidated source identifiers
неограниченные codec options
```
### 13.2. IPC

Предпочтительные варианты:
```
text
- anonymous/named pipe с ограниченным ACL;
- loopback local IPC с authentication token;
- inherited pipe handles от parent process.
```
Для первой версии достаточно parent-created inherited control/status pipe, если он соответствует требованиям:

- handles не наследуются посторонними процессами;
- protocol versioned;
- message size bounded;
- все входные поля валидируются;
- parent monitors child process exit;
- child detects closed control pipe и прекращает stream.

### 13.3. Stop semantics

`l4capture` должен поддерживать управляемую остановку:
```
text
STOP(reason)
→ stop accepting new frames
→ flush/drop according to bounded latency policy
→ close RTP/RTCP sockets
→ release capture and encoder resources
→ publish final typed status
→ exit
```
Остановка не должна ждать неограниченно. При превышении bounded timeout parent применяет Job Object/process termination согласно существующей recovery policy.

### 13.4. Parent death semantics

Если parent process умер, control pipe закрыт или inherited parent handle сигнализирован:
```
text
l4capture must stop RTP transmission and terminate.
```
Это предотвращает orphan media stream.

---

## 14. Health model и telemetry

### 14.1. Разделение статусов

Нужно разделить:
```
text
process_alive
capture_alive
encoder_alive
rtp_sender_alive
frames_actually_sent
```
Процесс, который существует, но не выдаёт кадры, не должен считаться `running` бесконечно.

### 14.2. Минимальные метрики
```
text
capture_backend
encoder_backend
hardware_accelerated
source_width/source_height
output_width/output_height
target_fps
actual_capture_fps
actual_encode_fps
actual_send_fps
target_bitrate_kbps
estimated_send_bitrate_kbps
frames_captured
frames_encoded
frames_sent
frames_dropped_pacing
frames_dropped_backpressure
frames_dropped_encoder
last_capture_at
last_encoded_at
last_rtp_sent_at
last_idr_at
capture_error_code
encoder_error_code
rtp_error_code
selected_gpu_adapter
fallback_reason
```
### 14.3. Health rules

Пример нормативной логики:
```
text
running:
  child process alive
  AND at least one frame was sent within configured health interval
  AND session/source/lease remain valid

stall:
  child process alive
  BUT no encoded/RTP frame within configured health interval

failed:
  unrecoverable capture/encoder/RTP initialization or runtime error

session_unavailable:
  user session or permitted desktop became unavailable

source_unavailable:
  selected display disappeared or no longer matches inventory/policy
```
Прогрессом считается именно успешная отправка RTP frame/access unit, а не внутренний capture loop.

---

## 15. Capability probe и выбор pipeline

### 15.1. Требование

Агент должен определять возможности терминала до запуска пользовательской сессии или при изменении display topology.

Probe не должен выполнять тяжёлую длительную диагностику при каждом start. Результат кэшируется и инвалидируется при:
```
text
agent restart
session change
monitor/display topology change
driver/GPU reset indication
repeated backend failure
explicit diagnostics request
```
### 15.2. Probe algorithm
```
text
1. Проверить, что разрешённая interactive console session доступна.
2. Найти выбранный display и его rectangle.
3. Попытаться инициализировать DXGI capture для данного output.
4. При успехе выполнить ограниченный capture test.
5. Попытаться создать Media Foundation H.264 encoder.
6. Выполнить короткий encode test в требуемом profile range.
7. При успехе выбрать DXGI + hardware H.264.
8. При неуспехе попытаться GDI + выбранный hardware H.264.
9. При неуспехе попытаться GDI + software H.264.
10. Сохранить capability result и diagnostic reason.
```
### 15.3. Backend selection matrix

| Состояние | Capture | Encoder | Результат |
|---|---|---|---|
| Современный Windows, DXGI и hardware encoder доступны | DXGI | Media Foundation hardware MFT / vendor backend | Максимальная эффективность |
| DXGI работает, hardware encoder не работает | DXGI | Software H.264 | Возможна CPU-нагрузка; ограничить profile |
| DXGI недоступен, hardware encoder доступен | GDI | Hardware H.264 | Совместимый mixed path |
| Windows 7/legacy/VM, hardware unavailable | GDI | Software H.264 | Обязательный compatibility path |
| Session/desktop недоступны | — | — | `session_unavailable` |
| Display пропал/не соответствует policy | — | — | `source_unavailable` |
| Нет работоспособного H.264 encoder | capture optional | — | `encoder_unavailable` |

---

## 16. Лицензионная, dependency и supply-chain политика

### 16.1. Цель

Снизить размер и сложность поставки по сравнению с FFmpeg, не создавая скрытых рисков через неучтённые codec DLL или vendor SDK.

### 16.2. Классы зависимостей

| Компонент | Категория | Предварительная политика |
|---|---|---|
| Win32, Direct3D 11, DXGI, Media Foundation, Winsock | Системные Windows API | Предпочтительны |
| Собственный RTP packetizer | Внутренний код | Предпочтителен: малая функциональная площадь |
| OpenH264 | Third-party software codec | Допустим после legal decision и SBOM |
| NVENC SDK | Vendor SDK | Опционален, после redistribution/license review |
| Intel oneVPL/QSV | Vendor/open SDK | Опционален, после compatibility/license review |
| AMD AMF | Vendor SDK | Опционален, после compatibility/license review |
| libyuv или аналог | Third-party pixel conversion | Возможен после license review |
| Live555 | RTP helper library | Не нужен по умолчанию; рассматривать только при подтверждённой экономии рисков |
| GStreamer/OBS/libwebrtc | Full media frameworks | Не использовать в `l4capture` v1 |

### 16.3. Обязательные материалы для каждого third-party компонента
```
text
- component name;
- exact version;
- source repository and release artifact URL;
- license text;
- license notice included in installer/package;
- SHA-256 of source/binary artifact;
- SBOM entry;
- CVE/vulnerability review;
- static/dynamic linking model;
- redistribution permissions;
- patent-related conclusions;
- upgrade and rollback procedure.
```
---

## 17. План реализации

### Phase 0 — contract discovery и baseline

1. Зафиксировать фактический RTP/H.264 contract между текущим sender и `l4media`.
2. Снять golden PCAP/SDP/RTCP fixtures для работающего desktop stream.
3. Зафиксировать поля orchestration lifecycle, state/reason, timeout/restart policy.
4. Определить Windows support matrix:
   - Windows 7 SP1;
   - Windows 10;
   - Windows 11;
   - Intel/NVIDIA/AMD;
   - no-usable-GPU;
   - VM;
   - single/multiple monitors.
5. Выполнить legal review вариантов software H.264.
6. Принять artifact layout, SBOM и third-party notices policy.

### Phase 1 — минимальный `l4capture` desktop path

1. Отдельный `l4capture.exe`.
2. GDI BitBlt capture.
3. Cursor capture/composition.
4. Software H.264 backend, выбранный после licensing decision.
5. RTP/H.264 sender, совместимый с golden fixtures.
6. Fixed `base_480p` profile.
7. Typed status pipe.
8. Интеграция через backend adapter в существующий supervisor.
9. Feature flag: `l4capture_preferred` для test group.
10. FFmpeg rollback остаётся доступным.

**Цель Phase 1:** функционально заменить FFmpeg для desktop `base_480p`, включая Windows 7.

### Phase 2 — современные Windows и DXGI

1. DXGI Desktop Duplication backend.
2. Mapping selected display to DXGI output.
3. DXGI cursor metadata/composition.
4. `DXGI_ERROR_ACCESS_LOST` recovery semantics.
5. Media Foundation H.264 MFT backend.
6. Runtime capability probe.
7. `premium_540p` и `premium_720p`.
8. GPU/driver/session telemetry.

**Цель Phase 2:** использовать нативный accelerated path на совместимых Windows 10/11 устройствах без потери Windows 7 fallback.

### Phase 3 — аппаратная оптимизация и устойчивость

1. Измерить Media Foundation coverage в production pilot.
2. При необходимости добавить NVENC/QSV/AMF как optional implementations `IEncoderBackend`.
3. Добавить RTCP PLI/FIR handling, если это требуется и подтверждено ingress.
4. Добавить controlled in-session fallback только по утверждённой политике.
5. Завершить performance acceptance.
6. Перевести новые desktop deployments в `l4capture_required`.

### Phase 4 — сокращение FFmpeg footprint

1. Подтвердить покрытие desktop scenarios.
2. Оставить FFmpeg только для явно непокрытых сценариев, например camera capture, если он ещё не перенесён.
3. Убрать FFmpeg из default desktop path.
4. Сохранить recovery artifact на утверждённый период.
5. Отдельно принять решение о переносе camera capture или сохранении другого специализированного backend-а.

---

## 18. Acceptance criteria

### 18.1. Функциональные

1. `l4capture` запускается и останавливается через существующую session orchestration.
2. Один terminal не создаёт больше одного активного desktop sender-а.
3. RTP/H.264 ingest `l4media` принимает поток без изменения server-side media contract.
4. Первый отображаемый кадр корректно декодируется после start.
5. После restart/reconnect новый decoder получает SPS/PPS и IDR.
6. Cursor отображается ровно один раз и соответствует координатам выбранного display.
7. При stop/lease expiry RTP прекращается в bounded time.
8. При agent restart orphan `l4capture` обнаруживается и завершается.
9. При lock/session loss/display removal поток получает корректный terminal state.
10. Windows 7 поддерживает `base_480p`.
11. `base_480p`, `premium_540p` и `premium_720p` работают на утверждённой матрице устройств.
12. При отсутствии usable GPU encoder software fallback либо корректный диагностируемый отказ работает предсказуемо.

### 18.2. Производительность

Для каждого утверждённого класса оборудования необходимо зафиксировать:
```
text
capture FPS
encoded FPS
end-to-end latency
CPU usage
GPU usage
memory usage
bitrate stability
frame drop rate
restart/recovery time
```
Минимальные критерии должны быть определены отдельно по profile и terminal class. Нельзя использовать единый FPS/CPU threshold для Windows 7, VM и современного NVIDIA terminal.

### 18.3. Security и lifecycle

1. `l4capture` не работает в Session 0 как desktop-capture substitute.
2. Нет попыток обхода UAC secure desktop, lock screen или protected content.
3. Local IPC не принимает произвольный media command line.
4. RTP destination ограничен approved loopback ingress.
5. Child process не остаётся живым после смерти parent/supervisor.
6. Frame queue bounded; stale frames отбрасываются.
7. All process, D3D, DXGI, Media Foundation, socket and IPC handles освобождаются на всех error paths.
8. Нет модальных окон, console flash или elevation UI при ошибках capture/encoder.

---

## 19. Основные риски и меры

| Риск | Влияние | Мера |
|---|---|---|
| Software H.264 legal/patent ambiguity | Блокирует universal fallback release | Early legal decision; encoder abstraction; SBOM; possible commercial codec alternative |
| Media Foundation encoder различается по driver/GPU | Непредсказуемый hardware coverage | Runtime probe; conservative fallback; vendor backend only after PoC |
| Windows 7 слабый CPU | Низкий FPS/высокая latency | `base_480p`, bounded queues, FPS cap, software codec benchmark |
| DXGI не работает на части VM/legacy terminals | Capture unavailable | GDI fallback mandatory |
| H.264 RTP bitstream несовместим с ingress | Black video/no decode | Golden PCAP/SDP fixtures; RTP interoperability tests before rollout |
| Hardware encoder выдаёт неподходящий profile/level | Downstream reject | Validate actual SPS/profile-level-id; enforce compatible settings or fallback |
| Multi-GPU/hybrid adapter mismatch | DXGI/encoder interop failures | Bind capture to selected display adapter; use CPU transfer or fallback where necessary |
| Cursor duplicated/misaligned | UX/input risk | Single cursor compositor; coordinate acceptance tests |
| Overengineering full media stack | Рост срока и attack surface | Strict non-goals; no GStreamer/OBS/libwebrtc in v1 |
| Логика FFmpeg-specific supervisor не переносится | Regression lifecycle | Introduce backend adapter, preserve states and tests |
| Автоматический fallback порождает два sender-а | Media collision | Serialize transition; old child exit confirmed before new child start |

---

## 20. Архитектурное решение, предлагаемое к утверждению

Утвердить следующие положения:

1. Создать отдельный нативный Windows process `l4capture.exe` для desktop capture → H.264 → RTP.
2. Сохранить существующий orchestration/session/lease/state contract через adapter между supervisor и media backend.
3. Использовать `l4capture` как default desktop backend после поэтапной acceptance; FFmpeg сохранить как временный fallback/rollback.
4. Сделать GDI BitBlt + software H.264 обязательным compatibility path для Windows 7 и non-GPU environments.
5. Сделать DXGI Desktop Duplication предпочтительным capture path для современных Windows.
6. Сделать Media Foundation H.264 MFT предпочтительным hardware encoding path.
7. Добавлять NVENC/QSV/AMF только как optional backend-ы после отдельного PoC, compatibility и licensing review.
8. Зафиксировать RTP/H.264 interoperability contract с `l4media` до реализации sender-а.
9. Ограничить первую версию тремя профилями:
   - `base_480p`: `854×480`, `10 FPS`, `500–700 kbit/s`;
   - `premium_540p`: `960×540`, `15 FPS`, `700–900 kbit/s`;
   - `premium_720p`: `1280×720`, `10–15 FPS`, `600–1000 kbit/s`.
10. Не включать в v1 аудио, запись, RTSP, WebRTC, контейнеры, arbitrary filters, camera capture и иной функционал FFmpeg вне desktop-to-H.264/RTP.
11. Не утверждать окончательный software H.264 dependency до юридической проверки модели распространения, H.264 patent obligations и third-party notices/SBOM requirements.
12. Провести rollout только после successful desktop/RTP compatibility acceptance на Windows 7, Windows 10/11, VM и доступных классах hardware.

---
```
