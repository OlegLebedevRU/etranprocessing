# OpenIPC через аппаратный прокси: статический анализ

## Task intake
- Цель: сравнить l4capture/l4media с OpenIPC и определить минимальный видеопередатчик с IP-управлением.
- Scope: чтение native RTP, leo4proxy, ingress/nginx, video mountpoint; OpenIPC upstream и siplite. Без реализации, подключения к устройствам, MQTT, сборки и деплоя.
- Владельцы: камера — захват/кодирование; аппаратный прокси — transport/identity/control adapter; l4media — ingress/routes/Janus; BFF — viewing session.
- Producer → transport → consumer: OpenIPC H.264/RTP UDP → hardware proxy L4RTP/1 mTLS/TCP → nginx → ingress → Janus → browser.
- Инварианты: существующий framing, проверка TLS, готовность route до start, lease/stop отдельно от RTP; без транскодирования на MCU.
- Уточнение пользователя: GK7205V300 + IMX335, flash8–16MB, типовая RAM, звук не нужен. Рабочая оценка RAM128MB, подтвердить на конкретной плате. Неизвестны версия Majestic и параметры реального SPS/PT/RTCP.

## Источники / evidence
- etranprocessing HEAD c4f8055, чтение основной рабочей копии 2026-10-10.
- siplite GitHub HEAD 8ad132a, shallow sparse clone во временный каталог; main/l4_rtp_bridge.c проверен. GitHub HTTP/raw вернул 404, чтение выполнено через git.
- OpenIPC wiki en/majestic-streamer.md и en/majestic-config.md, firmware gk7205v200_lite_defconfig, divinus docs/config: актуальные upstream-материалы, не evidence камеры.
- Код: tools/l4capture/src/network/{rtp_packetizer,rtp_sender}.c; tools/leo4proxy/src/rtp_tunnel.c; l4media/ingress/src/l4media_ingress.c; l4media/nginx/stream.conf; MenuBuilder/backend/app/routers/video.py.

## Выводы
- l4capture: H.264 RFC6184 single NAL/STAP-A/FU-A, PT96, 90kHz, payload target1200; RTCP SR/SDES/BYE и локальный PLI parser.
- leo4proxy инкапсулирует RTP/RTCP в L4RTP/1 и mTLS; ingress декапсулирует и пересылает в Janus. Обратный RTCP transport не реализован в прочитанном тракте.
- Janus mountpoint фиксирует audio=false, PT96, h264, profile-level-id=42e01f, packetization-mode=1. Совпадение SPS камеры должно проверяться, смена PT не меняет codec/profile.
- siplite bridge — UDP recvfrom/sendto, buffer1460, без TLS/framing/SN/control/RTCP. Комментарий о discard коротких пакетов не соответствует коду: len>=12 только защищает чтение seq.
- Majestic FPV — минимальный документированный вариант streamer: ~500KB против Lite913KB на GK7205V200; это размер бинарника, не firmware. RTP push и HTTP API сохраняются; опубликован для GK7205V200/Hi3516EV200 по текущей wiki. Lite8MB — более универсальная исходная firmware.
- Новый API использует outgoing.servers; старые outgoing.enabled/server/naluSize нельзя копировать без проверки версии/schema. video0.gopSize задаётся в секундах в текущей wiki.
- Divinus имеет открытый streamer/HAL, UDP destination и HTTP API, но минимальный размер и совместимость конкретной платы не измерены. smolrtsp сам по себе не содержит sensor/ISP/encoder integration.

## Обнаруженные дефекты / пробелы
- RTP packetizer buffer1212 не вмещает некоторые STAP-A. need=2+L, fits не учитывает STAP header; L1198 последним даёт запись за границей на1 байт. L1200 не последним попадает в accumulation с need1202 и при flush требует1215 байт. Это статическое доказательство по индексам, без sanitizer.
- PLI parser l4capture проверяет только первый RTCP packet: PT206/FMT1; полноценный compound parser/SSRC validation отсутствует.
- Bridge принимает произвольного UDP sender и не отбрасывает len<12; oversize detection отсутствует, возможна пересылка усечённого datagram.
- s_running/task handle lifecycle bridge требует корректного completion synchronization; task не сбрасывает handle перед self-delete.
- В nginx mTLS проверяет клиентский сертификат, ingress получает SN из preamble; cert↔SN binding в рассмотренном transport не доказан.

## Предлагаемая интеграция
- Уточнение под выбранную плату: базовый образ gk7205v300_lite, собственная минимизация под Ethernet/IMX335/один H.264 stream/HTTP API. Majestic FPV семейства gk7205v200 — кандидат для уменьшения бинарника; sandbox-fpv/notes_imx335_gk7205v300.md сообщает о такой комбинации, но это исторические заметки2023, не проверка текущего бинарника.
- Текущий upstream goke-osdrv-gk7205v200.mk включает GK7205V300 в128MB/5MP IMX335 группу и выбирает imx335_i2c_5M.ini. Старое имя imx335_i2c_4M.ini нельзя фиксировать универсально. Выход720p не означает, что sensor/ISP работают в720p.
- Majestic поставляется предсобранным PROPRIETARY package; majestic.mk сохраняет зависимости, включая opus-openipc. Отключение audio не доказывает возможность удаления libopus; проверять ELF dependencies FPV package перед сокращением rootfs.
- Одна камера на identity/SN proxy в первой версии: H.264 Baseline720p15fps, 1–2Mbit/s как исходная оценка, GOP1s, packets около1200B; actual SPS/PT/SPS-PPS repeat проверять capture.
- MCU: bounded UDP queue + L4RTP framing + TLS1.2 mTLS + partial write handling + DNS/SNI/trust/time + reconnect/backoff и discard stale data.
- Camera control: разрешённые команды → HTTP API камеры в LAN; schema/capability discovery, timeout, readback. Существующий upstream control использовать отдельным adapter, не менять media header.
- Start: route/lease ready → tunnel ready → camera publish → fresh RTP → browser decode. Stop/expiry: close gate/tunnel и stop publishing; нельзя автоматически восстанавливать expired lease.
- Для MVP короткий GOP; PLI path или подтверждённый camera IDR API — отдельная доработка. Не считать RTCP forwarding полным feedback loop.

## Проверки / оставшиеся действия
- [x] Producer/transport/consumer код и текущий siplite upstream прочитаны.
- [x] OpenIPC первичные документы и build config проверены.
- [ ] Firmware build/размер rootfs/RAM: нужен SoC и sensor.
- [ ] RTP capture: PT, SPS profile/level, packet size, SPS/PPS repeat, outgoing RTCP, SSRC change.
- [ ] ESP32 throughput/IDR bursts, TLS memory, latency under TCP loss и queue overflow.
- [ ] Browser first frame/late join/loss/reconnect/stop/lease expiry; это исследование не E2E-подтверждение.
- Изменён только этот handoff; runtime/config/source invariants не изменялись. Временные upstream файлы оставлены в %TEMP%/l4-openipc-audit-20261010 для воспроизводимости, без credentials.
- Карточки компонентов не менялись: это предложения и статические findings, не новая реализация.

## Углублённый разбор RTCP и packetizer по запросу пользователя
- Scope: разбор причин и состава исправления; cert↔SN исключён пользователем. Исходный код не изменён, деплой/устройства не затрагивались.
- Проверена версия Janus3c39ce8cf11c54cf6f1607030a47ac9db798389a из Dockerfile. В streaming plugin RTCP recvfrom запоминает source endpoint в rtcp_addr; PLI посылается обратно из rtcp_fd на этот endpoint. TLS termination nginx full duplex не является блокирующим компонентом.
- Ingress g_udp_sock сейчас не зарегистрирован для чтения и не маршрутизирует feedback в TCP. Общий socket требует demux по Janus endpoint; альтернативно отдельный RTCP UDP socket на активный stream упрощает mapping/epoch lifetime.
- Proxy recvfrom для RTCP использует NULL,NULL и не запоминает sender endpoint; worker не читает remote TLS. Feedback нужно вернуть с того же bound RTCP socket на ephemeral source port l4capture, а не на5005 proxy.
- SChannel recv может ждать остаток TLS record; plaintext/extra buffers могут уже содержать данные без select readiness. Нужна bounded/nonblocking receive семантика, различающая would-block и close; один owner TLS session или проверенная synchronization.
- Дополнительный дефект: main.c:1114 вызывает l4c_safety_take_idr при PLI. Функция не ставит idr_pending, а consumes pending и возвращает bool, который игнорируется. Нужна постановка IDR через общий безопасный rate-limited gate; actual encoding consumes request в raw.force_idr.
- PLI parser читает один datagram до256B и только первый RTCP packet; требуется bounded drain, compound iteration, length/version/padding и target SSRC checks. FIR/REMB/NACK не считать поддержанными автоматически.
- Packetizer repro с неизменённым source через MSVC x64 /Od /RTC1: NAL1197 → packet1212, без RTC failure; NAL1198 → packet1213, stack guard corruption; [NAL1200,NAL10] → packet1215 и22, stack guard corruption. Custom RTC handler печатает failure и завершает процесс86. Harness/exe/cmd в %TEMP%/l4-openipc-audit-20261010.
- ASan попытка не собрана: отсутствует clang_rt.asan_static_runtime_thunk-x86_64.lib. Это не ASan evidence; подтверждение выполнено /RTC1. Первый harness build с handler до stdlib исправлен локально во временном файле.
- Корректный STAP limit: 1 + sum(2+NAL_length) <=1200. NAL1198–1200 должны идти Single NAL при любой позиции. Packet count должен совпадать с actual emitted packets после изменения планирования; NRI STAP выводить из aggregated NAL headers. Отдельно проверить last marker при trailing empty NAL.
- Предложенный порядок: boundary fix + meaningful regression tests; PLI pending/parser; ingress feedback + proxy TLS framing/UDP return; two-stream isolation/reconnect/route deletion/slow reader; затем browser late join/loss E2E.
