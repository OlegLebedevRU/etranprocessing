# Архитектура подсистемы видеотрансляций l4media

> 📌 **Центральный архитектурный документ проекта:**  
> Полная официальная версия документа зарегистрирована в общесистемном каталоге документации платформы:  
> **[`docs/etran_arch-l4media-streaming-architecture.md`](../docs/etran_arch-l4media-streaming-architecture.md)**.

---

## 1. Назначение и контекст подсистемы

Подсистема **`l4media`** предназначена для защищённого приёма, маршрутизации и веб-трансляции видеопотоков от экранов платёжных терминалов и киосков платформы `etranprocessing` через **Janus WebRTC Gateway**.

### Ключевые свойства:
1. **Строгий mTLS:** Клиентские сертификаты X.509 из Windows Certificate Store (`LocalMachine\MY`) с неэкспортируемыми закрытыми ключами в Windows CNG KSP.
2. **Sub-second Latency (< 500 ms):** Прямая доставка через WebRTC (SRTP/UDP) без промежуточной HLS/DASH буферизации.
3. **Минимальная нагрузка на CPU киоска:** Профиль H.264 Baseline с тюнингом `zerolatency` и `ultrafast` без транскодирования на сервере.
4. **Zero-Plugin Web UI:** Воспроизведение в браузере оператора `MenuBuilder` через нативный HTML5 `<video>` элемент.

---

## 2. Сквозная схема потоков данных (End-to-End)

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
 └─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Матрица портов и протоколов

### 3.1. Внешние порты (WAN / Интернет, хост `87.242.100.34`)

| Порт / Протокол | Сервис | Описание и назначение | Доступ / Безопасность |
|---|---|---|---|
| **`8443/TCP`** | `l4media-nginx` | **Вход mTLS для терминалов** (`leo4proxy --rtp-tunnel`). | Публичный WAN. Строгая проверка клиентских сертификатов по CA `iot_leo4_ca.crt`. |
| **`20000–20100/UDP`** | `l4media-janus` | **WebRTC Media Range** (ICE, DTLS, SRTP) для браузеров. | Публичный WAN. Динамически выделяется Janus для браузерных клиентов. |
| **`443/TCP`** | Хост Nginx | Основной портал MenuBuilder (UI/API) и прокси WebRTC WS. | Публичный WAN (HTTPS / WSS). |

### 3.2. Внутренние порты Docker-сети (`l4media_net` / `user1_default`)

| Порт / Протокол | Сервис | Описание и назначение | Доступ |
|---|---|---|---|
| **`9000/TCP`** | `l4media-ingress` | Декапсулированный поток L4RTP/1 от Nginx к Ingress. | Только `l4media-nginx`. |
| **`9100/TCP`** | `l4media-ingress` | **Control & Stats API** (GET/PUT/DELETE `/routes`, `/stats`). | `menubuilder-backend`. |
| **`6000/UDP`** | `l4media-janus` | Приём RTP видеоданных Mountpoint 1. | Только `l4media-ingress`. |
| **`6001/UDP`** | `l4media-janus` | Приём RTCP данных Mountpoint 1. | Только `l4media-ingress`. |
| **`8088/TCP`** | `l4media-janus` | Janus HTTP REST API (`/janus`). | `menubuilder-backend`. |
| **`8188/TCP`** | `l4media-janus` | Janus WebSockets API (сигнализация WebRTC). | Браузеры (напрямую или через WSS). |
| **`7088/TCP`** | `l4media-janus` | Janus Admin/Monitor API (`/admin`). | `menubuilder-backend` (`admin_secret`). |

### 3.3. Локальные порты терминала (Windows Loopback `127.0.0.1`)

| Порт / Протокол | Модуль | Назначение |
|---|---|---|
| **`5004/UDP`** | `leo4proxy` | Приём локального RTP видеопотока от `ffmpeg`. |
| **`5005/UDP`** | `leo4proxy` | Приём локального RTCP потока от `ffmpeg`. |
| **`18443/TCP`** | `leo4proxy` | HTTP REST API метаданных устройства (`/_leo4/info`, `/_leo4/sn`). |
| **`18883/TCP` / `1883/TCP`** | `leo4proxy` | MQTT-канал приёма команд управления (`7000 STREAM_CONTROL`). |

---

## 4. Ожидаемый формат стрима

### 4.1. Wire-протокол L4RTP/1
1. **Преамбула (1 раз при старте mTLS-сессии):**
   * `[4B Magic: "L4RT"] [1B Ver: 0x01] [1B Flags: 0x00] [2B BE SN_Len] [SN_Len B: Serial Number]`
2. **Кадры данных:**
   * `[1B Type] [1B Flags: 0x00] [2B BE Length] [Length B: Payload]`
   * `Type 0x01`: RTP-пакет (видео H.264) -> отправляется Ingress на `rtp_port` Janus по UDP.
   * `Type 0x02`: RTCP-пакет -> отправляется Ingress на `rtcp_port` Janus по UDP.
   * `Type 0x03`: Keepalive -> сбрасывает таймер неактивности.

### 4.2. Параметры кодирования H.264
* **Кодек:** H.264 / AVC
* **Профиль:** Constrained Baseline Profile (`-profile:v baseline`, Level 3.1 `42e01f`) без B-кадров.
* **Packetization Mode:** `1` (Non-interleaved mode).
* **Payload Type (PT):** `96` (RTP Dynamic PT).
* **Интервал ключевых кадров (GOP):** `16–20 кадров` (каждые 1–2 секунды, `-g 16` при 8 fps).
* **Тюнинг задержки:** `-preset ultrafast -tune zerolatency`.
* **Размер пакета MTU:** `pkt_size=1200`.

### 4.3. Рекомендуемый запуск ffmpeg на терминале
```cmd
ffmpeg.exe -hide_banner -f gdigrab -framerate 8 -i desktop -an ^
  -vf "scale=854:480:force_original_aspect_ratio=decrease,pad=854:480:(ow-iw)/2:(oh-ih)/2" ^
  -c:v libx264 -preset ultrafast -tune zerolatency -profile:v baseline -pix_fmt yuv420p ^
  -g 16 -b:v 450k -maxrate 550k -bufsize 900k -payload_type 96 ^
  -f rtp "rtp://127.0.0.1:5004?rtcpport=5005&pkt_size=1200"
```

---

## 5. Способы интеграции с MenuBuilder

1. **Подключение сетей:**
   ```bash
   docker network connect user1_default l4media-ingress
   docker network connect user1_default l4media-janus
   ```
2. **Паттерн 1: Статические маунтпоинты (Alpha-MVP):**
   * Фиксированный `mountpoint 1` в `janus.plugin.streaming.jcfg` (videoport=6000, videortcpport=6001).
   * Статический маршрут в `/etc/l4media/routes.conf`.
   * Фронтенд напрямую запрашивает mountpoint 1 через Janus WebSocket.
3. **Паттерн 2: Динамическая On-Demand оркестрация (Production):**
   * Оператор нажимает «Смотреть трансляцию» в `MenuBuilder/frontend`.
   * Бэкенд `MenuBuilder` создаёт mountpoint через Janus Admin API (`POST http://l4media-janus:7088/admin`).
   * Бэкенд регистрирует маршрут в Ingress: `PUT http://l4media-ingress:9100/routes/<SN>?rtp=<p1>&rtcp=<p2>`.
   * Бэкенд отправляет команду запуска по MQTT (`7000 STREAM_CONTROL`) в топик `srv/<SN>/tsk`.
   * Фронтенд отображает видеопоток WebRTC в элементе `<video autoPlay playsInline muted />`.
   * При закрытии вкладки или таймауте сессия гасится (MQTT 7002, удаление маршрута, destroy маунтпоинта).
4. **Паттерн 3: Защищённый WSS прокси через Nginx:**
   * Проксирование WebSockets Janus через `/janus-ws` основного Nginx MenuBuilder (:443) с авторизацией по сессионным JWT cookie оператора.
