# CHANGELOG: l4desk

## [1.1.0] - 2026-09-10 (PROMPT 4.2)

### Added
- **Оркестрация FFmpeg**:
  - Запуск процесса FFmpeg (`CreateProcessW`) в скрытом режиме (`SW_HIDE`), перенаправление stdin (для soft stop по `q\n`) и stdout/stderr.
  - Логирование в ротационные файлы `<base>\ffmpeg\log\ffmpeg_<stream_instance_id>.log` (5 МБ, до 5 файлов).
  - Удержание процесса FFmpeg в Job Object (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`).
  - Whitelist параметров командной строки (`libx264`, `zerolatency`, профили `default` и `low`).
  - Метаданные владельца стрима `-metadata comment=l4desk:<stream_instance_id>`.
  - Модули `ffmpeg_cmdline.c/.h` и `ffmpeg_supervisor.c/.h`.
- **Инвентаризация дисплеев и DirectShow-камер**:
  - Перечисление мониторов (`EnumDisplayMonitors` + `EnumDisplayDevicesW`) со стабильным идентификатором `disp:<fnv1a_hex>`.
  - Поддержка виртуальных мониторов с отрицательными координатами (`SM_XVIRTUALSCREEN`, `SM_YVIRTUALSCREEN`).
  - Перечисление камер через DirectShow (`CLSID_VideoInputDeviceCategory`) со стабильным `cam:<fnv1a_hex>`.
  - Загрузка локальной политики из `l4desk_policy.ini` (`input`, `view`, `denied`).
  - Модуль `display_inventory.c/.h`.
- **Удалённый ввод**:
  - Маппинг нормализованных координат `[0.0, 1.0]` в виртуальный экран монитора с учётом отрицательного origin.
  - Обработка кликов мыши `left`, `right`, `middle`.
  - Клавиатурный ввод `key_event` с whitelist допустимых виртуальных кодов клавиш и блокировкой деструктивных комбинаций (Win, Ctrl+Alt+Del).
  - Поддержка ввода Unicode-символов (`KEYEVENTF_UNICODE`).
- **Протокол `ctl` (Envelope v1)**:
  - Обработка команд `inventory_get`, `stream_start`, `stream_stop`, `pointer_move`, `mouse_click`, `key_event`.
  - Controlled switch между источниками (бесшовный переход со статусом `switched`).
  - Расширенный `presence` (включает блоки `screen`, `inventory`, `stream`).
  - Публикация событий изменения состояния стрима `stream_event`.
  - Исчерпывающий набор кодов `nack` (`lease_mismatch`, `desktop_mismatch`, `stream_mismatch`, `source_not_allowed`, `source_unavailable`, `session_unavailable`, `busy_transition`, `ffmpeg_missing`, `ffmpeg_integrity`, `input_not_allowed_in_camera_mode`, `invalid_profile` и др.).
- **Отказоустойчивость и Reconciliation**:
  - Сохранение состояния в `<base>\l4desk\state\ffmpeg_state.json`.
  - Реконсиляция процессов при старте с защитой от PID reuse (сверка `creation_time` и метаданных).
  - Автоматический перезапуск упавшего процесса с экспоненциальным backoff (до 5 попыток за 10 мин).
  - Health-check (детекция stall кадров > 10 с, контроль интерактивности сессии).
  - Мягкая остановка FFmpeg при сигнале службы `Global\L4Desk_Stop_<SN>`.
- **Тестирование**:
  - Мок-бинарник `fake_ffmpeg.exe`.
  - Модульные тесты `test_ctl_protocol.exe` и `test_orchestrator.exe` (`tests/run_tests.cmd`).
  - Сквозные интеграционные тесты `integration_test.py` и `integration_test.ps1`.

## [1.0.0] - 2026-09-09

### Added
- Первоначальный релиз терминального агента удалённого ввода `l4desk.exe`.
- Zero-dependency архитектура на C / Win32 API со статической линковкой `/MT`.
- Встроенный клиент MQTT 3.1.1 поверх WinSock2 для подключения к localhost Mosquitto (`client_id = svc_desk`).
- LWT и периодический статус Presence в `dev/<SN>/ctl` (`retain=1, qos=1`).
- Подписка на команды управления `srv/<SN>/ctl` (QoS 1).
- Обработка `pointer_move` (best-effort без ACK, нормализованные координаты `0..65535`).
- Обработка `mouse_click` с подтверждением ACK / NACK (QoS 1, `retain=0`).
- Инъекция ввода через `SendInput` с флагами `MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK`.
- Определение состояния интерактивного экрана через `OpenInputDesktop` (детекция блокировки экрана / UAC).
- Bounded LRU-кеш дедупликации (256 записей) по `command_id`.
- Обнаружение серийного номера (SN) терминала через WinHTTP `GET http://127.0.0.1:18443/_leo4/sn`.
- Поддержка запуска в пользовательской сессии через `l4superv` (`--run`) и отладочного режима (`--console`).
- Совместимость с Windows 7 SP1+ (x86) и Windows 10/11 (x64).
