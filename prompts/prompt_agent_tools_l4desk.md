# PROMPT AGENT 3 — `tools/l4desk`: Протокол `ctl v1`, фиксация H.264 baseline/level 3.1 под Janus и сборка бинарников

Ты — Senior Windows / C Systems инженер. Работаешь автономно в репозитории `D:\repo\platerra\Public\etranprocessing`, каталог `tools/l4desk/` (разрешён явно).
Твоя цель — синхронизировать терминальную сторону протокола `ctl v1` (`stream_event`, `key_event`, координаты мыши), явно зафиксировать в аргументах FFmpeg профиль H.264 Baseline Level 3.1 под SDP-заявку Janus WebRTC, устранить расхождения и собрать чистые бинарники `l4desk` под x86 и x64.

### ЖЕСТКИЕ ОГРАНИЧЕНИЯ И ПРАВИЛА
1. **Tools suite на данном этапе НЕ ПЕРЕПАКОВЫВАЕМ**:
   - **СТРОГО ЗАПРЕЩЕНО** запускать `pack_zip.cmd`, модифицировать `tools/dist`, `tools.zip`, инсталлятор `l4install` или артефакты staging `l4superv`.
   - Работа ведется **только** внутри каталога `tools/l4desk/`.
   - Сборка выполняется исключительно скриптом `tools/l4desk/build.cmd`.
2. **Изоляция других подсистем**:
   - Не трогать серверные и веб-каталоги `FRONT/`, `BACK/`, `sqlFileExample/`, `stored-procedures/`, `MenuBuilder/`, `ProcessingBackend/`, `shared/`, `l4media/`.
3. **Требования к C-коду (`AGENTS.md`)**:
   - Чистый Win32/C, без внешних runtime-зависимостей, статическая линковка `/MT`, предупреждения `/W4`.
   - Обязательная поддержка двух архитектур: **x86** (Windows 7 SP1+ compatible, `/SUBSYSTEM:CONSOLE,6.01`) и **x64**.
   - MQTT-клиент имеет тип `svc_desk` (`dev/{SN}/ctl`, `127.0.0.1:1883`). Не менять топики presence!

---

## 1. Контекст и выявленные расхождения

### 1.1 Аргументы FFmpeg и профиль H.264 (`tools/l4desk/src/ffmpeg_cmdline.c`)
- **Проблема:** Серверная часть (`MenuBuilder/backend/app/routers/video.py`) регистрирует Streaming mountpoint в Janus со строгой заявкой:
  ```text
  videofmtp="profile-level-id=42e01f;packetization-mode=1"
  ```
  Это Constrained Baseline Profile, Level 3.1 с non-interleaved RTP-пакетизацией.
  Janus **не производит транскодирование** входящего RTP-потока.
  В то же время генератор командной строки в `ffmpeg_cmdline.c` использует:
  ```text
  -c:v libx264 -preset ultrafast -tune zerolatency
  ```
  без указания флагов `-profile:v` и `-level`. При кодировании x264 может автоматически выбрать High profile или Level 4.x (в зависимости от разрешения и версии библиотеки), что в некоторых браузерах (Safari, строгие сборки WebRTC) приводит к невозможности аппаратного/программного декодирования и черному экрану.
- **Решение:** Явно добавить в командную строку FFmpeg:
  ```text
  -profile:v baseline -level:v 3.1 -x264-params bframes=0:force-cfr=1
  ```
  а также убедиться, что GOP (`-g`) установлен строго в `params.fps * 2` с `-keyint_min %d -sc_threshold 0`.

### 1.2 Формат `stream_event` (`tools/l4desk/src/ctl_protocol.c`)
- В функции `ctl_build_stream_event_payload`:
  Убедиться, что формируемый JSON строго валиден и содержит все необходимые поля:
  ```c
  "{\"v\":1,\"type\":\"stream_event\",\"sn\":\"%s\",\"stream_instance_id\":\"%s\","
  "\"state\":\"%s\",\"reason\":\"%s\",\"timestamp\":\"%s\"}"
  ```
- Гарантировать, что `state` использует только утвержденный enum:
  `stopped`, `starting`, `running`, `stopping`, `restarting`, `failed`, `source_unavailable`, `session_unavailable`.

### 1.3 Обработка клавиатурного ввода `key_event` (`src/ctl_protocol.c`, `src/input_inject.c`)
- Проверить обработку команды `key_event`:
  - Проверка активного стрима: `g_active_stream.state == STREAM_STATE_RUNNING` и `g_active_stream.mode == STREAM_MODE_DESKTOP`.
  - Проверка совпадения `lease_id`, `desktop_id`, `stream_instance_id`. Если не совпадают — `nack` (`lease_mismatch` / `desktop_mismatch` / `stream_mismatch`).
  - Проверка политики выбранного экрана: `policy == POLICY_INPUT`. Если просмотр — `source_not_allowed`.
  - Whitelist виртуальных кодов `vk`:
    - 0x08 (Backspace), 0x09 (Tab), 0x0D (Enter), 0x1B (Esc), 0x20 (Space), 0x2E (Delete)
    - 0x25..0x28 (Стрелки)
    - 0x30..0x39 (0-9)
    - 0x41..0x5A (A-Z)
    - 0x70..0x7B (F1-F12)
    Запрещены деструктивные клавиши (Win, Ctrl+Alt+Del).
  - Вызов инъекции клавиши через `SendInput` (`KEYEVENTF_KEYUP` для `up`, обычный для `down`, последовательность down+up для `press`).

### 1.4 Координаты мыши (`src/input_inject.c`, `src/ctl_protocol.c`)
- Команды `pointer_move` и `mouse_click` могут приходить как в диапазоне целых чисел `0..65535`, так и в нормализованном float `0.0..1.0`.
- Убедиться, что функция маппинга координат в `input_inject.c`:
  1. Корректно определяет шкалу: если значение `> 1.0` (или целое число `> 1`), масштабирует от `65535.0f`, иначе от `1.0f`.
  2. Переводит нормализованные координаты кадра экрана в виртуальные координаты с учетом смещения монитора (`desktop_rect.x`, `desktop_rect.y`) и возможных отрицательных координат левых/верхних мониторов:
     ```c
     int target_x = desktop_rect.x + (int)(norm_x * desktop_rect.width);
     int target_y = desktop_rect.y + (int)(norm_y * desktop_rect.height);
     ```
  3. Для `SendInput` выполняет нормализацию относительно всего виртуального экрана (`SM_XVIRTUALSCREEN`, `SM_YVIRTUALSCREEN`, `SM_CXVIRTUALSCREEN`, `SM_CYVIRTUALSCREEN`).

---

## 2. Задачи разработки

### 2.1 Обновление аргументов FFmpeg в `src/ffmpeg_cmdline.c`
1. В функциях `ffmpeg_build_desktop_cmdline` и `ffmpeg_build_camera_cmdline`:
   Добавить флаги:
   ```c
   "-c:v libx264 -preset ultrafast -tune zerolatency "
   "-profile:v baseline -level 3.1 -x264-params bframes=0:force-cfr=1 "
   "-b:v %hs -maxrate %hs -bufsize %hs -g %d -keyint_min %d -sc_threshold 0 -pix_fmt yuv420p -r %d "
   ```
2. Убедиться, что `keyint_min` равен `gop` (чтобы все GOP были фиксированной длины), а `-sc_threshold 0` отключает вставку внеплановых I-кадров при резкой смене сцены.
3. Проверить расчет буферов и отсутствие переполнения `out_cmdline` (проверить размер `max_chars`).

### 2.2 Проверка и ужесточение протокола в `src/ctl_protocol.c`
1. Убедиться, что `ctl_handle_command` обрабатывает `key_event` строго по спецификации.
2. Проверить, что отправка `stream_event` происходит при переходах:
   - `starting` → `running`
   - `running` → `stopping`
   - `stopping` → `stopped`
   - `running` → `restarting` (при обнаружении сбоя процесса)
   - переход в `failed`
3. Убедиться, что `ack` на команду `stream_stop` отправляется **только после** фактического закрытия дескриптора процесса и освобождения сокетов.

### 2.3 Сборка через `tools/l4desk/build.cmd`
1. Запустить сборку:
   ```cmd
   cd D:\repo\platerra\Public\etranprocessing\tools\l4desk
   build.cmd all
   ```
2. Убедиться, что:
   - x86 сборка прошла успешно: `bin\x86\l4desk.exe`
   - x64 сборка прошла успешно: `bin\x64\l4desk.exe`
   - Файл по умолчанию скопирован: `bin\l4desk.exe`
   - Сборка компилируется без предупреждений (`/W4`).
3. **Напоминание:** Никаких операций со staging, `tools/dist` или `pack_zip.cmd` не выполнять!

---

## 3. Документация и отчетность

1. Обновить `tools/l4desk/README.md`:
   - Отразить поддержку H.264 Baseline Level 3.1 под Janus streaming plugin.
   - Зафиксировать схему валидации `key_event` и координатную модель.
2. Обновить `tools/l4desk/CHANGELOG.md` с описанием внесенных изменений.
3. Сформировать Markdown-отчёт:
   - Список модифицированных файлов исходного кода.
   - Итоговая строка команды запуска FFmpeg для desktop и camera.
   - Лог успешного выполнения `build.cmd all` с размерами полученных исполняемых файлов `bin\x86\l4desk.exe` и `bin\x64\l4desk.exe`.
