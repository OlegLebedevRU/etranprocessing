# Подпроект `l4media`: Серверный Alpha-MVP для видеостримов L4RTP/1 и WebRTC

> 📖 **Полная архитектурная спецификация:**  
> Детальное описание архитектуры, матрицы портов, формата видеопотока и способов интеграции с `MenuBuilder` представлено в **[`ARCHITECTURE.md`](ARCHITECTURE.md)** и **[`docs/etran_arch-l4media-streaming-architecture.md`](../docs/etran_arch-l4media-streaming-architecture.md)**.  
> 📊 **Профилирование ресурсов, юнит-бюджеты и расчет емкости (480p vs 720p):** **[`docs/etran_arch-l4media-resource-profiling-and-unit-budgets.md`](../docs/etran_arch-l4media-resource-profiling-and-unit-budgets.md)**.

Изолированный серверный программный стек для приёма видеостримов от терминалов под управлением `leo4proxy` (протокол **L4RTP/1**) через защищённый взаимный TLS (**mTLS**), демультиплексирования потоков по серийному номеру (**SN**) и ретрансляции в медиа-сервер **Janus Gateway** для последующей раздачи в WebRTC.

---

## 1. Архитектура и сетевая схема

```text
Терминал (Windows, локальная машина):
  ffmpeg (desktop capture) ──► RTP UDP 127.0.0.1:5004 / RTCP UDP 127.0.0.1:5005
  └──► leo4proxy.exe --rtp-tunnel (mTLS SChannel, кадры L4RTP/1)
       └──► TCP 87.242.100.34:8443

Сервер 87.242.100.34 (Docker Compose проект `l4media`):
  ┌────────────────────────────────────────────────────────────────────────┐
  │ [l4media-nginx] (Порт :8443)                                           │
  │   - Терминация mTLS (проверка клиентского сертификата по CA)          │
  │   - TCP Stream Proxy Pass ──► ingress:9000                             │
  └───────────────────────────────────┬────────────────────────────────────┘
                                      │ TCP (L4RTP/1 stream)
                                      ▼
  ┌────────────────────────────────────────────────────────────────────────┐
  │ [l4media-ingress] (C, POSIX epoll, без внешних зависимостей)            │
  │   - Разбор преамбулы: "L4RT" + v0x01 + SN                             │
  │   - Маршрутизация по SN (таблица routes.conf: SN -> RTP/RTCP порты)    │
  │   - Декапсуляция кадров: type 0x01 (RTP) -> UDP 6000                   │
  │                          type 0x02 (RTCP) -> UDP 6001                  │
  │                          type 0x03 (Keepalive)                         │
  │   - Внутренний Control & Stats HTTP API (:9100)                        │
  └─────────────────┬──────────────────────────────────┬───────────────────┘
                    │ UDP:6000 (RTP)                   │ UDP:6001 (RTCP)
                    ▼                                  ▼
  ┌────────────────────────────────────────────────────────────────────────┐
  │ [l4media-janus] (Janus WebRTC Gateway)                                 │
  │   - Mountpoint 1: RTP video ingest (H.264 / PT 96)                     │
  │   - WebRTC раздача (порты 20000-20100/udp для браузерных клиентов)     │
  │   - HTTP API (:8088), WebSocket (:8188), Admin API (:7088)             │
  └────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Спецификация сетевого протокола L4RTP/1

Протокол **L4RTP/1** разработан для надёжной инкапсуляции и передачи сырых UDP-дейтаграмм RTP и RTCP поверх одного шифрованного mTLS/TCP-соединения.

### 2.1. Преамбула соединения (Preamble)
Отправляется клиентом (`leo4proxy`) немедленно после установки TLS-соединения.

| Смещение | Размер | Поле | Значение / Описание |
|---|---|---|---|
| 0..3 | 4 байта | Magic | ASCII `"L4RT"` (`0x4C, 0x34, 0x52, 0x54`) |
| 4 | 1 байт | Version | `0x01` |
| 5 | 1 байт | Flags | `0x00` (зарезервировано) |
| 6..7 | 2 байта | SN Length | Длина серийного номера в байтах (Big-Endian, uint16) |
| 8..N | `sn_len` | Device SN | Идентификатор устройства / терминала (ASCII) |

Сервер валидирует Magic (`L4RT`) и версию (`0x01`), извлекает `SN` и ищет его в таблице маршрутизации. При невалидной преамбуле или неизвестном `SN` соединение разрывается.

### 2.2. Формат кадров (Frame Format)
После отправки преамбулы поток состоит из последовательности бинарных кадров:

| Смещение | Размер | Поле | Значение / Описание |
|---|---|---|---|
| 0 | 1 байт | Type | `0x01` = RTP<br>`0x02` = RTCP<br>`0x03` = KEEPALIVE |
| 1 | 1 байт | Flags | `0x00` |
| 2..3 | 2 байта | Length | Длина полезной нагрузки кадра (Big-Endian, uint16) |
| 4..N | `len` | Payload | Исходный RTP/RTCP пакет (от ffmpeg) |

- Кадры с типом `0x01` отправляются на `janus:<videoport>` (по умолчанию `6000/udp`).
- Кадры с типом `0x02` отправляются на `janus:<videortcpport>` (по умолчанию `6001/udp`).
- Кадры `0x03` служат для поддержания активности соединения при паузах в видеопотоке.

---

## 3. Структура подпроекта

```text
l4media/
├── compose.yaml                 # Docker Compose стек l4media (nginx, ingress, janus, archive-worker)
├── pyproject.toml               # Конфигурация пакета l4media-archive, ruff, pyright, pytest
├── README.md                    # Руководство по запуску, деплою и тестированию
├── ARCHITECTURE.md              # Архитектура, порты, формат стрима, API и архивный контракт
├── .env.example                 # Шаблон переменных окружения
├── archive/                     # Модуль детерминированной архивации (L4D-15C-MEDIA)
│   ├── canonical.py             # RecordEnvelope, ArchiveManifest, валидация Draft 2020-12 схем
│   ├── guards.py                # Барьеры: RetentionGuard (>3 мес), PathSecurity, ActiveStreams, Verification
│   ├── store.py                 # Горячее хранилище телеметрии TelemetryStore (SQLite) и session summaries
│   ├── pipeline.py              # Жизненный цикл temp -> sha256 -> verify -> atomic rename -> purge
│   ├── restore.py               # Инструмент инспекции, восстановления и аудита трёхлетнего хранения
│   ├── worker.py                # Фоновый воркер архивации (по умолчанию отключен)
│   └── cli.py                   # Интерфейс CLI l4media-archive (status, archive, verify, restore, check-retention)
├── tests/                       # Набор тестов архивации (21/21 passed)
│   ├── test_archive_canonical.py
│   ├── test_archive_guards.py
│   ├── test_archive_pipeline.py
│   ├── test_archive_restore.py
│   └── test_archive_cli.py
├── nginx/
│   ├── Dockerfile               # Образ nginx:stable-alpine со stream mTLS модулем
│   ├── nginx.conf               # Корневой конфиг Nginx со stream {} блоком
│   └── stream.conf              # Профиль TLS 1.2 Schannel и proxy_pass ingress:9000
├── ingress/
│   ├── Dockerfile               # Двухэтапная сборка минимального C-бинарника (Alpine)
│   ├── Makefile                 # Сборка gcc/musl с флагом -static
│   ├── routes.conf              # Таблица маршрутизации SN -> RTP/RTCP
│   ├── src/
│   │   ├── l4media_ingress.c    # POSIX epoll ingress-сервер и HTTP control API
│   │   └── media_lifecycle.h    # On-demand media lifecycle API, Janus orchestration, reconciliation
│   └── tests/
│       ├── test_ingress_regression.py
│       ├── test_ingress_unit.c
│       └── test_media_lifecycle.py
├── janus/
│   ├── janus.jcfg               # Конфигурация ядра Janus (nat_1_1, rtp_port_range)
│   ├── janus.plugin.streaming.jcfg # Статический RTP mountpoint 1 (H.264)
│   └── janus.transport.http.jcfg   # Настройка внутренних API 8088 и 7088
└── deploy/
    ├── deploy.sh                # Идемпотентный скрипт сборки и деплоя на сервер
    └── check.sh                 # Скрипт проверки статуса, логов, портов, mTLS и архиватора
```

---

## 4. Запуск и деплой

### 4.1. Автоматический деплой на сервер (`87.242.100.34`)

Скрипт `deploy/deploy.sh` полностью автоматизирует процесс:
1. Копирует файлы проекта в `~/l4media/` на сервере.
2. Проверяет наличие или создаёт self-signed серверный сертификат (`CN=87.242.100.34`).
3. Проверяет доступность CA-сертификата (`/home/user1/iot-rpc-rest-app/crt/iot_leo4_ca.crt`).
4. Запускает изолированный проект `docker compose -p l4media up -d --build`.
5. Проверяет, что ни один внешний контейнер не был перезапущен.

```bash
# Запуск с машины разработчика (Git Bash / Linux / macOS):
SSH_KEY="d:/.ssh/id_ed25519" ./l4media/deploy/deploy.sh
```

### 4.2. Проверка работоспособности (`check.sh`)

```bash
ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "bash /home/user1/l4media/deploy/check.sh"
```

Скрипт проверяет:
- Слушающий TCP-порт `:8443` на хосте.
- Логи всех трёх контейнеров (`l4media-nginx`, `l4media-ingress`, `l4media-janus`).
- Маршруты в Ingress Control API (`curl http://127.0.0.1:9100/routes`).
- Ответ Janus Gateway API (`curl http://janus:8088/janus/info`).
- Отклонение неавторизованных подключений без клиентского сертификата.

---

## 5. Интеграция с MenuBuilder Backend

Подпроект `l4media` изолирован в собственной сети `l4media_net`. Для интеграции с бэкендом административной панели MenuBuilder предусмотрены внутренние endpoint'ы:

| Сервис | Адрес внутри сети | Протокол / Назначение |
|---|---|---|
| Ingress Control API | `http://l4media-ingress:9100/routes` | Управление маршрутами (GET/PUT/DELETE) |
| Ingress Stats API | `http://l4media-ingress:9100/stats` | Метрики сессий, счётчики RTP/RTCP пакетов и байт |
| Janus HTTP Gateway | `http://l4media-janus:8088/janus` | WebRTC сессии, SDP offer/answer |
| Janus WebSocket | `ws://l4media-janus:8188` | Двунаправленный signaling для фронтенда |
| Janus Admin API | `http://l4media-janus:7088/admin` | Динамическое создание/удаление mountpoint'ов |

### Подключение к сети MenuBuilder (`user1_default`)
Для того чтобы `menubuilder-backend` мог обращаться к `l4media-ingress` и `l4media-janus`, выполните подключение:
```bash
docker network connect user1_default l4media-ingress
docker network connect user1_default l4media-janus
```

### Примеры работы с Control API:
```bash
# Получить все активные маршруты:
curl -s http://l4media-ingress:9100/routes

# Добавить/изменить маршрут для терминала:
curl -X PUT "http://l4media-ingress:9100/routes/a4b0000773c82116d210826?rtp=6000&rtcp=6001"

# Удалить маршрут:
curl -X DELETE "http://l4media-ingress:9100/routes/a4b0000773c82116d210826"

# Получить текущую статистику активных трансляций:
curl -s http://l4media-ingress:9100/stats
```

---

## 6. Приёмочный сценарий (Локальное тестирование на терминале)

Для проверки сквозного видеопотока выполните следующие шаги на клиентской Windows-машине с установленным сертификатом терминала (`SN: a4b0000773c82116d210826`).

### Шаг 1: Запуск Leo4Proxy в режиме RTP-туннеля
`leo4proxy` по умолчанию работает в режиме отключённой строгой проверки CA сервера (`insecure_server_cert = 1`), что позволяет подключаться к alpha self-signed сертификату.

```cmd
leo4proxy.exe -f -v --rtp-tunnel --rtp-remote 87.242.100.34:8443
```
*Примечание:* Туннель использует отложенное подключение (Lazy Connect) — исходящее mTLS соединение к серверу откроется автоматически при поступлении первого UDP-пакета от `ffmpeg`.

### Шаг 2: Запуск захвата рабочего стола через FFmpeg
Откройте второе окно командной строки и запустите захват экрана с кодированием в H.264:

```cmd
D:\ffmpeg\bin\ffmpeg.exe -hide_banner -f gdigrab -framerate 8 -i desktop -an ^
  -vf "scale=854:480:force_original_aspect_ratio=decrease,pad=854:480:(ow-iw)/2:(oh-ih)/2" ^
  -c:v libx264 -preset ultrafast -tune zerolatency -profile:v baseline -pix_fmt yuv420p ^
  -g 16 -b:v 450k -maxrate 550k -bufsize 900k -payload_type 96 ^
  -f rtp "rtp://127.0.0.1:5004?rtcpport=5005&pkt_size=1200"
```

### Шаг 3: Мониторинг на стороне сервера
На сервере `87.242.100.34` отслеживайте логи:

```bash
# 1. Nginx mTLS лог: подтверждение подключения
sudo docker logs -f l4media-nginx

# 2. Ingress лог: преамбула SN и статистика пересылки кадров
sudo docker logs -f l4media-ingress

# 3. Janus лог: приём RTP-пакетов mountpoint'ом 1
sudo docker logs -f l4media-janus

# 4. Просмотр счётчиков через Control API:
sudo docker exec l4media-ingress curl -s http://127.0.0.1:9100/stats
```

---

## 7. Архивация технических подробностей и управление хранением (L4D-15C-MEDIA)

Подсистема реализует требования шага **`L4D-15C-MEDIA`** и единого **Archive Manifest Contract v1** (`H-L4D-15A-DOCS-v1`).

### 7.1. Основные характеристики
* **Владелец архива (`owner_project`):** `l4media`.
* **Типы архивных записей (`record_types`):**
  * `media_stream_samples`: высокообъёмные периодические технические метрики потока (битрейт, fps, rtp/rtcp-пакеты, переданные байты, джиттер, rtt, потери, геометрия кадра).
  * `media_quality_events`: дискретные события деградации качества сессий (packet loss spike, freeze, keyframe request).
* **Горячие данные (STRICT HOT INVARIANT — никогда не удаляются):**
  * Таблицы маршрутизации Ingress (`routes.conf`, dynamic routing table).
  * Сводные строки и метаданные сессий (`tb_media_session_summaries`: длительность, итоговые счётчики пакетов/байт, причина завершения, тайминги).
  * Активные видеопотоки и данные, необходимые для активного обслуживания и реконсиляции.
* **Временной барьер (> 3 полных месяцев):**
  Архивируются и очищаются только данные закрытых месяцев старше 3 полных календарных месяцев. Попытка архивации более свежих месяцев отклоняется (`HOT_RETENTION_VIOLATION`).
* **Инвариант No-Purge-On-Mismatch:**
  Удаление горячих деталей допускается исключительно при 100% совпадении SHA-256, совпадении счётчиков строк, успешной выборочной декомпрессии и отсутствии активных потоков. При любом расхождении пакет помечается `failed`, а горячие данные не трогаются.

### 7.2. Команды CLI (`archive.cli`)
```bash
# Проверка конфигурации воркера и доступных закрытых месяцев:
python3 -m archive.cli status

# Выполнение пробной архивации без удаления (dry-run):
python3 -m archive.cli archive --month 2026-04 --dry-run

# Выполнение полной архивации с очисткой проверенных записей:
python3 -m archive.cli archive --month 2026-04

# Верификация контрольных сумм и структуры пакета на томе:
python3 -m archive.cli verify --batch-dir /mnt/l4desk-archive/2026/04/l4media/arch-media-2026-04-xxxx

# Восстановление записей из архива в SQLite или JSONL:
python3 -m archive.cli restore --batch-dir /mnt/l4desk-archive/2026/04/l4media/arch-media-2026-04-xxxx --output-jsonl /tmp/restored.jsonl

# Аудит 3-летнего хранения и видимости резервных копий (read-only):
python3 -m archive.cli check-retention --volume-root /mnt/l4desk-archive
```

### 7.3. Фоновый воркер и переменные окружения
Воркер запускается модулем `archive.worker`. По умолчанию он отключён для безопасности:
* `L4MEDIA_ARCHIVE_WORKER_ENABLED=false` (включение: `true`).
* `L4MEDIA_ARCHIVE_DRY_RUN=false` (режим симуляции без удаления).
* `L4MEDIA_ARCHIVE_VOLUME_ROOT=/mnt/l4desk-archive` (корень архивного тома).
* `L4MEDIA_HOT_TELEMETRY_DIR=/var/lib/l4media/telemetry` (каталог оперативной базы телеметрии).

---

## 8. Известные ограничения Alpha-MVP

1. **Серверный TLS-сертификат**: Используется self-signed сертификат (`CN=87.242.100.34`). Клиент `leo4proxy` подключается в режиме `insecure_server_cert` (по умолчанию в alpha). Для production потребуется выпуск доверенного сертификата (Let's Encrypt / внутренний CA).
2. **Статический Mountpoint**: В конфигурации зафиксирован один тестовый mountpoint `id = 1` (`rtp:6000`, `rtcp:6001`). Динамическое добавление mountpoint'ов через Janus Admin API планируется на этапе интеграции с MenuBuilder.
3. **Control API без авторизации**: Порт `9100` открыт только внутри внутренней Docker-сети `l4media_net`. Прямой доступ из публичного интернета отсутствует.
4. **Однонаправленный поток**: В alpha-версии реализована передача RTP и RTCP от терминала к серверу. Обратный feedback RTCP от Janus к терминалу не транслируется.
