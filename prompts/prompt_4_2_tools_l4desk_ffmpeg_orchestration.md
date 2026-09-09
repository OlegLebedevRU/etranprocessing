# PROMPT 4.2 — `tools/l4desk`: Инвентаризация дисплеев/камер, ввод (мышь/клавиатура) и оркестрация FFmpeg

Ты — Senior Windows/C системный инженер. Работаешь автономно в репозитории `D:\repo\platerra\Public\etranprocessing`, каталог `tools/l4desk/` (разрешён явно).
Серверную часть (MenuBuilder, app1, l4media) и службу `tools/l4superv/` **не изменяешь** — служба и инсталлятор FFmpeg подготовлены в PROMPT 4.1. Ты реализуешь прикладную сторону контракта управления трансляцией и удаленного ввода в интерактивной сессии пользователя.
Не трогай `FRONT/`, `BACK/`, `sqlFileExample/`, `stored-procedures/`, `MenuBuilder/`, `ProcessingBackend/`, `shared/`.

Обязательные правила репозитория (`AGENTS.md`, раздел «Key Rules for Tools Development» и «C / C++ Toolchains»):
- Каждый tool — изолированный подкаталог `C:\l4tools\<tool>\`; zero-dependency Win32/C, статическая сборка `/MT`, **обязательно x86 и x64** через `build.cmd` (MSVC Build Tools 2022, `vcvars32.bat`/`vcvars64.bat`; альтернативно CLion MinGW/CMake). Артефакты: `bin\x86\<tool>.exe`, `bin\x64\<tool>.exe`, `bin\<tool>.exe` (x86).
- MQTT-клиент `l4desk` имеет тип `svc_desk` (`AGENTS.md`, раздел MQTT-клиентов): presence/LWT в `dev/{SN}/ctl`, только localhost Mosquitto (`127.0.0.1:1883`), `client_id = svc_desk`. Не менять топики и payload presence-сценария.
- Целевые ОС: Windows 7 Embedded / POSReady 7 (x86) … Windows 10/11 (x64). Не ломать x86-сборку.
- Не выполняй деплой на терминалы/серверы без явного указания; работай локально, тестовые артефакты складывай в `tools/<tool>/obj` или `tools/dist`.

---

## 1. Контекст и предусловия

### 1.1 Предусловие: Инфраструктура PROMPT 4.1
- Бинарный пакет FFmpeg развёрнут в `<base>\ffmpeg\ffmpeg.exe` (по умолчанию `C:\l4tools\ffmpeg\ffmpeg.exe`), каталог логов `<base>\ffmpeg\log\`.
- Служба `l4superv` запускает `l4desk.exe` в активной пользовательской консольной сессии и удерживает его в `Job Object` (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`).
- При остановке/смене сессии `l4superv` выставляет событие `Global\L4Desk_Stop_<SN>` и предоставляет grace-период **≥ 8000 мс** для мягкой остановки FFmpeg перед закрытием джоба.

### 1.2 Существующий код `tools/l4desk/src`
- `main.c` — single-instance mutex `Local\L4Desk_SingleInstance`, `g_stop_event`, поток `supervisor_stop_watcher_thread` (`Global\L4Desk_Stop_<SN>`), `SetConsoleCtrlHandler`, `mqtt_client_run(&config, g_stop_event)`.
- `config.c/.h` — `L4DeskConfig` (`mqtt_host 127.0.0.1`, `mqtt_port 1883`, `proxy_http_port 18443`, `client_id svc_desk`, `sn`, `presence_interval_sec 30`, `log_file C:\l4tools\l4desk\log\l4desk.log`, `run_mode`, `console_mode`).
- `mqtt_protocol.c/.h`, `mqtt_client.c/.h` — минимальная реализация MQTT 3.1.1 (CONNECT/PUBLISH/SUBSCRIBE/PUBACK/PING, LWT), цикл приёма, подписка `srv/<SN>/ctl`, публикация `dev/<SN>/ctl`, presence.
- `ctl_protocol.c/.h` — envelope v1: `presence`, `ack`/`nack`, обработка команд `pointer_move`, `mouse_click`.
- `dedup_cache.c/.h` — идемпотентность по `command_id`; `json_min.c/.h` — мини-JSON; `log.c/.h`; `sn_discovery.c/.h`.
- `desktop_state.c/.h` — `desktop_is_interactive_available`, `desktop_get_screen_metrics` (виртуальный экран целиком), `desktop_get_current_session_id`.
- `input_inject.c/.h` — `input_inject_move(x,y)`, `input_inject_click(x,y)` (`SendInput` по координатам виртуального экрана). Клавиатура отсутствует.

### 1.3 `tools/leo4proxy`
`--rtp-tunnel`: принимает RTP/RTCP на UDP `127.0.0.1:5004/5005`, туннелирует в `l4media-nginx:8443` (mTLS, L4RTP/1).

---

## 2. Утверждённые архитектурные решения

1. **Никакого отдельного сервиса/бинарника для FFmpeg.** Orchestration полностью встроена в `l4desk`; `ffmpeg.exe` — дочерний процесс `l4desk`, без автозапуска, без своей службы.
2. **Канал управления** — существующий control-plane `srv/<SN>/ctl` → `dev/<SN>/ctl` (envelope v1, `command_id`, `ack`/`nack`, dedup). Топик presence `dev/{SN}/ctl` (`svc_desk`) сохраняется и расширяется.
3. **Режимы источника**: `desktop` (один выбранный display), `usb-camera` (одна камера), `stopped`. На терминале может быть активна **только одна** трансляция одновременно.
4. **Controlled switch**: при получении `stream_start` во время активного другого источника — последовательное контролируемое переключение (stop текущего → подтверждённый exit процесса → start нового), ответ `ack.result=switched`.
5. **Удаленный ввод (Remote input)** — мышь и клавиатура разрешены **только** в режиме `desktop`, только для выбранного display, только при совпадении `lease_id` + `desktop_id` + `stream_instance_id` с активной трансляцией и при `policy == input`.
6. `l4desk` запускает **только** `<base>\ffmpeg\ffmpeg.exe` (по умолчанию `C:\l4tools\ffmpeg\ffmpeg.exe`). Запрещены `PATH`, текущая директория, произвольные пути или аргументы из сети.

---

## 3. Контракт протокола `ctl` (envelope v1) — терминальная сторона

Реализуется в `ctl_protocol.c/.h`. Имена полей — snake_case. Все входящие команды проходят проверку `dedup_cache` по `command_id`.

### 3.1 Server → Terminal (`srv/<SN>/ctl`)
- `inventory_get {command_id, lease_id?}` → `ack` + блок `inventory`.
- `stream_start {command_id, lease_id, mode:"desktop"|"usb-camera", source_id, profile, stream_instance_id}`:
  - Ответ `ack.result = started | already_running | switched` (+ `stream_instance_id`, `state`) или `nack`.
  - `already_running` — если уже запущен тот же `source_id` + `stream_instance_id`.
  - Другой `source_id` или другой `stream_instance_id` → controlled switch.
- `stream_stop {command_id, lease_id, stream_instance_id?}`:
  - Ответ `ack.result = stopped | already_stopped`.
  - `ack` публикуется **только после фактического завершения процесса** и закрытия портов.
- `pointer_move` / `mouse_click {command_id, lease_id, desktop_id, stream_instance_id, x:float, y:float, ...}`:
  - Координаты `[0.0, 1.0]` нормализованы к кадру выбранного экрана; маппинг в виртуальные координаты экрана выполняет `l4desk`.
- `key_event {command_id, lease_id, desktop_id, stream_instance_id, kind:"down"|"up"|"press", vk:int, text?:str}`:
  - Валидация по whitelist виртуальных кодов клавиш `vk` (буквы, цифры, Enter, Esc, Tab, Backspace, пробел, стрелки, F1–F12). Запрещены потенциально деструктивные комбинации (Win, Ctrl+Alt+Del).

### 3.2 Terminal → Server (`dev/<SN>/ctl`)
- **Расширенный `presence`** (retain, публикация каждые `presence_interval_sec` и при любом изменении состояния/инвентаря):
  - `session_id`, `screen{virtual_x, virtual_y, virtual_width, virtual_height}`.
  - `inventory`:
    - `displays[]`: `desktop_id` (`disp:<hex>`), `name` (`\\.\DISPLAYn`), `primary` (bool), `x, y, width, height` (могут быть отрицательными), `session_id`, `policy: "input"|"view"|"denied"`.
    - `cameras[]`: `camera_id` (`cam:<hex>`), `name` (FriendlyName), `available` (bool).
  - `stream`: `state`, `mode`, `source_id`, `stream_instance_id`, `profile`, `reason`, `ffmpeg_pid`, `started_at`, `restart_count`.
- **`stream_event {stream_instance_id, state, reason, timestamp}`**:
  - Публикуется с QoS 1 при каждом изменении состояния трансляции.
- **Коды `nack.code`**:
  - `lease_mismatch`, `desktop_mismatch`, `stream_mismatch`, `source_not_allowed`, `source_unavailable`, `session_unavailable`, `busy_transition`, `ffmpeg_missing`, `ffmpeg_integrity`, `input_not_allowed_in_camera_mode`, `invalid_profile`.
- **Состояния `stream.state`**:
  - `stopped`, `starting`, `running`, `stopping`, `restarting`, `failed`, `source_unavailable`, `session_unavailable`.

---

## 4. Архитектура и новые модули `l4desk`

### 4.1 Инвентаризация дисплеев и камер (`display_inventory.c/.h`)
- **Дисплеи**:
  - Перечисление через `EnumDisplayMonitors` + `GetMonitorInfoW` (`MONITORINFOEXW`) + `EnumDisplayDevicesW(..., EDD_GET_DEVICE_INTERFACE_NAME)`.
  - Стабильный `desktop_id`: вычисление хэша (FNV-1a 32-bit hex) от `DeviceID`/интерфейсного имени монитора (`\\?\DISPLAY#...`), fallback — от `DeviceString` + экранных координат. Формат: `disp:<8 hex chars>`. Системное имя `\\.\DISPLAYn` отправляется только в поле `name`.
  - Учет отрицательных координат (монитор слева/сверху от primary) в координатах virtual screen.
  - `primary` определяется по флагу `MONITORINFOF_PRIMARY`.
- **Камеры**:
  - Перечисление видеоустройств через DirectShow (`ICreateDevEnum` / `CLSID_VideoInputDeviceCategory`).
  - Стабильный `camera_id`: хэш FNV-1a от DirectShow `DevicePath` (`@device:pnp:\\?\usb#...`). Формат: `cam:<8 hex chars>`. Поле `name` — `FriendlyName`.
  - Для запуска FFmpeg использовать детерминированное имя устройства: `-f dshow -i video=@<DevicePath>`.
- **Локальная политика**:
  - Чтение конфигурационного файла `C:\l4tools\l4desk\l4desk_policy.ini`:
    - `[displays]` — `allow=*|disp:...`, `view_only=disp:...`, `deny=disp:...` (по умолчанию все дисплеи `input`).
    - `[cameras]` — `allow=*|cam:...`, `deny=cam:...` (по умолчанию все камеры разрешены).
    - `[profiles]` — перечень разрешенных профилей (по умолчанию `default`, `low`).
- **Отслеживание изменений**:
  - Перечитывание инвентаря по оконным событиям `WM_DISPLAYCHANGE`/`WM_DEVICECHANGE` (через скрытое message-only окно) или периодическим таймером (≤ 10 с). При изменении — немедленная публикация обновленного `presence`.

### 4.2 Активная привязка и удаленный ввод (`input_inject.c/.h`)
- Хранение единой структуры активной трансляции: `stream_instance_id`, `mode`, `source_id`, `desktop_rect`, `session_id`, `lease_id`, `state`, `ffmpeg_pid`, `ffmpeg_start_time` (FILETIME), `started_at`, `restart_count`.
- **Валидация команд ввода**:
  - `pointer_move`, `mouse_click`, `key_event` исполняются **только если**:
    `state == running`, `mode == desktop`, `lease_id` совпадает, `desktop_id` совпадает, `stream_instance_id` совпадает, и политика экрана `policy == input`. Иначе возвращается `nack` с точным кодом ошибки.
- **Маппинг координат мыши**:
  - Перевод нормализованных координат кадра `(nx, ny)` в абсолютные виртуальные координаты экрана:
    `target_x = desktop_rect.x + (int)(nx * desktop_rect.width)`
    `target_y = desktop_rect.y + (int)(ny * desktop_rect.height)`
  - Нормализация в диапазон `0..65535` относительно виртуального экрана целиком (`SM_XVIRTUALSCREEN`, `SM_YVIRTUALSCREEN`, `SM_CXVIRTUALSCREEN`, `SM_CYVIRTUALSCREEN`) с корректным учетом отрицательного origin.
- **Клавиатурный ввод**:
  - Функция инжекции клавиш: генерация событий `SendInput` с `KEYEVENTF_KEYUP` / `KEYEVENTF_UNICODE`.
  - Проверка входящего `vk` по белому списку допустимых клавиш.

### 4.3 Оркестрация процесса FFmpeg (`ffmpeg_supervisor.c/.h`, `ffmpeg_cmdline.c/.h`)
- **Потокобезопасность и сериализация**:
  - Все операции жизненного цикла стрима сериализованы через единый мьютекс/критическую секцию.
  - Глобальный именованный мьютекс `Global\L4Desk_FFmpeg_<SN>` (гарантия единственности процесса FFmpeg на терминале).
  - Создание собственного вложенного Job Object для FFmpeg (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`) с fallback-контролем по дереву PID для Windows 7.
- **Скрытый запуск процесса**:
  - `CreateProcessW` с флагами `CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED`.
  - Флаги окна: `STARTF_USESHOWWINDOW` (`SW_HIDE`), `STARTF_USESTDHANDLES`.
  - Перенаправление stdin в pipe (для soft-остановки символом `q\n`), перенаправление stdout/stderr в pipe с асинхронным чтением отдельным потоком.
  - Логирование в ротационный лог-файл `<base>\ffmpeg\log\ffmpeg_<stream_instance_id>.log` (ротация по 5 МБ, до 5 файлов). Запрещены любые вызовы через `cmd.exe` или `ShellExecute`.
- **Командная строка FFmpeg (строгий whitelist)**:
  - Таргет: `-c:v libx264 -preset ultrafast -tune zerolatency -b:v <bitrate> -maxrate <maxrate> -bufsize <buf> -g <fps*2> -pix_fmt yuv420p -r <fps> -f rtp rtp://127.0.0.1:5004?rtcpport=5005`.
  - Метаданные владельца: `-metadata comment=l4desk:<stream_instance_id>`.
  - Экран: `-f gdigrab -framerate <fps> -offset_x <x> -offset_y <y> -video_size <w>x<h> -i desktop -draw_mouse 1`.
  - Камера: `-f dshow -rtbufsize 64M -framerate <fps> -video_size <w>x<h> -i video=@<DevicePath>`.
  - Прогресс: `-progress pipe:N` (или stderr pipe) для health-check.
- **Двухфазная остановка**:
  1. Фаза 1 (Soft stop): запись команды `q\n` в stdin-пайп (или `GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT)`) и ожидание `WaitForSingleObject(hProcess, 5000)`.
  2. Фаза 2 (Hard kill): если процесс не вышел за 5 секунд — вызов `TerminateJobObject`/`TerminateProcess`.
  3. Ожидание фактического завершения (`WAIT_OBJECT_0`) и закрытия сокетов UDP 5004/5005. Публикация статуса `stopped` в `ack` и `presence` только **после полного освобождения ресурсов**.
- **Controlled switch**:
  - Переход: `stopping(old) → stopped → starting(new) → running`. Публикуется единый `ack.result = switched`. При ошибке запуска нового источника — статус `failed` с сохранением `reason`, старый источник не перезапускается.
- **Политика рестартов и Health-check**:
  - Разделение штатной остановки (`stop_requested`) от сбоя/крэша.
  - При неожиданном падении: экспоненциальный backoff (2, 4, 8, 16, 30 с), ограничение до 5 рестартов за 10 минут, после чего переход в `failed` (`reason=restart_limit`).
  - При исчезновении источника (`source_unavailable`) — немедленная остановка цикла рестартов до повторного обнаружения устройства в инвентаре.
  - Health-check (таймер 2–5 с):
    - Процесс активен (проверка совпадения PID и `GetProcessTimes`).
    - Парсинг прогресса (`out_time_ms` / `frame=`): при отсутствии прогресса более 10 секунд — статус `stall` и перезапуск.
    - Проверка локального порта `leo4proxy` (UDP 5004/5005).
    - Проверка активной пользовательской сессии (`desktop_is_interactive_available`). При блокировке экрана для режима `desktop` — статус `session_unavailable`.
- **Файл состояния и Reconciliation при старте**:
  - Запись текущего состояния в `<base>\l4desk\state\ffmpeg_state.json`:
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
  - При старте `l4desk`: чтение `ffmpeg_state.json`. Если обнаружен процесс с указанным PID и временем создания, чей командлайн содержит маркер `l4desk:<stream_instance_id>`, выполнить двухфазную остановку этого процесса перед началом работы. Запрещено завершать чужие процессы `ffmpeg.exe` через `taskkill`.
- **Реакция на остановку службы**:
  - При получении `Global\L4Desk_Stop_<SN>` сначала выполняется мягкая двухфазная остановка FFmpeg, затем штатный выход самого `l4desk`.

---

## 5. Требования к тестированию

1. **Автоматические Unit-тесты (`tools/l4desk/tests/`)**:
   - Парсинг и формирование всех сообщений протокола `ctl` (включая все новые коды `nack`).
   - Валидация вычисления `desktop_id` и `camera_id` (стабильность хэша).
   - Маппинг координат мыши с учетом отрицательного origin мониторов.
   - Whitelist клавиатурных кодов.
   - Тестирование конечного автомата оркестратора с мок-процессом `fake_ffmpeg.exe` (эмуляция успешного выхода по `q`, зависания, краша, stall).
   - Reconciliation по файлу состояния (проверка защиты от PID reuse).
2. **Интеграционный сценарий (Python/PowerShell)**:
   - Подключение к локальному Mosquitto.
   - Проверка последовательности команд: `inventory_get` → `stream_start` → `already_running` → switch `desktop` ↔ `camera` → `stream_stop`.
   - Проверка возврата `nack` при несовпадении `lease_id` / `desktop_id`.
   - Проверка рестарта при принудительном `taskkill` дочернего FFmpeg.
   - Проверка освобождения UDP 5004/5005 после остановки стрима.

---

## 6. Критерии готовности

1. `ffmpeg.exe` запускается только из `<base>\ffmpeg\ffmpeg.exe` как дочерний процесс `l4desk`, без всплывающих окон консоли.
2. Реализована инвентаризация мониторов (со стабильными id и отрицательными координатами) и DirectShow-камер.
3. Команды ввода отклоняются при малейшем несовпадении `lease_id`, `desktop_id`, `stream_instance_id` или при неактивном стриме. В режиме камеры ввод отклоняется.
4. Корректно отрабатывают controlled switch, health-check, обнаружение stall и reconciliation после аварийного перезапуска.
5. `stopped` публикуется строго после реального завершения процесса и закрытия UDP-портов.
6. Модуль компилируется под x86 и x64 (`build.cmd`), unit-тесты успешно проходят.
7. Обновлена документация: `tools/l4desk/README.md`, `CHANGELOG.md`.

---

## 7. Отчёт по итогам

Предоставь подробный отчет:
- Реализованная схема состояний стрима и перечень кодов ошибок `nack`.
- Описание работы инвентаря дисплеев и DirectShow-камер.
- Механизм двухфазной остановки FFmpeg и результаты тестирования soft/hard stop.
- Результаты прогона unit и интеграционных тестов.
