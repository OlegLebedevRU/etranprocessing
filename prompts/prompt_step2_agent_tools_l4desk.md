# PROMPT AGENT 2.2 — `tools/l4desk`: Замкнутый recovery-loop супервизора FFmpeg и локальный lease watchdog (fail-closed)

Ты — Senior Windows / C Systems инженер. Работаешь автономно в репозитории `D:\repo\platerra\Public\etranprocessing`, каталог `tools/l4desk/` (разрешён явно).

Твоя цель — реализовать в терминальном агенте `l4desk` **замкнутый цикл восстановления (recovery-loop)** процесса захвата FFmpeg (задача R4 / P1) и **локальный сторожевой таймер аренды (watchdog, fail-closed)** (задача R3 / P0), исключающий бесконечную фоновую трансляцию экрана при потере связи с сервером или истечении срока аренды.

### ЖЕСТКИЕ ОГРАНИЧЕНИЯ И ПРАВИЛА
1. **Tools suite на данном этапе НЕ ПЕРЕПАКОВЫВАЕМ**:
   - **СТРОГО ЗАПРЕЩЕНО** запускать `pack_zip.cmd`, модифицировать `tools/dist`, `tools.zip`, инсталлятор `l4install` или артефакты staging `l4superv`.
   - Работа ведется **только** внутри каталога `tools/l4desk/`.
   - Сборка выполняется исключительно скриптом `tools/l4desk/build.cmd all`.
2. **Изоляция других подсистем**:
   - Не трогать серверные и веб-каталоги `FRONT/`, `BACK/`, `sqlFileExample/`, `stored-procedures/`, `MenuBuilder/`, `ProcessingBackend/`, `shared/`, `l4media/`.
3. **Требования к C-коду (`AGENTS.md`)**:
   - Чистый Win32/C, без внешних runtime-зависимостей, статическая линковка `/MT`, предупреждения `/W4`.
   - Обязательная поддержка двух архитектур: **x86** (Windows 7 SP1+ compatible, `/SUBSYSTEM:CONSOLE,6.01`) и **x64**.
   - MQTT-клиент имеет тип `svc_desk` (`dev/{SN}/ctl`, `127.0.0.1:1883`). Не изменять топики presence!

---

## 1. Контекст и архитектурная проблема

### 1.1 Незамкнутый цикл восстановления супервизора (R4)
В `tools/l4desk/src/ffmpeg_supervisor.c`:
- При непредвиденном завершении FFmpeg или stall (>10 с без логов/прогресса) супервизор обнаруживает сбой, устанавливает состояние `g_sup.state = "restarting"` и отправляет событие:
  ```c
  notify_stream_event(cur_stream_id, "restarting", "unexpected_exit");
  ```
- **Проблема:** Фактического автоматического перезапуска процесса FFmpeg после перехода в `restarting` **не происходит**. Состояние зависает в `restarting`, пока оператор вручную не остановит и не запустит стрим заново.
- **Решение:** Реализовать рабочий reconciliation-loop с экспоненциальным backoff, jitter, учетом лимита перезапусков (restart budget: не более 5 попыток за 10 минут) и корректным перезапуском процесса FFmpeg с активными параметрами (`mode`, `source_id`, `profile`, `stream_instance_id`).

### 1.2 Отсутствие локального контроля истечения аренды (fail-closed, R3)
- **Проблема:** Если связь терминала с сервером обрывается (разрыв сети, падение MQTT-моста leo4proxy или сбой app1), процесс FFmpeg продолжает захватывать и передавать экран бесконечно, а зажатые клавиши/кнопки мыши могут остаться в нажатом состоянии.
- **Решение:** Локальный lease watchdog. Каждая команда (`stream_start`, `pointer_move`, `mouse_click`, `key_event`) несет `expires_at_ms`. Если локальное время превышает `expires_at_ms + 5000` (grace 5 с) и продления не поступило, супервизор обязан принудительно остановить FFmpeg, отпустить все клавиши и перевести состояние в `stopped` (`reason="lease_expired"`).

---

## 2. Задачи разработки

### 2.1 Реализация Recovery-Loop в `src/ffmpeg_supervisor.c`
1. **Сохранение параметров активного стрима**:
   - Убедиться, что структура `FFmpegSupervisorState` сохраняет параметры активной трансляции:
     - `mode` (`desktop` или `usb-camera`)
     - `source_id` (например, `disp:3f8a12bc` или `cam:12345678`)
     - `profile` (`default` или `low`)
     - `stream_instance_id`
     - `lease_id`
     - `next_restart_time` (время следующей попытки запуска)
2. **Логика автоматического перезапуска в `ffmpeg_supervisor_check()`**:
   - При обнаружении падения процесса или stall:
     - Очистить дескрипторы старого процесса (`cleanup_process_handles_internal()`).
     - Проверить бюджет: если за текущее 10-минутное скользящее окно выполнено более 5 попыток (`restart_count > 5`), перевести состояние в `"failed"`, `reason = "restart_limit"`, отправить `stream_event` и прекратить попытки.
     - Если лимит не превышен:
       - Рассчитать задержку backoff с псевдослучайным jitter: например, `delay_sec = min(30, (1 << restart_count)) + (rand() % 1000) / 1000.0`.
       - Установить `next_restart_time = now + delay_sec`.
       - Установить `state = "restarting"`.
   - В периодическом тике супервизора (`ffmpeg_supervisor_check()`):
     - Если `state == "restarting"` и `now >= next_restart_time`:
       - Вызвать запуск нового процесса FFmpeg с сохраненными параметрами источника и профиля.
       - При успешном старте: обновить `hProcess`, `ffmpeg_pid`, `ffmpeg_start_time`, перевести состояние в `"running"`, отправить `stream_event(state="running", reason="recovered")`.
       - При ошибке старта: инкрементировать `restart_count` и запланировать следующий повтор по backoff.
3. **Отмена перезапуска при `stream_stop`**:
   - Если пришла команда `stream_stop`, состояние переводится в `"stopping"`. Любой запланированный restart немедленно отменяется, а процесс (если успел запуститься) штатно останавливается через `q\n` или Job Object.
4. **Инвариант одного процесса**:
   - Никогда не допускать одновременной работы двух процессов FFmpeg. Перед спавном нового процесса убедиться, что Job Object и предыдущий `hProcess` закрыты.

### 2.2 Локальный сторожевой таймер аренды (Fail-Closed Watchdog)
1. В `src/ctl_protocol.c` и `src/ffmpeg_supervisor.c`:
   - При получении команд управления обновлять поле `g_sup.lease_expires_at_ms`.
   - В `ffmpeg_supervisor_check()` проверять:
     ```c
     uint64_t now_ms = get_current_time_ms();
     if (g_sup.state == FFMPEG_SUPERVISOR_STATE_RUNNING && g_sup.lease_expires_at_ms > 0) {
         if (now_ms > g_sup.lease_expires_at_ms + 5000) { /* 5s grace */
             log_warn("Local lease expired (now=%llu > expires=%llu + 5s). Fail-closed stop.",
                      now_ms, g_sup.lease_expires_at_ms);
             /* Освободить все удерживаемые клавиши ввода */
             input_release_all();
             /* Принудительно остановить процесс FFmpeg */
             ffmpeg_supervisor_stop("lease_expired");
             notify_stream_event(g_sup.stream_instance_id, "stopped", "lease_expired");
         }
     }
     ```
2. В `src/input_inject.c`:
   - Реализовать функцию `input_release_all(void)`:
     - Сбросить состояние зажатых клавиш клавиатуры (отправить `KEYEVENTF_KEYUP` для всех активных виртуальных кодов).
     - Сбросить зажатые кнопки мыши (`MOUSEEVENTF_LEFTUP`, `MOUSEEVENTF_RIGHTUP`, `MOUSEEVENTF_MIDDLEUP`).

---

## 3. Сборка и верификация

### 3.1 Сборка бинарников
В каталоге `tools/l4desk/`:
```cmd
cd D:\repo\platerra\Public\etranprocessing\tools\l4desk
build.cmd all
```
Убедись, что:
1. Сборка x86 прошла успешно: `bin\x86\l4desk.exe`
2. Сборка x64 прошла успешно: `bin\x64\l4desk.exe`
3. Основной файл скопирован: `bin\l4desk.exe`
4. Компилятор MSVC не выдает предупреждений (`/W4`).

**ВАЖНО:** Ни в коем случае не запускать `pack_zip.cmd` и не трогать `tools/dist/`!

### 3.2 Документация
1. Обновить `tools/l4desk/README.md`:
   - Описать логику замкнутого recovery-loop супервизора FFmpeg (restart budget 5/10 min, backoff + jitter).
   - Описать fail-closed поведение при истечении срока аренды (lease watchdog, grace 5 с, сброс зажатых клавиш).
2. Зафиксировать изменения в `tools/l4desk/CHANGELOG.md`.

---

## 4. Формат отчёта

По завершении сформируй Markdown-отчёт:
1. **Список измененных файлов** в `tools/l4desk/src/`.
2. **Описание реализации recovery-loop**: как рассчитывается backoff, как отменяется перезапуск при stop.
3. **Описание реализации watchdog**: логика fail-closed и функция `input_release_all`.
4. **Лог компиляции `build.cmd all`**: подтверждение успешной чистой сборки x86 и x64 с размерами бинарников.
