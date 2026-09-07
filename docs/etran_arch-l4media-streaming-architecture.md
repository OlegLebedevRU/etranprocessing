# Архитектура подсистемы видеотрансляций l4media

Данный документ является **авторитетной архитектурной спецификацией (Single Source of Truth)** для подсистемы приёма, маршрутизации и веб-трансляции защищённых видеопотоков от платёжных терминалов и киосков платформы `etranprocessing`.

Документ определяет:
1. Архитектуру и сквозной поток данных между терминалами, медиасервером и веб-интерфейсом `MenuBuilder`.
2. Полную матрицу сетевых портов и протоколов на всех участках маршрута.
3. Ожидаемый формат видеострима, спецификацию wire-протокола L4RTP/1 и параметры энкодинга H.264.
4. Способы и архитектурные паттерны интеграции с `MenuBuilder` (FastAPI backend и React 19 frontend).

---

## 1. Назначение и контекст подсистемы

В процессе эксплуатации сети платёжных терминалов операторам и службе технической поддержки требуется визуальный контроль состояния экранов киосков в реальном времени (мониторинг интерфейса ПО, диагностика зависаний, проверка работы сенсорного экрана и видеокамер наблюдения).

### Ключевые требования и вызовы:
1. **Безопасность и Non-Exportable ключи:** Терминалы используют клиентские сертификаты X.509 из хранилища `LocalMachine\MY` Windows с неэкспортируемыми закрытыми ключами в Windows CNG KSP. Любое медиа-соединение в облако обязано проходить строгую взаимную TLS-аутентификацию (mTLS).
2. **Низкая задержка (Sub-second Latency):** Задержка трансляции должна быть менее 0.5–1 секунды (Real-Time WebRTC), что исключает использование протоколов с буферизацией (HLS, DASH).
3. **Минимальная нагрузка на CPU киоска:** Процессоры терминалов часто ограничены (Intel Atom, Celeron). Требуется аппаратное или ультралёгкое программное кодирование без тяжёлых промежуточных транскодеров на стороне сервера.
4. **Zero-Plugin Web UI:** Оператор в веб-портале `MenuBuilder` должен просматривать трансляцию в стандартном браузере (Chrome, Firefox, Safari, Edge) через HTML5 `<video>` без сторонних плагинов.

Подсистема **`l4media`** решает эту задачу, предоставляя полностью изолированный серверный стек на базе Nginx mTLS, высокопроизводительного маршрутизатора `l4media-ingress` на чистом C и медиашлюза **Janus WebRTC Gateway**.

---

## 2. Сквозная архитектура и поток данных (End-to-End Flow)

```text
 ┌────────────────────────────────────────────────────────────────────────────────────────┐
 │                               Терминал Windows (Киоск)                                 │
 │                                                                                        │
 │  ┌───────────────────────┐           RTP (UDP :5004)           ┌────────────────────┐  │
 │  │      ffmpeg.exe       │────────────────────────────────────►│   leo4proxy.exe    │  │
 │  │ (gdigrab / desktop /  │           RTCP (UDP :5005)          │ (--rtp-tunnel,     │  │
 │  │  H.264 Baseline)      │────────────────────────────────────►│  SChannel mTLS,    │  │
 │  └───────────────────────┘                                     │  L4RTP/1 framing)  │  │
 └────────────────────────────────────────────────────────────────┼────────────────────┘  │
                                                                  │                       │
                                                      WAN / mTLS  │ TCP :8443             │
                                                      (Client Cert│ auth по               │
                                                       iot_leo4_ca│ .crt)                 │
                                                                  ▼                       │
 ┌────────────────────────────────────────────────────────────────────────────────────────┴┐
 │ Сервер 87.242.100.34 (Docker-проект l4media)                                            │
 │                                                                                         │
 │   ┌─────────────────────────────────────────────────────────────────────────────────┐   │
 │   │ [l4media-nginx] (Порт :8443)                                                    │   │
 │   │  • stream {} TLS-терминация со строгим ssl_verify_client on                     │   │
 │   │  • Профиль шифров Windows SChannel (@SECLEVEL=1, AES-GCM + RSA fallbacks)       │   │
 │   └────────────────────────────────────────┬────────────────────────────────────────┘   │
 │                                            │ TCP :9000 (внутри docker bridge)           │
 │                                            │ расшифрованный поток L4RTP/1               │
 │                                            ▼                                            │
 │   ┌─────────────────────────────────────────────────────────────────────────────────┐   │
 │   │ [l4media-ingress] (Чистый C, epoll, non-blocking)                               │   │
 │   │  • 1. Парсинг преамбулы: Magic "L4RT" + ver 0x01 + SN устройства                │   │
 │   │  • 2. Валидация по таблице маршрутизации (routes.conf / Dynamic Memory Table)   │   │
 │   │  • 3. Декапсуляция кадров: Type 0x01 (RTP) и 0x02 (RTCP)                        │   │
 │   │  • 4. HTTP Control & Stats API (Порт :9100) — маршруты и метрики                │   │
 │   └───────────────────┬────────────────────────────────────────┬────────────────────┘   │
 │                       │                                        │                        │
 │     RTP (UDP :6000)   │                      RTCP (UDP :6001)  │                        │
 │                       ▼                                        ▼                        │
 │   ┌─────────────────────────────────────────────────────────────────────────────────┐   │
 │   │ [l4media-janus] (Janus WebRTC Gateway)                                          │   │
 │   │  • janus.plugin.streaming: RTP Mountpoints (H.264, PT 96)                       │   │
 │   │  • Core APIs: HTTP :8088 (/janus), WS :8188, Admin :7088 (/admin)               │   │
 │   │  • WebRTC подсистема: ICE, DTLS-SRTP шифрование медиапотоков                    │   │
 │   └────────────────────────────────────────┬────────────────────────────────────────┘   │
 └────────────────────────────────────────────┼────────────────────────────────────────────┘
                                              │
                                              │ WebRTC Media (SRTP / UDP :20000-20100)
                                              │ Сигнализация (WS :8188 или через Nginx :443)
                                              ▼
 ┌─────────────────────────────────────────────────────────────────────────────────────────┐
 │ Браузер Оператора (MenuBuilder Web UI / React 19 SPA)                                   │
 │  • Вкладка / Модальное окно «Видеомониторинг терминала» (Ant Design 6)                  │
 │  • Видеоплеер HTML5 <video autoPlay playsInline muted />                                │
 │  • Управление качеством, битрейт, FPS, RTT и кнопка перезапуска потока                  │
 └─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Полная матрица сетевых портов

В подсистеме задействованы сетевые порты на трёх уровнях: внешнем (WAN), внутреннем (Docker-сеть) и локальном (на терминале).

### 3.1. Внешние публичные порты сервера (`87.242.100.34`)

| Порт / Протокол | Сервис / Контейнер | Назначение | Политика безопасности / Доступ |
|---|---|---|---|
| **`8443/TCP`** | `l4media-nginx` | **Входной шлюз L4RTP/1 mTLS** для терминалов (`leo4proxy`). | Открыт в Интернет. Вход строго по клиентским сертификатам, подписанным доверенным CA (`iot_leo4_ca.crt`). Соединения без сертификата или с невалидным сертификатом сбрасываются на этапе TLS-рукопожатия. |
| **`20000–20100/UDP`** | `l4media-janus` | **WebRTC Media Range (ICE / DTLS / SRTP)** для браузерных клиентов. | Открыт в Интернет. Порты динамически выделяются Janus Gateway для передачи зашифрованного медиатрафика браузерным клиентам. |
| **`443/TCP`** | Основной Nginx хоста (`nginx-default`) | **MenuBuilder Web UI & API**. Опциональный WSS/HTTPS прокси для WebRTC сигнализации Janus. | Открыт в Интернет. Публичный HTTPS доступ операторов с проверкой JWT-сессии. |

### 3.2. Внутренние порты Docker-сети (`l4media_net` / `user1_default`)

Данные порты **не публикуются наружу** и доступны только внутри изолированных Docker-сетей:

| Порт / Протокол | Сервис | Назначение | Потребители |
|---|---|---|---|
| **`9000/TCP`** | `l4media-ingress` | Внутренний приём декапсулированного TCP-потока L4RTP/1. | Только `l4media-nginx` (директива `proxy_pass ingress:9000`). |
| **`9100/TCP`** | `l4media-ingress` | **HTTP Control & Stats API** (маршруты `/routes`, статистика `/stats`). | `menubuilder-backend`, системы мониторинга и автоматизации. |
| **`6000/UDP`** | `l4media-janus` | Приём входящего RTP-видеопотока Mountpoint 1 (H.264). | `l4media-ingress` (по таблице маршрутов). |
| **`6001/UDP`** | `l4media-janus` | Приём входящих RTCP-пакетов Mountpoint 1. | `l4media-ingress` (по таблице маршрутов). |
| **`8088/TCP`** | `l4media-janus` | **Janus Core HTTP REST API** (`/janus`). Управление сессиями WebRTC. | `menubuilder-backend`, веб-клиенты через reverse proxy. |
| **`8188/TCP`** | `l4media-janus` | **Janus WebSockets API** (`/`). Низколатентная сигнализация WebRTC. | Веб-клиенты `MenuBuilder/frontend` (напрямую или через WSS). |
| **`7088/TCP`** | `l4media-janus` | **Janus Admin/Monitor HTTP API** (`/admin`). Создание/удаление маунтпоинтов on-demand. | Только `menubuilder-backend` (защищено токеном `admin_secret`). |

### 3.3. Локальные порты на терминале (Windows Loopback `127.0.0.1`)

| Порт / Протокол | Модуль | Назначение | Примечание |
|---|---|---|---|
| **`5004/UDP`** | `leo4proxy` | Приём локального RTP-потока от `ffmpeg`. | Без `SO_REUSEADDR` во избежание перехвата другими процессами. Буфер `SO_RCVBUF = 512 KB`. |
| **`5005/UDP`** | `leo4proxy` | Приём локального RTCP-потока от `ffmpeg`. | Статистика качества и таймингов видео. |
| **`18443/TCP`** | `leo4proxy` | Локальный REST API прокси (`/_leo4/info`, `/_leo4/sn`). | Автоматическое определение серийного номера устройства локальными скриптами. |
| **`18883/TCP` / `1883/TCP`** | `leo4proxy` / Mosquitto | Локальный MQTT-канал управления терминалом. | Приём RPC-команд старта/остановки видео (`7000 STREAM_CONTROL`). |

---

## 4. Ожидаемый формат стрима и протоколы

Передача медиаданных от захвата экрана до браузера оператора строится на трёх уровнях:
1. Захват экрана и упаковка в RTP на терминале.
2. Инкапсуляция в протокол **L4RTP/1** и передача по mTLS.
3. Декапсуляция и выдача через WebRTC (SRTP).

### 4.1. Спецификация wire-протокола L4RTP/1 (Транспорт TCP/mTLS)

Поскольку прямое прохождение сырого UDP через Интернет блокируется многими мобильными операторами, корпоративными файрволами и NAT платёжных терминалов, все RTP/RTCP пакеты туннелируются через единое надёжное TCP-соединение с SChannel mTLS шифрованием.

#### Структура пакета 1: Преамбула установления сессии (Client ➔ Server)
Отправляется терминалом ровно **один раз** сразу после успешного TLS-рукопожатия:

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|       'L'     |       '4'     |       'R'     |       'T'     |  Magic: "L4RT" (0x4C345254)
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Ver: 0x01    |  Flags: 0x00  |          SN Length (BE)       |  Version (1B), Flags (1B), SN Len (2B)
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                 Serial Number (SN) [1..127 байт]              |  Серийный номер устройства (ASCII)
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

* Поля преамбулы:
  * `Magic` (4 байта): константа `"L4RT"` (`0x4C, 0x34, 0x52, 0x54`).
  * `Version` (1 байт): версия протокола, строго `0x01`.
  * `Flags` (1 байт): зарезервировано, в текущей версии `0x00`.
  * `SN Length` (2 байта, Big-Endian): длина серийного номера (от 1 до 127).
  * `SN` (N байт): строковый серийный номер терминала (совпадает с Common Name клиентского сертификата).

#### Структура пакета 2: Кадр данных потока (Stream Frame)
Циклично передаётся в установленной сессии:

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Type: 0x01.. |  Flags: 0x00  |       Payload Length (BE)     |  Header: 4 байта
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                 Payload (RTP / RTCP / Keepalive)              |  Данные (до 65535 байт)
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

* Коды типов кадров (`Type`):
  * `0x01` (`L4RTP_FRAME_RTP`): исходный RTP-пакет (видеоданные H.264). Ingress пересылает его по UDP на `rtp_port` целевого маунтпоинта Janus.
  * `0x02` (`L4RTP_FRAME_RTCP`): пакет протокола RTCP. Ingress пересылает его по UDP на `rtcp_port`.
  * `0x03` (`L4RTP_FRAME_KEEPALIVE`): прикладной пинг при длительном отсутствии кадров (игнорируется в ingress, сбрасывает idle-таймаут).

### 4.2. Параметры видеокодека H.264 для WebRTC

Для того чтобы видео транслировалось в браузер мгновенно и без необходимости ресурсоёмкого транскодирования на сервере, параметры энкодера на терминале должны строго соответствовать стандартам WebRTC:

| Параметр | Требуемое значение | Обоснование / Влияние |
|---|---|---|
| **Видеокодек** | **H.264 (AVC / MPEG-4 Part 10)** | Нативная поддержка во всех современных браузерах через аппаратное ускорение GPU. |
| **H.264 Profile** | **Constrained Baseline Profile** (`baseline`) | Полное исключение B-кадров (B-frames = 0). Гарантирует декодирование без задержки на реорганизацию кадров. |
| **Profile-Level-ID** | `42e01f` | Baseline Profile, Level 3.1 (SD/HD видео до 720p/1080p при умеренных битрейтах). |
| **Packetization Mode** | `1` (Non-interleaved mode) | Обязателен для WebRTC H.264 SDP (`videofmtp = "profile-level-id=42e01f;packetization-mode=1"`). |
| **RTP Payload Type (PT)** | `96` (Dynamic PT) | Стандартный динамический номер типа нагрузки RTP для видео. |
| **Интервал ключевых кадров (GOP / IDR)** | **16–20 кадров** (каждые 1–2 секунды) | **Критично:** При подключении нового зрителя в браузере WebRTC не может отобразить картинку до получения первого IDR-кадра. Короткий GOP обеспечивает мгновенное открытие видео (< 1.5 с). |
| **Тюнинг задержки** | `-tune zerolatency -preset ultrafast` | Минимизация внутреннего буфера энкодера x264 до 0 кадров. |
| **Размер пакета (MTU)** | `pkt_size = 1200` | Предотвращение фрагментации UDP-пакетов при пересылке внутри Docker-сети. |
| **Цветовой формат** | `yuv420p` | Универсальный формат цветности для браузеров. |

### 4.3. Рекомендуемые профили стриминга (Пресеты ffmpeg)

#### Профиль 1: Мониторинг интерфейса терминала (Desktop / FWVGA — Рекомендуемый)
Обеспечивает превосходную читаемость текста при минимальном потреблении трафика и минимальной нагрузке на процессор терминала (CPU load < 5%):
* Разрешение: **`854x480`** (16:9)
* Частота кадров: **`8–10 fps`**
* Битрейт: **`450 kbps`** (maxrate: `550 kbps`, bufsize: `900 kbps`)
* Команда запуска на Windows-терминале:
  ```cmd
  ffmpeg.exe -hide_banner -f gdigrab -framerate 8 -i desktop -an ^
    -vf "scale=854:480:force_original_aspect_ratio=decrease,pad=854:480:(ow-iw)/2:(oh-ih)/2" ^
    -c:v libx264 -preset ultrafast -tune zerolatency -profile:v baseline -pix_fmt yuv420p ^
    -g 16 -b:v 450k -maxrate 550k -bufsize 900k -payload_type 96 ^
    -f rtp "rtp://127.0.0.1:5004?rtcpport=5005&pkt_size=1200"
  ```

#### Профиль 2: Диагностика высокой чёткости (HD 720p)
Используется при необходимости рассмотреть мелкие системные шрифты, логи или чеки:
* Разрешение: **`1280x720`**
* Частота кадров: **`12–15 fps`**
* Битрейт: **`800 kbps`** (maxrate: `1000 kbps`, bufsize: `1500 kbps`)
* Команда запуска:
  ```cmd
  ffmpeg.exe -hide_banner -f gdigrab -framerate 12 -i desktop -an ^
    -vf "scale=1280:720:force_original_aspect_ratio=decrease,pad=1280:720:(ow-iw)/2:(oh-ih)/2" ^
    -c:v libx264 -preset ultrafast -tune zerolatency -profile:v baseline -pix_fmt yuv420p ^
    -g 24 -b:v 800k -maxrate 1000k -bufsize 1500k -payload_type 96 ^
    -f rtp "rtp://127.0.0.1:5004?rtcpport=5005&pkt_size=1200"
  ```

---

## 5. Способы интеграции с MenuBuilder

Интеграция с порталом управления `MenuBuilder` (FastAPI backend + React 19 frontend) строится на объединении сетевых контуров и программных API.

### 5.1. Сетевое сопряжение контейнеров

Сервисы `l4media` развёрнуты в изолированной Docker-сети `l4media_net`. Для взаимодействия с `MenuBuilder` контейнеры `l4media-ingress` и `l4media-janus` подключаются к внешней сети `user1_default` (основная сеть MenuBuilder):

```bash
# Однократное подключение сервисов к общей сети (или через compose.yaml):
docker network connect user1_default l4media-ingress
docker network connect user1_default l4media-janus
```

После подключения сервисы становятся доступны бэкенду `MenuBuilder` по внутренним именам:
* Ingress Control API: `http://l4media-ingress:9100`
* Janus HTTP Gateway: `http://l4media-janus:8088/janus`
* Janus WebSockets: `ws://l4media-janus:8188`
* Janus Admin API: `http://l4media-janus:7088/admin`

---

### 5.2. Архитектурный паттерн 1: Статические маунтпоинты (Static Mountpoints — Alpha/Dev)

Самый простой способ интеграции, реализованный в Alpha-MVP:
1. В конфигурационном файле Janus (`janus.plugin.streaming.jcfg`) заранее задаётся статический маунтпоинт:
   ```jcfg
   stream-1: {
       type = "rtp"
       id = 1
       description = "Terminal Live Desktop"
       video = true
       audioport = 0
       videoport = 6000
       videortcpport = 6001
       videopt = 96
       videocodec = "h264"
       videofmtp = "profile-level-id=42e01f;packetization-mode=1"
   }
   ```
2. В файле маршрутов Ingress (`/etc/l4media/routes.conf`) прописывается соответствие серийного номера терминала портам маунтпоинта:
   ```text
   a4b0000773c82116d210826 6000 6001
   ```
3. Приложение `MenuBuilder/frontend` при открытии вкладки видеомониторинга терминала подключается по WebSocket к Janus, подписывается на маунтпоинт с `id: 1` и отображает видеопоток.

* **Преимущества:** Минимальная сложность, отсутствие динамической логики выделения портов.
* **Ограничения:** Подходит только для фиксированного стенда или одного терминала; для N терминалов требует ручной настройки портов.

---

### 5.3. Архитектурный паттерн 2: Динамическая On-Demand оркестрация (Целевая Production модель)

В боевой эксплуатации терминалы не должны непрерывно транслировать видео в облако (экономия трафика сим-карт и ресурсов сервера). Трансляция инициируется **только по требованию оператора**:

```text
 [MenuBuilder Web UI]      [MenuBuilder Backend]       [l4media-janus]    [l4media-ingress]   [MQTT Broker]      [Terminal Киоск]
          │                          │                        │                   │                 │                   │
  1. Клик │                          │                        │                   │                 │                   │
   «Смотреть трансляцию»             │                        │                   │                 │                   │
          │─────────────────────────►│                        │                   │                 │                   │
          │                          │ 2. POST /admin         │                   │                 │                   │
          │                          │    (create mountpoint) │                   │                 │                   │
          │                          │───────────────────────►│                   │                 │                   │
          │                          │◄───────────────────────│ (ports 6010/6011) │                 │                   │
          │                          │                        │                   │                 │                   │
          │                          │ 3. PUT /routes/<SN>?rtp=6010&rtcp=6011     │                 │                   │
          │                          │───────────────────────────────────────────►│                 │                   │
          │                          │◄───────────────────────────────────────────│                 │                   │
          │                          │                                            │                 │                   │
          │                          │ 4. Публикация задачи 7000 STREAM_CONTROL   │                 │                   │
          │                          │─────────────────────────────────────────────────────────────►│                   │
          │                          │                                                              │───► Приём задачи  │
          │                          │                                                              │     Запуск ffmpeg │
          │                          │                                                              │     и leo4proxy   │
          │                          │                                                              │                   │
          │ 5. mountpoint_id=1001    │                                                              │                   │
          │◄─────────────────────────│                                                              │                   │
          │                                                                                         │                   │
          │ 6. WebRTC Signaling (ws://... / SDP Offer / Answer)                                     │                   │
          │──────────────────────────────────────────────────►│                                     │                   │
          │                                                   │◄────────────────────────────────────┴───────────────────│
          │                                                   │  7. mTLS -> L4RTP/1 -> Ingress -> UDP RTP/RTCP          │
          │ 8. Отображение видео WebRTC (H.264)               │                                                         │
          │◄══════════════════════════════════════════════════│                                                         │
          │                                                   │                                                         │
  9. Закрытие вкладки / Таймаут неактивности                  │                                                         │
          │─────────────────────────►│ 10. POST /admin (destroy)                                                        │
          │                          │ 11. DELETE /routes/<SN>                                                          │
          │                          │ 12. MQTT 7002 (STOP STREAM)                                                      │
```

#### Пошаговый сценарий динамической сессии:
1. **Запрос оператора:** Оператор в панели терминала (`/terminals/{id}`) нажимает кнопку «Включить трансляцию экрана».
2. **Создание маунтпоинта в Janus:**
   Бэкенд `MenuBuilder` отправляет запрос в Janus Admin API (`POST http://l4media-janus:7088/admin`):
   ```json
   {
     "janus": "message_plugin",
     "transaction": "txn_create_1001",
     "admin_secret": "janusoverlord",
     "plugin": "janus.plugin.streaming",
     "request": {
       "request": "create",
       "type": "rtp",
       "id": 1001,
       "name": "Live stream for SN a4b0000773c82116d210826",
       "description": "On-demand session",
       "is_private": false,
       "video": true,
       "videoport": 6010,
       "videortcpport": 6011,
       "videopt": 96,
       "videocodec": "h264",
       "videofmtp": "profile-level-id=42e01f;packetization-mode=1"
     }
   }
   ```
3. **Регистрация в Ingress:**
   Бэкенд регистрирует динамический маршрут в `l4media-ingress`:
   ```http
   PUT http://l4media-ingress:9100/routes/a4b0000773c82116d210826?rtp=6010&rtcp=6011
   ```
4. **Старт стрима на терминале через MQTT:**
   Бэкенд отправляет стандартную RPC-задачу диагностики (метод `7000 CMD_DIAG_STREAM_CONTROL` согласно `ops_run-remote-console-diagnostics.md`) в топик `srv/<SN>/tsk`:
   ```json
   {
     "method": 7000,
     "action": "start",
     "profile": "desktop_fwvga",
     "ttl_sec": 300,
     "rtp_host": "87.242.100.34",
     "rtp_port": 8443
   }
   ```
   Агент на киоске запускает локальный фоновый захват `ffmpeg` и активирует `--rtp-tunnel` в `leo4proxy`.
5. **Просмотр в браузере:**
   Фронтенд `MenuBuilder` получает `mountpoint_id = 1001`, подключается к Janus через WebSockets, производит обмен SDP (Offer/Answer) и выводит поток в тег `<video>`.
6. **Автоматическое завершение:**
   При закрытии вкладки в браузере или по истечении времени сессии (`ttl_sec`) бэкенд MenuBuilder:
   * Отправляет команду `7002 CMD_DIAG_CANCEL` по MQTT на терминал для глушения ffmpeg.
   * Удаляет маршрут из Ingress (`DELETE http://l4media-ingress:9100/routes/<SN>`).
   * Уничтожает маунтпоинт в Janus (`request: "destroy", id: 1001`).

---

### 5.4. Архитектурный паттерн 3: Защищённый WebRTC Gateway через Nginx (Безопасность)

Для публикации WebRTC-сигнализации наружу **запрещено открывать сырой порт 8188 или 7088 в открытый Интернет**. Вместо этого WebSockets Janus проксируются через основной обратный прокси Nginx хоста с проверкой авторизации:

```nginx
# В конфигурации Nginx MenuBuilder (:443):
location /janus-ws {
    # Проверка JWT cookie оператора (только авторизованные администраторы)
    auth_request /api/auth/validate-session;

    proxy_pass http://l4media-janus:8188;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_set_header Host $host;
    proxy_read_timeout 3600s;
}
```

Это гарантирует:
* Полную защиту медиашлюза от несанкционированного доступа.
* Тенантную изоляцию (оператор может просматривать видео только тех терминалов, к которым имеет доступ согласно своей роли и организации в MenuBuilder).
* Защиту от перебора идентификаторов маунтпоинтов.

---

### 5.5. Клиентский веб-компонент в MenuBuilder/frontend (React 19 + Ant Design 6)

В интерфейсе администратора `MenuBuilder` видеотрансляция интегрируется в карточку терминала (например, как дополнительная вкладка `«Видеомониторинг»` рядом с вкладкой удалённой консоли `DeviceConsoleTab`):

```tsx
/**
 * Концептуальный компонент видеомониторинга терминала
 */
import React, { useEffect, useRef, useState } from 'react';
import { Card, Button, Space, Badge, Alert, Spin } from 'antd';
import { PlayCircleOutlined, StopOutlined, VideoCameraOutlined } from '@ant-design/icons';

interface TerminalVideoTabProps {
  terminalSn: string;
}

export const TerminalVideoTab: React.FC<TerminalVideoTabProps> = ({ terminalSn }) => {
  const videoRef = useRef<HTMLVideoElement>(null);
  const [isStreaming, setIsStreaming] = useState(false);
  const [loading, setLoading] = useState(false);
  const [stats, setStats] = useState<{ fps?: number; bitrate?: string; rtt?: number }>({});

  const startStream = async () => {
    setLoading(true);
    try {
      // 1. Запрос к API MenuBuilder на запуск сессии стриминга
      const res = await fetch(`/api/admin/terminals/${terminalSn}/stream/start`, { method: 'POST' });
      const data = await res.json();
      
      // 2. Инициализация WebRTC PeerConnection к Janus /janus-ws для data.mountpoint_id
      // (Подключение videoRef.current.srcObject = remoteStream)
      setIsStreaming(true);
    } finally {
      setLoading(false);
    }
  };

  const stopStream = async () => {
    await fetch(`/api/admin/terminals/${terminalSn}/stream/stop`, { method: 'POST' });
    setIsStreaming(false);
  };

  return (
    <Card
      title={
        <Space>
          <VideoCameraOutlined />
          <span>Экран терминала в реальном времени</span>
          <Badge status={isStreaming ? "processing" : "default"} text={isStreaming ? "LIVE" : "Остановлен"} />
        </Space>
      }
      extra={
        <Space>
          {!isStreaming ? (
            <Button type="primary" icon={<PlayCircleOutlined />} onClick={startStream} loading={loading}>
              Включить трансляцию
            </Button>
          ) : (
            <Button danger icon={<StopOutlined />} onClick={stopStream}>
              Остановить
            </Button>
          )}
        </Space>
      }
    >
      <div style={{ position: 'relative', width: '100%', maxWidth: 854, background: '#000', borderRadius: 8, overflow: 'hidden' }}>
        <video
          ref={videoRef}
          autoPlay
          playsInline
          muted
          style={{ width: '100%', height: 'auto', display: isStreaming ? 'block' : 'none' }}
        />
        {!isStreaming && (
          <div style={{ height: 480, display: 'flex', alignItems: 'center', justifyContent: 'center', color: '#888' }}>
            <span>Трансляция не запущена. Нажмите кнопку «Включить трансляцию» для просмотра экрана.</span>
          </div>
        )}
      </div>
    </Card>
  );
};
```

---

## 6. Отказоустойчивость, лимиты и жизненный цикл соединений

1. **Таймауты неактивности (Idle Disconnect):**
   * В `leo4proxy` настроен параметр `--rtp-idle-timeout 30` (при отсутствии кадров от ffmpeg в течение 30 секунд mTLS-туннель разрывается, переводя прокси в режим ожидания `IDLE`).
   * В Nginx stream задан `proxy_timeout 3600s` для предотвращения обрыва длительных сессий оператора.
2. **Экспоненциальный Backoff при сбоях связи:**
   * При разрыве mTLS-соединения с сервером `leo4proxy` переходит в состояние `STATE_BACKOFF` с удвоением интервала реконнекта (от 3 до 30 секунд), предотвращая перегрузку сервера запросами.
3. **Лимиты ресурсов (Docker Resource Constraints):**
   * Контейнер `l4media-ingress`: лимит памяти 128 MB (реальное потребление < 15 MB).
   * Контейнер `l4media-nginx`: лимит памяти 128 MB.
   * Контейнер `l4media-janus`: лимит памяти 512 MB.
4. **Контроль целостности буфера при сетевых всплесках:**
   * Локальные UDP-сокеты в `leo4proxy` имеют буфер `SO_RCVBUF = 512 KB` для предотвращения потерь пакетов во время пачек ключевых кадров (IDR-bursts).
   * Выходной UDP-сокет в `l4media-ingress` имеет буфер `SO_SNDBUF = 1 MB`.

---

## 7. Сводная таблица соответствия спецификации

| Характеристика | Спецификация | Текущая реализация в Alpha-MVP |
|---|---|---|
| **Входной протокол на сервере** | mTLS TCP :8443 (TLS 1.2, SChannel ciphers, Client Cert CA) | Полностью реализовано (`l4media-nginx`, образ Alpine stream) |
| **Формат транспортного фрейминга** | Преамбула L4RTP/1 (`L4RT`), кадры RTP (`0x01`), RTCP (`0x02`), Keepalive (`0x03`) | Полностью реализовано (`l4media-ingress` на чистом C) |
| **Маршрутизация по серийному номеру** | Таблица маршрутов SN -> (rtp_port, rtcp_port) | Реализовано: файл `routes.conf` + динамический HTTP API на порту 9100 |
| **Выходной медиасервер** | Janus WebRTC Gateway (H.264, PT 96) | Полностью реализовано (`l4media-janus`, mountpoint 1) |
| **Интеграция с MenuBuilder** | Сеть `user1_default`, Control API 9100, Janus APIs (8088/8188/7088), MQTT RPC 7000 | Сетевые интерфейсы и Control API готовы к подключению |
