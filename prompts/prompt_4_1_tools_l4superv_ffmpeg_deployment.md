# PROMPT 4.1 — `tools/l4superv`: Job Object, изоляция процессов Session 0, дистрибуция и инсталлятор FFmpeg

Ты — Senior Windows/C системный инженер. Работаешь автономно в репозитории `D:\repo\platerra\Public\etranprocessing`, каталоги `tools/l4superv/`, `tools/ffmpeg/`, `tools/dist/` (разрешены явно).
Серверную часть (MenuBuilder, app1, l4media) и интерактивный агент `tools/l4desk/` **не изменяешь** — `l4desk` разрабатывается параллельно/следующим шагом агентом PROMPT 4.2. Твоя задача — подготовить инфраструктуру супервизора, гарантированную изоляцию дерева процессов, упаковку и безопасный инсталлятор пакета FFmpeg.
Не трогай `FRONT/`, `BACK/`, `sqlFileExample/`, `stored-procedures/`, `MenuBuilder/`, `ProcessingBackend/`, `shared/`.

Обязательные правила репозитория (`AGENTS.md`, раздел «Key Rules for Tools Development» и «C / C++ Toolchains»):
- Каждый tool — изолированный подкаталог `C:\l4tools\<tool>\`; zero-dependency Win32/C, статическая сборка `/MT`, **обязательно x86 и x64** через `build.cmd` (MSVC Build Tools 2022, `vcvars32.bat`/`vcvars64.bat`; альтернативно CLion MinGW/CMake). Артефакты: `bin\x86\<tool>.exe`, `bin\x64\<tool>.exe`, `bin\<tool>.exe` (x86).
- Любой новый/обновлённый tool или артефакт — стадия в `tools/l4superv/pack_zip.cmd` и обработка в `tools/l4superv/src/installer_main.c`.
- Целевые ОС: Windows 7 Embedded / POSReady 7 (x86) … Windows 10/11 (x64). Не ломать x86-сборку.
- Не выполняй деплой на терминалы/серверы без явного указания; работай локально, тестовые артефакты складывай в `tools/<tool>/obj` или `tools/dist`.

---

## 1. Контекст и существующая архитектура

### 1.1 Иерархия процессов и файлы
```
l4superv.exe (Windows-служба, Session 0)          tools/l4superv/src
  └─ l4desk.exe (интерактивная user session)       tools/l4desk/src (разрабатывается в 4.2)
       └─ ffmpeg.exe (дочерний процесс l4desk)     C:\l4tools\ffmpeg\ffmpeg.exe
leo4proxy.exe (--rtp-tunnel, UDP 5004/5005 → mTLS) tools/leo4proxy
```

### 1.2 `tools/l4superv/src`
- `orchestrator.c` — watchdog: `sp_get_active_console_session()`; запуск `l4desk.exe` из `<base>\l4desk\l4desk.exe` (fallback `\x86\`, `\x64\`) с аргументами `cfg->l4desk_args` или `--run --presence-interval 30`, workdir `<base>\l4desk`; restart с backoff `g_l4desk_backoff_sec`; остановка при отсутствии/смене сессии: событие `Global\L4Desk_Stop_<SN>` → `sp_stop(&pi, evt, 3000)`; `orchestrator_get_l4desk_status(pid, session)`.
- `session_proc.c/.h` — `sp_enable_system_privileges`, `sp_get_active_console_session`, `sp_start_in_session` (`WTSQueryUserToken` + `CreateProcessAsUserW`, `lpDesktop=winsta0\default`, флаги `CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_BREAKAWAY_FROM_JOB`), `sp_is_alive`, `sp_stop` (событие → ожидание → `TerminateProcess`). **Job Object сейчас отсутствует** — при аварии службы или смене сессии дочерние процессы (включая будущий FFmpeg) рискуют остаться в системе.
- `installer_main.c` (`l4install_x86/x64.exe`) — `zip_extract_all(zip, dest, CURRENT_INSTALLER_ARCH, verbose)` для `tools.zip` (ищет рядом с exe: `tools.zip`, `l4tools.zip`; `--zip`, `--dest`, default `C:\l4tools`), создание `l4desk\log`, регистрация службы, вывод сводки.
- `zip_extractor.c/.h` (miniz) — при распаковке пропускает каталог противоположной архитектуры и срезает сегмент `x86|x64` (`transform_arch_path`), т.е. `l4desk\x64\l4desk.exe` → `C:\l4tools\l4desk\l4desk.exe`.
- `pack_zip.cmd` — staging `obj\staging\<tool>\{x86,x64}\…` + `.cmd`/`README`/`CHANGELOG` → `bin\tools.zip` (PowerShell `Compress-Archive`) → копии `tools\l4superv\tools.zip`, `tools\tools.zip`, `tools\dist\tools.zip`; в `tools\dist` также `l4install_x86.exe|x64.exe` и `.cmd`.
- `config.c/.h`, `state_mgr.c`, `mosquitto_conf.c`, `proxy_client.c`, `hardware_fingerprint.c`, `service_mgr.c`, `supervisor_main.c`; скрипты `l4superv_*.cmd`, `l4install_run.cmd`.

### 1.3 Дистрибутив FFmpeg на машине разработки
`D:\ffmpeg` — FFmpeg **9.0.1 full_build gyan.dev, shared, x64** (`bin\ffmpeg.exe`, `ffprobe.exe`, `ffplay.exe`, DLL: `avcodec-63`, `avdevice-63`, `avfilter-12`, `avformat-63`, `avutil-61`, `swresample-7`, `swscale-10`; `bin\` ≈ 239 МБ; каталоги `include\`, `lib\`, `doc\`, `presets\`, `LICENSE`, `README.txt`). Требует Windows 10+/UCRT. **x86-сборки в нём нет.**

---

## 2. Утверждённые архитектурные решения

1. **Никакого отдельного Windows-сервиса для FFmpeg.** FFmpeg запускается только как дочерний процесс `l4desk` при активной трансляции.
2. **`l4superv`** отвечает за:
   - Выбор активной интерактивной сессии пользователя.
   - Запуск `l4desk` в этой сессии.
   - Restart с backoff при аварийном завершении `l4desk`.
   - Остановку по событию `Global\L4Desk_Stop_<SN>`.
   - **Гарантированную очистку дерева процессов через Job Object** (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`) при logout, смене сессии, stop/restart службы или аварийном падении службы супервизора.
3. **FFmpeg — отдельный артефакт `ffmpeg.zip`** рядом с `tools.zip` в `tools/dist`, структура `ffmpeg\x64\…` и `ffmpeg\x86\…` (совместимо с `zip_extract_all`), установка в `C:\l4tools\ffmpeg\`, минимально необходимый набор файлов.
4. **Безопасная установка/обновление**: инсталлятор `l4install` обязан предотвращать перезапись используемых файлов, проверять состояние работающего FFmpeg через файл статуса и выполнять атомарную замену с проверкой контрольных сумм.

---

## 3. Требования к реализации

### 3.1 `l4superv`: Внедрение Job Object и изоляция дерева процессов
Файлы: `tools/l4superv/src/session_proc.c/.h`, `orchestrator.c/.h`.
- Создавать именованный или анонимный **Job Object** для пользовательской сессии:
  - `CreateJobObjectW(NULL, NULL)`.
  - Установить `JOBOBJECT_EXTENDED_LIMIT_INFORMATION`: флаг `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`.
  - Флаг `JOB_OBJECT_LIMIT_BREAKAWAY_OK` **не выставлять** (дочерним процессам запрещено покидать Job Object).
- Запуск процесса `l4desk`:
  - `sp_start_in_session`: вызывать `CreateProcessAsUserW` с флагом `CREATE_SUSPENDED`.
  - Флаг `CREATE_BREAKAWAY_FROM_JOB` выставлять только в том случае, если сам процесс `l4superv` уже запущен внутри какого-то родительского Job Object (проверять через `IsProcessInJob(GetCurrentProcess(), NULL, &in_job)`). Если служба не в Job Object — убрать этот флаг.
  - Поместить созданный процесс в Job Object через `AssignProcessToJobObject(hJob, pi.hProcess)`.
  - Возобновить выполнение основного потока: `ResumeThread(pi.hThread)`.
- Остановка и таймауты:
  - В `sp_stop`: увеличить grace-период ожидания мягкой остановки по событию `Global\L4Desk_Stop_<SN>` с 3000 мс до **≥ 8000 мс** (рекомендуется 8000–10000 мс), чтобы `l4desk` успел штатно закрыть сессию стрима и завершить `ffmpeg.exe`.
  - Если по истечении grace-периода процесс не завершился: `TerminateJobObject(hJob, 1)` перед вызовом `sp_stop`/`TerminateProcess`.
  - При закрытии дескриптора Job Object операционная система автоматически уничтожит все процессы дерева (`l4desk` + `ffmpeg`), что гарантирует отсутствие orphan-процессов при аварийном падении `l4superv`.
- Статус супервизора (`orchestrator_get_l4desk_status`, `l4superv_status.cmd`):
  - Добавить считывание состояния FFmpeg из файла `<base>\l4desk\state\ffmpeg_state.json` (если файл существует: PID, state, `stream_instance_id`).
  - Отображать эти данные в статусе службы (например: `L4Desk: RUNNING (PID 1234), FFmpeg: RUNNING (PID 5678, stream_abc)`).

### 3.2 Упаковка пакета `ffmpeg.zip`
Файлы: каталог `tools/ffmpeg/`, скрипт `tools/ffmpeg/pack_ffmpeg.cmd` (или интеграция в `tools/l4superv/pack_zip.cmd`).
- **x64 дистрибутив**:
  - Источник: `D:\ffmpeg` (переопределяется через переменную окружения `FFMPEG_SRC_X64`, по умолчанию `D:\ffmpeg\bin`).
  - Включать **только**: `bin\ffmpeg.exe`, зависимые библиотеки DLL (`avcodec-63.dll`, `avdevice-63.dll`, `avfilter-12.dll`, `avformat-63.dll`, `avutil-61.dll`, `swresample-7.dll`, `swscale-10.dll`) и `LICENSE`.
  - Проверить зависимости через `dumpbin /dependents` — не включать `ffprobe.exe`, `ffplay.exe`, `doc\`, `include\`, `lib\`, `presets\`.
  - Зафиксировать в README, что gyan.dev 9.x требует UCRT (на Windows 7 x64 требуется установленный UCRT update KB2999226 / `ucrtbase.dll`).
- **x86 дистрибутив**:
  - Задокументировать источник и требования в `tools/ffmpeg/SOURCES.md`: предпочтителен единый статический `ffmpeg.exe` (без внешних DLL), собранный для win32 (BtbN / community win32 static / архивные Zeranoe 4.4.x static для совместимости с Windows 7 / POSReady 7).
  - Если локальный бинарник x86 отсутствует на машине сборки: задокументировать URL, версию и SHA-256; создать в архиве заглушку/манифест с описанием, чтобы инсталлятор корректно обрабатывал отсутствие x86 бинарника (согласно §6.1 базового контракта).
- **Структура архива `ffmpeg.zip`**:
  ```
  ffmpeg\x64\ffmpeg.exe
  ffmpeg\x64\*.dll
  ffmpeg\x64\LICENSE
  ffmpeg\x86\ffmpeg.exe (при наличии)
  ffmpeg\x86\LICENSE
  ffmpeg\ffmpeg.sha256     (манифест контрольных сумм relative-path -> sha256)
  ffmpeg\VERSION.txt       (версии и источники по архитектурам)
  ffmpeg\README.md
  ```
- **Сборка архива**:
  - `pack_ffmpeg.cmd` выполняет staging в `tools/obj/staging_ffmpeg/` и архивацию через PowerShell `Compress-Archive` в `tools/dist/ffmpeg.zip`.
  - Генерация контрольной суммы `tools/dist/ffmpeg.zip.sha256`.

### 3.3 Обновление инсталлятора `l4install`
Файлы: `tools/l4superv/src/installer_main.c`, `l4install_run.cmd`.
- Инсталлятор ищет `ffmpeg.zip` рядом с `l4install.exe` (как и `tools.zip`), поддерживает опции `--ffmpeg-zip <path>`, `--skip-ffmpeg`.
- Распаковка:
  - Использовать существующий `zip_extract_all(zip, dest, CURRENT_INSTALLER_ARCH, verbose)` для извлечения содержимого в `<dest>\ffmpeg` (с автоматическим преобразованием архитектурного пути: `ffmpeg\x64\ffmpeg.exe` → `<dest>\ffmpeg\ffmpeg.exe`).
  - Создавать каталог логов `<dest>\ffmpeg\log\`.
- Безопасное и атомарное обновление:
  - Перед распаковкой/перезаписью: проверить файл состояния `<dest>\l4desk\state\ffmpeg_state.json`. Если указан PID процесса, и процесс с таким PID активен (проверка `OpenProcess`/`GetProcessTimes` или `GetExitCodeProcess`) — вывести предупреждение об ошибке: трансляция активна, требуется остановить службу перед обновлением (`l4superv_stop.cmd`).
  - Атомарность: распаковка во временный каталог `<dest>\ffmpeg.new\`, проверка контрольных сумм по манифесту `ffmpeg.sha256`, переименование `<dest>\ffmpeg` в `<dest>\ffmpeg.old`, переименование `<dest>\ffmpeg.new` в `<dest>\ffmpeg`, удаление `<dest>\ffmpeg.old`.
  - При возникновении `ERROR_SHARING_VIOLATION` или повреждении архива — безопасный откат без повреждения рабочей версии.
- Вывод итоговой сводки: путь установки, архитектура, версия FFmpeg, статус проверки целостности.

---

## 4. Межкомпонентный контракт для агента PROMPT 4.2 (`l4desk`)

Твоя реализация обязана гарантировать для агента `l4desk`:
1. **Пути к файлам**:
   - Исполняемый файл: `<base>\ffmpeg\ffmpeg.exe` (по умолчанию `C:\l4tools\ffmpeg\ffmpeg.exe`).
   - Каталог логов: `<base>\ffmpeg\log\`.
   - Файл состояния (создаваемый `l4desk`): `<base>\l4desk\state\ffmpeg_state.json`.
2. **Схема `ffmpeg_state.json`** (для считывания инсталлятором и супервизором):
   ```json
   {
     "pid": 12345,
     "creation_time": 133500000000000000,
     "stream_instance_id": "stream_123",
     "session_id": 1,
     "mode": "desktop",
     "source_id": "disp:1a2b3c4d",
     "state": "running",
     "owner": "l4desk",
     "sn": "TERM001"
   }
   ```
3. **Управление жизненным циклом**:
   - `l4superv` гарантирует не менее 8 секунд grace-периода после сигнала `Global\L4Desk_Stop_<SN>` перед принудительным закрытием Job Object.

---

## 5. Критерии готовности

1. `l4superv` помещает процесс `l4desk` в Job Object с флагом `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`. При принудительном завершении `l4superv` дерево процессов автоматически закрывается ОС Windows.
2. Grace-период при остановке службы увеличен до ≥ 8 секунд.
3. Скрипт `pack_ffmpeg.cmd` собирает архив `tools/dist/ffmpeg.zip` (с манифестом `ffmpeg.sha256`, версиями и лицензиями).
4. `l4install` успешно распаковывает `ffmpeg.zip` в `C:\l4tools\ffmpeg\`, создает `C:\l4tools\ffmpeg\log\`, проверяет SHA-256 манифест.
5. Инсталлятор блокирует обновление при активном стриме (по `ffmpeg_state.json`) и защищен от `ERROR_SHARING_VIOLATION`.
6. Обе архитектуры (`l4superv`, `l4install`) компилируются под x86 и x64 через `build.cmd` без предупреждений и ошибок.
7. Обновлена документация: `tools/dist/README.md`, `tools/l4superv/README.md`, `docs/terminal-tools-user-guide.md`.

---

## 6. Отчёт по итогам

Предоставь краткий отчет:
- Изменения в `session_proc.c` и `orchestrator.c` (настройка Job Object, флаги, grace-таймаут).
- Изменения в `installer_main.c` (обработка `ffmpeg.zip`, атомарность, проверка занятости).
- Структура и размер сформированного `ffmpeg.zip`.
- Результаты тестов завершения дочерних процессов при остановке службы.
