# L4C-10-WIN7-TELEMETRY — Расширенная матрица совместимости Windows 7 / Embedded, точная инвентарная телеметрия и 2-часовой стресс-тест на утечки

```yaml
prompt_id: L4C-10-WIN7-TELEMETRY
scope_project: tools/l4capture
scope_root: D:\repo\platerra\Public\etranprocessing\tools\l4capture
prompt_type: implementation-step
required_handoff_ids:
  - H-L4C-09-v1
sequence_gate_status: READY_FOR_L4C_10
output_handoff_id: H-L4C-10-v1
next_prompt_id: L4C-11-RELEASE-PACKAGE
branch: l4capture/l4c-10-win7-telemetry
report_path: docs/l4capture/handoffs/L4C-10-WIN7-TELEMETRY-report.md
candidate_format: DETACHED_V1
candidate_path: docs/l4capture/handoffs/L4C-10-WIN7-TELEMETRY-candidate.md
architecture_sections: [1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14]
consumers:
  - L4C-11-RELEASE-PACKAGE
  - ALL_FOLLOWING
```

---

## 0. Как использовать промпт и источники истины

Этот документ — задание на **будущую реализацию**, а не декларация о её завершении. При запросе на ревью/редактирование промпта изменяется только документ: не выполнять Contract Gate как запуск шага, не создавать код, report/candidate, не переключать ветку, не вносить изменения в журнал и общую инфраструктуру.

При явном запуске реализации обязательны:
- [Архитектурный релиз](../l4capture_arch_final.md), в особенности §3 (Платформа, сборка и поставка), §8 (Диагностика), §12.3 (Шаг 10), §13 (Измеримые критерии приёмки), §14 (Матрица рисков R1–R4).
- [Стандарт выполнения промптов](PROMPT-STANDARD.md), [регламент контроллера каскада](HANDOFF-CONTROLLER-PROMPT.md), [журнал принятых контрактов](contract-handoff.md).
- Принятый входной блок `H-L4C-09-v1` со статусом `ACCEPTED`, его отчёт `L4C-09-PROFILES-DEGRADE-report.md` и фактические контракты в коде (`video_profile.h`, `degrade_controller.h`, `telemetry.h`, `ipc_protocol.h`).
- Для обеспечения совместимости — ранее принятые контракты L4C-01 .. L4C-08 (включая C-интерфейсы захвата, кодирования, сетевой отправки и адаптера агента `l4desk`). Чтение внешних контрактов не даёт права на их изменение вне разрешённого scope.

Архитектура определяет инварианты, журнал фиксирует принятые входы, данный промпт детализирует требования шага. При обнаружении противоречий следует немедленно остановиться с фиксацией адресного блокера (`BLOCKED_*`), не выбирая произвольную трактовку.

---

## 1. Цель и архитектурная миссия

Ты выступаешь в роли **Ведущего системного инженера платформенной совместимости Windows, телеметрии и эксплуатационной надёжности (L4Capture-Win7Telemetry-Agent)** в рамках комплекса `tools suite`.

Твоя задача — реализовать и доказать на практике полную платформенную пригодность `l4capture.exe` для реального парка терминалов в целевой директории `tools\l4capture\`:
1. Подтвердить запуск и стабильную работу на **чистой Windows 7 SP1 (x86 и x64)**, **Windows Embedded Standard 7 (WES7)** и **POSReady 7** без предустановленного пакета Visual C++ Redistributable (`msvcr*.dll`, `vcruntime*.dll`);
2. Реализовать сбор **фактических системных и конвейерных метрик** в реальном времени (настоящие значения Private Bytes через Win32 API, GDI-дескрипторов, перцентиля времени кодирования p95, окна битрейта и FPS) взамен временных плейсхолдеров;
3. Внедрить безопасное **ротируемое инвентарное логирование** (до 5 МБ × 2) с фиксацией параметров платформы, CPU, памяти, графического адаптера и причин fallback;
4. Провести полномасштабный **2-часовой стресс-тест на утечки (Soak Test)** на репрезентативном слабом терминальном стенде с доказательством строгой ресурсной непроницаемости (дрейф Private Bytes $\le 5$ МиБ, отсутствие утечек GDI/User Handles).

### Архитектурный контекст и системная ценность

Главным приоритетом функционирования платёжной инфраструктуры является максимизация доступности (uptime) платёжных терминалов самообслуживания и минимизация рисков прерывания финансовых транзакций при одновременном снижении затрат на обслуживание парка устройств.

Платёжные терминалы эксплуатируются на широком спектре оборудования — от современных систем на Windows 10/11 IoT Enterprise до устаревших аппаратов под управлением Windows 7 SP1, Windows Embedded Standard 7 (WES7) и POSReady 7 на базе 2-ядерных энергоэффективных процессоров (Intel Celeron, Atom, Core 2 Duo) с объёмом оперативной памяти 1–2 ГБ:
1. **Уязвимость к скрытым зависимостям:** Если сервис видеозахвата требует внешних динамических библиотек рантайма C++ (`vcruntime140.dll`, `msvcp140.dll`) или использует системные API более поздних ОС без динамической проверки, терминал при старте видеосессии сталкивается с крахом процесса (`STATUS_DLL_NOT_FOUND` / 0xC0000135). Это лишает оператора удалённого контроля и требует дорогостоящего выезда сервисного инженера.
2. **Опасность ресурсного истощения (Memory/Handle Leaks):** Платёжный терминал работает в непрерывном режиме 24/7. Даже незначительная утечка дескрипторов GDI/User (`CreateCompatibleDC`, `CreateDIBSection`, шрифты, регионы) или фрагментация неуправляемой памяти со временем приводит к падению UI платёжного интерфейса, отказу купюроприёмника и срыву транзакций клиентов.
3. **Необходимость объективной телеметрии:** При удалённом мониторинге операторам критически важно опираться на достоверные данные о состоянии процесса (реальное потребление Private Bytes, нагрузка на кодер p95, сетевые сбросы, истинный FPS). Синтетические плейсхолдеры в телеметрии создают слепую зону, не позволяя превентивно обнаружить перегрев или деградацию канала.

Реализация **L4C-10-WIN7-TELEMETRY** устраняет эти системные риски:
- Гарантируется автономная работоспособность на базовом парке Win7/Embedded за счёт статической компоновки `/MT`, изоляции импортов и динамической подгрузки опциональных библиотек;
- Повторяется полная верификация вехи M-1 (R1–R4) на чистой среде Windows 7;
- Предоставляется точный инструментарий телеметрии без накладных расходов для основного конвейера;
- Подтверждается эксплуатационная стабильность в ходе длительного 2-часового непрерывного прогона.

---

## 2. Непереговорные рамочные принципы и изоляция

1. **Строгая изоляция директорий (Directory Boundary):**
   - **Код, заголовки, скрипты сборки и тесты:** создаются и изменяются строго в `tools\l4capture\`.
   - **Архитектура, регламенты, журнал контрактов:** располагаются в `docs\l4capture\`; исполнитель промпта их не модифицирует.
   - **Отчёты и candidate-файлы:** формируются строго в `docs\l4capture\handoffs\`.
   - **КАТЕГОРИЧЕСКИЙ ЗАПРЕТ `l4desk-service`:** Строжайше запрещено создавать, читать или изменять любые файлы внутри каталога `l4desk-service`. Проект `l4capture` автономен.
   - **ЗАПРЕТ модификации других подсистем:** Запрещено изменять код в `BACK\`, `FRONT\`, `tools\l4desk`, `tools\leo4proxy`, `l4media\`, `ProcessingBackend\`, `MenuBuilder\`, `shared\`.
   - Сохранять чистоту репозитория: не коммитить без отдельной команды, не сбрасывать чужие изменения.
2. **Стандарты компиляции чистого C и поддержка Windows 7:**
   - Код пишется строго на **C99/C11** (подмножество MSVC, компиляция как C через `cl.exe`, не C++).
   - Статическая компоновка CRT: флаг `/MT` для Release-сборки (`/MTd` только для отладки).
   - Целевая платформа: Windows 7 SP1 x86 и x64 (`/D_WIN32_WINNT=0x0601`, x86: `/SUBSYSTEM:CONSOLE,6.01`, x64: `/SUBSYSTEM:CONSOLE`).
   - Нулевая толерантность к предупреждениям: компиляция с `/W4 /WX`.
3. **Чистота зависимостей и безопасная динамическая загрузка системных API:**
   - Статическая таблица импортов (`dumpbin /imports`) бинарников `l4capture.exe` должна содержать **только базовые системные библиотеки Windows 7**: `KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `WS2_32.dll`, `ole32.dll`, `OLEAUT32.dll`, `PSAPI.dll`, `ADVAPI32.dll`.
   - Любые библиотеки, специфичные для Windows 8+ или опциональные для урезанных сборок Embedded (Media Foundation `mfplat.dll`, `mf.dll`, DirectX `dxgi.dll`, `d3d11.dll`, `dwmapi.dll`), обязаны загружаться исключительно динамически через `LoadLibraryExW` с флагом `LOAD_LIBRARY_SEARCH_SYSTEM32` (поиск строго в `%SystemRoot%\System32`).
   - На чистой системе Windows 7 SP1 при отсутствии опциональных DLL процесс обязан стартовать без сбоев и прозрачно активировать бэкенд `GDI + OpenH264`.
4. **Достоверность телеметрии и заморозка сетевых схем (Wire Freeze):**
   - Плейсхолдеры в телеметрии недопустимы. Замеры Private Bytes, GDI Handles, p95 времени кодирования, FPS и битрейта должны вычисляться на базе реальных вызовов Win32 и оконных счётчиков.
   - Запрещено изменять wire-протокол IPC (`L4C_IPC_VERSION = 1`, структура заголовка, типы команд `CMD_*`/`EVENT_*`). Смещение полей в `EVENT_METRICS` обязано строго соответствовать принятому контракту `ipc_protocol.h`.
   - Новые диагностические параметры, не помещающиеся в существующий фиксированный wire-фрейм, выводятся **исключительно в локальный ротируемый лог** `l4capture.log` (§8 архитектуры).
5. **Безопасность данных и защита приватности (Security & Privacy Guardrails):**
   - В логах, телеметрии и отчётах **категорически запрещено** сохранять: содержимое видеокадров, дамп буферов экрана, PIN-коды терминалов, токены авторизации, приватные ключи, реквизиты карт и платёжные данные клиентов.
   - Размер локального диагностического лога строго ограничен: не более двух файлов по 5 МБ (ротация 5 МБ × 2). Запись выполняется с ограничением частоты (rate-limiting) без влияния на производительность конвейера видеозахвата.
6. **Ресурсная непроницаемость (Zero-Leak Policy):**
   - В ходе непрерывного 2-часового теста под нагрузкой недопустим монотонный рост потребления памяти (дрейф Private Bytes $> 5$ МиБ после прогрева блокирует приёмку).
   - Счётчики дескрипторов GDI и User по окончании сессий обязаны возвращаться к исходному базовому уровню.

---

## 3. Pre-Flight Check & Contract Gate (Шаг 1)

Перед внесением любых изменений в кодовую базу выполни обязательную процедуру Contract Gate:

1. Открой нормативные документы, перечисленные в §0. Зафиксируй текущую ветку Git, хеш коммита HEAD и статус рабочего каталога (`git status`).
2. Проверь наличие в `docs/l4capture/prompts/contract-handoff.md` завершённого блока `H-L4C-09-v1` со статусом `ACCEPTED`. Убедись, что выходной блок `H-L4C-10-v1` ещё не внесён.
3. Проверь статус шага 10 в таблице реестра `contract-handoff.md`: должен быть `READY_FOR_L4C_10` («Готов к запуску»).
4. Сверь контрольные суммы SHA-256 реальных байтов входных артефактов из блока `H-L4C-09-v1`:
   - `docs/l4capture/handoffs/L4C-09-PROFILES-DEGRADE-report.md`: `a2b2a4f8f3eb46f94cbffccc1c0cdc219ff56dc80129b4f7b2b6f4b47b190957`
   - `tools/l4capture/include/l4capture/video_profile.h`: `027aa76c332edabbe0c303452318d704011e8b738f037b9cc00e0a2d7144bca5`
   - `tools/l4capture/include/l4capture/degrade_controller.h`: `3fc37cd35fc36a84ae5fac1f2692444540a4e749f1782f16856087838947cbcc`
   - `tools/l4capture/src/pipeline/video_profile.c`: `0e0c60adea598d0ac0188c7ef4b106985cdba0231636e484a73030c3cfd93c32`
   - `tools/l4capture/src/pipeline/degrade_controller.c`: `97d6d36712a8f7f42af0241a7f41f42b167c039119d2609d192413d273c739a3`
   - `tools/l4capture/tests/test_profiles_degrade.c`: `c1e6a1162bca886e91d04fe21afe422fa9245666992b45e850018131b5811e94`
   - `tools/l4capture/src/main.c`: `f9fce1ca169bc6dc37dd576d41ffd4ea562552d8771c2420e2f2c14dd90f1d94`
   - `tools/l4capture/bin/x86/l4capture.exe`: `20bf7ec1cf32857d0df310c87004db1701e225af28795879b3b3050da9f3960c`
   - `tools/l4capture/bin/x64/l4capture.exe`: `7e54eedaedb636e027690bb41c00f18618ef26187cc2a3fcf861556d7fa1f652`
5. Сохрани таблицу валидации хешей в отчёте. При обнаружении несоответствий, отсутствии входного контракта или неверной ветке немедленно заверши процедуру со статусом `BLOCKED_CONTRACT`.

---

## 4. Архитектурные требования и техническая спецификация

Реализация шага L4C-10 разделена на пять взаимосвязанных направлений в рамках директории `tools\l4capture\`.

### 4.1. Расширенная матрица совместимости Windows 7 / Embedded и чистота импортов

1. **Целевая матрица операционных систем:**
   - **Обязательные базовые платформы:**
     - Windows 7 SP1 x86 (32-bit);
     - Windows 7 SP1 x64 (64-bit);
     - Windows Embedded Standard 7 (WES7) SP1 (x86/x64);
     - Windows Embedded POSReady 7 (x86).
   - **Современные и серверные редакции (регрессионная совместимость):**
     - Windows 10 IoT Enterprise (LTSC 2019 / 2021);
     - Windows 11 Pro / Enterprise;
     - Windows Server 2008 R2 SP1, Server 2012 R2, Server 2016, Server 2019, Server 2022.
2. **Анализ зависимостей бинарников (`dumpbin`):**
   - Провести проверку команд:
     ```cmd
     dumpbin /dependents bin\x86\l4capture.exe
     dumpbin /dependents bin\x64\l4capture.exe
     dumpbin /imports bin\x86\l4capture.exe
     dumpbin /imports bin\x64\l4capture.exe
     ```
   - Убедиться в полном отсутствии зависимостей от Visual C++ Redistributables (`msvcr*.dll`, `vcruntime*.dll`).
   - Проверить, что все вызовы Win32 API, импортируемые статически, поддерживаются ядром Windows 7 SP1.
   - Проверить адресацию системных библиотек: опциональные вызовы (MF, DXGI, DWM) загружаются через `LoadLibraryExW(..., NULL, LOAD_LIBRARY_SEARCH_SYSTEM32)` либо `GetModuleHandleW`.
3. **Повторная сквозная верификация M-1 на чистой Windows 7 SP1 (Fault-матрица R1–R4):**
   - На стенде под управлением Windows 7 SP1 (чистая установка без VC++ Redistributable):
     - **R1 (Lease/Session Safety):** Истечение дедлайна аренды прекращает передачу RTP за время $\le 500$ мс; блокировка рабочего стола (Win+L), вызов UAC или отключение сессии переводят сервис в остановку без зависаний; перезапуск не происходит при истёкшей аренде.
     - **R2 (External Delivery):** Восстановление видеопотока после сетевого разрыва со свежим IDR за время $\le 3$ с без ожидания обратного RTCP PLI; отсутствие зацикливания устаревших кадров.
     - **R3 (Internal Stability):** Стабильная работа GDI-захвата при 100 циклах старт/стоп; устойчивость к задержкам кодирования и сбросу кадров по схеме latest-frame.
     - **R4 (Platform & Geometry):** Корректный захват физического экрана и отрисовка курсора через `GetCursorInfo`/`DrawIconEx`; масштабирование в растр `854×480`; кодирование через статический OpenH264; отсутствие искажений цветовых плоскостей I420.

### 4.2. Точная телеметрия конвейера и сбор системных ресурсов

В текущей кодовой базе (`src/main.c`, строки 180–198) параметры телеметрии передаются через фиктивные значения (плейсхолдеры: `encode_p95_ms = 0`, `queue_depth = frames_sent`, `private_bytes_kb = frames_captured`, `gdi_handles = frames_encoded`, `bitrate_kbps = target`).

Необходимо реализовать модуль точного сбора телеметрии `src/pipeline/telemetry.c` и обновить `include/l4capture/telemetry.h`:

1. **Реальные метрики памяти процесса (`private_bytes_kb`):**
   - Вычислять реальный объём выделенной закрытой памяти процесса (Private Bytes):
     ```c
     PROCESS_MEMORY_COUNTERS_EX pmc;
     memset(&pmc, 0, sizeof(pmc));
     pmc.cb = sizeof(pmc);
     /* Совместимость с Win7: динамическое разрешение GetProcessMemoryInfo из psapi.dll */
     if (pfn_GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
         metrics.private_bytes_kb = (uint32_t)(pmc.PrivateUsage / 1024u);
     }
     ```
   - Замер должен строго соответствовать значению колонки «Commit Size» / «Private Bytes» в Process Explorer / Task Manager.
2. **Реальные дескрипторы графической подсистемы (`gdi_handles`):**
   - Вызывать системный API `GetGuiResources`:
     ```c
     DWORD gdi_count = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
     metrics.gdi_handles = (uint32_t)gdi_count;
     ```
   - Дополнительно фиксировать в локальном логе `GR_USEROBJECTS` для комплексного контроля утечек дескрипторов оконной подсистемы.
3. **Реальный перцентиль времени кодирования (`encode_p95_ms`):**
   - Для каждого кадра в рамках 1-секундного окна фиксировать время работы энкодера (`uint32_t enc_ms = (uint32_t)(t_after - t_before)`).
   - Сохранять выборку временных замеров в кольцевой/статический буфер (до `L4C_DEGRADE_MAX_P95_SAMPLES = 256` семплов).
   - По завершении 1-секундного окна вычислять истинное значение 95-го перцентиля с помощью алгоритма `l4c_degrade_p95_from_samples`.
4. **Реальный битрейт и FPS за скользящее окно 1000 мс:**
   - `fps`: фактическое количество Access Units (кадров H.264), успешно закодированных и переданных в RTP-отправитель за истёкшие 1000 мс.
   - `bitrate_kbps`: суммарный объём байт всех переданных Access Units за 1000 мс, пересчитанный в килобиты в секунду: `(total_au_bytes * 8u) / 1000u`.
5. **Глубина очередей и дропы (`queue_depth`, drops):**
   - `queue_depth`: текущее число кадровых буферов, ожидающих обработки (в архитектуре zero-queue: 0 или 1 при наличии готового захваченного кадра перед кодированием).
   - Точные счётчики сбросов:
     - `raw_drops`: сбросы на этапе захвата/pacing (пропуск тиков таймера, замещение старого кадра свежим);
     - `encoder_drops`: пропуски кадров кодером (`L4C_ERR_NO_FRAME` / frame skip);
     - `transport_drops`: сбои отправки UDP-сокетом (`sendto` failure / буфер переполнен).

### 4.3. Совместимость сериализатора и потребителя (Producer → IPC → Consumer)

1. **Выравнивание фрейма `EVENT_METRICS` (30 байт):**
   - Контракт `L4C_EVENT_METRICS` в `ipc_protocol.h` и `ipc_protocol.c` определяет полезную нагрузку ровно **30 байт**:
     - `offset 0..1`: `fps` (`uint16_t`)
     - `offset 2..5`: `bitrate_kbps` (`uint32_t`)
     - `offset 6..9`: `raw_drops` (`uint32_t`)
     - `offset 10..13`: `encoder_drops` (`uint32_t`)
     - `offset 14..17`: `transport_drops` (`uint32_t`)
     - `offset 18..19`: `encode_p95_ms` (`uint16_t`)
     - `offset 20..21`: `queue_depth` (`uint16_t`)
     - `offset 22..25`: `private_bytes_kb` (`uint32_t`)
     - `offset 26..29`: `gdi_handles` (`uint32_t`)
   - **Анализ потребителя (`tools/l4desk/src/l4capture_adapter.c`):** В коде адаптера проверка выполнена как `if (plen >= 28)` для `private_bytes_kb` и `if (plen >= 32)` для `gdi_handles`. При длине 30 байт `gdi_handles` отсекается проверкой адаптера.
   - **Правило изоляции:** Так как шаг L4C-10 строго изолирован в директории `tools/l4capture/`, изменение кода `l4desk` **запрещено**. Пакет полезной нагрузки `L4C_EVENT_METRICS` в `l4capture` остаётся строго 30 байт в соответствии с `valid_length()` (`case L4C_EVENT_METRICS: return length == 30;`). Несоответствие потребителя в `l4desk` документируется в отчёте как известный факт без нарушения границ scope.
2. **Словарь внутренней инвентарной телеметрии (§8 архитектуры):**
   - Согласовать соответствие ключей словаря:
     - `video_capture_backend`: `1` (GDI), `2` (DXGI);
     - `video_encoder_backend`: `1` (OPENH264_SOFTWARE), `2` (MF_HARDWARE);
     - `video_profile_requested`: `1` (low), `3` (default);
     - `video_profile_actual`: `1` (480p), `2` (540p), `3` (720p);
     - `video_hw_accel`: `true`, если активен аппаратный MFT;
     - `video_degradation_state`: `0` (NOMINAL), `1` (FPS_THROTTLED), `2` (DEGRADED_540P), `3` (DEGRADED_480P);
     - `video_fallback_reason`: `0` (NONE), `1` (DXGI_ACCESS_LOST), `2` (MFT_UNAVAILABLE), `3` (WIN7_LEGACY), `4` (HIGH_LOAD).

### 4.4. Локальный инвентарный ротируемый журнал (`l4capture.log`)

Для всесторонней диагностики инцидентов на терминалах без изменения серверных протоколов реализуй локальный ротируемый журнал:

1. **Политика ротации и размер файлов:**
   - Путь журнала: `l4capture.log` в рабочей директории процесса или каталоге логов агента.
   - Максимальный размер активного файла — **5 242 880 байт (5 МиБ)**.
   - При превышении лимита файл переименовывается в `l4capture.log.old` (старый `.old` перезаписывается), создаётся новый `l4capture.log`. Общий объём журналов на диске гарантированно **не превышает 10 МиБ**.
2. **Структурированный инвентарный заголовок при запуске (Startup Inventory Header):**
   При старте процесса в лог записывается полный слепок конфигурации оборудования и окружения:
   - **ОС:** Версия ядра (Major.Minor.Build), Service Pack, разрядность процесса и ОС (x86/x64), тип продукта (Workstation, Server, Embedded/POSReady);
   - **Процессор:** Количество логических ядер, маска процессоров, модель/семейство CPU;
   - **Память:** Общий объем физической памяти (RAM) и доступный объем на момент старта (`GlobalMemoryStatusEx`);
   - **Экран и геометрия:** Разрешение монитора, координаты исходного прямоугольника `RECT` (включая проверку отрицательных координат), DPI-масштабирование (DPI-awareness);
   - **Медиа-тракт:** Запрошенный профиль, выбранный бэкенд захвата (GDI/DXGI), выбранный бэкенд кодирования (MFT/OpenH264), причина fallback при отказе от аппаратных средств;
   - **Лимиты:** Дедлайн аренды, начальный FPS, целевой и максимальный битрейт.
3. **Безопасность и отсутствие конфиденциальных данных:**
   - Запрещено логировать: пиксели/дампы экранов, содержимое H.264 NAL-юнитов, PIN-коды, ключи шифрования, пароли или полный текст команд управления.
   - Сообщения об ошибках и деградации логгируются в компактном текстовом формате с временными метками `[YYYY-MM-DD HH:MM:SS.mmm]`.

### 4.5. 2-часовой стресс-тест на утечки ресурсов (2-Hour Soak Test)

Проведи непрерывный 2-часовой прогон на репрезентативном низкопроизводительном терминальном стенде (Windows 7 SP1 или эквивалентный двухъядерный ПК):

1. **Параметры теста:**
   - Длительность: **2 часа (120 минут)** непрерывного захвата и кодирования;
   - Профиль: `base_480p` (`854×480`), 10 FPS, кодер OpenH264;
   - Фоновая нагрузка: циклическое перемещение тестовых окон, отрисовка меняющегося счётчика миллисекунд и скроллинг текста для постоянной генерации видеопотока.
2. **Критерии прохождения теста (Pass Criteria):**
   - **Утечки памяти (Private Bytes):** После начального прогрева длительностью 5 минут фиксируется базовый уровень Private Bytes. На протяжении последующих 115 минут **монотонный рост памяти строго запрещён**. Итоговый дрейф за 2 часа должен составлять $\le 5$ МиБ. Дрейф $> 5$ МиБ считается критическим дефектом и блокирует приёмку.
   - **Дескрипторы GDI и User (Handles):** Количество объектов GDI (`GR_GDIOBJECTS`) и User (`GR_USEROBJECTS`) должно стабилизироваться на постоянном значении без монотонного тренда. По окончании сессии дескрипторы обязаны вернуться к baseline значению.
   - **Дескрипторы ОС (Kernel Handles):** Число дескрипторов процесса (`GetProcessHandleCount`) стабильно.
   - **Стабильность потока:** 0 падений процесса (crashes), 0 зависаний потоков (hangs), средний FPS $\ge 9.5$.

---

## 5. План разработки и контрольные точки

1. **Этап 1: Contract Gate и аудит окружения:**
   - Сверить хеши артефактов `H-L4C-09-v1` в `contract-handoff.md`;
   - Проверить чистоту ветки `l4capture/l4c-10-win7-telemetry`.
2. **Этап 2: Реализация модуля точной телеметрии (`telemetry.c` / `telemetry.h`):**
   - Написать функции запроса Private Bytes (`GetProcessMemoryInfo`), GDI Handles (`GetGuiResources`);
   - Интегрировать расчёт `encode_p95_ms` из выборки замеров окна;
   - Заменить фиктивные поля в `send_event_metrics` в `src/main.c` на реальные расчётные значения.
3. **Этап 3: Реализация инвентарного ротируемого журнала (`logger.c` / `logger.h`):**
   - Реализовать ротацию файлов (5 МиБ × 2);
   - Реализовать генерацию расширенного инвентарного заголовка при старте (ОС, CPU, RAM, GPU/Display, Backends);
   - Обеспечить фильтрацию конфиденциальных данных.
4. **Этап 4: Верификация импортов и запуск на чистой Windows 7 SP1 (x86/x64):**
   - Собрать бинарники через `build.cmd all` (`/MT /W4 /WX`);
   - Проверить таблицы импортов через `dumpbin /dependents` и `/imports`;
   - Запустить smoke-тест и тесты M-1 (R1–R4) на чистой виртуальной машине/стенде Windows 7 SP1 без VC++ Redistributable.
5. **Этап 5: Проведение 2-часового Soak-теста на терминальном стенде:**
   - Запустить 2-часовой прогон с логированием метрик каждую секунду;
   - Построить графики/таблицу расхода Private Bytes, GDI Handles, FPS, p95;
   - Подтвердить отсутствие монотонного роста памяти ($\le 5$ МиБ дрейфа).
6. **Этап 6: Оформление отчёта и формирование Candidate (DETACHED_V1):**
   - Сформировать отчёт `docs/l4capture/handoffs/L4C-10-WIN7-TELEMETRY-report.md`;
   - Вычислить контрольные суммы SHA-256 реальных байтов артефактов;
   - Создать `docs/l4capture/handoffs/L4C-10-WIN7-TELEMETRY-candidate.md` и передать контроллеру каскада.

---

## 6. Измеримые критерии приёмки и матрица доказательств (A1–A14)

Каждый пункт матрицы обязан сопровождаться прямым воспроизводимым доказательством (evidence):

| № | Критерий приёмки | Метод верификации | Порог прохождения |
|---|---|---|---|
| **A1** | Чистота бинарников Win7 x86/x64 | `dumpbin /dependents` и `/imports` | 0 сторонних DLL, 0 зависимостей от `vcruntime*.dll` / `msvcr*.dll`. |
| **A2** | Запуск на чистой Windows 7 SP1 | Выполнение на чистой Win7 SP1 x86 и x64 | Успешный запуск, инициализация GDI + OpenH264, отправка RTP. |
| **A3** | Матрица Win7 / Embedded / Server | Запуск на стендах/образах WES7, POSReady 7, Server 2008 R2+ | Процесс стартует, определяет платформу и работает штатно. |
| **A4** | Повтор проверок M-1 (R1–R4) на Win7 | Fault-сценарии §12.2 на Windows 7 | R1: стоп $\le 500$ мс; R2: реконнект $\le 3$ с; R3: стабильность; R4: цвета/DPI. |
| **A5** | Замер памяти `private_bytes_kb` | Сравнение телеметрии с Task Manager / VMMap | Расхождение $\le 2\%$, отражает реальный Private Commit процесса. |
| **A6** | Замер дескрипторов `gdi_handles` | Сравнение с `GetGuiResources` в Task Manager | 100% совпадение счётчика GDI-дескрипторов. |
| **A7** | Расчёт перцентиля `encode_p95_ms` | Тест выборки с эталонным расчётом перцентиля | Корректный ранг `ceil(0.95*N)` из массива замеров кадра. |
| **A8** | Замер оконных `fps` и `bitrate_kbps` | Проверка за 1-секундное скользящее окно | Реальные значения переданных кадров и килобит H.264 в секунду. |
| **A9** | Состояние `queue_depth` | Проверка состояния конвейера при захвате | Значения 0 или 1, отсутствие фиктивных накопительных счётчиков. |
| **A10** | Инвентарный заголовок лога | Проверка лог-файла `l4capture.log` при старте | Наличие версий ОС, ядер CPU, RAM, дисплея, бэкендов и fallback. |
| **A11** | Ротация логов и безопасность | Генерация $> 5$ МиБ логов, проверка данных | Создание `.old`, суммарный размер $\le 10$ МиБ, 0 PIN/ключей/кадров. |
| **A12** | 2-часовой Soak-тест (Память) | Непрерывный прогон 120 минут под нагрузкой | Рост Private Bytes после прогрева $\le 5$ МиБ, нет монотонного тренда. |
| **A13** | 2-часовой Soak-тест (Дескрипторы) | Непрерывный прогон 120 минут под нагрузкой | 0 утечек GDI/User/Kernel Handles, возврат к baseline после стопа. |
| **A14** | Изоляция scope и чистота кода | Git status, компиляция MSVC `/W4 /WX /MT` | Изменения строго в `tools/l4capture`, 0 warnings, DETACHED_V1 готов. |

---

## 7. Регламент оформления результатов (DETACHED_V1)

1. По завершении этапов разработки и верификации исполнитель формирует отчёт:
   `docs/l4capture/handoffs/L4C-10-WIN7-TELEMETRY-report.md`.
2. В отчёт включаются: таблица Contract Gate, список созданных/изменённых файлов, протоколы `dumpbin`, результаты тестов M-1 на Win7, телеметрические замеры, графики/логи 2-часового soak-теста, матрица A1–A14.
3. Код фиксируется в ветке `l4capture/l4c-10-win7-telemetry` (коммит `producer_commit`).
4. Вычисляются контрольные суммы SHA-256 реальных байтов отчёта и всех выходных артефактов.
5. Формируется отдельный файл кандидата:
   `docs/l4capture/handoffs/L4C-10-WIN7-TELEMETRY-candidate.md`.
6. Кандидат и отчёт передаются Агенту-Контроллеру каскада (`HANDOFF-CONTROLLER-PROMPT.md`). Исполнитель останавливается и не редактирует `contract-handoff.md` самостоятельно.

---

## 8. Формат отчёта и Candidate (DETACHED_V1)

### Структура отчёта (`docs/l4capture/handoffs/L4C-10-WIN7-TELEMETRY-report.md`):
- Заголовок с Prompt ID: `L4C-10-WIN7-TELEMETRY`;
- Статус заявки (`ACCEPTED` как предложение исполнителя контроллеру либо `BLOCKED_*`);
- Ветка и SHA коммита `producer_commit`;
- Таблица валидации входного Contract Gate (`H-L4C-09-v1`);
- Перечень созданных и изменённых файлов в `tools/l4capture/`;
- Протокол проверки импортов (`dumpbin /dependents`, `/imports`) для x86 и x64;
- Протокол прохождения сквозных тестов M-1 на Windows 7 SP1;
- Протокол верификации точной телеметрии (память, GDI, p95, битрейт, FPS);
- Отчёт о 2-часовом стресс-тесте (график памяти, handles, средний FPS, дрейф Private Bytes);
- Заполненная матрица A1–A14 с путями к сохранённым логам и доказательствам;
- Перечень выявленных ограничений или известных рисков.

### Шаблон Кандидата (`docs/l4capture/handoffs/L4C-10-WIN7-TELEMETRY-candidate.md`):

```yaml
<!-- HANDOFF:H-L4C-10-v1:BEGIN -->
handoff_id: H-L4C-10-v1
status: ACCEPTED
contract_kinds:
  - WIN7_SP1_RUNTIME_VERIFIED
  - EMBEDDED_MATRIX_COMPLIANCE
  - ACCURATE_RUNTIME_TELEMETRY
  - INVENTORY_ROTATING_LOGGER
  - TWO_HOUR_SOAK_VERIFIED
  - ZERO_RESOURCE_LEAKS
producer_prompt_id: L4C-10-WIN7-TELEMETRY
producer_scope_project: tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-10-WIN7-TELEMETRY-report.md
producer_branch: l4capture/l4c-10-win7-telemetry
producer_commit: <git_commit_sha>
accepted_at_utc: <actual_utc_timestamp_iso8601>
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-10-WIN7-TELEMETRY-report.md
  - tools/l4capture/include/l4capture/telemetry.h
  - tools/l4capture/src/pipeline/telemetry.c
  - tools/l4capture/src/common/logger.c
  - tools/l4capture/src/main.c
  - tools/l4capture/bin/x86/l4capture.exe
  - tools/l4capture/bin/x64/l4capture.exe
artifact_sha256:
  - <sha256_of_report_file>
  - <sha256_of_telemetry_h>
  - <sha256_of_telemetry_c>
  - <sha256_of_logger_c>
  - <sha256_of_main_c>
  - <sha256_of_x86_exe>
  - <sha256_of_x64_exe>
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
    - H-L4C-02-v1
    - H-L4C-03-v1
    - H-L4C-04-v1
    - H-L4C-05-v1
    - H-L4C-06-v1
    - H-L4C-07-v1
    - H-L4C-08-v1
    - H-L4C-09-v1
  breaking_changes: false
  notes: Extended Windows 7 SP1 and Embedded compliance verified without VC Redistributables. Real telemetry metrics (Private Bytes via Win32, GDI handles via GetGuiResources, encode p95) and rotating inventory log (5MBx2) integrated without wire schema modification. 2-hour soak test completed with memory drift <= 5 MiB and zero handle leaks.
deployment_status: LOCAL_TESTS_PASSED
deployed_environment: local_build_and_win7_testbed
feature_flags: {}
contract_payload:
  platform_matrix:
    verified_platforms:
      - Windows 7 SP1 x86 (Build 7601) clean
      - Windows 7 SP1 x64 (Build 7601) clean
      - Windows Embedded Standard 7 (WES7) SP1
      - Windows Embedded POSReady 7
      - Windows 10 IoT Enterprise LTSC
      - Windows 11 Enterprise
      - Windows Server 2008 R2 SP1 / 2012 R2 / 2016 / 2019 / 2022
    redistributable_dependency: none_pure_mt_static
    static_imports_allowed: [KERNEL32.dll, USER32.dll, GDI32.dll, WS2_32.dll, ole32.dll, OLEAUT32.dll, PSAPI.dll, ADVAPI32.dll]
    dynamic_optional_dlls: [mfplat.dll, mf.dll, dxgi.dll, d3d11.dll, dwmapi.dll]
  telemetry:
    wire_event: EVENT_METRICS
    wire_length_bytes: 30
    measured_fields:
      fps: measured_sent_au_per_second
      bitrate_kbps: measured_au_payload_bits_per_sec
      raw_drops: pacing_missed_and_replaced_frames
      encoder_drops: codec_skipped_frames
      transport_drops: udp_sendto_failures
      encode_p95_ms: actual_95th_percentile_from_window_samples
      queue_depth: pipeline_active_slots_zero_or_one
      private_bytes_kb: real_win32_private_usage_commit
      gdi_handles: real_win32_get_gui_resources_gdi
    local_inventory_logger:
      active_file: l4capture.log
      archive_file: l4capture.log.old
      max_size_bytes: 5242880
      max_total_bytes: 10485760
      header_fields: [os_version, build, sp, arch, cpu_cores, ram_total, ram_avail, display_rect, dpi, capture_backend, encoder_backend, fallback_reason]
      privacy_enforced: true_no_pin_no_keys_no_frames
  soak_verification:
    duration_hours: 2
    resolution: 854x480
    target_fps: 10
    codec: OpenH264
    private_bytes_drift_limit_mib: 5
    gdi_handle_leak: 0
    crashes: 0
    hangs: 0
  evidence:
    matrix_a1_a14: all_verified
supersedes: []
known_risks:
  - Adapter l4capture_adapter.c plen check (plen >= 32) ignores gdi_handles on 30-byte payload; documented for separate adapter corrective without breaking wire freeze.
  - Software OpenH264 CPU consumption on single-core / low-end Atom terminal remains ~20-25% at 480p/10fps; within stand budget.
consumers:
  - L4C-11-RELEASE-PACKAGE
  - ALL_FOLLOWING
next_prompt_id: L4C-11-RELEASE-PACKAGE
<!-- HANDOFF:H-L4C-10-v1:END -->
```

---

**Definition of Done исполнителя:** Все критерии матрицы A1–A14 подтверждены проверяемыми свидетельствами (evidence), чистый запуск на Windows 7 SP1 x86/x64 без сторонних DLL доказан, замеры телеметрии и 2-часовой soak-тест завершены с нулевыми утечками ресурсов, подготовлены отчёт `L4C-10-WIN7-TELEMETRY-report.md` и candidate `L4C-10-WIN7-TELEMETRY-candidate.md` в формате `DETACHED_V1` для проверки контроллером каскада.
