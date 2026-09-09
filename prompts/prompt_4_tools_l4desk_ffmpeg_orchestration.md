# Промпт для агента разработки tools-пакета: `l4desk` + FFmpeg orchestration, `l4superv`, упаковка и deployment FFmpeg

Ты — Senior Windows/C системный инженер. Работаешь автономно в репозитории `D:\repo\platerra\Public\etranprocessing`, каталог `tools/` (разрешён явно). Серверную часть (MenuBuilder, app1, l4media) **не изменяешь** — она делается отдельно; ты реализуешь терминальную сторону контракта §3 и считаешь его спецификацией. Не трогай `FRONT/`, `BACK/`, `sqlFileExample/`, `stored-procedures/`, `MenuBuilder/`, `ProcessingBackend/`, `shared/`.

Обязательные правила репозитория (`AGENTS.md`, раздел «Key Rules for Tools Development» и «C / C++ Toolchains»):
- Каждый tool — изолированный подкаталог `C:\l4tools\<tool>\`; zero-dependency Win32/C, статическая сборка `/MT`, **обязательно x86 и x64** через `build.cmd` (MSVC Build Tools 2022, `vcvars32.bat`/`vcvars64.bat`; альтернативно CLion MinGW/CMake). Артефакты: `bin\x86\<tool>.exe`, `bin\x64\<tool>.exe`, `bin\<tool>.exe` (x86).
- Любой новый/обновлённый tool или артефакт — стадия в `tools/l4superv/pack_zip.cmd` и обработка в `tools/l4superv/src/installer_main.c`.
- MQTT-клиент `l4desk` имеет тип `svc_desk` (`AGENTS.md`, раздел MQTT-клиентов): presence/LWT в `dev/{SN}/ctl`, только localhost Mosquitto, `client_id = svc_desk`. Не менять топики и payload presence-сценария.
- Целевые ОС: Windows 7 Embedded / POSReady 7 (x86) … Windows 10/11 (x64). Не ломать x86-сборку.
- Не выполняй деплой на терминалы/серверы без явного указания; работай локально, тестовые артефакты складывай в `tools/<tool>/obj` или `tools/dist`.

Если обнаружишь архитектурное противоречие, которое нельзя решить в рамках промпта, — зафиксируй в отчёте с предложением, не блокируя остальную работу.

---

## 1. Контекст: что уже существует (изучи в первую очередь)

### 1.1 Иерархия процессов и файлы
```
l4superv.exe (Windows-служба, Session 0)      tools/l4superv/src
  └─ l4desk.exe (интерактивная user session)   tools/l4desk/src
       └─ ffmpeg.exe (будет: дочерний процесс) C:\l4tools\ffmpeg\ffmpeg.exe (будет)
leo4proxy.exe (--rtp-tunnel, UDP 5004/5005 → mTLS) tools/leo4proxy
```

### 1.2 `tools/l4superv/src`
- `orchestrator.c` — watchdog: `sp_get_active_console_session()`; запуск `l4desk.exe` из `<base>\l4desk\l4desk.exe` (fallback `\x86\`, `\x64\`) с аргументами `cfg->l4desk_args` или `--run --presence-interval 30`, workdir `<base>\l4desk`; restart с backoff `g_l4desk_backoff_sec`; остановка при отсутствии/смене сессии: событие `Global\L4Desk_Stop_<SN>` → `sp_stop(&pi, evt, 3000)`; `orchestrator_get_l4desk_status(pid, session)`.
- `session_proc.c/.h` — `sp_enable_system_privileges`, `sp_get_active_console_session`, `sp_start_in_session` (`WTSQueryUserToken` + `CreateProcessAsUserW`, `lpDesktop=winsta0\default`, флаги `CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_BREAKAWAY_FROM_JOB`), `sp_is_alive`, `sp_stop` (событие → ожидание → `TerminateProcess`). **Job Object отсутствует.**
- `installer_main.c` (`l4install_x86/x64.exe`) — `zip_extract_all(zip, dest, CURRENT_INSTALLER_ARCH, verbose)` для `tools.zip` (ищет рядом с exe: `tools.zip`, `l4tools.zip`; `--zip`, `--dest`, default `C:\l4tools`), создание `l4desk\log`, регистрация службы, вывод сводки.
- `zip_extractor.c/.h` (miniz) — при распаковке пропускает каталог противоположной архитектуры и срезает сегмент `x86|x64` (`transform_arch_path`), т.е. `l4desk\x64\l4desk.exe` → `C:\l4tools\l4desk\l4desk.exe`.
- `pack_zip.cmd` — staging `obj\staging\<tool>\{x86,x64}\…` + `.cmd`/`README`/`CHANGELOG` → `bin\tools.zip` (PowerShell `Compress-Archive`) → копии `tools\l4superv\tools.zip`, `tools\tools.zip`, `tools\dist\tools.zip`; в `tools\dist` также `l4install_x86.exe|x64.exe` и `.cmd`. Есть `docs/terminal-tools-user-guide.md` в корне пакета.
- `config.c/.h`, `state_mgr.c`, `mosquitto_conf.c`, `proxy_client.c`, `hardware_fingerprint.c`, `service_mgr.c`, `supervisor_main.c`; скрипты `l4superv_*.cmd`, `l4install_run.cmd`; `CHANGELOG.md`, `README.md`.

### 1.3 `tools/l4desk/src`
- `main.c` — single-instance mutex `Local\L4Desk_SingleInstance`, `g_stop_event`, поток `supervisor_stop_watcher_thread` (`Global\L4Desk_Stop_<SN>`), `SetConsoleCtrlHandler`, `mqtt_client_run(&config, g_stop_event)`.
- `config.c/.h` — `L4DeskConfig` (`mqtt_host 127.0.0.1`, `mqtt_port 1883`, `proxy_http_port 18443`, `client_id svc_desk`, `sn`, `presence_interval_sec 30`, `keepalive_sec`, `reconnect_sec`, `log_file C:\l4tools\l4desk\log\l4desk.log`, `verbose`, `run_mode` (`--run`), `console_mode` (`--console`/`-f`)); `L4DESK_VERSION_STR "1.0.0"`.
- `mqtt_protocol.c/.h`, `mqtt_client.c/.h` — собственная минимальная реализация MQTT 3.1.1 (CONNECT/PUBLISH/SUBSCRIBE/PUBACK/PING, LWT), цикл приёма, подписка `srv/<SN>/ctl`, публикация `dev/<SN>/ctl`, presence.
- `ctl_protocol.c/.h` — envelope v1: `ctl_build_presence_payload(status, desktop_available, ScreenMetrics*)`, `ctl_build_ack_payload(command_id, lease_id, sn, terminal_time_ms)`, `ctl_build_nack_payload(..., code, message, ...)`, `ctl_handle_command(payload, own_sn, out_resp, ..., p_should_publish, p_qos)`; команды `pointer_move`, `mouse_click`.
- `dedup_cache.c/.h` — идемпотентность по `command_id`; `json_min.c/.h` — мини-JSON; `log.c/.h`; `sn_discovery.c/.h`.
- `desktop_state.c/.h` — `desktop_is_interactive_available`, `desktop_get_screen_metrics(ScreenMetrics*)` (виртуальный экран целиком: `virtual_x/y/width/height`), `desktop_get_current_session_id`.
- `input_inject.c/.h` — `input_inject_move(x,y)`, `input_inject_click(x,y)` (SendInput, абсолютные координаты виртуального экрана). Клавиатуры нет.
- `res/l4desk.rc`, `CMakeLists.txt`, `l4desk_console.cmd`, `README.md`, `CHANGELOG.md`.

### 1.4 `tools/leo4proxy`
`--rtp-tunnel`: принимает RTP/RTCP на UDP `127.0.0.1:5004/5005`, туннелирует в `l4media-nginx:8443` (mTLS, L4RTP/1). Примеры ручного запуска: `examples/ffmpeg_rtp_tunnel_example.cmd` (`ffmpeg -hide_banner -f dshow -i "<dev>" -c:v libx264 -preset ultrafast -tune zerolatency -b:v 800k -f rtp rtp://127.0.0.1:5004?rtcpport=5005`), `ffmpeg_stream_example.cmd`, `test_presence_suite.py`. Примеры ищут ffmpeg в `PATH`/`D:\ffmpeg`/`C:\ffmpeg` — это **запрещённые** источники для целевого решения (примеры допускается обновить на `C:\l4tools\ffmpeg\ffmpeg.exe`).

### 1.5 Дистрибутив FFmpeg на машине разработки
`D:\ffmpeg` — FFmpeg **9.0.1 full_build gyan.dev, shared, x64** (`bin\ffmpeg.exe`, `ffprobe.exe`, `ffplay.exe`, DLL: `avcodec-63`, `avdevice-63`, `avfilter-12`, `avformat-63`, `avutil-61`, `swresample-7`, `swscale-10`; `bin\` ≈ 239 МБ; каталоги `include\`, `lib\`, `doc\`, `presets\`, `LICENSE`, `README.txt`). Требует Windows 10+/UCRT. **x86-сборки в нём нет.**

### 1.6 Документация
`docs/etran_arch-remote-input-control.md` (планы разделения Video/Control plane, координатная модель), `docs/etran_arch-l4media-streaming-architecture.md`, `l4media/ARCHITECTURE.md`, `docs/terminal-tools-user-guide.md`, `D:\work\iot.leo4.ru\iot-rpc-rest-app\docs\remote-input-protocol.md` (envelope v1, QoS, presence stale 90 c — только для чтения).

---

## 2. Утверждённые архитектурные решения (не пересматривать)

1. **Никакого отдельного сервиса/бинарника для FFmpeg.** Orchestration встроена в `l4desk`; `ffmpeg.exe` — дочерний процесс `l4desk`, без автозапуска, без своей службы.
2. **Канал управления** — существующий control-plane `srv/<SN>/ctl` → `dev/<SN>/ctl` (envelope v1, `command_id`, `ack`/`nack`, dedup). RPC `tsk/req/rsp` (l4con) не используется. Новых топиков не вводить; presence `svc_desk` (`dev/{SN}/ctl`) не менять.
3. **`l4superv`** остаётся ответственным за: выбор активной интерактивной сессии, запуск `l4desk` в ней, restart, остановку и **гарантированную очистку дерева процессов** (Job Object) при logout, смене сессии, stop/restart службы.
4. **Режимы источника**: `desktop` (один выбранный display), `usb-camera` (одна камера), `stopped`. Одна трансляция на терминал. При `stream_start` во время активного другого источника — **controlled switch** (stop → подтверждённый exit → start), ответ `ack.result=switched`.
5. **Remote input** (мышь/клавиатура) — только в режиме `desktop`, только для выбранного display, только при совпадении `lease_id` + `desktop_id` + `stream_instance_id` с активной привязкой.
6. **FFmpeg** — отдельный артефакт `ffmpeg.zip` рядом с `tools.zip` в `tools/dist`, структура `ffmpeg\x64\…` и `ffmpeg\x86\…` (совместимо с `zip_extract_all`), установка в `C:\l4tools\ffmpeg\`, минимальный набор файлов. Нужны **обе** архитектуры.
7. `l4desk` запускает **только** `C:\l4tools\ffmpeg\ffmpeg.exe` (точнее `<base>\ffmpeg\ffmpeg.exe`, где `<base>` — каталог установки tools, по умолчанию `C:\l4tools`). Запрещены `D:\ffmpeg`, `PATH`, текущая директория, произвольные пути, аргументы/пути/device name из сети.

---

## 3. Контракт протокола `ctl` (envelope v1) — терминальная сторона

Реализуй в `ctl_protocol.c/.h` (+ новые модули §4). Поля — snake_case, как в существующих сообщениях. Все входящие команды проходят `dedup_cache` по `command_id`; повтор → тот же ответ.

### 3.1 Server → Terminal (`srv/<SN>/ctl`)
- `inventory_get {command_id, lease_id?}` → `ack` + `inventory`.
- `stream_start {command_id, lease_id, mode:"desktop"|"usb-camera", source_id, profile, stream_instance_id}` → `ack.result = started|already_running|switched` (+ `stream_instance_id`, `state`) или `nack`.
  `already_running` — если уже запущен тот же `source_id`+`stream_instance_id`. Другой `source_id` или другой `stream_instance_id` → controlled switch.
- `stream_stop {command_id, lease_id, stream_instance_id?}` → `ack.result = stopped|already_stopped`. `ack` публикуется **только после фактического завершения** процесса.
- `pointer_move` / `mouse_click` — теперь с обязательными `desktop_id`, `stream_instance_id`; координаты нормализованы к кадру (как сейчас), маппинг на прямоугольник выбранного display выполняет `l4desk`.
- `key_event {command_id, lease_id, desktop_id, stream_instance_id, kind:"down"|"up"|"press", vk:int, text?:str}` — новый; whitelist `vk` (буквы, цифры, Enter, Esc, Tab, Backspace, стрелки, F1–F12; запрещены Win, Ctrl+Alt+Del-подобные комбинации).

### 3.2 Terminal → Server (`dev/<SN>/ctl`)
- `presence` расширенный (retain, каждые `presence_interval_sec` и при любом изменении состояния):
  `session_id`, `screen{virtual_*}`, `inventory{displays[],cameras[]}`, `stream{state,mode,source_id,stream_instance_id,profile,reason,ffmpeg_pid,started_at,restart_count}`.
  `displays[]`: `desktop_id` (стабильный, см. §4.2), `name` (`\\.\DISPLAYn`), `primary`, `x,y,width,height` (могут быть отрицательными), `session_id`, `policy: "input"|"view"|"denied"`.
  `cameras[]`: `camera_id` (стабильный), `name`, `available`.
- `stream_event {stream_instance_id, state, reason, timestamp}` — QoS 1 при каждом переходе состояния.
- `nack.code`: существующие + `lease_mismatch`, `desktop_mismatch`, `stream_mismatch`, `source_not_allowed`, `source_unavailable`, `session_unavailable`, `busy_transition`, `ffmpeg_missing`, `ffmpeg_integrity`, `input_not_allowed_in_camera_mode`, `invalid_profile`.

Состояния `stream.state`: `stopped → starting → running → stopping → stopped`; дополнительно `restarting`, `failed`, `source_unavailable`, `session_unavailable`.

---

## 4. Целевая архитектура терминальной стороны

### 4.1 `l4superv` (изменения минимальные, но обязательные)
- Создавать **Job Object** (`CreateJobObjectW`, `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` + `JOB_OBJECT_LIMIT_BREAKAWAY_OK` не выставлять) и помещать в него `l4desk` после `CreateProcessAsUserW` (`CREATE_SUSPENDED` → `AssignProcessToJobObject` → `ResumeThread`). Флаг `CREATE_BREAKAWAY_FROM_JOB` оставить только если процесс службы сам в job (проверить `IsProcessInJob`), иначе убрать.
- `sp_stop`: событие `Global\L4Desk_Stop_<SN>` → ожидание grace (увеличить до ≥ 8 с, чтобы `l4desk` успел мягко остановить ffmpeg) → `TerminateJobObject`. Закрытие handle job = гарантированная зачистка дерева при падении самой службы.
- При logout / смене сессии / stop / restart службы — тот же путь; убедиться, что ffmpeg не переживает `l4desk`.
- Добавить в статус (`orchestrator_get_l4desk_status`, `l4superv_status.cmd`) сведения о ffmpeg (PID, состояние) — читать из файла состояния `l4desk` (§4.4) или через существующий локальный канал, если он есть.
- Обновить `installer_main.c`: установка `ffmpeg.zip` (§6), создание `C:\l4tools\ffmpeg\log`, вывод сводки о наличии/версии ffmpeg, безопасное обновление (§6.4).

### 4.2 `l4desk`: inventory и выбор display (`desktop_state.c` → расширить, новый `display_inventory.c/.h`)
- `EnumDisplayMonitors` + `GetMonitorInfoW` (`MONITORINFOEXW`) + `EnumDisplayDevicesW(deviceName, 0, &dd, EDD_GET_DEVICE_INTERFACE_NAME)`: стабильный `desktop_id` = хэш (например FNV-1a/CRC32 hex) от `DeviceID`/интерфейсного имени монитора (`\\?\DISPLAY#...`), fallback — от `DeviceString`+позиции. Формат `disp:<hex>`. `\\.\DISPLAYn` публиковать только как `name`.
- Прямоугольник display в координатах virtual screen (учитывать отрицательные `x/y`); `primary` по `MONITORINFOF_PRIMARY`; `session_id` = `desktop_get_current_session_id()`.
- Локальная политика: файл `C:\l4tools\l4desk\l4desk_policy.ini` (или существующий формат конфигов tools — проверь `config.c`): секции `[displays]` с `allow=*|disp:...`, `view_only=disp:...`, `deny=disp:...`; `[cameras]` `allow=*|cam:...`; `[profiles]` перечень разрешённых профилей. По умолчанию: все display `input`, все камеры разрешены, профили `default`, `low`.
- Камеры: enumerate через DirectShow (`ICreateDevEnum`/`CLSID_VideoInputDeviceCategory`, `DevicePath` → стабильный `camera_id = cam:<hex>`; `FriendlyName` → `name`). Для ffmpeg передавать `-f dshow -i video=@device_pnp_...` (DevicePath) — детерминированно и не зависит от порядка. Не использовать `ffmpeg -list_devices` парсинг как основной механизм.
- Перечитывать inventory при `WM_DISPLAYCHANGE`/`WM_DEVICECHANGE` (скрытое message-only окно) или таймером ≤ 10 с; при изменениях — новый `presence`.

### 4.3 `l4desk`: активная привязка и ввод
Хранить одну структуру активной трансляции: `stream_instance_id`, `mode`, `source_id`, `desktop_rect`, `session_id`, `lease_id`, `state`, `ffmpeg_pid`, `ffmpeg_start_time` (FILETIME), `started_at`, `restart_count`, `reason`.
- `pointer_move`/`mouse_click`/`key_event` принимаются только если: `state==running`, `mode==desktop`, `lease_id` совпадает, `desktop_id` совпадает, `stream_instance_id` совпадает, policy display = `input`. Иначе `nack` с точным кодом.
- Маппинг координат: нормализованные `(nx, ny)` кадра → `desktop_rect.x + nx*width`, `desktop_rect.y + ny*height` → в абсолютные координаты `SendInput` (0..65535 относительно **virtual screen**, с учётом отрицательного origin). Расширить `input_inject.c` функцией для клавиатуры (`SendInput` с `KEYEVENTF_*`, `text` через `KEYEVENTF_UNICODE`).

### 4.4 `l4desk`: FFmpeg orchestration (новые модули `ffmpeg_supervisor.c/.h`, `ffmpeg_cmdline.c/.h`)
Один сериализованный state machine, все переходы под одним критическим секцией/мьютексом; команды из MQTT-потока ставятся в очередь и обрабатываются одним worker-потоком.

- **Единственный экземпляр**: глобальный именованный mutex `Global\L4Desk_FFmpeg_<SN>` (владение на время жизни процесса ffmpeg) + собственный Job Object `l4desk` для ffmpeg (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, вложенные job поддерживаются с Windows 8; на Win7 fallback — дерево процессов по `NtQueryInformationProcess`/`CreateToolhelp32Snapshot` с проверкой parent PID + start time).
- **Запуск скрыто**: `CreateProcessW` с `CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED`, `STARTF_USESHOWWINDOW/SW_HIDE`, `STARTF_USESTDHANDLES` — stdin закрыт (или pipe для мягкого `q`), stdout/stderr → pipe → поток чтения → ротационный лог `C:\l4tools\ffmpeg\log\ffmpeg_<stream_instance_id>.log` (ротация по размеру, например 5 МБ × 5 файлов; переиспользуй `log.c`). Никаких `cmd.exe`/`ShellExecute`.
- **Аргументы** формируются только из локального whitelist (§5). Ни один параметр не берётся из сети как строка для командной строки.
- **Двухфазная остановка**: (1) мягко — записать `q\n` в stdin ffmpeg **или** `GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT)` для отдельной консольной группы (`CREATE_NEW_PROCESS_GROUP`) — выбери и обоснуй по результатам теста; ждать `WaitForSingleObject(hProcess, stop_timeout_ms)` (по умолчанию 5000); (2) жёстко — `TerminateJobObject`/`TerminateProcess` дерева; после `WaitForSingleObject == WAIT_OBJECT_0` → освободить порты (убедиться, что UDP-сокеты закрыты — ffmpeg завершён) → `state=stopped` → публиковать `ack`/`stream_event`. Никогда не публиковать `stopped` до фактического завершения.
- **Идемпотентность**: `stream_start` при `running` с тем же источником → `already_running`; `stream_stop` при `stopped` → `already_stopped`; тот же `command_id` → ответ из `dedup_cache`; `stream_start` во время `starting|stopping|restarting` → либо ждать завершения перехода (≤ 15 с), либо `nack busy_transition` — реализуй ожидание с таймаутом.
- **Controlled switch**: `stopping(old) → stopped → starting(new) → running`, один `ack.result=switched` в конце; при сбое запуска нового источника — `failed` с `reason`, старый не восстанавливать (явно задокументируй).
- **Restart policy**: отличать intentional stop (флаг `stop_requested`) от crash (неожиданный выход); backoff 2, 4, 8, 16, 30 с (макс.), лимит `max_restarts` (например 5 за 10 минут) → `failed` с `reason=restart_limit`; при `source_unavailable` (камера отключена/дисплей исчез) — не рестартовать бесконечно: переход в `source_unavailable`, повторная попытка только при появлении источника в inventory (event-driven) или по команде `stream_start`.
- **Health-check** (таймер 2–5 с): (a) процесс жив (`GetExitCodeProcess`, PID + `GetProcessTimes` creation time совпадают с сохранёнными — защита от PID reuse); (b) источник есть в inventory; (c) прогресс — использовать `-progress pipe:N`/`-stats_period` и парсить `frame=`/`out_time_ms` из pipe: отсутствие прогресса > `stall_timeout` (например 10 с) → `restarting` с `reason=stall`; (d) локальный ingress: проверять, что `leo4proxy` слушает UDP `5004/5005` (`GetExtendedUdpTable`) и/или его HTTP-статус на `proxy_http_port 18443` (см. `proxy_client.c` в l4superv для формата) — при недоступности `reason=ingress_unavailable`; (e) активная сессия = `WTSGetActiveConsoleSessionId()`; если `l4desk` не в ней или desktop недоступен (`desktop_is_interactive_available()==false`, экран блокировки) → `session_unavailable` (для camera-mode продолжать, если ffmpeg работает — камера не требует desktop).
- **Reconciliation при старте `l4desk`**: прочитать файл состояния `C:\l4tools\l4desk\state\ffmpeg_state.json` (PID, creation time, `stream_instance_id`, `session_id`, `owner=l4desk`, `sn`); если процесс с таким PID и creation time существует, его образ — `<base>\ffmpeg\ffmpeg.exe` и командная строка содержит маркер `-metadata comment=l4desk:<stream_instance_id>` (или аналогичный явный маркер владения) → корректно завершить его двухфазно и только потом продолжить со `stopped`. **Запрещено** `taskkill /IM ffmpeg.exe` и любое массовое завершение по имени.
- **Session 0 / неправильная сессия**: desktop-режим стартует только если `ProcessIdToSessionId(self) == WTSGetActiveConsoleSessionId()` и `desktop_is_interactive_available()`; иначе `nack session_unavailable`.
- При получении `Global\L4Desk_Stop_<SN>` или завершении `l4desk`: сначала двухфазный stop ffmpeg, затем выход; учитывай grace `l4superv`.

---

## 5. Профили и локальная валидация

- `profile` ∈ whitelist (`default`, `low`; при необходимости `high`) → фиксированные наборы параметров в коде/ini: `-c:v libx264 -preset ultrafast -tune zerolatency -b:v <кбит> -maxrate -bufsize -g <fps*2> -pix_fmt yuv420p -r <fps> -f rtp rtp://127.0.0.1:5004?rtcpport=5005` (+ `-payload_type 96`, SSRC при необходимости — согласуй с текущим примером `examples/ffmpeg_rtp_tunnel_example.cmd` и `l4media/janus/janus.plugin.streaming.jcfg`).
- `desktop`: `-f gdigrab -framerate <fps> -offset_x <x> -offset_y <y> -video_size <w>x<h> -i desktop` (координаты display, включая отрицательные; проверить, что gdigrab корректно работает с отрицательным offset на целевой Windows — иначе документировать альтернативу `-i title=`/`ddagrab` для x64 Windows 8+). Добавлять `-draw_mouse 1`.
- `usb-camera`: `-f dshow -rtbufsize <N>M -framerate <fps> -video_size <w>x<h> -i video=@<DevicePath>`; `DevicePath` берётся из **локального** inventory по `camera_id`, никогда из команды.
- Отклонять: неизвестный `source_id` (`source_not_allowed`), `policy=denied` (`source_not_allowed`), `policy=view` + попытка input (`desktop_mismatch`/`input_not_allowed`), неизвестный `profile` (`invalid_profile`), `source_id` не из текущего inventory (`source_unavailable`), любые лишние поля в команде игнорировать.
- Проверять `ffmpeg.exe`: существование по фиксированному пути, PE-архитектура совместима с ОС, версия (`ffmpeg -version` при старте `l4desk`, результат в presence/лог) и целостность по `C:\l4tools\ffmpeg\ffmpeg.sha256` (манифест из пакета §6). Ошибка → `ffmpeg_missing`/`ffmpeg_integrity`, состояние `failed`, без рестарт-цикла.

---

## 6. Упаковка и deployment FFmpeg

### 6.1 Источник дистрибутивов
- **x64**: `D:\ffmpeg` (FFmpeg 9.0.1 gyan.dev full shared). Взять только `bin\ffmpeg.exe` и DLL, от которых он зависит (`avcodec-63.dll`, `avdevice-63.dll`, `avfilter-12.dll`, `avformat-63.dll`, `avutil-61.dll`, `swresample-7.dll`, `swscale-10.dll`) + `LICENSE`. Проверь зависимости через `dumpbin /dependents` (MSVC) — если есть дополнительные внешние DLL (кроме системных/UCRT), включи их. `ffplay.exe`, `ffprobe.exe`, `doc\`, `include\`, `lib\`, `presets\` — **не включать**. Учти: gyan.dev 9.x требует UCRT (Windows 10+; на Windows 7 x64 нужен установленный UCRT) — зафиксируй в README и в проверке установщика (`GetFileVersionInfo`/наличие `ucrtbase.dll`).
- **x86**: официальных win32-автосборок у gyan.dev/BtbN нет. Варианты в порядке предпочтения: (1) собрать static win32 из `BtbN/FFmpeg-Builds` (`./makeimage.sh win32 gpl && ./build.sh win32 gpl`, docker/WSL) с минимальным набором энкодеров (`libx264`, `gdigrab`, `dshow`, `rtp`), при необходимости на релизной ветке (`7.1`/`8.x`); (2) community static win32 сборки `defisym/FFmpeg-Builds-Win32` (GitHub Releases) — проверить лицензию и SHA-256; (3) архивные static win32 сборки Zeranoe 4.4.x (без UCRT, гарантированно работают на Windows 7/POSReady 7). Требования: **один статический `ffmpeg.exe`** (без DLL) предпочтителен для x86; обязательна проверка запуска на 32-битной Windows 7 Embedded/POSReady 7 (VM). Зафиксируй источник, версию, URL, SHA-256 в `tools/ffmpeg/SOURCES.md`. Если сборка невозможна в отведённое время — задокументируй, оставь x86-слот в пакете пустым, `l4desk` на x86 публикует `ffmpeg_missing`.
- Не коммитить бинарники в git, если репозиторий этого не делает для других tools (проверь `.gitignore` и как хранятся `tools/dist/*.zip`); следуй существующей практике.

### 6.2 Структура `ffmpeg.zip`
```
ffmpeg\x64\ffmpeg.exe, *.dll, LICENSE
ffmpeg\x86\ffmpeg.exe, LICENSE
ffmpeg\ffmpeg.sha256        (манифест: относительный путь + sha256 для обеих архитектур)
ffmpeg\VERSION.txt          (версии и источник по архитектурам)
ffmpeg\README.md
```
`zip_extract_all` с `target_arch` даст `C:\l4tools\ffmpeg\ffmpeg.exe` (+ DLL для x64). Каталог `C:\l4tools\ffmpeg\log\` создаёт установщик.

### 6.3 Сборка пакета
- Новый `tools/ffmpeg/` с `pack_ffmpeg.cmd` (или стадия в `tools/l4superv/pack_zip.cmd` — выбери в соответствии с существующей практикой, но **`ffmpeg.zip` — отдельный архив**, не внутри `tools.zip`, из-за размера): staging → `Compress-Archive` → `tools/dist/ffmpeg.zip` (+ `ffmpeg.zip.sha256`). Источник x64 — параметр/переменная окружения `FFMPEG_SRC_X64` по умолчанию `D:\ffmpeg\bin`; x86 — `FFMPEG_SRC_X86`.
- Обнови `tools/dist/README.md`, `tools/l4superv/README.md`, `docs/terminal-tools-user-guide.md`, `tools/l4desk/README.md` и `CHANGELOG.md` обоих tools, поднять `L4DESK_VERSION_STR` и версию l4superv по правилам проекта.

### 6.4 Установка/обновление (`installer_main.c`, скрипты `l4install_*.cmd`)
- `l4install` ищет `ffmpeg.zip` рядом с exe (как `tools.zip`), опция `--ffmpeg-zip <path>`, `--skip-ffmpeg`. Распаковка в `<dest>\ffmpeg` через `zip_extract_all`.
- Перед перезаписью: если запущен управляемый ffmpeg (проверить `state\ffmpeg_state.json` + PID/creation time) — потребовать остановку через `l4superv` (`l4superv_stop.cmd` уже останавливает l4desk → l4desk двухфазно останавливает ffmpeg) и **не** перезаписывать используемые файлы; при `ERROR_SHARING_VIOLATION` — понятная ошибка, откат.
- Атомарность: распаковка во временный `ffmpeg.new\` → проверка манифеста sha256 → переименование (`ffmpeg` → `ffmpeg.old`, `ffmpeg.new` → `ffmpeg`) → удаление `ffmpeg.old`. Повреждённый/частичный архив → отказ до изменения рабочего каталога.
- Вывод сводки: путь, архитектура, версия ffmpeg, результат проверки манифеста.

---

## 7. Порядок работы

1. Прочитай файлы §1 полностью (особенно `orchestrator.c`, `session_proc.c`, `installer_main.c`, `zip_extractor.c`, `pack_zip.cmd`, `main.c`, `mqtt_client.c`, `ctl_protocol.c`, `desktop_state.c`, `input_inject.c`, `config.c`) и `AGENTS.md`. Зафиксируй карту точек изменений.
2. `l4superv`: Job Object, grace, статус ffmpeg, установщик `ffmpeg.zip`, безопасное обновление.
3. `l4desk`: inventory (§4.2), привязка/ввод/клавиатура (§4.3), `ffmpeg_supervisor` (§4.4), профили/валидация (§5), расширение `ctl_protocol`/`presence` (§3), файл состояния/reconciliation, логи.
4. Упаковка `ffmpeg.zip` (§6), обновление `pack_zip.cmd`/`dist`, документация.
5. Сборка x86+x64 обоих tools (`build.cmd`), проверка отсутствия предупреждений/UB; тесты (§9).
6. Отчёт (§10).

Не меняй серверный код и топики; при необходимости уточнения контракта — опиши предложение в отчёте.

---

## 8. Критерии готовности

- `ffmpeg.exe` запускается только из `<base>\ffmpeg\ffmpeg.exe`, скрыто, дочерним к `l4desk`, в Job Object; на терминале никогда не существует > 1 управляемого экземпляра (проверено тестами повторного/конкурентного `stream_start`).
- Полный lifecycle со состояниями §3 и корректной публикацией `presence`/`stream_event`/`ack`; `stopped` публикуется только после фактического завершения процесса; порты 5004/5005 свободны после stop.
- Inventory дисплеев (стабильные id, отрицательные координаты, primary, session) и камер (стабильные id), локальная policy `input/view/denied`; захват только выбранного display; ввод только в выбранный display и только при полном совпадении `lease_id`/`desktop_id`/`stream_instance_id`; в camera-mode ввод отклоняется.
- Controlled switch desktop↔camera работает; crash → ограниченный restart с backoff; зависание → restart по stall; отсутствие камеры/дисплея → `source_unavailable` без restart loop; отсутствие/повреждение ffmpeg → `ffmpeg_missing`/`ffmpeg_integrity` без loop.
- Logout, session switch, restart `l4desk`, restart/stop `l4superv`, перезагрузка — без orphan `ffmpeg.exe` (проверка `tasklist`/Process Explorer по PID и creation time, не по имени).
- Reconciliation после аварийного рестарта `l4desk` завершает «свой» потерянный ffmpeg и не трогает чужие процессы `ffmpeg.exe`.
- `ffmpeg.zip` (x64 из `D:\ffmpeg`, x86 из задокументированного источника) лежит в `tools/dist` рядом с `tools.zip`; `l4install` устанавливает в `C:\l4tools\ffmpeg`, проверяет манифест, безопасно обновляет, не перезаписывает используемые файлы.
- Обе архитектуры `l4desk`/`l4superv` собираются `build.cmd`; x86 сборка работает на 32-битной Windows 7 (проверка в VM или задокументированное ограничение).
- Документация (`README`, `CHANGELOG`, `terminal-tools-user-guide.md`, `docs/etran_arch-remote-input-control.md` в части терминала) обновлена.

---

## 9. Требования к тестированию

### 9.1 Автоматические
- Unit-тесты на C (по образцу существующих тестов в `tools/`, если есть; иначе `tools/l4desk/tests/` с простым test runner в `build.cmd test`): парсинг/сборка сообщений `ctl` (все новые типы, все `nack.code`), state machine ffmpeg (мок процесса — тестовый `fake_ffmpeg.exe`, который умеет спать/падать/зависать/игнорировать `q`), валидация профилей/источников, маппинг координат для display с отрицательным origin, стабильность `desktop_id`/`camera_id`, dedup по `command_id`, чтение policy.ini, reconciliation по файлу состояния (PID reuse — процесс с тем же PID, но другим creation time не трогается).
- Интеграционный скрипт (PowerShell/Python, по образцу `tools/leo4proxy/examples/test_presence_suite.py`, локальный Mosquitto): публикует команды в `srv/<SN>/ctl`, проверяет `dev/<SN>/ctl`: повторный start → `already_running`; два конкурентных start → один процесс; stop во время `starting` → корректное завершение и один `stopped`; desktop↔camera switch → `switched`; неверные `desktop_id`/`stream_instance_id`/`lease_id` → `nack`; input в camera-mode → `nack`; kill ffmpeg → `restarting`→`running`, счётчик; зависший ffmpeg → `stall` restart; отключение камеры (или несуществующий `camera_id`) → `source_unavailable` без loop; отсутствие ffmpeg.exe → `ffmpeg_missing`.
- Проверка отсутствия orphan-процессов и освобождения UDP 5004/5005 после каждого сценария (`Get-NetUDPEndpoint`/`netstat -ano`).

### 9.2 Ручные (описать шаги и ожидаемые результаты в отчёте, выполнить всё, что возможно локально/в VM)
- Несколько экранов (в т.ч. монитор слева от primary с отрицательными координатами): выбор каждого, захват только его области, клик попадает в тот же display; попытка ввода в другой display отклоняется.
- Logout / смена пользователя / lock screen / session switch: ffmpeg завершён, после входа `l4desk` перезапущен `l4superv`, состояние `stopped`.
- Restart `l4desk` (kill), restart `l4superv` (`l4superv_restart.cmd`), stop службы, перезагрузка терминала — без orphan; после перезагрузки трансляция не стартует сама (нет автозапуска).
- USB reconnect камеры во время трансляции.
- Обновление tools-пакета (`l4install` поверх работающей системы): отказ перезаписи при активной трансляции; успешное обновление после остановки; повреждённый `ffmpeg.zip` → откат.
- x86 сборка на 32-битной Windows 7/POSReady 7 VM: запуск `l4desk`, наличие/отсутствие ffmpeg, поведение при `ffmpeg_missing`.

---

## 10. Формат отчёта

Верни отчёт в Markdown с разделами:
1. **Карта изменений** — файлы в `tools/` и назначение каждого изменения; новые модули.
2. **Реализованный контракт `ctl`** — итоговые JSON-примеры всех сообщений и `nack.code`, диаграмма состояний ffmpeg; отклонения от §3, если были, с обоснованием.
3. **Process supervision** — как устроены Job Object в `l4superv` и `l4desk`, двухфазная остановка, reconciliation, restart policy, health-check (таймауты/лимиты с значениями по умолчанию и где настраиваются).
4. **Inventory и policy** — алгоритм стабильных id, формат `l4desk_policy.ini`, поведение по умолчанию.
5. **Артефакты поставки** — состав `ffmpeg.zip` по архитектурам (версии, источник, SHA-256, размер), изменения `pack_zip.cmd`/`installer_main.c`/`dist`, порядок установки и обновления.
6. **Тесты** — автоматические (как запускать, результаты) и ручные (сценарий → результат/не выполнено, среда).
7. **Известные ограничения и риски** — x86/Win7 (UCRT, источник сборки), gdigrab с отрицательными offset, nested Job Object на Win7, ограничения `q`/CTRL_BREAK, что требуется от серверной стороны.
8. **Изменения документации и версий**.
