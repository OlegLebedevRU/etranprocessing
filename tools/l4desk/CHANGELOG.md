# CHANGELOG: l4desk

## [Unreleased]

### Fixed

- Запуск `l4capture` использует физические координаты DPI; остановка потока подтверждает завершение дочернего процесса до события `stopped`.

### Changed
- INFO-лог типовой работы без ошибок очищен от рутинного шума; подробности доступны через `--verbose` (DEBUG):
  - `MQTT recv bytes_recvd=…` (PINGRESP/PUBACK) → DEBUG;
  - heartbeat `lease_renew`/`stream_renew`/`inventory_get` (`Received command`, `update_lease…`, `Lease renewed`) → DEBUG;
  - периодический `Published presence` → DEBUG (payload уже был DEBUG).
- В INFO остаются: connect/subscribe/shutdown, `stream_start`/`stream_stop`/input, `stream_event`, lifecycle child, WARN/ERROR (отказы, recovery, expiry).

## [1.7.5] - 2026-09-15

### Changed & Improved
- Версия компонента синхронизирована до 1.7.5 в заголовках, манифестах, ресурсах и протокольных тестах.
- Согласована работа с правилами брандмауэра Windows Defender Firewall для подсистемы видеостриминга FFmpeg и RTCP портов.

## [1.7.4] - 2026-09-14

### Changed & Improved
- Версия компонента синхронизирована до 1.7.4 в заголовках, манифестах, ресурсах и протокольных тестах.
- Обеспечена совместимость с запуском под повышенным уровнем целостности High Integrity Level из `l4superv` для инъекции событий ввода в окна приложений Администратора.

## [1.7.3] - 2026-09-14

### Added & Fixed
- В сообщения presence (`ctl_build_presence_payload`, `ctl_build_extended_presence_payload`) добавлена явная передача версии (`"version": "1.7.3"`) и поддерживаемых возможностей (`"capabilities": ["quick_actions", "shortcut_action", "right_click"]`).
- Устранена ошибка `consumer_version_unsupported` при отправке быстрых действий (`shortcut_action` F12, win_d, alt_f4) и клике правой кнопкой мыши из Web UI Menubuilder.
- Стабилизированы тесты краш-детекции оркестратора FFmpeg (`tests/test_orchestrator.c`).

## [1.7.2] - 2026-09-14

### Added & Changed
- Версионирование компонентов синхронизировано до 1.7.2.
- Реализована инъекция системных сочетаний клавиш `shortcut_action` (`win_d`, `alt_f4`, `f12`) с контролем фокуса и блокировкой на безопасных десктопах Winlogon/UAC.
- Контракт типов команд ввода: `mouse_click` (кнопки только `"left"` и `"right"`), `key_event` и `shortcut_action` (действия `"f12"`, `"alt_f4"`, `"win_d"`).

## [1.5.0] - 2026-09-11 (Ревизия контракта Step 4: строгая валидация lease_renew, защита эпохи и контрактные фикстуры)

### Added
- **Строгая валидация входящих команд `lease_renew` / `stream_renew` (`src/ctl_protocol.c`)**:
  - Проверка соответствия эпохи потока `stream_instance_id`: при несовпадении с текущей активной трансляцией возвращается NACK `stream_mismatch`.
  - Проверка временных меток и границ `expires_at_ms`: отклоняются нулевые, отрицательные, просроченные (`expires_at_ms <= now_ms`) и аномальные/переполненные метки (`> 5000000000000LL`) с кодом NACK `invalid_payload`.
  - Fail-closed инвариант: локальный срок аренды `lease_expires_at_ms` обновляется строго при успешной валидации; фиктивный ACK без продления таймера исключён.
- **Поддержка изоляции сессионного мьютекса при тестировании (`src/main.c`)**:
  - При явном указании серийного номера `--sn <sn>` (в отладочном режиме или интеграционных тестах) мьютекс именуется как `Local\L4Desk_SingleInstance_<SN>`, что предотвращает конфликты с работающим в фоновом режиме экземпляром службы.
- **Интеграционные и контрактные тесты (`tests/test_ctl_protocol.c`, `tests/integration_test.py`)**:
  - Репродукционный тест серверного инцидента 2026-09-11 (F1/F2): проверка фикстуры с некорректным полем `cmd_id` вместо `command_id` с подтверждением возврата NACK `invalid_payload` и неизменности локального срока аренды.
  - Проверка канонической фикстуры с UUID в поле `command_id`: успешный ACK, обновление `lease_expires_at_ms`.
  - Проверка идемпотентного кеширования дубликатов renew в LRU-кеше дедупликации.
  - Проверка отклонения renew с несовпадающим `stream_instance_id` (NACK `stream_mismatch`).
  - Улучшена надежность `integration_test.py`: использование уникального MQTT `client_id` для предотвращения вытеснения сессий брокером Mosquitto и внутренняя очередь сообщений в `SimpleMQTTClient`.

## [1.4.0] - 2026-09-11 (Команда продления аренды `lease_renew`, Reconcile-события и верификация recovery)

### Added
- **Команда продления аренды `lease_renew` / `stream_renew` (`src/ctl_protocol.c`)**:
  - Добавлена обработка входящих команд `lease_renew` и `stream_renew` в `ctl_handle_command`:
    - Валидация состояния активного стрима: стрим должен находиться в состоянии `running` или `restarting` (при других состояниях возвращается NACK `stream_not_running`).
    - Проверка совпадения идентификатора аренды `lease_id` с активным стримом (при несовпадении возвращается NACK `lease_mismatch`).
    - Обновление срока действия локального fail-closed сторожевого таймера супервизора через `ffmpeg_supervisor_update_lease(lease_id, expires_at_ms)`.
    - Формирование ответа ACK и кеширование в bounded LRU-кеш дедупликации.
  - Устранена преждевременная остановка трансляции локальным сторожевым таймером (`lease_expired`) при непрерывной сессии просмотра без кликов мыши.
- **Оповещение сервера при `reconcile` осиротевших процессов (`src/ffmpeg_supervisor.c`, `src/mqtt_client.c`)**:
  - В `ffmpeg_supervisor_reconcile()` при обнаружении и завершении осиротевшего процесса FFmpeg (подтвержденного по `creation_time`) инициируется отправка события:
    `notify_stream_event(stream_id, "stopped", "agent_restart_reconcile")`.
  - В `src/mqtt_client.c` добавлена буферизация событий стрима (`has_pending_stream_event`), возникших до установки MQTT-соединения, с немедленной отправкой накопленного события `publish_stream_event` сразу после получения `CONNACK` и публикации первичного статуса `presence online`.
  - Сервер `app1` и веб-интерфейс гарантированно получают статус завершения предыдущего стрима после перезапуска агента (`taskkill l4desk.exe`), предотвращая ложное отображение статуса «В эфире».
- **Методика верификации Recovery супервизора**:
  - Зафиксировано разграничение между штатной остановкой по безопасности (`lease_expired`, после которой перезапуск категорически запрещён) и сбоем процесса FFmpeg во время активной аренды (`unexpected_exit`).
  - Регламентирована процедура проверки recovery через принудительное завершение только процесса FFmpeg (`Get-Process ffmpeg | Stop-Process -Force`), подтверждающая прохождение цепочки:
    `unexpected_exit` -> публикация `stream_event [restarting]` -> экспоненциальный backoff -> запуск нового FFmpeg -> публикация `stream_event [running] reason="recovered"`.
- **Расширение модульных тестов (`tests/test_ctl_protocol.c`, `tests/test_orchestrator.c`)**:
  - В `test_ctl_protocol.c` добавлены тесты для `lease_renew` (NACK `stream_not_running`, NACK `lease_mismatch`, ACK и продление `lease_expires_at_ms`, проверка псевдонима `stream_renew`).
  - В `test_orchestrator.c` добавлена проверка регистрации callback-события и валидация отправки `stopped` / `agent_restart_reconcile` при обнаружении осиротевшего дочернего процесса.
  - Оптимизирован цикл тиков в тесте лимита бюджета перезапусков (тест 10) для гарантированного исчерпания всех 5 попыток и перехода в `failed` / `restart_limit`.

## [1.3.0] - 2026-09-11 (PROMPT AGENT 2.2: Closed recovery-loop & Local fail-closed lease watchdog)

### Added
- **Замкнутый reconciliation / recovery-loop супервизора FFmpeg (`src/ffmpeg_supervisor.c`)**:
  - Сохранение активных параметров трансляции: `mode` (desktop/usb-camera), `source_id`, `profile`, `stream_instance_id`, `lease_id`, а также параметров камеры и геометрии дисплея.
  - Автоматический перезапуск процесса FFmpeg при неожиданном завершении процесса (`STILL_ACTIVE` false) или зависании кадров (stall > 10 с).
  - Экспоненциальный backoff со случайным jitter: `delay_sec = min(30, (1 << restart_count)) + (rand() % 1000) / 1000.0`.
  - Ограничение бюджета перезапусков (restart budget): не более 5 попыток за 10-минутное скользящее окно. При исчерпании лимита супервизор переходит в состояние `failed` (`reason="restart_limit"`), отправляет событие `stream_event` и прекращает попытки перезапуска.
  - Успешный перезапуск переводит стрим в состояние `running` с отправкой события `stream_event(state="running", reason="recovered")`.
  - Инвариант одного процесса: обязательное освобождение дескрипторов старого процесса и закрытие Job Object перед созданием нового процесса.
  - Отмена перезапуска: команда `stream_stop` немедленно отменяет любой запланированный restart (`next_restart_time = 0`), штатно останавливает процесс и переводит супервизор в `stopped`.
- **Локальный сторожевой таймер аренды (Fail-Closed Lease Watchdog, `src/ctl_protocol.c`, `src/ffmpeg_supervisor.c`)**:
  - Обновление `lease_expires_at_ms` при получении валидных команд управления (`stream_start`, `pointer_move`, `mouse_click`, `key_event`).
  - Мониторинг срока действия аренды в тике супервизора: если локальное время терминала превышает `expires_at_ms + 5000` (5 секунд льготного периода grace) и продления не поступило, супервизор выполняет принудительный fail-closed останов:
    - Вызов `input_release_all()` для немедленного отпускания всех клавиш и кнопок мыши.
    - Останов процесса FFmpeg и очистка дескрипторов.
    - Перевод состояния стрима в `stopped` с `reason="lease_expired"`.
    - Отправка события `stream_event(state="stopped", reason="lease_expired")`.
- **Сброс зажатого ввода `input_release_all()` (`src/input_inject.c`, `src/input_inject.h`)**:
  - Сброс зажатых кнопок мыши (`MOUSEEVENTF_LEFTUP`, `MOUSEEVENTF_RIGHTUP`, `MOUSEEVENTF_MIDDLEUP`).
  - Сброс всех активных клавиш клавиатуры (`KEYEVENTF_KEYUP` с правильными скан-кодами и `KEYEVENTF_EXTENDEDKEY` для навигационных клавиш).
  - Отслеживание состояния инъекции `down`/`up` и проверка глобального состояния `GetAsyncKeyState`.
- **Расширенные модульные тесты (`tests/test_orchestrator.c`, `tests/test_ctl_protocol.c`)**:
  - Тест 9: автоперезапуск recovery-loop (переход `restarting` -> `running` / `recovered`).
  - Тест 10: лимит бюджета перезапусков (> 5 попыток -> `failed` / `restart_limit`).
  - Тест 11: немедленная отмена перезапуска при `stream_stop`.
  - Тест 12: fail-closed остановка стрима при истечении срока аренды (lease watchdog, grace 5 с).
  - Тест 13: валидация вызова `input_release_all()`.
  - Тест монотонного обновления срока аренды в протоколе `ctl`.

## [1.2.0] - 2026-09-11 (AGENT 3: ctl v1 protocol sync & H.264 Baseline Level 3.1)

### Changed
- **FFmpeg H.264 Baseline Level 3.1 под Janus WebRTC Streaming Plugin**:
  - В аргументы командной строки FFmpeg (`src/ffmpeg_cmdline.c`) для desktop и camera явно добавлены флаги:
    `-profile:v baseline -level 3.1 -x264-params bframes=0:force-cfr=1`.
  - Установлен строгий фиксированный GOP: `-g %d -keyint_min %d` (`gop = params.fps * 2`) и отключение внеплановых I-кадров `-sc_threshold 0`.
  - Обеспечено полное соответствие SDP-заявке Janus: `videofmtp="profile-level-id=42e01f;packetization-mode=1"`.

- **Протокол ctl v1 и жизненный цикл stream_event (`src/ctl_protocol.c`, `src/ffmpeg_supervisor.c`)**:
  - Строгая валидация утверждённого enum состояний стрима:
    `stopped`, `starting`, `running`, `stopping`, `restarting`, `failed`, `source_unavailable`, `session_unavailable`.
  - Обеспечена отправка событий `stream_event` на всех фазах жизненного цикла:
    - `starting` -> `running` (после успешного запуска процесса)
    - `running` -> `stopping` (при начале остановки или switch)
    - `stopping` -> `stopped` (после фактического завершения процесса и освобождения сокетов)
    - `running` -> `restarting` (при обнаружении сбоя процесса или stall)
    - переход в `failed` (при ошибке старта или исчерпании лимита перезапусков)
    - переходы в `source_unavailable` / `session_unavailable`
  - Ответ `ack` на команду `stream_stop` отправляется строго **после** фактического завершения процесса FFmpeg, закрытия дескрипторов и освобождения сетевых сокетов.

- **Синхронизация клавиатурного ввода `key_event` (`src/ctl_protocol.c`, `src/input_inject.c`)**:
  - Проверка активного стрима: `state == STREAM_STATE_RUNNING` и `mode == STREAM_MODE_DESKTOP`.
  - Проверка соответствия контекста: `lease_id`, `desktop_id`, `stream_instance_id` с возвратом соответствующих NACK (`lease_mismatch`, `desktop_mismatch`, `stream_mismatch`).
  - Проверка локальной политики выбранного экрана: `policy == POLICY_INPUT` (возврат `source_not_allowed` при политике только просмотра или запрета).
  - Whitelist виртуальных клавиш: `0x08` (Backspace), `0x09` (Tab), `0x0D` (Enter), `0x1B` (Esc), `0x20` (Space), `0x2E` (Delete), `0x25..0x28` (Стрелки), `0x30..0x39` (0-9), `0x41..0x5A` (A-Z), `0x70..0x7B` (F1-F12), Numpad, модификаторы и OEM-символы.
  - Блокировка деструктивных клавиш (Win-клавиши, комбинация Ctrl+Alt+Del).
  - Инъекция через `SendInput` с поддержкой режимов `down`, `up`, `press` и флагом `KEYEVENTF_EXTENDEDKEY` для навигационных клавиш.

- **Координатная модель мыши (`src/input_inject.c`, `src/ctl_protocol.c`)**:
  - Поддержка шкал `0.0..1.0` (float) и `0..65535` (integer): автоматическое масштабирование от `65535.0f` при значениях `> 1.0`.
  - Перевод нормализованных координат в виртуальные координаты монитора с учётом смещения (`desktop_rect.x`, `desktop_rect.y`) и возможных отрицательных координат в многомониторных конфигурациях.
  - Нормализация для `SendInput` относительно полного виртуального рабочего стола (`SM_XVIRTUALSCREEN`, `SM_YVIRTUALSCREEN`, `SM_CXVIRTUALSCREEN`, `SM_CYVIRTUALSCREEN`).

### Fixed
- Устранено предупреждение C4201 из системного заголовка `olectl.h` в `display_inventory.c`.
- Сборка `tools/l4desk/build.cmd all` компилируется под x86 и x64 со строгим уровнем предупреждений `/W4` с нулевым количеством ворнингов.

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
