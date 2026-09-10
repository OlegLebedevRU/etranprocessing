# l4desk — Leo4 Terminal Remote Desktop, Input & Media Streaming Agent

`l4desk` — легковесный, автономный агент удалённого рабочего стола, ввода и оркестрации видеопотоков FFmpeg для платёжных терминалов и киосков под управлением Windows (Windows 7 SP1+ x86 / Windows 10/11 x64).

## 1. Назначение и функциональность (PROMPT 4.2)

Агент работает в активной интерактивной сессии пользователя (запускается супервизором `l4superv`) и реализует прикладную сторону контракта управления трансляцией и удалённого ввода:
- **Инвентаризация дисплеев**: перечисление мониторов (`EnumDisplayMonitors` + `EnumDisplayDevicesW`), генерация стабильных `desktop_id` (`disp:<fnv1a_hex>`), учёт отрицательных координат в виртуальном пространстве, чтение локальной политики экранов (`input`, `view`, `denied`).
- **Инвентаризация камер**: перечисление видеоустройств DirectShow (`CLSID_VideoInputDeviceCategory`), генерация стабильных `camera_id` (`cam:<fnv1a_hex>`), формирование аргументов запуска `-f dshow -i video=@<DevicePath>`.
- **Удалённый ввод**: инъекция движений мыши (`pointer_move`), кликов (`mouse_click`) с маппингом нормализованных координат `[0.0, 1.0]` в виртуальный экран, а также клавиатурных нажатий (`key_event`) со строгим whitelist допустимых клавиш и защитой от деструктивных комбинаций.
- **Оркестрация FFmpeg**: запуск строго из `<base>\ffmpeg\ffmpeg.exe` (по умолчанию `C:\l4tools\ffmpeg\ffmpeg.exe`) в скрытом режиме (`SW_HIDE`), перенаправление stdin (для soft stop по `q\n`), перенаправление stdout/stderr в ротационный лог-файл (до 5 файлов по 5 МБ), контроль через Job Object (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`).
- **Двухфазная остановка**: фаза 1 (soft stop `q\n` с ожиданием до 5 с) → фаза 2 (hard kill через `TerminateJobObject`/`TerminateProcess`) → подтверждение освобождения портов и ресурсов.
- **Controlled switch**: бесшовное последовательное переключение источников (`stopping` старого → `stopped` → `starting` нового → `running`) с ответом `ack.result = switched`.
- **Файл состояния и Reconciliation**: запись состояния в `<base>\l4desk\state\ffmpeg_state.json`. При старте — поиск и мягкая остановка зависших процессов FFmpeg от предыдущих сессий с защитой от PID reuse (проверка `creation_time` и метаданных).
- **Health-check и отказоустойчивость**: отслеживание неожиданного падения процесса, экспоненциальный backoff перезапуска (ограничение до 5 рестартов за 10 мин), обнаружение stall (зависание кадров более 10 с), контроль интерактивности сессии.

## 2. Архитектура и сетевая изоляция

- **Zero-Dependency Win32/C**: статическая компиляция `/MT`, поддержка x86 и x64.
- **Локальный транспорт**: подключение строго к `127.0.0.1:1883` (Mosquitto), `client_id = svc_desk`.
- **Управление**: control-plane `srv/<SN>/ctl` → `dev/<SN>/ctl` (envelope v1, `command_id`, dedup-кеш, ACK/NACK).
- **Трансляция медиа**: RTP/RTCP направляются на UDP `127.0.0.1:5004/5005` в локальный туннель `leo4proxy` (`--rtp-tunnel`), который передаёт поток в `l4media-nginx:8443` по mTLS.

## 3. Контракт протокола `ctl` (envelope v1)

### 3.1 Входящие команды (`srv/<SN>/ctl`)

1. `inventory_get {command_id, lease_id?}`
   - Возвращает `ack` со структурой `inventory` (списки `displays` и `cameras`).
2. `stream_start {command_id, lease_id, mode:"desktop"|"usb-camera", source_id, profile, stream_instance_id}`
   - Ответы: `ack.result = "started" | "already_running" | "switched"` или `nack`.
3. `stream_stop {command_id, lease_id, stream_instance_id?}`
   - Ответы: `ack.result = "stopped" | "already_stopped"` (публикуется только после фактического завершения процесса).
4. `pointer_move {command_id, lease_id, desktop_id, stream_instance_id, x:float, y:float}`
   - Нормализованные координаты `[0.0, 1.0]` кадра, исполняется только при активном стриме в режиме `desktop` и совпадающих ID.
5. `mouse_click {command_id, lease_id, desktop_id, stream_instance_id, x:float, y:float, button?}`
   - Поддерживает кнопки `left`, `right`, `middle`. Возвращает `ack.result = "injected"` или `nack`.
6. `key_event {command_id, lease_id, desktop_id, stream_instance_id, kind:"down"|"up"|"press", vk:int, text?:str}`
   - Проверка по белому списку виртуальных клавиш. Возвращает `ack.result = "injected"` или `nack`.

### 3.2 Исходящие сообщения (`dev/<SN>/ctl`)

- **Расширенный `presence`** (`retain=1, qos=1`):
  - `status`: `online` / `offline`
  - `desktop_available`: boolean
  - `session_id`: DWORD
  - `screen`: `{virtual_x, virtual_y, virtual_width, virtual_height}`
  - `inventory`:
    - `displays[]`: `{desktop_id, name, primary, x, y, width, height, session_id, policy}`
    - `cameras[]`: `{camera_id, name, available}`
  - `stream`: `{state, mode, source_id, stream_instance_id, profile, reason, ffmpeg_pid, started_at, restart_count}`
  - `timestamp`: ISO-8601
- **`stream_event`** (`retain=0, qos=1`):
  - Публикуется при каждом изменении состояния стрима (`{stream_instance_id, state, reason, timestamp}`).
- **Коды `nack.code`**:
  - `lease_mismatch`, `desktop_mismatch`, `stream_mismatch`, `source_not_allowed`, `source_unavailable`, `session_unavailable`, `busy_transition`, `ffmpeg_missing`, `ffmpeg_integrity`, `input_not_allowed_in_camera_mode`, `invalid_profile`, `invalid_sn`, `invalid_payload`, `expired`, `unsupported`, `inject_failed`.

## 4. Локальная политика (`l4desk_policy.ini`)

Расположение: `<base>\l4desk\l4desk_policy.ini` (по умолчанию `C:\l4tools\l4desk\l4desk_policy.ini`).

```ini
[displays]
allow=*
view_only=disp:11223344
deny=disp:99999999

[cameras]
allow=*
deny=cam:deadbeef

[profiles]
allow=default,low
```

## 5. Сборка и тестирование

### Сборка бинарников (`build.cmd`):
```cmd
tools\l4desk\build.cmd all
```
Артефакты:
- `bin\x86\l4desk.exe` — 32-битный бинарник (совместим с Windows 7 SP1+ x86)
- `bin\x64\l4desk.exe` — 64-битный бинарник (Windows 10/11 x64)
- `bin\l4desk.exe` — копия 32-битного бинарника по умолчанию

### Модульные тесты:
```cmd
tools\l4desk\tests\run_tests.cmd
```
Запускает:
1. `fake_ffmpeg.exe` (мок-процесс)
2. `test_ctl_protocol.exe` (тесты парсинга, FNV-1a хэшей, маппинга мыши, whitelist клавиатуры, всех кодов NACK)
3. `test_orchestrator.exe` (тесты запуска, двухфазного soft/hard stop, зависания, краша, stall, reconciliation и защиты от PID reuse)

### Интеграционные тесты (Python / PowerShell):
```cmd
python tools\l4desk\tests\integration_test.py
```
или
```powershell
powershell -File tools\l4desk\tests\integration_test.ps1
```
