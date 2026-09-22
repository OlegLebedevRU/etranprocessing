# L4C-05-AGENT-ADAPTER — Адаптер бэкенда захвата в l4desk: Job Object, allowlisted pipes, deadline аренды (без grace), строгий Input Gate, Kiosk Focus Guard (Adaptive Fallback & Lifecycle Backend) и bounded recovery

```yaml
prompt_id: L4C-05-AGENT-ADAPTER
scope_project: tools/l4desk + tools/l4capture
scope_root: D:\repo\platerra\Public\etranprocessing
prompt_type: implementation-step
required_handoff_ids:
  - H-L4C-04-v1
sequence_gate_status: READY_FOR_L4C_05
output_handoff_id: H-L4C-05-v1
next_prompt_id: L4C-06-MILESTONE-LIVE-VERIFY
branch: l4capture/l4c-05-agent-adapter
report_path: docs/l4capture/handoffs/L4C-05-AGENT-ADAPTER-report.md
candidate_format: DETACHED_V1
candidate_path: docs/l4capture/handoffs/L4C-05-AGENT-ADAPTER-candidate.md
architecture_sections: [1, 2, 3, 7, 8, 9, 10, 11, 12, 13, 14]
consumers:
  - L4C-06-MILESTONE-LIVE-VERIFY
  - ALL_FOLLOWING
```

---

## 1. Цель и архитектурная миссия

Ты выступаешь в роли **Ведущего системного инженера интеграции супервизора и адаптера захвата (L4Capture-AgentAdapter-Agent)** в рамках комплекса `tools suite`.

Твоя задача — реализовать и интегрировать безопасный модуль адаптера медиабэкенда (`media_backend_adapter`) в агент терминала `tools/l4desk/`, связав существующий супервизор управления видеосессиями с нативным модулем видеозахвата `tools/l4capture/bin/<arch>/l4capture.exe` через анонимные каналы IPC, Windows Job Object и строгие контракты безопасности аренды и удалённого ввода.

На шаге **L4C-05-AGENT-ADAPTER** замыкается контур управления и контроля безопасности перед выходом на контрольную веху **M-1 (L4C-06-MILESTONE-LIVE-VERIFY)**.

Ключевые инженерные задачи адаптера согласно архитектурным требованиям `l4capture_arch_final.md` (§1, §2, §7, §8, §9, §12, §13, §14):
1. **Абстракция медиабэкенда (`media_backend_t`):**
   - Внедрение изолированного адаптера в `tools/l4desk/` без ломающего переименования внешних контрактов control plane.
   - Поддержка двух бэкендов: существующий `ffmpeg` (для камер и контролируемого отката) и новый нативный `l4capture`.
   - Feature flag конфигурации (`media_backend = "l4capture" | "ffmpeg"`), запрещающий автоматическое переключение на лету во время активной сессии.
2. **Изолированный запуск процесса и Job Object:**
   - Запуск дочернего `l4capture.exe` строго в интерактивной пользовательской сессии (отказ при Session 0).
   - Создание процесса в состоянии `CREATE_SUSPENDED`.
   - Привязка к Windows Job Object с флагом `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`. При падении или завершении родительского процесса ОС гарантированно уничтожает дочерний процесс захвата (исключение orphan-процессов).
   - Дескриптор Job Object **строго запрещено** наследовать дочернему процессу.
   - Наследование дескрипторов строго через `STARTUPINFOEX` и `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` (handle allowlist: исключительно дескрипторы входного и выходного анонимных пайпов).
   - Немедленное закрытие дочерних концов пайпов в родителе после вызова `CreateProcess`.
3. **Бинарный протокол IPC и framing:**
   - Формирование команд: `CMD_START` (0x0001), `CMD_RENEW_LEASE` (0x0002), `CMD_STOP` (0x0003), `CMD_FORCE_IDR` (0x0004).
   - Приём и разбор событий от `l4capture`: `EVENT_READY` (0x0101), `EVENT_METRICS` (0x0102), `EVENT_DEGRADED` (0x0103), `EVENT_ERROR` (0x0104).
   - Неблокирующий reader / асинхронный поток чтения событий из `pipe-out`: зависший поток чтения или переполнение пайпа не должны блокировать watchdog супервизора или препятствовать останову.
4. **Управление арендой (Lease Lifecycle) и строгий дедлайн БЕЗ grace-периода:**
   - Валидация внешних параметров аренды: `lease_id`, `stream_id`, `epoch`, `expires_at_ms`.
   - Преобразование TTL во внутренний абсолютный `GetTickCount64()` deadline системного аптайма.
   - **Инвариант исключения 5-секундного grace:** В отличие от существующего FFmpeg-супервизора (`ffmpeg_supervisor.c:893`), для `l4capture` **категорически запрещено** добавлять 5 секунд grace. При наступлении дедлайна или получении `CMD_STOP` трансляция RTP прекращается **строго $\le 500$ мс** (fail-closed).
   - Защита от устаревших продлений: отклонение renew с меньшим дедлайном, устаревшим `request_seq` или неверной эпохой (`wrong epoch`). Дедупликация идентичных запросов.
   - Запрет drain: при останове или экспирации запрещено досылать буферизованные кадры.
5. **Строгий шлюз ввода (Input Gate) на M-1:**
   - Агент обязан валидировать право на удалённый ввод независимо от UI.
   - Ввод разрешён **ТОЛЬКО** при одновременном выполнении 6 условий:
     1. Явный запрос пользователем профиля `low`;
     2. Фактический профиль видеопотока `base_480p` (подтверждённый через `EVENT_READY` / `EVENT_METRICS`);
     3. Источник является desktop (экран), а не DirectShow-камерой;
     4. Действующая активная аренда (`lease`);
     5. Совпадающая эпоха стрима (`stream_instance_id`);
     6. Подтверждённая и неизменная физическая геометрия дисплея (`geometry_generation`).
   - **Абсолютный запрет ввода при `default`:** Если запрошен профиль `default`, удалённый ввод **КАТЕГОРИЧЕСКИ ЗАПРЕЩЁН**, даже если видеопоток фактически деградировал до 480p! Автодеградация `default -> 480p` не даёт права ввода.
   - Гарантированный откат ввода (`input_release_all()`): при любом событии безопасности (lock экрана, UAC, disconnect, expiry аренды, смена геометрии, останов стрима) агент немедленно отпускает все зажатые клавиши/кнопки и блокирует обработку ввода.
6. **Ограниченный контур восстановления (Bounded Recovery Loop):**
   - Разрешён только при неожиданном выходе процесса (`unexpected exit`) или сбое устройства при живой аренде и доступном десктопе.
   - Скользящий бюджет: **не более 5 перезапусков за 10 минут (600 секунд)** с экспоненциальным backoff (1s, 2s, 4s, 8s, 16s + jitter).
   - Команда `CMD_STOP` или экспирация аренды во время backoff немедленно отменяет запланированный перезапуск.
   - Гарантия единственного процесса: новый процесс не запускается до подтверждённого завершения старого.
7. **Телеметрия и статус:**
   - Трансляция метрик из `EVENT_METRICS` в существующие статусы `l4desk` без расширения внешних сетевых схем.
8. **Управление фокусом киоск-приложения (Kiosk Focus Management) и адаптивный fallback (Adaptive Fallback Policy):**
   - Решение критической проблемы потери фокуса киоск-приложением (`platerraterminal.exe`) из-за системных уведомлений ОС, всплывающих окон, скрытых окон или фоновых процессов, делающей невозможным клавиатурный ввод и вызывающей сбои кликов мыши.
   - Опция киоск-режима активна по умолчанию (`--kiosk-process platerraterminal.exe` или значение в манифесте).
   - Поддержка трех эксплуатационных состояний агента ввода:
     - **Kiosk Mode Active** (процесс киоска запущен и обнаружен): строгий контроль и восстановление foreground-статуса окна киоска, упреждающий refocus перед кликами, защита от нецелевого ввода.
     - **Adaptive Generic Fallback** (киоск временно отключен, упал, не был запущен или оператор вышел из него): автоматический переход в прозрачный режим управления рабочим столом Windows без блокировок ввода, ложных NACK или паразитных нажатий клавиш. При повторном старте киоска агент автоматически возвращается в строгий Kiosk Guard.
     - **Pure Generic Desktop Mode** (имя процесса не задано / пусто): постоянное прозрачное удалённое управление рабочим столом без привязки к конкретному окну.
   - Механизм активного восстановления фокуса (Active Refocus Engine): связка `AttachThreadInput` + `BringWindowToTop` + `SetForegroundWindow` + обход блокировки ASFW (Alt-key bypass trick) + `SPI_SETFOREGROUNDLOCKTIMEOUT`.
   - Защита ввода (Fail-Safe Focus Guard): блокировка клавиатурного ввода с возвратом NACK `kiosk_focus_lost`, только если киоск запущен, но потерял фокус и refocus не удался.
   - Расширенная телеметрия состояния сессии: `kiosk_mode`, `kiosk_running`, `kiosk_in_focus`, `active_input_mode` (`"kiosk"` | `"generic_fallback"` | `"generic"`), `foreground_process`.
9. **Бэкенд управления жизненным циклом киоск-приложения на терминале (`kiosk_lifecycle` в `tools/`):**
   - Автономный программный бэкенд на стороне терминала для запуска, мягкого останова, перезапуска и мониторинга киоск-приложения через протокол управления `ctl_protocol`: команды `kiosk_start`, `kiosk_stop` (двухфазный graceful shutdown через `WM_CLOSE` + fallback на `TerminateProcess`), `kiosk_restart` и `kiosk_status` (`IsHungAppWindow`).
   - Запуск строго в интерактивной пользовательской сессии с наследованием окружения рабочего стола.
   - Периметр безопасности: управление строго процессом, совпадающим с доверенным `kiosk_process` (запрет запуска произвольных бинарников).
   - **Границы реализации:** На текущем этапе модуль реализуется **исключительно в `tools/`** (`tools/l4desk/`). В `MenuBuilder` (веб-интерфейс оператора) поддержка кнопок управления киоском будет добавлена на последующем этапе на основе зафиксированного контракта протокола.

---

## 2. Непереговорные рамочные принципы и изоляция

1. **Разрешённый scope реализации:**
   - Изменения вносятся строго в `tools/l4desk/` и `tools/l4capture/`.
   - Документация, промпты и журналы размещаются в `docs/l4capture/`.
   - Отчёты и candidate-файлы формируются в `docs/l4capture/handoffs/`.
   - **КАТЕГОРИЧЕСКИЙ ЗАПРЕТ `l4desk-service`:** Строжайше запрещено создавать, читать или изменять файлы в каталоге `l4desk-service`. Серверный проект `l4desk-service` полностью изолирован от нативного каскада `l4capture`.
   - **ЗАПРЕТ модификации других подсистем:** Запрещено изменять файлы в `BACK/`, `FRONT/`, `ProcessingBackend/`, `MenuBuilder/`, `shared/`, `tools/leo4proxy/`.
2. **Стандарты компиляции и переносимости:**
   - Чистый C (C99/C11 подмножество MSVC, компиляция через `cl.exe`, не C++).
   - Статическая компоновка Runtime: `/MT` в Release-конфигурации (никаких `msvcrt*.dll`, `vcruntime*.dll`).
   - Поддержка Windows 7 SP1 x86/x64: компиляция с `/D_WIN32_WINNT=0x0601`, подсистема консоли `6.01` для x86.
   - Таблица импортов (`dumpbin /dependents`): только базовые системные библиотеки Windows (`KERNEL32.dll`, `USER32.dll`, `ADVAPI32.dll`, `WS2_32.dll`).
   - Нулевая толерантность к предупреждениям компилятора: уровень `/W4` с zero warnings.
3. **Безопасность и принцип Fail-Closed:**
   - Потеря авторизации, дедлайн аренды, блокировка экрана или падение родителя переводят систему в безопасное состояние (поток остановлен, ввод заблокирован и отпущен).
   - Никаких фоновых orphan-процессов: контроль через Job Object на уровне ядра ОС.

---

## 3. Pre-Flight Check & Contract Gate (Шаг 1)

Перед началом внесения изменений агент обязан выполнить валидацию входных контрактов:

1. Открой файл журнала `docs/l4capture/prompts/contract-handoff.md`.
2. Убедись, что блок `H-L4C-04-v1` присутствует в секции `## 5. Принятые handoff-блоки` и имеет статус `ACCEPTED`.
3. Сверь контрольные суммы входных артефактов из блока `H-L4C-04-v1`:
   - `docs/l4capture/handoffs/L4C-04-RTP-SENDER-report.md`: `ed6ced854282cc5f6aaaee7b657f91abf6b1958d2e0bb8979dff3f1b4c1f47f0`
   - `tools/l4capture/include/l4capture/rtp_sender.h`: `757339ad278499e06de168c6a9c9ed1f0de0cf16147d945271038c768dcd5922`
   - `tools/l4capture/include/l4capture/rtp_packetizer.h`: `a9ef3deb4007c32e558901a162e7e3101d192bc5988043a68b80e0fc11d727fa`
   - `tools/l4capture/bin/x86/l4capture.exe`: `d6bbfc7e9bb5c53a768bd487e9324992839661416e7c642d94bd1ec233899138`
   - `tools/l4capture/bin/x64/l4capture.exe`: `19e4a50743f84048b5d62bd1ebb9582ddfa8bde6fab24f73e720d0b557ba7ada`
4. Проверь рабочую ветку Git: `l4capture/l4c-05-agent-adapter`.
5. При обнаружении несоответствий или повреждений заверши работу со статусом `BLOCKED_CONTRACT`.

---

## 4. Архитектурные требования и техническая спецификация

### 4.1. Архитектура адаптера бэкендов в `tools/l4desk`

Создай и интегрируй абстракцию медиабэкенда `media_backend_t` в `tools/l4desk`:

```c
#ifndef L4D_MEDIA_BACKEND_H
#define L4D_MEDIA_BACKEND_H

#include <stdint.h>
#include <stdbool.h>
#include <windows.h>

typedef enum {
    L4D_BACKEND_FFMPEG   = 0,
    L4D_BACKEND_L4CAPTURE = 1
} l4d_backend_type_t;

typedef enum {
    L4D_STREAM_STOPPED   = 0,
    L4D_STREAM_STARTING  = 1,
    L4D_STREAM_RUNNING   = 2,
    L4D_STREAM_RESTARTING= 3,
    L4D_STREAM_FAILED    = 4
} l4d_stream_state_t;

typedef struct l4d_stream_params {
    const char *lease_id;
    const char *stream_id;
    const char *source_id;       /* "disp:0", "disp:1" */
    const char *profile;         /* "low" (480p) или "default" (720p) */
    RECT source_rect;            /* Физические координаты экрана */
    uint64_t geometry_gen;       /* Поколение топологии дисплея */
    uint16_t rtp_port;           /* 5004 */
    uint16_t rtcp_port;          /* 5005 */
    uint64_t deadline_tick_ms;   /* Абсолютный GetTickCount64() */
} l4d_stream_params_t;

typedef struct l4d_backend_metrics {
    uint16_t fps;
    uint32_t bitrate_kbps;
    uint32_t raw_drops;
    uint32_t encoder_drops;
    uint32_t transport_drops;
    uint16_t encode_p95_ms;
    uint16_t queue_depth;
    uint32_t private_bytes_kb;
    uint32_t gdi_handles;
    uint16_t actual_width;
    uint16_t actual_height;
    uint16_t degradation_state;
} l4d_backend_metrics_t;

struct l4d_media_backend;

typedef struct l4d_media_backend_vtable {
    bool (*start)(struct l4d_media_backend *self, const l4d_stream_params_t *params);
    bool (*stop)(struct l4d_media_backend *self, const char *stream_id);
    bool (*renew_lease)(struct l4d_media_backend *self, const char *lease_id, uint64_t new_deadline_tick_ms);
    bool (*force_idr)(struct l4d_media_backend *self, const char *stream_id);
    void (*poll)(struct l4d_media_backend *self);
    bool (*get_metrics)(struct l4d_media_backend *self, l4d_backend_metrics_t *out_metrics);
    bool (*is_running)(struct l4d_media_backend *self);
    void (*destroy)(struct l4d_media_backend *self);
} l4d_media_backend_vtable_t;

typedef struct l4d_media_backend {
    const l4d_media_backend_vtable_t *vtable;
    l4d_backend_type_t type;
    void *impl_ctx;
} l4d_media_backend_t;

/* Фабрика создания бэкенда по типу */
l4d_media_backend_t *l4d_media_backend_create(l4d_backend_type_t type, const char *bin_dir);

#endif /* L4D_MEDIA_BACKEND_H */
```

### 4.2. Изолированный запуск процесса, Job Object и Handle Allowlist

Реализация бэкенда `l4capture` обязана использовать строгие механизмы изоляции Windows Win32 API:

1. **Проверка пользовательской интерактивной сессии:**
   - Перед созданием процесса получить ID сессии супервизора:
     ```c
     DWORD session_id = 0;
     ProcessIdToSessionId(GetCurrentProcessId(), &session_id);
     if (session_id == 0) {
         /* Отказ: Session 0 (службы Windows) не имеет интерактивного рабочего стола */
         return false;
     }
     ```
2. **Создание анонимных каналов (Anonymous Pipes):**
   - Создаются 2 пары дескрипторов пайпов: `hChildStdInRead / hChildStdInWrite` и `hChildStdOutRead / hChildStdOutWrite`.
   - Флаг наследования выставляется **только** для концов дочернего процесса:
     ```c
     SetHandleInformation(hChildStdInRead, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
     SetHandleInformation(hChildStdOutWrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
     /* Концы родителя НЕ наследуются! */
     SetHandleInformation(hChildStdInWrite, HANDLE_FLAG_INHERIT, 0);
     SetHandleInformation(hChildStdOutRead, HANDLE_FLAG_INHERIT, 0);
     ```
3. **Формирование Handle Allowlist через `STARTUPINFOEX`:**
   - Использование `InitializeProcThreadAttributeList` и `UpdateProcThreadAttribute` с `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`.
   - В массив разрешённых к наследованию дескрипторов включаются **исключительно** `hChildStdInRead` и `hChildStdOutWrite`:
     ```c
     HANDLE inherit_handles[2] = { hChildStdInRead, hChildStdOutWrite };
     UpdateProcThreadAttribute(attr_list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                               inherit_handles, sizeof(inherit_handles), NULL, NULL);
     ```
4. **Создание и настройка Windows Job Object:**
   - Создание объекта: `hJob = CreateJobObject(NULL, NULL)`.
   - Конфигурация расширенной информации о лимитах:
     ```c
     JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {0};
     jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
     SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
     ```
   - **Инвариант:** Дескриптор `hJob` никогда не передаётся дочернему процессу и не включается в allowlist!
5. **Создание процесса в suspended-состоянии и привязка к Job:**
   - Формирование командной строки:
     `l4capture.exe --pipe-in=<DEC_HANDLE_IN> --pipe-out=<DEC_HANDLE_OUT>`
   - Вызов `CreateProcess`:
     ```c
     PROCESS_INFORMATION pi = {0};
     BOOL ok = CreateProcess(exe_path, cmdline, NULL, NULL, TRUE,
                             CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW,
                             NULL, NULL, &si_ex.StartupInfo, &pi);
     ```
   - Привязка к Job: `AssignProcessToJobObject(hJob, pi.hProcess)`. Если привязка не удалась — немедленное уничтожение процесса `TerminateProcess(pi.hProcess, 1)` и отказ запуска.
   - **Немедленное закрытие дочерних концов в родителе:**
     ```c
     CloseHandle(hChildStdInRead);
     CloseHandle(hChildStdOutWrite);
     ```
   - Возобновление выполнения процесса: `ResumeThread(pi.hThread); CloseHandle(pi.hThread);`.

### 4.3. Бинарный протокол IPC и сериализация фреймов

Взаимодействие осуществляется строго по спецификации IPC v1 (`include/l4capture/ipc_protocol.h`):

1. **Заголовок сообщения (16 байт Little-Endian):**
   - `version: uint16_t` = `0x0001`
   - `type: uint16_t` (код команды/события)
   - `payload_length: uint32_t` ($\le 4096$)
   - `request_seq: uint64_t` (монотонный счетчик сообщений супервизора)
2. **Команды супервизора (`tools/l4desk` $\to$ `l4capture`):**
   - `CMD_START` (`0x0001`):
     - Payload: `lease_id:uuid(16)`, `stream_id:uuid(16)`, `source_rect:rect(16)`, `geometry_gen:u64`, `profile_id:u16` (`1=480p, 2=540p, 3=720p`), `rtp_port:u16`, `rtcp_port:u16`, `deadline_tick_ms:u64`.
   - `CMD_RENEW_LEASE` (`0x0002`):
     - Payload: `lease_id:uuid(16)`, `new_deadline_tick_ms:u64`.
   - `CMD_STOP` (`0x0003`):
     - Payload: `stream_id:uuid(16)`.
   - `CMD_FORCE_IDR` (`0x0004`):
     - Payload: `stream_id:uuid(16)`.
3. **События от захвата (`l4capture` $\to$ `tools/l4desk`):**
   - `EVENT_READY` (`0x0101`):
     - Payload: `stream_id:uuid(16)`, `actual_width:u32`, `actual_height:u32`, `actual_fps:u16`, `capture_backend:u16`, `encoder_backend:u16`.
     - Фиксирует переход стрима в состояние `RUNNING`.
   - `EVENT_METRICS` (`0x0102`):
     - Секундный агрегат метрик: `fps:u16`, `bitrate_kbps:u32`, `raw_drops:u32`, `encoder_drops:u32`, `transport_drops:u32`, `encode_p95_ms:u16`, `queue_depth:u16`, `private_bytes_kb:u32`, `gdi_handles:u32`.
   - `EVENT_DEGRADED` (`0x0103`):
     - Уведомление о снижении качества: `degrade_state:u16`, `reason:u16`.
   - `EVENT_ERROR` (`0x0104`):
     - `error_code:u32`, `message_len:u16`, `message_utf8:char[]`.
4. **Неблокирующий Reader событий:**
   - Чтение из `hChildStdOutRead` организуется асинхронно или через `PeekNamedPipe` с проверкой доступности байт перед `ReadFile`.
   - Поток чтения не должен блокировать главный цикл супервизора.
   - При обнаружении закрытия канала (`ERROR_BROKEN_PIPE`) фиксируется завершение процесса.

### 4.4. Точный Deadline аренды без grace-периода и валидация продлений

1. **Расчёт дедлайна:**
   - При приёме внешней аренды супервизор вычисляет:
     $$\text{deadline\_tick\_ms} = \text{GetTickCount64}() + \text{remaining\_ttl\_ms}$$
   - **Категорический запрет grace для `l4capture`:** Существующая логика `expires_at_ms + 5000` в `ffmpeg_supervisor.c:893` **не применяется** к `l4capture`. Для нативного бэкенда дедлайн передаётся как есть, без добавления 5000 мс!
2. **Обработка `lease_renew`:**
   - Продление действительно только при соблюдении условий:
     1. `lease_id` совпадает с текущей активной арендой;
     2. `new_deadline_tick_ms > current_deadline_tick_ms`;
     3. `request_seq > last_renew_seq`.
   - Уменьшающие срок запросы или запросы с чужой эпохой отбрасываются без изменения состояния.
   - Дублирующие запросы дедуплицируются.
3. **Fail-Closed останов при экспирации:**
   - Если `GetTickCount64() >= deadline_tick_ms`:
     - Супервизор немедленно отправляет `CMD_STOP`.
     - Отправка медиапотока дочерним процессом прекращается $\le 500$ мс.
     - Очереди сбрасываются без drain.
     - Немедленно вызывается `input_release_all()`.

### 4.5. Шлюз ввода (Input Gate) — Нормативный контракт M-1

Реализуй функцию строгой валидации ввода `bool l4d_input_gate_check(...)`:

```c
bool l4d_input_gate_check(const l4d_input_gate_ctx_t *ctx) {
    /* 1. Источник обязан быть рабочим столом */
    if (!ctx->is_desktop_source) return false;

    /* 2. Аренда обязана быть активной и не просроченной */
    if (!ctx->has_active_lease || GetTickCount64() >= ctx->deadline_tick_ms) return false;

    /* 3. Эпоха стрима обязана точно совпадать */
    if (ctx->stream_instance_id == 0 || ctx->stream_instance_id != ctx->expected_stream_id) return false;

    /* 4. Геометрия дисплея подтверждена и не менялась */
    if (ctx->geometry_generation != ctx->expected_geometry_gen) return false;

    /* 5. Пользователь ЯВНО запросил профиль "low" */
    if (strcmp(ctx->requested_profile, "low") != 0) {
        /* Ввод для "default" КАТЕГОРИЧЕСКИ ЗАПРЕЩЁН! */
        return false;
    }

    /* 6. Фактический видеопрофиль строго "base_480p" (854x480) */
    if (ctx->actual_width != 854 || ctx->actual_height != 480) {
        return false;
    }

    /* 7. В режиме Kiosk Mode при запущенном приложении киоска: проверка удержания фокуса.
     * Если киоск не запущен (адаптивный fallback при выходе или отключении),
     * проверка фокуса не блокирует ввод, разрешая работу с десктопом! */
    if (ctx->kiosk_mode_enabled && ctx->kiosk_running && !ctx->kiosk_in_focus) {
        return false;
    }

    return true;
}
```

- **Правило профиля `default`:** Даже если запрос `default` (720p) из-за перегрузки терминала автоматически деградировал до 480p (`video_degradation_state = DEGRADED_480P`), `requested_profile` остаётся `"default"`, поэтому **ввод остаётся заблокированным**. Автодеградация разрешения не включает ввод!
- **Правило сброса ввода:** При переходе любого из условий в `false` (включая lock экрана, UAC, timeout аренды или stop) супервизор обязан вызвать `input_release_all()`, посылающий `keyUp` для всех удерживаемых виртуальных клавиш и отпускающий кнопки мыши.
- **Интеграция с Kiosk Focus Guard и Adaptive Fallback:**
  - Если режим Kiosk Mode включен (`kiosk_mode_enabled == true`), но процесс киоска **не запущен** (`kiosk_running == false`, например при выходе из киоска, падении или до его старта), агент переходит в режим `generic_fallback`: 7-е условие **не блокирует ввод**, позволяя оператору свободно работать с рабочим столом Windows через стандартный `SendInput`.
  - Если процесс киоска **запущен** (`kiosk_running == true`), но окно потеряло фокус (`kiosk_in_focus == false`) и процедура `kiosk_focus_force()` не смогла его восстановить, ввод блокируется fail-closed с возвратом NACK `kiosk_focus_lost`.
  - В режиме Generic Desktop Mode (`kiosk_mode_enabled == false`) проверка фокуса киоска отключена постоянно.

### 4.6. Bounded Recovery Loop супервизора

Супервизор реализует строгий алгоритм восстановления при авариях:

1. **Критерии разрешения рестарта:**
   - Неожиданное завершение процесса (`unexpected exit` с ненулевым кодом или падение).
   - Аренда активна (`GetTickCount64() < deadline_tick_ms`).
   - Интерактивный рабочий стол доступен.
2. **Лимиты восстановления:**
   - Максимум **5 попыток перезапуска за 10 минут (600 секунд)**.
   - Скользящее окно: сохранение таймстемпов последних 5 запусков.
   - Экспоненциальный backoff с джиттером:
     $$\text{delay}_n = \min(16000, 1000 \times 2^{n-1}) + \text{jitter}(0..500\text{ мс})$$
3. **Отмена перезапуска:**
   - Если во время ожидания backoff получена команда остановки (`stream_stop`) или истёк дедлайн аренды — таймер перезапуска немедленно отменяется, состояние переходит в `STOPPED` или `FAILED` с `reason = "lease_expired"` / `reason = "user_stopped"`.
4. **Гарантия одного процесса:**
   - Перед запуском нового процесса дождаться завершения старого (`WaitForSingleObject(pi.hProcess, 2000)`). При превышении таймаута — принудительный `TerminateProcess` и закрытие дескрипторов Job.
   - Наличие двух параллельных процессов захвата строго запрещено.

### 4.7. Конфигурация и Feature Flag

В конфигурационный файл или аргументы запуска `tools/l4desk` добавляется параметр:
```ini
[media]
backend = l4capture   ; Допустимые значения: "l4capture" (по умолчанию для M-1) | "ffmpeg"
```
- При `backend = l4capture` для захвата экрана вызывается нативный `l4capture.exe`.
- Для источников DirectShow (веб-камеры) по-прежнему вызывается `ffmpeg.exe` (камеры остаются на проверенном пути).
- Автоматический fallback с `l4capture` на `ffmpeg` на лету в рамках одной сессии **запрещён**. Переключение возможно только при новом старте сессии администратором.

### 4.8. Контроль фокуса киоск-приложения (Kiosk Focus Management) и два режима работы

#### 4.8.1. Архитектурная развилка конфигурации: CLI-параметры vs Манифест
Для указания имени целевого киоск-приложения реализуется **двухуровневая иерархическая конфигурация (Hybrid Hierarchy)**:
1. **Приоритет 1 (Командная строка CLI):**
   - Аргумент запуска `--kiosk-process <image_name>` (например, `--kiosk-process platerraterminal.exe`).
   - Данный параметр уже встроен в парсер `config.c` агента `l4desk` и естественным образом передаётся супервизором `l4superv` из шаблона аргументов `services.l4desk.args` файла `config.json`.
   - Позволяет напрямую переопределять процесс в сервисной конфигурации, в bat-файлах и при отладочном запуске из консоли (`--console`).
2. **Приоритет 2 (Конфигурационный файл / Манифест):**
   - Если в командной строке аргумент `--kiosk-process` отсутствует, `l4desk` обращается к локальному файлу конфигурации/манифеста установки (например, `%L4_TOOLS_BASE_PATH%\l4desk\l4desk.json` или секции `"kiosk"` манифеста терминала) и считывает строковое поле `"kiosk_process"`.
   - Это обеспечивает удобство централизованной раскатки типовых образов терминалов через инсталляционные пакеты без необходимости ручной модификации аргументов запуска.
3. **Поведение по умолчанию (Без киоск-приложения):**
   - Если имя процесса не задано ни в аргументах CLI, ни в манифесте (`cfg->kiosk_process[0] == L'\0'`), агент инициализируется в режиме **Generic Desktop Mode**.

#### 4.8.2. Сравнительный анализ трех состояний работы агента ввода

| Параметр / Аспект | Состояние 1: Kiosk Mode Active (`kiosk_process` запущен) | Состояние 2: Adaptive Generic Fallback (`kiosk_process` отключен/вышел) | Состояние 3: Pure Generic Desktop (`kiosk_process` не задан) |
|---|---|---|---|
| **Условие включения** | Задан `kiosk_process`, процесс и окно присутствуют в ОС | Задан `kiosk_process`, но процесс киоска **не запущен** | Параметр `kiosk_process` пуст (`L""`) |
| **Целевой сценарий** | Штатная работа терминала с активным UI киоска (`platerraterminal.exe`) | Сервисное обслуживание, регламентные работы, выход из киоска, аварийный сбой | Серверное/ПК администрирование без киоск-оболочки |
| **Привязка ввода** | Строго к окну киоск-приложения | Свободный ввод в текущее foreground-окно или окно под курсором | Свободный ввод в текущее окно рабочего стола |
| **Поведение при потере фокуса** | Автоматический refocus; при неудаче — отсечка ввода с NACK `kiosk_focus_lost` | Отсутствие принуждения фокуса; свободное управление рабочим столом | Отсутствие принуждения фокуса; свободное управление |
| **Инъекция клавиш (`input_inject_key`)** | Только в подтверждённое foreground-окно киоска | В текущее окно с фокусом через стандартный `SendInput` | В текущее окно с фокусом через стандартный `SendInput` |
| **Инъекция кликов мыши (`input_inject_click`)** | Упреждающий refocus для исключения потери клика на `WM_MOUSEACTIVATE` | Стандартный `SendInput` с координатами `MOUSEEVENTF_ABSOLUTE \| MOUSEEVENTF_VIRTUALDESK` | Стандартный `SendInput` с экранными координатами |
| **Защита комбинации Alt+F4** | Защита от закрытия окон киоска и Explorer/Tray | Защита системных окон (Progman/Tray); окна приложений закрываются штатно | Защита системных окон (Progman/Tray); окна закрываются штатно |
| **Телеметрия (`active_input_mode`)** | `"kiosk"` (`kiosk_running: true, kiosk_in_focus: true`) | `"generic_fallback"` (`kiosk_running: false, kiosk_in_focus: false`) | `"generic"` (`kiosk_running: false, kiosk_in_focus: true`) |

#### 4.8.3. Идентификация и кеширование окна киоска (HWND Discovery & Caching)
Для надёжной и производительной работы создаётся модуль `kiosk_focus.h` / `kiosk_focus.c`:
- **Поиск окна киоска:**
  - При старте и по требованию агент выполняет перечисление окон текущего сеанса рабочего стола (`EnumWindows`).
  - Фильтрация: проверяются только видимые окна (`IsWindowVisible`), исключаются свернутые окна (`IsIconic`).
  - Для каждого окна определяется PID (`GetWindowThreadProcessId`).
  - Через `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` и `QueryFullProcessImageNameW` извлекается имя исполняемого файла и сравнивается (без учёта регистра) с `cfg->kiosk_process`.
- **Кеширование дескриптора `HWND`:**
  - Найденный дескриптор окна сохраняется в `s_cached_kiosk_hwnd`.
  - При каждом событии ввода проверяется быстрая валидность кеша: `IsWindow(s_cached_kiosk_hwnd)` и совпадение PID окна.
  - Тяжёлое перечисление окон через `EnumWindows` выполняется **только** при инициализации, после сброса/инвалидации кеша или при неудачной попытке refocus, но не чаще 1 раза в 500 мс (rate-limited).

#### 4.8.4. Механизм активного восстановления фокуса (Active Refocus Engine)
В операционных системах Windows (начиная с Windows 2000/XP/7) действует политика Foreground Activation Restrictions (ASFW / LockSetForegroundWindow), запрещающая фоновым процессам произвольно перехватывать фокус. Чтобы гарантированно вернуть фокус окну киоска при перехвате системными уведомлениями или фоновыми утилитами, модуль `kiosk_focus` применяет проверенный протокол активации:

```c
bool kiosk_focus_force(HWND hKiosk) {
    if (!hKiosk || !IsWindow(hKiosk)) return false;

    HWND hForeground = GetForegroundWindow();
    if (hForeground == hKiosk) return true;

    /* Восстановление окна, если оно было свернуто */
    if (IsIconic(hKiosk)) {
        ShowWindow(hKiosk, SW_RESTORE);
    }

    DWORD curThread = GetCurrentThreadId();
    DWORD fgThread = hForeground ? GetWindowThreadProcessId(hForeground, NULL) : 0;
    DWORD kioskThread = GetWindowThreadProcessId(hKiosk, NULL);

    /* 1. Присоединение очереди ввода текущего потока к foreground-потоку и потоку киоска */
    if (fgThread && fgThread != curThread) {
        AttachThreadInput(curThread, fgThread, TRUE);
    }
    if (kioskThread && kioskThread != curThread) {
        AttachThreadInput(curThread, kioskThread, TRUE);
    }

    /* 2. Обход системной блокировки ASFW через синтетическое нажатие и отпускание клавиши Alt */
    keybd_event(VK_MENU, 0, 0, 0);
    keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);

    /* 3. Принудительное перемещение окна на передний план и передача фокуса */
    BringWindowToTop(hKiosk);
    SetForegroundWindow(hKiosk);
    SetActiveWindow(hKiosk);
    SetFocus(hKiosk);

    /* 4. Отсоединение очередей потоков ввода */
    if (kioskThread && kioskThread != curThread) {
        AttachThreadInput(curThread, kioskThread, FALSE);
    }
    if (fgThread && fgThread != curThread) {
        AttachThreadInput(curThread, fgThread, FALSE);
    }

    /* 5. Контрольная верификация: стало ли окно киоска действительным foreground */
    return (GetForegroundWindow() == hKiosk);
}
```

Кроме того, при старте агента вызывается системная настройка тайм-аута блокировки фокуса:
```c
SystemParametersInfo(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, (LPVOID)0, SPIF_SENDWININICHANGE | SPIF_UPDATEINIFILE);
```

#### 4.8.5. Защита ввода (Fail-Safe Focus Guard) и работа с Adaptive Fallback
Интеграция в диспетчер ввода (`input_inject.c`):
1. **Клавиатурный ввод (`input_inject_key`, `input_inject_shortcut_*`):**
   - Если `kiosk_mode_enabled == true`:
     - Вызывается проверка `kiosk_is_process_running()`:
       - **Если киоск НЕ запущен (`kiosk_running == false`):**
         - Активен режим **Adaptive Generic Fallback** (киоск закрыт, упал или временно отключен).
         - Клавиатурный ввод отправляется напрямую в текущее окно с клавиатурным фокусом через `SendInput`.
         - Никаких блокировок, NACK `kiosk_focus_lost` не генерируется, синтетический Alt не нажимается.
       - **Если киоск запущен (`kiosk_running == true`):**
         - Активен режим **Kiosk Mode Active**.
         - Перед вызовом `SendInput` выполняется проверка: `kiosk_focus_ensure_foreground()`.
         - Если окно киоска не является foreground, запускается `kiosk_focus_force()`.
         - Если после попытки окно киоска **не смогло получить фокус** (например, открыто системное диалоговое окно UAC, защищённый экран Winlogon или процесс киоска не отвечает):
           - Ввод **КАТЕГОРИЧЕСКИ БЛОКИРУЕТСЯ**;
           - `SendInput` не вызывается (исключая ввод в чужие/системные окна);
           - Возвращается ошибка `ERROR_INVALID_TARGET_HANDLE`;
           - В протокол `ctl_protocol.c` выдаётся NACK: `code = "kiosk_focus_lost"`, `message = "Kiosk window is not foreground; remote input blocked"`.
   - Если `kiosk_mode_enabled == false`: ввод отправляется напрямую через `SendInput` (Generic Desktop Mode).
2. **Клики мыши (`input_inject_click`):**
   - В режиме Kiosk Mode при `kiosk_running == true`: перед кликом выполняется упреждающий вызов `kiosk_focus_ensure_foreground()`. Это гарантирует, что удалённый клик сразу попадает в активное окно киоска и обрабатывается целевой кнопкой с первого раза.
   - В режиме Adaptive Fallback (`kiosk_running == false`) и Generic Desktop Mode: клики транслируются напрямую в `SendInput` с координатами `MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK` без навязывания фокуса и без вызовов ASFW Alt-key bypass.

#### 4.8.6. Телеметрия фокуса и протокол управления (Visibility & Control)
1. **Расширение телеметрии присутствия и сессии (Presence & Stream Status):**
   В исходящие статусные сообщения агента включаются поля:
   - `kiosk_mode` (`bool`): активен ли режим киоска по конфигурации;
   - `kiosk_process` (`string`): имя контролируемого процесса (`"platerraterminal.exe"` или `""`);
   - `kiosk_running` (`bool`): запущен ли процесс киоска в текущий момент;
   - `kiosk_in_focus` (`bool`): находится ли окно киоска в текущий момент в фокусе (при `kiosk_running == false` всегда `false`);
   - `active_input_mode` (`string`): текущий фактический режим ввода — `"kiosk"` (активен строгий контроль киоска), `"generic_fallback"` (киоск сконфигурирован, но не запущен; ввод свободен), `"generic"` (режим киоска не сконфигурирован);
   - `foreground_process` (`string`): имя процесса окна, владеющего фокусом в данный момент (например, `"platerraterminal.exe"`, `"explorer.exe"`, `"unknown"`).
2. **Команда принудительного возврата фокуса (`refocus_kiosk`):**
   В `ctl_protocol.c` реализуется обработка команды `refocus_kiosk`:
   - Вызывается оператором из веб-интерфейса MenuBuilder UI или автоматически фронтендом при получении NACK `kiosk_focus_lost`.
   - Если `kiosk_running == false`, возвращает NACK `kiosk_not_running`.
   - Если `kiosk_running == true`, выполняет `kiosk_focus_force()`.
   - Возвращает ACK при успешной активации киоска или NACK `refocus_failed` при невозможности перехватить фокус.
3. **Обратная связь для интерфейса оператора (MenuBuilder UI):**
   - При `kiosk_running == true && kiosk_in_focus == false`: статусное предупреждение *"Окно киоска потеряло фокус (активно: explorer.exe)"* с кнопкой *"Активировать киоск"*.
   - При `kiosk_running == false`: информационный статус *"Киоск-приложение отключено. Активен режим свободного управления рабочим столом (Generic Desktop)"*.
   - Любая попытка ввода при потере фокуса активным киоском возвращает понятный NACK `kiosk_focus_lost`.

#### 4.8.7. Адаптивный фолбэк (Adaptive Fallback Policy): работа с десктопом при отсутствии или выходе из киоска

Для исключения блокировок (deadlock) при регламентных работах инженера реализуется нормативная политика **Adaptive Fallback**:

1. **Детекция состояния процесса (Process State Polling):**
   - Модуль `kiosk_focus` разделяет две проверки:
     - `kiosk_is_process_running()`: быстрая проверка наличия активного процесса `kiosk_process` в таблице процессов текущей интерактивной сессии (снапшот процессов через `CreateToolhelp32Snapshot` или проверка кешированного дескриптора процесса `hProcess` не чаще 1 раза в 1000 мс);
     - `kiosk_is_window_focused()`: проверка соответствия `GetForegroundWindow() == s_cached_kiosk_hwnd`.
2. **Бесшовный автоматический переход (Seamless State Transition):**
   - **Сценарий «Выход из киоска»:** Инженер или оператор закрывает киоск (через локальное меню выхода, диспетчер задач или удалённую команду `kiosk_stop`).
     - При выходе из процесса `s_cached_kiosk_hwnd` инвалидируется, флаг `kiosk_running` переходит в `false`.
     - Агент **мгновенно** переключается в режим `Adaptive Generic Fallback`.
     - Шлюз ввода не блокирует события, оператор беспрепятственно работает с рабочим столом Windows (Проводник, консоль cmd/powershell, конфигурационные утилиты).
   - **Сценарий «Запуск киоска»:** Инженер запускает приложение киоска (вручную кликом по ярлыку или удалённо через команду `kiosk_start`).
     - При появлении окна киоска модуль фиксирует `kiosk_running = true` и кеширует новый `HWND`.
     - Агент **автоматически** возвращается в строгий Kiosk Guard: активирует окно на передний план и восстанавливает защиту ввода от системных окон.
3. **Исключение паразитных эффектов при отсутствии киоска:**
   - Если `kiosk_running == false`, модуль `kiosk_focus` **категорически не выполняет** синтетическое нажатие Alt (`ASFW bypass`) и не вызывает сканирование всех окон `EnumWindows` при каждом клике/нажатии.

#### 4.8.8. Бэкенд управления жизненным циклом киоск-приложения (`kiosk_lifecycle`) на терминале (`tools/` only)

Для обеспечения удалённого перезапуска и выключения/включения киоска в `tools/l4desk` реализуется автономный модуль `kiosk_lifecycle` (`include/kiosk_lifecycle.h`, `src/kiosk_lifecycle.c`).

##### C API интерфейс (`include/kiosk_lifecycle.h`):
```c
#ifndef L4D_KIOSK_LIFECYCLE_H
#define L4D_KIOSK_LIFECYCLE_H

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

typedef enum {
    L4D_KIOSK_STATUS_STOPPED      = 0,
    L4D_KIOSK_STATUS_RUNNING      = 1,
    L4D_KIOSK_STATUS_STARTING     = 2,
    L4D_KIOSK_STATUS_STOPPING     = 3,
    L4D_KIOSK_STATUS_UNRESPONSIVE = 4   /* Зависание (IsHungAppWindow) */
} l4d_kiosk_status_t;

typedef struct {
    l4d_kiosk_status_t status;
    DWORD pid;
    HWND hwnd;
    uint64_t uptime_sec;
    bool is_hung;
    wchar_t process_name[MAX_PATH];
} l4d_kiosk_info_t;

/* Инициализация модуля с привязкой к имени процесса и пути запуска */
bool kiosk_lifecycle_init(const wchar_t *process_name, const wchar_t *launch_path, const wchar_t *cmd_args);

/* Запуск приложения киоска в интерактивной пользовательской сессии */
bool kiosk_lifecycle_start(DWORD *out_pid, DWORD *out_error);

/* Двухфазный останов киоска (WM_CLOSE с таймаутом, fallback на TerminateProcess) */
bool kiosk_lifecycle_stop(uint32_t timeout_ms, bool force_terminate, DWORD *out_error);

/* Последовательный перезапуск (stop + start) */
bool kiosk_lifecycle_restart(uint32_t stop_timeout_ms, DWORD *out_pid, DWORD *out_error);

/* Запрос детального статуса процесса киоска */
bool kiosk_lifecycle_get_status(l4d_kiosk_info_t *out_info);

#endif /* L4D_KIOSK_LIFECYCLE_H */
```

##### Алгоритм двухфазного останова (Graceful Shutdown):
1. **Фаза 1 (Мягкое закрытие):**
   - Находится главное окно киоска `HWND`.
   - Окну отправляется `PostMessage(hwnd, WM_CLOSE, 0, 0)`.
   - Запускается ожидание завершения процесса с таймаутом $\le 3000$ мс (`WaitForSingleObject(hProcess, timeout_ms)`).
2. **Фаза 2 (Принудительное завершение при зависании):**
   - Если процесс не завершился по истечении таймаута или окно признано зависшим (`IsHungAppWindow(hwnd) == TRUE`), вызывается `TerminateProcess(hProcess, 1)`.
   - Дескрипторы закрываются, кеш окна очищается, статус переходит в `L4D_KIOSK_STATUS_STOPPED`.
   - Ввод автоматически переключается в `generic_fallback`.

##### Алгоритм запуска (Interactive User Session Launch):
- Запуск выполняется строго в интерактивной пользовательской сессии с рабочего стола:
  - Формирование полного пути из конфигурации `kiosk_path` (или директории терминала).
  - Вызов `CreateProcessW` с наследованием текущего сессионного токена пользователя (`TokenLinkedToken` / активная пользовательская сессия, запрет запуска под Session 0).
  - Проверка валидности запуска: ожидание появления окна киоска в течение до 5000 мс (`WaitForInputIdle`).

##### Периметр безопасности (Security Restrictions):
- Модуль имеет строгий **allowlist**: запуск и останов разрешены **ТОЛЬКО** для исполняемого файла, точное имя которого задано в конфигурации `kiosk_process`.
- Попытки передать произвольный путь к системным утилитам (`cmd.exe`, `powershell.exe`) отклоняются на уровне API с ошибкой `ERROR_ACCESS_DENIED`.

##### Команды протокола управления `ctl_protocol.c` на терминале:
1. `{"cmd": "kiosk_start"}` $\implies$ ACK `{"status": "ok", "pid": 1234}` или NACK `{"status": "error", "code": "launch_failed"}`.
2. `{"cmd": "kiosk_stop", "timeout_ms": 3000, "force": true}` $\implies$ ACK `{"status": "ok"}` или NACK `{"status": "error", "code": "stop_failed"}`.
3. `{"cmd": "kiosk_restart"}` $\implies$ ACK `{"status": "ok", "pid": 5678}` или NACK `{"status": "error", "code": "restart_failed"}`.
4. `{"cmd": "kiosk_status"}` $\implies$ ACK `{"status": "ok", "kiosk": {"running": true, "pid": 1234, "is_hung": false, "in_focus": true, "mode": "kiosk"}}`.

##### Границы реализации и интеграция с MenuBuilder:
- **Сейчас реализуется ТОЛЬКО в `tools/`** (`tools/l4desk/`).
- В `MenuBuilder` (бэкенд и фронтенд) кнопки «Остановить киоск», «Запустить киоск», «Перезапустить киоск» и индикатор зависания будут добавлены на последующих этапах строго по спецификации зафиксированного здесь контракта `KIOSK_LIFECYCLE_BACKEND`.

---

## 5. Структура файлов и обновлений в `tools/`

```text
tools\l4desk\
├── include\
│   ├── media_backend.h          # [НОВЫЙ] Абстрактный интерфейс бэкенда видеозахвата
│   ├── l4capture_adapter.h      # [НОВЫЙ] Адаптер процесса l4capture: Job, pipes, IPC, deadline
│   ├── input_gate.h             # [НОВЫЙ] Нормативный шлюз разрешения ввода (6 условий + kiosk focus)
│   ├── kiosk_focus.h            # [НОВЫЙ] Контроль фокуса киоска: HWND discovery, refocus, guard, adaptive fallback
│   ├── kiosk_lifecycle.h        # [НОВЫЙ] Управление процессом киоска: start, stop, restart, status
│   └── ...
├── src\
│   ├── media_backend.c          # [НОВЫЙ] Фабрика и диспетчер бэкендов (l4capture vs ffmpeg)
│   ├── l4capture_adapter.c      # [НОВЫЙ] Реализация запуска в Job, allowlist, IPC, reader
│   ├── input_gate.c             # [НОВЫЙ] Проверка прав ввода, блокировка default, input_release
│   ├── kiosk_focus.c            # [НОВЫЙ] Реализация AttachThreadInput, SetForegroundWindow, ASFW bypass, fallback
│   ├── kiosk_lifecycle.c        # [НОВЫЙ] Graceful shutdown (WM_CLOSE), TerminateProcess, CreateProcessW, status
│   ├── input_inject.c           # [ОБНОВЛЕНИЕ] Интеграция focus guard перед SendInput для мыши и клавиш
│   ├── config.c                 # [ОБНОВЛЕНИЕ] Приоритет CLI --kiosk-process и fallback на манифест
│   ├── ctl_protocol.c           # [ОБНОВЛЕНИЕ] Команды kiosk_start/stop/restart/status, refocus_kiosk, NACK, телеметрия
│   ├── ffmpeg_supervisor.c      # [ОБНОВЛЕНИЕ] Интеграция с media_backend_t, исключение grace для l4capture
│   └── ...
├── tests\
│   ├── test_runner.c            # [ОБНОВЛЕНИЕ] Регистрация тестов адаптера, контроля фокуса и жизненного цикла
│   └── test_l4capture_adapter.c # [НОВЫЙ] 20 тестов: адаптер, Job, IPC, Input Gate, Recovery, Kiosk Focus & Lifecycle
tools\l4capture\
├── src\
│   └── main.c                   # [ОБНОВЛЕНИЕ/СВЕРКА] Полное соответствие аргументам --pipe-in, --pipe-out
```

---

## 6. Тестовая стратегия и автономная валидация (`tests/`)

В тестовом наборе `tools/l4desk/tests/test_l4capture_adapter.c` реализуй 20 обязательных тестов:

1. **`test_adapter_job_object_kill_on_close` (Уничтожение процесса при закрытии Job):**
   - Запуск дочернего `l4capture.exe` внутри Job Object.
   - Принудительное закрытие дескриптора Job родительским процессом (`CloseHandle(hJob)`).
   - Ожидание $\le 1000$ мс: дочерний процесс гарантированно уничтожен операционной системой (`WaitForSingleObject(hProcess, 1000) == WAIT_OBJECT_0`).
2. **`test_adapter_handle_allowlist_isolation` (Изоляция дескрипторов):**
   - Проверка, что через `STARTUPINFOEX` передаются исключительно дескрипторы `pipe-in` и `pipe-out`.
   - Дескриптор `hJob` не утекает дочернему процессу.
3. **`test_adapter_session0_rejection` (Запрет запуска в Session 0):**
   - Эмуляция контекста Session 0.
   - Проверка: адаптер возвращает ошибку до вызова `CreateProcess`.
4. **`test_adapter_single_process_invariant` (Инвариант одного процесса):**
   - Запуск активного стрима через адаптер.
   - Попытка второго вызова `start()` без остановки первого: адаптер возвращает ошибку, второй процесс не создаётся.
5. **`test_adapter_ipc_start_handshake` (Рукопожатие CMD_START $\to$ EVENT_READY):**
   - Отправка `CMD_START` с валидными параметрами в пайп дочернего процесса.
   - Получение и валидация события `EVENT_READY`: совпадение `stream_id`, фактический растр $854 \times 480$, статус переходит в `RUNNING`.
6. **`test_adapter_lease_renew_and_dedup` (Продление аренды и дедупликация):**
   - Отправка `CMD_RENEW_LEASE` с увеличивающимся дедлайном $\implies$ успешное обновление.
   - Отправка запроса с уменьшающимся дедлайном $\implies$ отбрасывание.
   - Отправка дубликата с тем же дедлайном $\implies$ дедупликация без повторной отправки.
7. **`test_adapter_wrong_epoch_rejection` (Отвержение неверной эпохи):**
   - Отправка команды управления с чужим `stream_id` или устаревшей эпохой $\implies$ отклонение запроса.
8. **`test_adapter_expiry_strict_500ms_no_grace` (Останов по дедлайну без grace):**
   - Установка короткого дедлайна ($+200$ мс).
   - Проверка: ровно по наступлении дедлайна супервизор инициирует останов, отправка прекращается $\le 500$ мс, пятисекундный grace отсутствует.
9. **`test_adapter_input_gate_low_480p_permitted` (Разрешение ввода для low 480p):**
   - Подача контекста: `profile="low"`, `actual=854x480`, активная аренда, desktop.
   - Проверка: `l4d_input_gate_check()` возвращает `true`.
10. **`test_adapter_input_gate_default_strictly_denied` (Запрет ввода для default):**
    - Подача контекста: `profile="default"`, `actual=854x480` (деградировал), активная аренда, desktop.
    - Проверка: `l4d_input_gate_check()` возвращает `false` (ввод для `default` строго запрещён независимо от фактического растра).
11. **`test_adapter_input_release_on_safety_events` (Сброс ввода при событиях безопасности):**
    - Эмуляция блокировки экрана / UAC / expiry аренды.
    - Проверка: вызывается `input_release_all()`, сбрасываются все зажатые клавиши.
12. **`test_adapter_recovery_loop_and_backoff_cancel` (Bounded Recovery и отмена backoff):**
    - Эмуляция аварийного падения процесса.
    - Проверка экспоненциального нарастания задержки перезапуска (backoff) и лимита $\le 5$ попыток за 10 минут.
    - Подача команды `stop` во время таймера backoff $\implies$ немедленная отмена таймера, переход в `STOPPED`, повторный запуск не производится.
13. **`test_kiosk_focus_detection_and_caching` (Обнаружение и кеширование окна киоска):**
    - Инициализация агента в режиме Kiosk Mode (`kiosk_process = L"test_kiosk.exe"`).
    - Проверка обнаружения целевого `HWND` через `EnumWindows`, валидации PID и кеширования дескриптора без лишних повторных сканирований.
14. **`test_kiosk_focus_recovery_attach_thread_input` (Восстановление фокуса через AttachThreadInput):**
    - Эмуляция ситуации, когда активным окном становится стороннее фоновое окно.
    - Вызов `kiosk_focus_force()`.
    - Проверка: потоки ввода временно объединяются через `AttachThreadInput`, выполняется синтетическое нажатие Alt, окно киоска успешно становится foreground (`GetForegroundWindow() == hKiosk`).
15. **`test_kiosk_keyboard_input_blocked_when_unfocused` (Блокировка клавиатуры при сбое восстановления фокуса):**
    - Эмуляция состояния, когда окно киоска заблокировано и `kiosk_focus_force()` не может восстановить фокус.
    - Попытка инъекции клавиши через `input_inject_key()`.
    - Проверка: вызов `SendInput` блокируется, возвращается ошибка `ERROR_INVALID_TARGET_HANDLE`, формируется NACK `kiosk_focus_lost`.
16. **`test_generic_desktop_mode_bypasses_kiosk_focus_checks` (Прозрачный ввод в режиме Generic Desktop):**
    - Запуск агента без параметра `--kiosk-process` (`kiosk_mode == false`).
    - Проверка: события мыши и клавиш направляются напрямую в текущее окно через `SendInput` без принудительной проверки PID киоска и без вызова `AttachThreadInput`.
17. **`test_kiosk_adaptive_fallback_when_process_absent` (Прозрачный ввод в Generic Desktop при отсутствии киоска):**
    - Инициализация агента в Kiosk Mode (`kiosk_process = L"absent_kiosk.exe"`), но процесс не запущен в ОС.
    - Проверка: `kiosk_running == false`, `active_input_mode == "generic_fallback"`.
    - Проверка: ввод мыши и клавиатуры через `input_inject_key` и `input_inject_click` не блокируется, NACK `kiosk_focus_lost` не генерируется, синтетический Alt не нажимается.
18. **`test_kiosk_adaptive_transition_on_app_exit_and_relaunch` (Бесшовный переход при выходе и повторном запуске):**
    - Запущен процесс киоска $\implies$ активен строгий Kiosk Guard.
    - Эмуляция закрытия/выхода из киоска $\implies$ мгновенный автоматический переход в `generic_fallback` без задержек.
    - Эмуляция запуска киоска $\implies$ автоматическое обнаружение `HWND` и возврат в строгий Kiosk Guard.
19. **`test_kiosk_lifecycle_graceful_stop_and_kill` (Двухфазный останов через WM_CLOSE с fallback на TerminateProcess):**
    - Запуск тестового GUI-процесса киоска.
    - Вызов `kiosk_lifecycle_stop(1000, true)`: отправка `WM_CLOSE`, подтверждение выхода процесса.
    - Эмуляция зависшего окна (`IsHungAppWindow`): проверка срабатывания принудительного `TerminateProcess` по истечении таймаута и очистки дескрипторов.
20. **`test_kiosk_lifecycle_start_and_status_tracking` (Запуск в интерактивной сессии и телеметрия статуса):**
    - Вызов `kiosk_lifecycle_start()` для тестового бинарника.
    - Проверка создания процесса в интерактивной сессии, получение PID, проверка перехода статуса в `L4D_KIOSK_STATUS_RUNNING`.
    - Проверка отклонения запуска недоверенного бинарника (путь/имя вне allowlist `kiosk_process`) с кодом `ERROR_ACCESS_DENIED`.

---

## 7. Пошаговый алгоритм выполнения агентом (Agent Workflow)

1. **Этап 1: Contract Gate & Контроль допуска**
   - Открыть `docs/l4capture/prompts/contract-handoff.md`.
   - Проверить наличие принятого блока `H-L4C-04-v1` (`status: ACCEPTED`).
   - Сверить контрольные суммы 5 артефактов `H-L4C-04-v1`.
2. **Этап 2: Заголовочные файлы адаптера, Input Gate, Kiosk Focus и Lifecycle**
   - Создать `include/media_backend.h`, `include/l4capture_adapter.h`, `include/input_gate.h`, `include/kiosk_focus.h`, `include/kiosk_lifecycle.h`.
3. **Этап 3: Реализация изоляции процесса и Job Object**
   - Реализовать запуск `l4capture.exe` через `STARTUPINFOEX` с `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`.
   - Реализовать создание Job Object с `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`.
4. **Этап 4: Реализация IPC-канала и неблокирующего Reader**
   - Реализовать сериализацию `CMD_START`, `CMD_RENEW_LEASE`, `CMD_STOP`, `CMD_FORCE_IDR`.
   - Реализовать асинхронный приём `EVENT_READY`, `EVENT_METRICS`, `EVENT_DEGRADED`, `EVENT_ERROR`.
5. **Этап 5: Реализация Input Gate и строгой политики дедлайна**
   - Реализовать `input_gate.c` с проверкой 6 базовых условий и запретом ввода при `default`.
   - Интегрировать `input_release_all()`.
   - Реализовать отсечку аренды строго по дедлайну без 5 с grace.
6. **Этап 6: Реализация модулей Kiosk Focus Management, Adaptive Fallback и Kiosk Lifecycle**
   - Создать `src/kiosk_focus.c`: обнаружение окна киоска, кеширование `HWND`, проверка наличия процесса, реализация `kiosk_focus_force()` через `AttachThreadInput` + `SetForegroundWindow` + ASFW bypass.
   - Реализовать адаптивный фолбэк: автоматический переход в `generic_fallback` при `kiosk_running == false`.
   - Создать `src/kiosk_lifecycle.c`: двухфазный останов (WM_CLOSE + TerminateProcess), запуск в интерактивной сессии с allowlist, опрос статуса (`IsHungAppWindow`).
   - Интегрировать Focus Guard в `input_inject.c`: блокировка `input_inject_key()` при потере фокуса только когда киоск запущен, упреждающая активация перед `input_inject_click()`.
   - Добавить команды `kiosk_start`, `kiosk_stop`, `kiosk_restart`, `kiosk_status`, `refocus_kiosk`, NACK `kiosk_focus_lost` и поля телеметрии в `ctl_protocol.c`.
7. **Этап 7: Реализация Bounded Recovery Loop**
   - Реализовать лимит 5 рестартов за 600 с, backoff и отмену при `stop`/`expiry`.
8. **Этап 8: Интеграция с существующим супервизором и конфигурацией**
   - Подключить `media_backend_t` в `ffmpeg_supervisor.c`, добавить feature flag конфигурации.
   - Поддержать иерархию конфигурации процесса киоска в `config.c` (CLI `--kiosk-process` + fallback на манифест).
9. **Этап 9: Сборка и прогон тестов**
   - Собрать проект в конфигурациях x86 и x64 (`build.cmd`).
   - Запустить тестовый раннер и убедиться в прохождении всех 20 тестов.
   - Проверить отсутствие внешних runtime DLL (`dumpbin /dependents`).
10. **Этап 10: Оформление результатов (Handoff Artifacts)**
    - Сформировать отчёт `docs/l4capture/handoffs/L4C-05-AGENT-ADAPTER-report.md`.
    - Вычислить SHA-256 реальных байтов отчёта и артефактов.
    - Сформировать кандидат `docs/l4capture/handoffs/L4C-05-AGENT-ADAPTER-candidate.md` в формате `DETACHED_V1`.

---

## 8. Формат сдачи результатов (Handoff Artifacts & Report)

Результаты работы оформляются строго в формате `DETACHED_V1`:

### 8.1. Отчёт о реализации (`docs/l4capture/handoffs/L4C-05-AGENT-ADAPTER-report.md`)
Отчёт обязан содержать:
- Назначенный `prompt_id: L4C-05-AGENT-ADAPTER`.
- Перечень созданных и модифицированных файлов в `tools/l4desk/` и `tools/l4capture/`.
- Результаты компиляции x86/x64 без предупреждений (`/W4`).
- Результаты прохождения всех 20 тестов в `test_l4capture_adapter.c` (20/20 PASSED).
- Протокол проверки изоляции Job Object и handle allowlist.
- Протокол валидации Input Gate: доказательство запрета ввода при `default` и разрешения при `low` 480p.
- Протокол тестирования Kiosk Focus Management и Adaptive Fallback: проверка режимов (Kiosk Active vs Adaptive Generic Fallback vs Generic Desktop), отсечки клавиатурного ввода и восстановления фокуса.
- Протокол бэкенда жизненного цикла киоска: проверка команд `kiosk_start`, двухфазного `kiosk_stop` (мягкий и TerminateProcess), `kiosk_restart`, `kiosk_status` (`IsHungAppWindow`).
- Протокол дедлайна аренды: останов $\le 500$ мс без 5 с grace.

### 8.2. Кандидат на приёмку (`docs/l4capture/handoffs/L4C-05-AGENT-ADAPTER-candidate.md`)

```yaml
handoff_id: H-L4C-05-v1
status: CANDIDATE
contract_kinds:
  - MEDIA_BACKEND_ADAPTER
  - PROCESS_JOB_ISOLATION
  - IPC_SUPERVISOR_CHANNEL
  - LEASE_DEADLINE_STRICT
  - INPUT_GATE_POLICY
  - BOUNDED_RECOVERY_LOOP
  - KIOSK_FOCUS_MANAGEMENT
  - KIOSK_LIFECYCLE_BACKEND
producer_prompt_id: L4C-05-AGENT-ADAPTER
producer_scope_project: tools/l4desk + tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-05-AGENT-ADAPTER-report.md
producer_branch: l4capture/l4c-05-agent-adapter
producer_commit: <git_commit_sha>
accepted_at_utc: null
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-05-AGENT-ADAPTER-report.md
  - tools/l4desk/include/media_backend.h
  - tools/l4desk/include/l4capture_adapter.h
  - tools/l4desk/include/input_gate.h
  - tools/l4desk/include/kiosk_focus.h
  - tools/l4desk/include/kiosk_lifecycle.h
  - tools/l4desk/bin/x86/l4desk.exe
  - tools/l4desk/bin/x64/l4desk.exe
artifact_sha256:
  - <sha256_report>
  - <sha256_media_backend_h>
  - <sha256_l4capture_adapter_h>
  - <sha256_input_gate_h>
  - <sha256_kiosk_focus_h>
  - <sha256_kiosk_lifecycle_h>
  - <sha256_l4desk_x86_exe>
  - <sha256_l4desk_x64_exe>
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
    - H-L4C-02-v1
    - H-L4C-03-v1
    - H-L4C-04-v1
  breaking_changes: false
  notes: Адаптер бэкенда l4capture в l4desk. Запуск через Job Object (KILL_ON_JOB_CLOSE) и allowlist дескрипторов, бинарный IPC, дедлайн аренды без grace (<= 500 мс), строгий Input Gate (запрет ввода для default, разрешение для low 480p), Kiosk Focus Management (2 режима + Adaptive Generic Fallback, AttachThreadInput refocus, NACK kiosk_focus_lost), Kiosk Lifecycle Backend на терминале (kiosk_start/stop/restart/status), ограниченный recovery-loop (до 5 попыток за 600 с).
deployment_status: LOCAL_BUILD_VERIFIED
deployed_environment: local_development
feature_flags:
  l4capture_backend_adapter: enabled
  l4capture_input_gate: enabled
  l4desk_kiosk_focus_guard: enabled
  l4desk_kiosk_lifecycle_backend: enabled
contract_payload:
  process_management:
    job_object_flags: JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    job_handle_leak_prevented: true
    handle_allowlist: true (PROC_THREAD_ATTRIBUTE_HANDLE_LIST)
    session_0_check: rejected
    single_process_enforced: true
  lease_safety:
    grace_period_ms: 0 (strict deadline enforcement)
    stop_latency_ms: <= 500 ms
    drain_permitted: false
    renew_validation: monotonic deadline and request_seq check, dedup enabled
  input_gate:
    permitted_profile: low (strictly base_480p)
    denied_profile: default (strictly denied, even if degraded to 480p)
    input_release_on_safety_events: true (input_release_all)
  kiosk_focus_management:
    modes_supported:
      - kiosk_mode (strict foreground guard & refocus)
      - adaptive_generic_fallback (automatic transparent desktop input when kiosk app is absent or exited)
      - generic_desktop_mode (permanent transparent input routing)
    configuration_hierarchy:
      primary: CLI --kiosk-process <name>
      fallback: manifest or l4desk.json
      default: generic_desktop_mode
    refocus_mechanism: AttachThreadInput + SetForegroundWindow + ASFW bypass (Alt-key trick)
    fail_safe_input_guard: reject keyboard input with NACK kiosk_focus_lost only when kiosk process is running but unfocused
    frontend_visibility: telemetry flags (kiosk_mode, kiosk_running, kiosk_in_focus, active_input_mode, foreground_process) and refocus_kiosk command
  kiosk_lifecycle_backend:
    scope: tools_only (menubuilder integration scheduled for subsequent phase)
    commands:
      - kiosk_start (launches trusted kiosk executable in interactive user session)
      - kiosk_stop (two-phase shutdown: WM_CLOSE with timeout <= 3000 ms, fallback to TerminateProcess)
      - kiosk_restart (sequential stop + start)
      - kiosk_status (retrieves PID, uptime, is_hung via IsHungAppWindow)
    security_allowlist: executable name strictly matched with configured kiosk_process
    adaptive_fallback_policy:
      when_process_absent: automatic fallback to generic_desktop_mode without input blocking
      when_process_launched: automatic reinstatement of strict kiosk focus guard
  recovery_loop:
    max_restarts: 5
    window_sec: 600
    backoff: exponential with jitter
    cancel_on_stop_or_expiry: true
supersedes: []
known_risks:
  - R1: Потеря состояния при UAC/Lock — парируется жестким safety gate <= 500 мс и input_release_all()
  - R3: Зависание дочернего процесса — Job Object гарантирует уничтожение, watchdog супервизора не зависит от worker-блокировок
  - R4: Перехват фокуса системным окном — парируется автоматическим refocus через AttachThreadInput и fail-closed блокировкой ввода
  - R5: Зависание киоск-приложения при останове — двухфазный kiosk_stop гарантированно завершает процесс через TerminateProcess
consumers:
  - L4C-06-MILESTONE-LIVE-VERIFY
  - ALL_FOLLOWING
next_prompt_id: L4C-06-MILESTONE-LIVE-VERIFY
```

---

## 9. Чек-лист самоконтроля перед передачей результатов

- [ ] Все изменения выполнены строго в `tools/l4desk/` и `tools/l4capture/`.
- [ ] Каталог `l4desk-service` не открывался, не читался и не модифицировался.
- [ ] Серверные проекты (`ProcessingBackend`, `MenuBuilder`, `shared`) и легаси не затронуты.
- [ ] Дочерний процесс `l4capture.exe` привязан к Job Object с `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`.
- [ ] Дескриптор Job Object не передаётся дочернему процессу.
- [ ] Наследование дескрипторов ограничено allowlist через `STARTUPINFOEX`.
- [ ] Отсутствует 5-секундный grace-период для дедлайна `l4capture` (останов $\le 500$ мс).
- [ ] Ввод для `default` категорически запрещён даже при деградации до 480p.
- [ ] Ввод разрешён исключительно при `low` + `base_480p` + валидная lease + совпадающая эпоха + подтверждённая геометрия.
- [ ] Поддерживаются два базовых режима и адаптивный fallback: Kiosk Mode, Adaptive Generic Fallback и Generic Desktop Mode.
- [ ] Реализован адаптивный fallback: при выходе из киоска или если он не запущен, ввод в рабочий стол не блокируется (deadlock исключён).
- [ ] Реализован модуль `kiosk_focus` с кешированием `HWND` и восстановлением фокуса через `AttachThreadInput`.
- [ ] Клавиатурный ввод при потере фокуса киоском блокируется с возвратом NACK `kiosk_focus_lost` только когда киоск реально запущен.
- [ ] Реализован модуль `kiosk_lifecycle` на стороне терминала: команды `kiosk_start`, `kiosk_stop` (WM_CLOSE + TerminateProcess), `kiosk_restart`, `kiosk_status`.
- [ ] В протокол управления добавлены команды управления киоском, `refocus_kiosk` и телеметрия `kiosk_running`, `active_input_mode`.
- [ ] Bounded recovery ограничен 5 попытками за 600 с и отменяется при `stop`/`expiry`.
- [ ] Все 20 тестов в `test_l4capture_adapter.c` проходят успешно (20/20 PASSED).
- [ ] Сборки x86 и x64 проходят без предупреждений (`/W4`).
- [ ] `dumpbin /dependents` подтверждает отсутствие сторонних рантайм-зависимостей.
- [ ] Candidate и Report сформированы в формате `DETACHED_V1` с валидными контрольными суммами SHA-256.

---

## 10. Формальные критерии приёмки и критерии отказа

### Критерии успешной приёмки:
1. Запуск `l4capture.exe` из `tools/l4desk` происходит с изоляцией Job Object и allowlist анонимных труб.
2. Протокол IPC полностью функционален: команды отправляются, события принимаются неблокирующим образом.
3. Останов по экспирации дедлайна аренды происходит без 5-секундного grace $\le 500$ мс.
4. Input Gate надёжно блокирует ввод при профиле `default` и разрешает только при `low` 480p.
5. При любых сбоях вызывается `input_release_all()`.
6. Реализован модуль контроля фокуса киоск-приложения с поддержкой Kiosk Mode, Adaptive Generic Fallback и Generic Desktop Mode.
7. Адаптивный fallback гарантирует свободную работу с рабочим столом Windows при закрытии киоска или если киоск не был запущен.
8. Механизм `kiosk_focus_force()` восстанавливает foreground через `AttachThreadInput` и обход ASFW.
9. Клавиатурный ввод защищён от утечки в фоновые окна при потере фокуса киоском (NACK `kiosk_focus_lost`).
10. Реализован бэкенд управления жизненным циклом киоск-приложения на терминале (`tools/l4desk`): `kiosk_start`, `kiosk_stop`, `kiosk_restart`, `kiosk_status`.
11. Цикл восстановления ограничен 5 перезапусками и прерывается при останове.
12. Все 20 модульных тестов завершились со статусом `PASSED`.

### Критерии безусловного отказа (REJECTED):
1. Модификация файлов в каталоге `l4desk-service` или других подсистемах вне разрешённого scope (включая запрет модификации `MenuBuilder/` на текущем шаге).
2. Утечка дескриптора Job Object дочернему процессу или запуск без Job Object.
3. Наличие 5-секундного grace-периода для дедлайна аренды `l4capture`.
4. Разрешение удалённого ввода для профиля `default` (включая случай деградации до 480p).
5. Блокировка ввода в рабочий стол (deadlock) при неактивном или закрытом приложении киоска (отсутствие адаптивного fallback).
6. Утечка клавиатурного ввода в сторонние фоновые окна при потере фокуса киоском в режиме Kiosk Mode.
7. Неконтролируемый бесконечный цикл перезапуска процесса при сбоях.
8. Падение хотя бы одного из 20 обязательных тестов.
9. Зависимость бинарников от сторонних CRT DLL (`msvcrt*.dll`).
