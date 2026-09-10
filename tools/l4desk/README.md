# l4desk — Leo4 Terminal Remote Desktop, Input & Media Streaming Agent

`l4desk` — легковесный, автономный агент удалённого рабочего стола, ввода и оркестрации видеопотоков FFmpeg для платёжных терминалов и киосков под управлением Windows (Windows 7 SP1+ x86 / Windows 10/11 x64).

## 1. Назначение и функциональность (PROMPT 4.2)

Агент работает в активной интерактивной сессии пользователя (запускается супервизором `l4superv`) и реализует прикладную сторону контракта управления трансляцией и удалённого ввода:
- **Инвентаризация дисплеев**: перечисление мониторов (`EnumDisplayMonitors` + `EnumDisplayDevicesW`), генерация стабильных `desktop_id` (`disp:<fnv1a_hex>`), учёт отрицательных координат в виртуальном пространстве, чтение локальной политики экранов (`input`, `view`, `denied`).
- **Инвентаризация камер**: перечисление видеоустройств DirectShow (`CLSID_VideoInputDeviceCategory`), генерация стабильных `camera_id` (`cam:<fnv1a_hex>`), формирование аргументов запуска `-f dshow -i video=@<DevicePath>`.
- **Удалённый ввод**: инъекция движений мыши (`pointer_move`), кликов (`mouse_click`) с маппингом нормализованных координат (`[0.0, 1.0]` float или `0..65535` integer) в виртуальный экран с учётом отрицательного смещения мониторов, а также клавиатурных нажатий (`key_event`) со строгим whitelist допустимых клавиш и защитой от деструктивных комбинаций.
- **Оркестрация FFmpeg и профиль H.264 Baseline Level 3.1**: запуск строго из `<base>\ffmpeg\ffmpeg.exe` (по умолчанию `C:\l4tools\ffmpeg\ffmpeg.exe`) в скрытом режиме (`SW_HIDE`), перенаправление stdin (для soft stop по `q\n`), перенаправление stdout/stderr в ротационный лог-файл (до 5 файлов по 5 МБ), контроль через Job Object (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`). Явная фиксация флагов кодирования H.264 Constrained Baseline Level 3.1 под SDP-заявку Janus Streaming Plugin: `-profile:v baseline -level 3.1 -x264-params bframes=0:force-cfr=1 -g %d -keyint_min %d -sc_threshold 0 -pix_fmt yuv420p`.
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
   - Ответы: `ack.result = "stopped" | "already_stopped"` (публикуется **только после** фактического закрытия дескрипторов процесса и освобождения сокетов/портов).
4. `pointer_move {command_id, lease_id, desktop_id, stream_instance_id, x, y}`
   - Координаты могут приходить как в нормализованном float `0.0..1.0`, так и в integer `0..65535` (значения `> 1.0` автоматически масштабируются от `65535.0f`). Исполняется только при активном стриме в режиме `desktop` и совпадающих `lease_id`, `desktop_id`, `stream_instance_id`.
5. `mouse_click {command_id, lease_id, desktop_id, stream_instance_id, x, y, button?}`
   - Поддерживает кнопки `left`, `right`, `middle`. Возвращает `ack.result = "injected"` или `nack`.
6. `key_event {command_id, lease_id, desktop_id, stream_instance_id, kind:"down"|"up"|"press", vk:int, text?:str}`
   - Валидация:
     - Проверка активного стрима: `state == running` и `mode == desktop` (иначе `stream_mismatch` / `input_not_allowed_in_camera_mode`).
     - Проверка совпадения контекста: `lease_id`, `desktop_id`, `stream_instance_id` (при несовпадении — `lease_mismatch`, `desktop_mismatch`, `stream_mismatch`).
     - Проверка локальной политики выбранного экрана: `policy == input` (если `view` или `denied` — `source_not_allowed`).
     - Белый список виртуальных кодов `vk`: `0x08` (Backspace), `0x09` (Tab), `0x0D` (Enter), `0x1B` (Esc), `0x20` (Space), `0x2E` (Delete), `0x25..0x28` (Стрелки), `0x30..0x39` (0-9), `0x41..0x5A` (A-Z), `0x70..0x7B` (F1-F12), Numpad, модификаторы и OEM-символы.
     - Запрещены деструктивные клавиши (`VK_LWIN`, `VK_RWIN`, `VK_APPS`) и комбинация Ctrl+Alt+Del.
     - Инъекция через `SendInput` (`down`, `up`, `press`) с флагом `KEYEVENTF_EXTENDEDKEY` для навигационных клавиш. Возвращает `ack.result = "injected"` или `nack`.

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
  - `timestamp`: ISO-8601 UTC
- **`stream_event`** (`retain=0, qos=1`):
  - Формат payload:
    ```json
    {"v":1,"type":"stream_event","sn":"<SN>","stream_instance_id":"<ID>","state":"<STATE>","reason":"<REASON>","timestamp":"<ISO_UTC>"}
    ```
  - Утверждённый enum `state`:
    `stopped`, `starting`, `running`, `stopping`, `restarting`, `failed`, `source_unavailable`, `session_unavailable`.
  - Отправка гарантируется при всех переходах:
    - `starting` → `running` (после успешного запуска процесса)
    - `running` → `stopping` (при начале остановки или switch)
    - `stopping` → `stopped` (после завершения процесса и освобождения сокетов)
    - `running` → `restarting` (при сбое процесса или stall)
    - переход в `failed` (при ошибке старта или исчерпании лимита перезапусков)
    - `source_unavailable` / `session_unavailable` (при исчезновении экрана/камеры или блокировке сессии)
- **Координатная модель**:
  1. Определение шкалы: если `x > 1.0` или `y > 1.0`, координаты масштабируются от `65535.0f`, иначе от `1.0f`.
  2. Перевод в экранные виртуальные координаты кадра с учётом смещения дисплея (`desktop_rect.x`, `desktop_rect.y`) и возможных отрицательных координат в многомониторных конфигурациях:
     ```c
     int target_x = desktop_rect.x + (int)(norm_x * desktop_rect.width);
     int target_y = desktop_rect.y + (int)(norm_y * desktop_rect.height);
     ```
  3. Нормализация для `SendInput` относительно полного виртуального рабочего стола (`SM_XVIRTUALSCREEN`, `SM_YVIRTUALSCREEN`, `SM_CXVIRTUALSCREEN`, `SM_CYVIRTUALSCREEN`).
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
