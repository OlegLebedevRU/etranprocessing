# L4C-01-FRAMEWORK — Каркас проекта, базовые C-контракты, бинарный IPC, deadline/safety-контроль и тестовый harness

```yaml
prompt_id: L4C-01-FRAMEWORK
scope_project: tools/l4capture
scope_root: D:\repo\platerra\Public\etranprocessing\tools\l4capture
prompt_type: framework-foundation
required_handoff_ids: []
sequence_gate_status: READY_FOR_L4C_01
output_handoff_id: H-L4C-01-v1
next_prompt_id: L4C-02-GDI-CAPTURE
branch: l4capture/l4c-01-framework
report_path: docs/l4capture/handoffs/L4C-01-FRAMEWORK-report.md
candidate_format: DETACHED_V1
candidate_path: docs/l4capture/handoffs/L4C-01-FRAMEWORK-candidate.md
architecture_sections: [1, 2, 3, 4, 5, 8, 9, 11, 12, 13, 14, 15]
consumers:
  - L4C-02-GDI-CAPTURE
  - L4C-03-OPENH264-CODEC
  - L4C-04-RTP-SENDER
  - L4C-05-AGENT-ADAPTER
  - ALL_FOLLOWING
```

---

## 1. Цель и архитектурная миссия

Ты выступаешь в роли **Ведущего архитектора C-решений и системного инженера native-подсистем (L4Capture-Framework-Agent)** в рамках комплекса `tools suite`.

Твоя задача — заложить фундаментальный каркас проекта нативного модуля захвата видео экрана `l4capture.exe` в целевой директории `tools\l4capture\`. Модуль создаётся как высоконадёжный, автономный дочерний процесс Windows, заменяющий ресурсоёмкий `ffmpeg.exe` для desktop-трансляции терминалов самообслуживания.

На шаге **L4C-01-FRAMEWORK** формируется скелет системы, обеспечивающий строгое исполнение архитектурных инвариантов `l4capture_arch_final.md`:
1. Полностью автономная сборка MSVC для двух архитектур: **x86 (32-bit)** с таргетингом на Windows 7 SP1 и **x64 (64-bit)** с полностью статической компоновкой CRT (`/MT`).
2. Определение базовых C-интерфейсов (`ICaptureBackend`, `IEncoderBackend`), структур доступа к кадрам (`FrameView`, `AccessUnit`), типов ошибок и admission-лимитов памяти (до 4K растра).
3. Реализация и верификация бинарного протокола IPC по анонимным наследуемым каналам (pipes) с 16-байтным little-endian заголовком, строгой валидацией длины/версий и конечным автоматом устойчивого частичного чтения/записи.
4. Железобетонный монотонный контроль дедлайна аренды (`GetTickCount64`) и подсистема **Safety Gate**: мгновенная остановка видеопотока (RTP stop $\le 500$ мс) при истечении срока, смене сессии, блокировке рабочего стола или закрытии дескриптора процесса родителем (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`). Никаких остаточных сбросов (drain) и скрытых grace-периодов!
5. Ранний spike-тест C API сторонней библиотеки OpenH264 (`wels/codec_api.h`) и подтверждение модели статической линковки без появления внешних DLL в таблице импортов.
6. Разработка бескомпромиссного автономного C-тест-раннера (`tests/`), покрывающего протокол IPC, расчеты переполнения, таймеры и дедлайны.

---

## 2. Непереговорные рамочные принципы и изоляция

1. **Строгая изоляция директорий (Directory Boundary):**
   - **Код, сборочные скрипты, тесты и локальные бинарники:** строго в `tools\l4capture\`.
   - **Документация, промпты, журнал контрактов:** находятся в `docs\l4capture\`.
   - **Отчёты и candidate-файлы:** формируются строго в `docs\l4capture\handoffs\`.
   - **КАТЕГОРИЧЕСКИЙ ЗАПРЕТ `l4desk-service`:** Строжайше запрещено создавать, изменять или использовать файлы внутри каталога `l4desk-service`. Проект `l4capture` полностью автономен.
   - **ЗАПРЕТ на модификацию других подсистем:** Запрещено изменять файлы в `BACK\`, `FRONT\`, `tools\l4desk`, `ProcessingBackend\`, `MenuBuilder\`, `shared\`, `sqlFileExample\`, `stored-procedures\`.
2. **Стандарты компиляции и переносимости C:**
   - Код пишется строго на чистом **C99/C11** (подмножество MSVC). Файлы компилируются компилятором C (`cl.exe`), а не C++.
   - Статическая компоновка Runtime: флаг `/MT` для всех Release-сборок. Использование динамического CRT (`/MD`, `msvcrt*.dll`, `vcruntime*.dll`) категорически запрещено.
   - Обязательный таргет Windows 7 SP1: флаги компилятора `/D_WIN32_WINNT=0x0601` и линкера `/SUBSYSTEM:CONSOLE,6.01` для x86. Бинарники обязаны запускаться на «чистой» Windows 7 SP1 x86/x64 без установленных пакетов Visual C++ Redistributable.
   - Таблица импортов (`dumpbin /dependents` и `/imports`) должна содержать исключительно системные библиотеки Win32 (`KERNEL32.dll`, `USER32.dll`, `ADVAPI32.dll`, `GDI32.dll`, `WS2_32.dll`, `OLE32.dll`).
3. **Границы ответственности процесса (Process Boundary):**
   - `l4capture.exe` — изолированный дочерний процесс рабочего стола. Внутри него **нет и не может быть** MQTT, WebRTC, TLS, авторизации, обработки входящего ввода пользователя или самостоятельного переподключения к серверу.
   - Процесс взаимодействует исключительно с родительским процессом (`l4desk`) через 2 анонимных канала и передает UDP-поток на loopback (`127.0.0.1:5004/5005`).
4. **Принцип безопасности Fail-Closed и тайминги:**
   - При любой ошибке протокола IPC, невалидной версии/типе команды, нарушении границ буфера, потере дескриптора или истечении дедлайна процесс переходит в аварийный останов.
   - RTP прекращается **не позднее 500 мс** от наступления события.
   - Отправка остаточных кадров (drain) после команды остановки или экспирации дедлайна **строго запрещена**.
   - Существующий в родительском процессе (`l4desk`) 5-секундный grace-период для `l4capture` **запрещён** (дочерний процесс контролирует жесткий лимит самостоятельно).

---

## 3. Pre-Flight Check & Contract Gate (Шаг 1)

Перед началом внесения любых изменений выполни проверку контрактного допуска:

1. Открой файл журнала `docs\l4capture\prompts\contract-handoff.md`.
2. Убедись, что журнал находится в статусе `READY_FOR_L4C_01` (это шаг №1 каскада, предшествующих handoff не требуется).
3. Проверь отсутствие конфликтующих записей `H-L4C-01-v1` в секции принятых блоков.
4. Зафиксируй исходное состояние Git: чистый рабочий каталог в `tools/l4capture`, ветка `l4capture/l4c-01-framework`.
5. При обнаружении расхождений немедленно заверши работу со статусом `BLOCKED_CONTRACT`.

---

## 4. Структура проекта и сборочная инфраструктура MSVC

Создай в `tools\l4capture\` следующую каноническую структуру каталогов и файлов:

```text
tools\l4capture\
├── include\
│   └── l4capture\
│       ├── types.h              # Базовые целочисленные типы, enum ошибок, форматы пикселей
│       ├── limits.h             # Константы admission-контроля, макс. разрешение (4K), лимиты буферов
│       ├── clock.h              # Монотонные системные часы (GetTickCount64) и утилиты времени
│       ├── deadline.h           # Контроль дедлайна аренды, расчет остатка времени, jump-защита
│       ├── capture_backend.h    # C-контракт ICaptureBackend и структура FrameView
│       ├── encoder_backend.h    # C-контракт IEncoderBackend, NAL-дескрипторы и AccessUnit
│       ├── pipeline.h           # Контракт конвейера: pacing, буферный слот, drop-политика
│       ├── ipc_protocol.h       # Бинарный фрейминг IPC: структуры сообщений, enum команд/событий
│       ├── ipc_pipe.h           # Неблокирующий транспорт над анонимными пайпами (I/O state machine)
│       ├── safety_gate.h        # Session gate, Job Object binding, аварийный таймер
│       └── telemetry.h          # Словарь внутренней диагностики и метрик (mapping contract)
├── src\
│   ├── common\
│   │   ├── clock.c              # Реализация монотонного таймера
│   │   └── limits.c             # Валидация входных размеров и защита от целочисленного переполнения
│   ├── ipc\
│   │   ├── ipc_protocol.c       # Сериализация/десериализация фреймов (endian-safe)
│   │   └── ipc_pipe.c           # Чтение и запись в pipes с поддержкой частичной передачи
│   ├── safety\
│   │   └── safety_gate.c        # Контроль дедлайна, опрос интерактивной сессии и аварийный стоп
│   ├── spike\
│   │   └── openh264_spike.c     # Ранняя проверка C API OpenH264 и компоновки
│   └── main.c                   # Точка входа: разбор CLI аргументов, связывание подсистем, stub-цикл
├── tests\
│   ├── test_runner.c            # Автономный раннер модульных тестов (без сторонних DLL)
│   ├── test_ipc_framing.c       # Тесты парсера IPC, фрагментации, переполнения длины, EOF
│   ├── test_deadline.c          # Тесты монотонного дедлайна, сдвигов системного времени, дедупликации
│   ├── test_limits.c            # Тесты граничных размеров растра, 4K cap, stride overflow
│   └── test_safety_gate.c       # Тесты логики fail-closed, эмуляции session loss и жесткого таймаута
├── res\
│   └── l4capture.rc             # Windows version info & resource script
├── bin\
│   ├── x86\                     # Каталог 32-битного бинарника
│   └── x64\                     # Каталог 64-битного бинарника
├── build.cmd                    # Скрипт полной компиляции x86 и x64 через MSVC
├── test.cmd                     # Скрипт сборки и запуска тестового набора
└── README.md                    # Актуализированное описание проекта
```

### Сборочный скрипт `build.cmd`:
- Автоматически находит `VsDevCmd.bat` (по путям Visual Studio 2022 Community / BuildTools).
- Принимает параметр архитектуры: `build.cmd [all|x86|x64]`. По умолчанию собирает обе целевые платформы.
- Флаги компилятора x86:
  `cl.exe /nologo /O2 /MT /W4 /utf-8 /D_WIN32_WINNT=0x0601 /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS /I include /Foobj\x86\ ...`
- Флаги линкера x86:
  `link.exe /nologo /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\x86\l4capture.exe kernel32.lib user32.lib advapi32.lib gdi32.lib ws2_32.lib ole32.lib`
- Флаги компилятора x64:
  `cl.exe /nologo /O2 /MT /W4 /utf-8 /D_WIN32_WINNT=0x0601 /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS /I include /Foobj\x64\ ...`
- Флаги линкера x64:
  `link.exe /nologo /SUBSYSTEM:CONSOLE /OUT:bin\x64\l4capture.exe kernel32.lib user32.lib advapi32.lib gdi32.lib ws2_32.lib ole32.lib`
- Правило копирования tools: `bin\l4capture.exe` создаётся как копия `bin\x86\l4capture.exe`.
- Компиляция ресурсов: `rc.exe /nologo /fo obj\x86\l4capture.res res\l4capture.rc`.
- Запрет: предупреждения компилятора на уровне `/W4` не допускаются.

---

## 5. Базовые интерфейсные C-контракты и абстракции (`include/l4capture/`)

Зафиксируй точные заголовочные файлы C без неоднозначных `bool` без заголовка, указателей без длины или платформенно-зависимых типов:

### 5.1. Базовые типы и ошибки (`types.h`):
```c
#ifndef L4C_TYPES_H
#define L4C_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    L4C_OK                       = 0,
    L4C_ERR_NO_FRAME             = 1, /* Очередной кадр ещё не готов (не фатально) */
    L4C_ERR_DEVICE_LOST          = 2, /* Восстановимая ошибка устройства захвата */
    L4C_ERR_SESSION_UNAVAILABLE  = 3, /* Рабочий стол заблокирован, UAC, Session 0 */
    L4C_ERR_OUT_OF_MEMORY        = 4, /* Превышение лимита памяти или сбой выделения */
    L4C_ERR_INVALID_ARG          = 5, /* Некорректный аргумент или параметр */
    L4C_ERR_OVERFLOW             = 6, /* Переполнение растра, очереди или времени */
    L4C_ERR_PROTOCOL             = 7, /* Нарушение формата или фрейминга IPC */
    L4C_ERR_DEADLINE_EXPIRED     = 8, /* Истёк монотонный срок аренды (lease) */
    L4C_ERR_PIPE_BROKEN          = 9, /* Канал связи с родителем разорван (EOF) */
    L4C_ERR_FATAL                = 99 /* Невосстановимая системная ошибка */
} l4c_status_t;

typedef enum {
    L4C_PIX_FMT_BGRA = 1, /* Исходный формат GDI / DXGI */
    L4C_PIX_FMT_I420 = 2, /* Целевой формат для OpenH264 */
    L4C_PIX_FMT_NV12 = 3  /* Целевой формат для Hardware MFT */
} l4c_pixel_format_t;

typedef struct {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} l4c_rect_t;

#endif /* L4C_TYPES_H */
```

### 5.2. Контракт захвата (`capture_backend.h`):
```c
#ifndef L4C_CAPTURE_BACKEND_H
#define L4C_CAPTURE_BACKEND_H

#include "types.h"

typedef struct {
    const uint8_t *data;           /* Указатель на непрерывный буфер BGRA */
    uint32_t width;                /* Физическая ширина в пикселях */
    uint32_t height;               /* Физическая высота в пикселях */
    int32_t stride;                /* Байт на строку (всегда положительный, top-down) */
    size_t buffer_size;            /* Полный размер буфера в байтах */
    l4c_rect_t physical_rect;      /* Исходные координаты (могут быть отрицательными) */
    uint64_t pts_ms;               /* Монотонная временная метка захвата */
    uint64_t geometry_generation;  /* Счётчик смены геометрии дисплея */
} l4c_frame_view_t;

typedef struct {
    l4c_rect_t target_rect;
    bool capture_cursor;
} l4c_capture_config_t;

struct l4c_capture_backend;

typedef struct l4c_capture_backend_vtable {
    l4c_status_t (*init)(struct l4c_capture_backend *self, const l4c_capture_config_t *config);
    l4c_status_t (*acquire_frame)(struct l4c_capture_backend *self, l4c_frame_view_t *out_frame, uint32_t timeout_ms);
    void (*release_frame)(struct l4c_capture_backend *self, l4c_frame_view_t *frame);
    void (*destroy)(struct l4c_capture_backend *self);
} l4c_capture_backend_vtable_t;

typedef struct l4c_capture_backend {
    const l4c_capture_backend_vtable_t *vtable;
    void *impl_ctx;
} l4c_capture_backend_t;

#endif /* L4C_CAPTURE_BACKEND_H */
```

### 5.3. Контракт энкодера (`encoder_backend.h`):
```c
#ifndef L4C_ENCODER_BACKEND_H
#define L4C_ENCODER_BACKEND_H

#include "types.h"

typedef struct {
    const uint8_t *data;    /* NAL без стартового кода / AVCC-префикса */
    uint32_t length;        /* Длина NAL в байтах */
    uint8_t nal_type;       /* 5 = IDR, 7 = SPS, 8 = PPS, 1 = non-IDR и т.д. */
} l4c_nal_desc_t;

typedef struct {
    l4c_nal_desc_t *nals;   /* Массив описателей NAL-единиц */
    uint32_t nal_count;     /* Количество NAL в Access Unit */
    uint64_t pts_ms;        /* Временная метка кадра */
    bool is_idr;            /* Содержит ли Access Unit ключевой кадр IDR */
    size_t total_bytes;     /* Суммарный объём H.264 полезной нагрузки */
} l4c_access_unit_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t target_fps;
    uint32_t target_bitrate_kbps;
    uint32_t max_bitrate_kbps;
    l4c_pixel_format_t input_format;
} l4c_encoder_config_t;

typedef struct {
    const uint8_t *planes[3];
    uint32_t strides[3];
    uint32_t width;
    uint32_t height;
    l4c_pixel_format_t format;
    uint64_t pts_ms;
    bool force_idr;
} l4c_raw_frame_t;

struct l4c_encoder_backend;

typedef struct l4c_encoder_backend_vtable {
    l4c_status_t (*init)(struct l4c_encoder_backend *self, const l4c_encoder_config_t *config);
    l4c_status_t (*encode)(struct l4c_encoder_backend *self, const l4c_raw_frame_t *raw, l4c_access_unit_t *out_au);
    l4c_status_t (*force_idr)(struct l4c_encoder_backend *self);
    void (*release_au)(struct l4c_encoder_backend *self, l4c_access_unit_t *au);
    void (*destroy)(struct l4c_encoder_backend *self);
} l4c_encoder_backend_vtable_t;

typedef struct l4c_encoder_backend {
    const l4c_encoder_backend_vtable_t *vtable;
    void *impl_ctx;
} l4c_encoder_backend_t;

#endif /* L4C_ENCODER_BACKEND_H */
```

### 5.4. Лимиты и Admission-контроль (`limits.h`):
- `L4C_MAX_PIXELS_AREA = 8294400` (4K UHD $3840 \times 2160$ по площади). Превышение даёт отказ `L4C_ERR_OVERFLOW` до вызова `malloc`.
- `L4C_MAX_AU_SIZE = 2097152` (2 МиБ — жесткий лимит на один закодированный Access Unit).
- `L4C_ADMISSION_MEMORY_LIMIT = 134217728` (128 МиБ — жесткий предел пула управляемых буферов).
- `L4C_TARGET_PRIVATE_BYTES_480P = 47185920` (45 МиБ — целевой бюджет памяти для тракта 1080p $\to$ 480p GDI).
- `L4C_IPC_MAX_PAYLOAD = 4096` (4096 байт — максимальный размер полезной нагрузки кадра IPC).
- `L4C_FORCE_IDR_MIN_INTERVAL_MS = 500` (коалесцирование запросов ключевого кадра: не чаще 1 раза в 500 мс).
- Очередь конвейера: ровно 1 кадр в обработке и не более 1 ожидающего (`latest-frame semantics`). Устаревший ожидающий заменяется новым.

---

## 6. Монотонные часы, дедлайн аренды и Safety-Gate

Вся временная логика модуля опирается на системный аптайм, не зависящий от пользовательского изменения времени и сетевой синхронизации (NTP).

### 6.1. Монотонные часы (`clock.h`):
- `uint64_t l4c_now_monotonic_ms(void)` — использует Win32 `GetTickCount64()`.
- Исключается использование `time()`, `GetLocalTime()` или `GetSystemTimeAsFileTime()` для расчета интервалов и дедлайнов.
- Корректная обработка выхода из спящего режима (Sleep/Resume): перед отправкой первого пакета после пробуждения обязательно проверяется актуальный дедлайн.

### 6.2. Управление дедлайном (`deadline.h`):
- Родительский процесс (`l4desk`) передаёт абсолютное значение `deadline_tick_ms` (монотонное время, до которого аренда действительна).
- Продление (`CMD_RENEW_LEASE`):
  - Принимается только если `new_deadline > current_deadline` и `request_seq > last_seq`.
  - Уменьшающие или устаревшие продления отбрасываются.
  - Повторные дубликаты дедуплицируются без изменения состояния.
- Инвариант: для `l4capture` **запрещено** добавлять 5 секунд grace. При наступлении `now >= deadline_tick_ms` видеопоток прекращается мгновенно.

### 6.3. Подсистема Safety-Gate (`safety_gate.h`):
1. **Проверка окружения сессии:**
   - Модуль обязан работать в интерактивной пользовательской сессии. При старте запрашивается ID сессии через `ProcessIdToSessionId(GetCurrentProcessId(), &session_id)`. Если `session_id == 0` (Session 0 — службы Windows), модуль завершает работу с кодом `L4C_ERR_SESSION_UNAVAILABLE`.
2. **Опрос доступности рабочего стола:**
   - До захвата кадра и перед отправкой пакетов с частотой не реже 1 раза в 100 мс вызывается проверка активного Desktop (`OpenInputDesktop(0, FALSE, DESKTOP_SWITCHDESKTOP)`).
   - При блокировке экрана (Win+L), вызове экрана UAC, смене пользователя или RDP-дисконнекте флаг доступности сбрасывается.
   - Реакция: немедленный сброс очередей, прекращение RTP $\le 500$ мс, завершение без передачи черных заглушек или старых зависших кадров. После разблокировки автоматический перезапуск потока **запрещён** (требуется новый авторизованный запуск от родительского процесса `l4desk`).
3. **Привязка к Windows Job Object:**
   - Процесс проверяет нахождение в Job Object с флагом `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`. При закрытии дескриптора Job родительским процессом ОС гарантированно уничтожает процесс `l4capture`.
4. **Безопасный останов и коды возврата:**
   - Останов по `CMD_STOP` или EOF пайпа: нормальное завершение, код возврата `0`.
   - Останов по экспирации дедлайна или недоступности сессии: диагностический ненулевой код возврата.

---

## 7. Бинарный протокол IPC и межпроцессное взаимодействие

Модуль запускается родительским процессом (`l4desk`) с двумя открытыми анонимными пайпами:
`l4capture.exe --pipe-in=<HANDLE_IN_DEC> --pipe-out=<HANDLE_OUT_DEC>`

### 7.1. Формат кадра IPC (16-байтный Little-Endian заголовок):

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          version              |             type              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        payload_length                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         request_seq                           |
|                         (64-bit)                              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                  payload (0 .. 4096 байт)                     |
|                              ...                              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

- `version: uint16_t` — версия протокола (всегда `0x0001`). При любом другом значении — `L4C_ERR_PROTOCOL` и аварийный останов.
- `type: uint16_t` — идентификатор команды или события.
- `payload_length: uint32_t` — длина тела в байтах (не более 4096). При превышении — немедленный разрыв соединения.
- `request_seq: uint64_t` — монотонный порядковый номер сообщения.
- `payload` — бинарное тело сообщения. Числовые поля сериализуются строго в little-endian (явные функции чтения `read_u16_le`, `read_u32_le`, `read_u64_le`). Прямой `memcpy` структур между процессами запрещён во избежание проблем с alignment/padding.

### 7.2. Таблица команд и событий:

| Направление | Имя | Type ID | Назначение и поля payload |
|---|---|---:|---|
| Parent $\to$ Capture | `CMD_START` | `0x0001` | Инициализация: `lease_id:uuid(16)`, `stream_id:uuid(16)`, `source_rect:rect(16)`, `geometry_gen:u64`, `profile_id:u16`, `rtp_port:u16`, `rtcp_port:u16`, `deadline_tick_ms:u64`. Ровно один запуск на процесс. |
| Parent $\to$ Capture | `CMD_RENEW_LEASE` | `0x0002` | Продление: `lease_id:uuid(16)`, `new_deadline_tick_ms:u64`. Проверка `new_deadline > current` и `seq > last_seq`. |
| Parent $\to$ Capture | `CMD_STOP` | `0x0003` | Идемпотентная остановка трансляции: `stream_id:uuid(16)`. Завершение без drain. |
| Parent $\to$ Capture | `CMD_FORCE_IDR` | `0x0004` | Внеплановый запрос IDR-кадра: `stream_id:uuid(16)`. Коалесцирование $\le 1$ раз в 500 мс. |
| Capture $\to$ Parent | `EVENT_READY` | `0x0101` | Первый локальный SPS/PPS+IDR отправлен: `stream_id:uuid(16)`, `actual_width:u32`, `actual_height:u32`, `actual_fps:u16`, `capture_backend:u16`, `encoder_backend:u16`. |
| Capture $\to$ Parent | `EVENT_METRICS` | `0x0102` | Секундный агрегат: `fps:u16`, `bitrate_kbps:u32`, `raw_drops:u32`, `encoder_drops:u32`, `transport_drops:u32`, `encode_p95_ms:u16`, `queue_depth:u16`, `private_bytes_kb:u32`, `gdi_handles:u32`. |
| Capture $\to$ Parent | `EVENT_DEGRADED` | `0x0103` | Уведомление о снижении качества: `degrade_state:u16`, `reason:u16`. |
| Capture $\to$ Parent | `EVENT_ERROR` | `0x0104` | Ошибка: `error_code:u32`, `message_len:u16`, `message_utf8:char[]`. |

### 7.3. Управление потоками и блокировками IPC:
- Поток обработки управляющих сообщений (Control Thread) полностью изолирован от цикла захвата и кодирования (Media Worker). Никаких общих долгих мьютексов.
- Запись событий из media-воркера в IPC выполняется через ограниченный ring buffer (емкостью до 16 сообщений).
- Если дескриптор канала записи заблокирован более 500 мс — активируется аварийный локальный останов. Нельзя заблокировать media loop или safety timer из-за зависшего читателя статусов.

### 7.4. Фиксация маппинга внутренней телеметрии:
В `telemetry.h` фиксируются нормативные ключи, которые передаются через `EVENT_METRICS` и преобразуются адаптером `l4desk` в локальные отчеты и статус:
- `video_capture_backend`: `1=GDI`, `2=DXGI`.
- `video_encoder_backend`: `1=OPENH264_SOFTWARE`, `2=MF_HARDWARE`.
- `video_profile_requested` / `video_profile_actual`: `1=480p`, `2=540p`, `3=720p`.
- `video_hw_accel`: булевый признак, вычисляемый из активного кодера (а не названия видеокарты).
- `video_degradation_state`: `0=NOMINAL`, `1=FPS_THROTTLED`, `2=DEGRADED_540P`, `3=DEGRADED_480P`.
- `video_stream_fps`, `video_stream_bitrate`: фактические значения за окно 1 с.
- `video_fallback_reason`: `0=NONE`, `1=DXGI_ACCESS_LOST`, `2=MFT_UNAVAILABLE`, `3=WIN7_LEGACY`, `4=HIGH_LOAD`.

---

## 8. Ранний Spike: C API OpenH264 и статическая линковка

Для исключения рисков на шаге L4C-03 и Milestone M-1 в рамках L4C-01 выполняется ранний spike (`src/spike/openh264_spike.c`):
1. Проверяется доступность и компилируемость заголовков C API OpenH264:
   ```c
   #include <wels/codec_api.h>
   ```
2. Проверяется создание и освобождение интерфейса кодера через публичный C API:
   `WelsCreateSVCEncoder` и `WelsDestroySVCEncoder`.
3. Анализируется компоновка со статической сборкой OpenH264 (`openh264.lib`) в конфигурации `/MT`:
   - Убедиться, что компоновщик MSVC не запрашивает динамический C++ Runtime (`msvcp*.dll`).
   - Убедиться, что таблица импортов результирующего бинарника не содержит сторонних зависимостей.
4. Результаты spike (доступность API, используемые типы вызова, символы) документируются в отчёте `L4C-01-FRAMEWORK-report.md`.

---

## 9. Тестовая стратегия и автономный Test Harness (`tests/`)

Создай автономную подсистему модульного тестирования на чистом C без привлечения внешних тяжелых фреймворков. Тесты собираются и запускаются скриптом `test.cmd`:

1. **`test_ipc_framing.c` (Тесты фрейминга и протокола):**
   - Проверка упаковки/распаковки всех типов команд и событий.
   - Тест фрагментации: чтение сообщения по 1, 3, 7 байт (эмуляция дробления потока в pipe).
   - Тест невалидной версии (`version = 0x0002` $\to$ возврат `L4C_ERR_PROTOCOL`).
   - Тест превышения длины (`payload_length = 4097` $\to$ отказ до выделения памяти).
   - Тест неполного сообщения при обрыве соединения (эмуляция EOF).
2. **`test_limits.c` (Тесты admission-контроля и вычислений):**
   - Переполнение при расчете площади $W \times H$: проверка защиты от integer overflow.
   - Запрос растра более 8 294 400 пикселей $\to$ немедленный возврат `L4C_ERR_OVERFLOW` без выделения памяти.
   - Проверка вычисления stride с выравниванием (16 байт) и проверка корректности 4K буферов.
3. **`test_deadline.c` (Тесты дедлайна и времени):**
   - Проверка наступления дедлайна при превышении монотонного тика.
   - Тест сдвига времени: прыжок wall clock не влияет на монотонный дедлайн.
   - Тест дедупликации `CMD_RENEW_LEASE`: отклонение продления с меньшим или равным тиком.
   - Тест устаревшего `request_seq`: отклонение внеочередных команд.
4. **`test_safety_gate.c` (Тесты безопасности сессии):**
   - Эмуляция блокировки рабочего стола $\to$ переход в состояние аварийной остановки.
   - Проверка флага завершения: останов RTP фиксируется за время $\le 500$ мс.
   - Проверка очистки очередей: отсутствие остаточных кадров в буфере после сигнала остановки.
5. **Тест бинарников x86 и x64 (`dumpbin`):**
   - Проверка зависимостей: `dumpbin /dependents bin\x86\l4capture.exe` — только базовые Win32 DLL.
   - Проверка подсистемы x86: Console 6.01 (Windows 7 SP1).

---

## 10. План поэтапной реализации агентом

При выполнении промпта агент обязан следовать строгому алгоритму:

1. **Этап 1: Contract Gate & Инициализация**
   - Проверить `contract-handoff.md` (`READY_FOR_L4C_01`).
   - Создать каталоги `tools\l4capture\include\l4capture`, `src`, `tests`, `res`, `bin`.
2. **Этап 2: Заголовочные файлы C-контрактов**
   - Создать `types.h`, `limits.h`, `clock.h`, `deadline.h`, `capture_backend.h`, `encoder_backend.h`, `pipeline.h`, `ipc_protocol.h`, `ipc_pipe.h`, `safety_gate.h`, `telemetry.h`.
3. **Этап 3: Реализация базовых модулей и IPC**
   - Реализовать `clock.c`, `limits.c`.
   - Реализовать `ipc_protocol.c` (little-endian маршалинг) и `ipc_pipe.c` (пайп-транспорт с кольцевым буфером).
   - Реализовать `safety_gate.c` (монотонный дедлайн, проверка Session 0, проверка десктопа).
4. **Этап 4: Точка входа `main.c` и ранний spike OpenH264**
   - Реализовать CLI парсер параметров `--pipe-in` и `--pipe-out`.
   - Интегрировать связывание Safety Gate, IPC loop и stub-обработку команд.
   - Реализовать `openh264_spike.c` для валидации C API кодера.
5. **Этап 5: Сборочная система и версионные ресурсы**
   - Создать `res/l4capture.rc`.
   - Создать `build.cmd` для компиляции x86 (`/SUBSYSTEM:CONSOLE,6.01`) и x64 (`/MT`).
   - Выполнить сборку и убедиться в полном отсутствии предупреждений (Zero Warnings при `/W4`).
6. **Этап 6: Тестовый набор и валидация**
   - Реализовать тесты в `tests/` и раннер `test.cmd`.
   - Прогнать все тесты: 100% прохождение, 0 ошибок.
   - Выполнить аудит таблицы импортов через `dumpbin`.
7. **Этап 7: Формирование отчёта и Candidate Handoff**
   - Подготовить отчёт `docs/l4capture/handoffs/L4C-01-FRAMEWORK-report.md`.
   - Вычислить реальный SHA-256 хеш отчёта.
   - Подготовить кандидат `docs/l4capture/handoffs/L4C-01-FRAMEWORK-candidate.md` в формате `DETACHED_V1`.

---

## 11. Критерии приёмки (Definition of Done)

Шаг считается успешно выполненным только при одновременном соблюдении следующих условий:

1. **Компиляция и артефакты:**
   - Бинарники `bin\x86\l4capture.exe` и `bin\x64\l4capture.exe` успешно собираются через `build.cmd` компилятором MSVC с флагом `/MT`.
   - `bin\l4capture.exe` идентичен `bin\x86\l4capture.exe`.
   - Компиляция проходит чисто: 0 errors, 0 warnings при уровне `/W4`.
2. **Импорты и платформенная совместимость:**
   - `dumpbin /dependents` показывает только системные библиотеки (`KERNEL32.dll`, `USER32.dll`, `ADVAPI32.dll`, `GDI32.dll`, `WS2_32.dll`, `OLE32.dll`).
   - Отсутствуют любые зависимости от `MSVCR*.dll`, `VCRUNTIME*.dll`.
   - Заголовок PE x86 содержит версию подсистемы `6.01` (Windows 7).
3. **Функциональные и модульные тесты:**
   - Все тесты в `tests/` выполняются успешно через `test.cmd` (0 failures).
   - Тесты IPC подтверждают устойчивость к фрагментации, отсечение некорректных версий и защиту от переполнения длины.
   - Тесты дедлайна подтверждают независимость от сдвигов wall clock и корректность дедупликации.
   - Тесты лимитов подтверждают отказ при площади $> 8\,294\,400$ пикселей без вызова аллокаций.
4. **Ранний spike OpenH264:**
   - Подтверждена компилируемость заголовков `wels/codec_api.h` в чистом C.
   - Подтверждена возможность статической линковки без внешних DLL.
5. **Документация и изоляция:**
   - Файлы `l4desk-service` не затронуты.
   - Сформирован детальный отчёт и candidate в `docs/l4capture/handoffs/`.

---

## 12. Оформление отчёта и Candidate Handoff (`DETACHED_V1`)

По завершении всех этапов сформируй два обязательных файла по регламенту `PROMPT-STANDARD.md`:

### 1. Отчёт исполнителя: `docs/l4capture/handoffs/L4C-01-FRAMEWORK-report.md`
Должен содержать:
- Статус: строго `ACCEPTED`.
- Ветка и SHA коммита реализации (`producer_commit`).
- Полный перечень созданных файлов в `tools/l4capture/`.
- Вывод команд `build.cmd` и `test.cmd`.
- Вывод `dumpbin /dependents` и `/imports` для x86 и x64.
- Описание результатов OpenH264 spike.
- Подтверждение соблюдения лимитов и таймингов безопасности.

### 2. Файл кандидата: `docs/l4capture/handoffs/L4C-01-FRAMEWORK-candidate.md`
Вычисли SHA-256 хеш реальных байтов отчёта (PowerShell: `(Get-FileHash -Algorithm SHA256 docs\l4capture\handoffs\L4C-01-FRAMEWORK-report.md).Hash.ToLower()`).

Файл кандидата оформляется строго в формате:

```markdown
<!-- HANDOFF:H-L4C-01-v1:BEGIN -->
```yaml
handoff_id: H-L4C-01-v1
status: ACCEPTED
contract_kinds:
  - FRAMEWORK_FOUNDATION
  - C_INTERFACES
  - IPC_PROTOCOL
  - SAFETY_GATE
producer_prompt_id: L4C-01-FRAMEWORK
producer_scope_project: tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-01-FRAMEWORK-report.md
producer_branch: l4capture/l4c-01-framework
producer_commit: <COMMIT_SHA>
accepted_at_utc: <TIMESTAMP_UTC>
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-01-FRAMEWORK-report.md
  - tools/l4capture/include/l4capture/types.h
  - tools/l4capture/include/l4capture/capture_backend.h
  - tools/l4capture/include/l4capture/encoder_backend.h
  - tools/l4capture/include/l4capture/ipc_protocol.h
  - tools/l4capture/include/l4capture/safety_gate.h
  - tools/l4capture/bin/x86/l4capture.exe
  - tools/l4capture/bin/x64/l4capture.exe
artifact_sha256:
  - <SHA256_OF_REPORT_MD>
  - <SHA256_OF_TYPES_H>
  - <SHA256_OF_CAPTURE_BACKEND_H>
  - <SHA256_OF_ENCODER_BACKEND_H>
  - <SHA256_OF_IPC_PROTOCOL_H>
  - <SHA256_OF_SAFETY_GATE_H>
  - <SHA256_OF_BIN_X86>
  - <SHA256_OF_BIN_X64>
compatibility:
  backward_compatible_with: []
  breaking_changes: false
  notes: Стартовый каркас нативного модуля l4capture.exe. Базовые C-интерфейсы, бинарный IPC, deadline/safety-контроль, чистый C99/C11, /MT, поддержка Windows 7 SP1 (x86/x64).
deployment_status: LOCAL_BUILD_VERIFIED
deployed_environment: local_development
feature_flags:
  l4capture_native_pipeline: enabled
contract_payload:
  identifiers: lease_id (UUID), stream_id (UUID), request_seq (u64), geometry_generation (u64)
  c_interfaces:
    - ICaptureBackend (init, acquire_frame, release_frame, destroy)
    - IEncoderBackend (init, encode, force_idr, release_au, destroy)
    - FrameView (BGRA top-down, physical_rect, monotonic pts_ms)
    - AccessUnit (NAL descriptors, is_idr, pts_ms)
  ipc_protocol:
    wire_format: Little-Endian, 16-byte header (version:u16, type:u16, payload_len:u32, seq:u64), payload <= 4096 bytes
    commands: CMD_START (0x0001), CMD_RENEW_LEASE (0x0002), CMD_STOP (0x0003), CMD_FORCE_IDR (0x0004)
    events: EVENT_READY (0x0101), EVENT_METRICS (0x0102), EVENT_DEGRADED (0x0103), EVENT_ERROR (0x0104)
    transport: 2 anonymous inheritable pipes (--pipe-in, --pipe-out)
  safety_invariants:
    monotonic_clock: GetTickCount64, no grace period
    rtp_stop_timeout_ms: <= 500 ms upon stop/expiry/session_lock
    desktop_availability_poll_ms: <= 100 ms
    memory_admission_cap_mb: 128 MB (max 4K raster 8294400 px, max AU 2 MB)
    process_binding: JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE, Session 0 rejected
supersedes: []
known_risks:
  - R1: Потеря состояния при UAC/Lock — парируется жестким safety gate <= 500 мс
  - R3: Перегрузка/утечки — парируется жесткими лимитами растра (4K) и отсутствием неограниченных очередей
  - R4: Несовместимость рантайма Win7 — парируется статической компоновкой /MT и subsystem 6.01
consumers:
  - L4C-02-GDI-CAPTURE
  - L4C-03-OPENH264-CODEC
  - L4C-04-RTP-SENDER
  - L4C-05-AGENT-ADAPTER
  - ALL_FOLLOWING
next_prompt_id: L4C-02-GDI-CAPTURE
```
<!-- HANDOFF:H-L4C-01-v1:END -->
```

---

## 13. Завершение работы агента

После завершения всех шагов, прогона тестов и публикации отчёта/кандидата выведи итоговое резюме:
1. Подтверди успешную компиляцию x86 и x64 (`0 errors, 0 warnings`).
2. Приведи результаты тестов IPC, дедлайна и admission-контроля.
3. Приведи вычисленный SHA-256 хеш отчёта и путь к candidate-файлу.
4. Передай управление Агенту-Контроллеру каскада для верификации и внесения `H-L4C-01-v1` в `docs/l4capture/prompts/contract-handoff.md`.
