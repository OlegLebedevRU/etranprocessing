# Changelog — Leo4 Setup (`l4setup`)

## [1.7.2] — 2026-09-14

### Changed & Improved
- Обновление версионирования до 1.7.2.
- Зафиксированы контракты кодов возврата CLI (0=ready, 10=activation_required, 11=ready_for_online, 12=degraded, 20=access_denied, 21=unsupported_os, 22=drainage_failed, 23=payload_failed, 24=service_failed, 25=pin_rejected, 26=activation_timeout, 27=critical_smoke_failed, 28=setup_busy, 29=downgrade_blocked, 30=preflight_failed, 31=cancelled).
- Поддержка события graceful stop `Global\L4Desk_Stop_<SN>` и таймаутов служб.

## [1.7.1] — 2026-09-13

### Changed
- Включение rtp_tunnel_enabled: true по умолчанию (Lazy Connect для видеопотока L4RTP/1) и запуск службы Leo4Proxy с аргументом --rtp-tunnel.

## [1.7.0] — 2026-09-13

### Changed
- **Умный Cert Guard**: автоматическое разрешение обновления сертификата по новому PIN-коду при сроке действия <= 30 дней без флага принудительного перевыпуска (`--force` / `--force-reissue`).

## [1.6.0] — 2026-09-12

### Added
- **Единый Zero-Touch инсталлятор (`l4setup.exe`)**: 32-битный bootstrapper (`/SUBSYSTEM:WINDOWS,6.01`, MSVC `/MT`), совместимый с Windows 7 SP1+ (x86 и x64), Windows 10/11 и Windows Server.
- **Интеллектуальный менеджер UAC elevation**: Запуск в контексте `asInvoker`. Команды `--version` и `--help` исполняются без повышения привилегий. При необходимости elevation выполняется через `ShellExecuteExW(runas)`; отказ пользователя возвращает код `20`.
- **Preflight Engine**:
  - Детекция истинной версии ОС через `RtlGetVersion` (отказ с кодом `21` при NT < 6.1).
  - Автоматическая настройка TLS 1.2 в Schannel Client на Windows 7 SP1 (реестр `HKLM\...\SCHANNEL\Protocols\TLS 1.2\Client`).
  - Проверка наличия Universal C Runtime (`ucrtbase.dll`) с предупреждением `ucrtbase_missing`.
  - Идемпотентная настройка правил Windows Firewall (`L4Tools-*`) для портов `1883`, `18443`, `18883` и бинарных файлов утилит.
- **Drainage Engine**:
  - Остановка служб `Leo4Proxy`, `mosquitto`, `L4Con`, `L4Superv` с таймаутом 5 с и принудительным завершением по PID при зависании.
  - Завершение осиротевших процессов `ffmpeg.exe` и `l4desk.exe` строго внутри каталога установки `%dest%` (защита чужих процессов).
  - Освобождение loopback-портов `1883`, `18443`, `18883` через `GetExtendedTcpTable` с гарантией невмешательства в чужие процессы (при занятости сторонним процессом возвращается отказ `22`).
  - Детекция активного видеострима `ffmpeg`: в `--silent` прерывание с кодом `22` и предупреждением `active_stream`; в интерактивном режиме — запрос подтверждения у пользователя.
- **Распаковка и Rollback**:
  - Распаковка архивов во временный каталог `.staging-<pid>\`.
  - Атомарная замена рабочих подкаталогов и сохранение одной предыдущей версии в `rollback\<prev_version>\`.
  - Сохранение пользовательских конфигураций (`l4superv.json`, `state.json`, `mosquitto.conf`, `pending_pin.json`) и каталогов логов.
  - Настройка системного `Path` и переменной `MOSQUITTO_DIR` через реестр и широковещательное уведомление `WM_SETTINGCHANGE`.
  - Регистрация служб в SCM (повторная регистрация только при изменении пути бинарника).
- **Фаза 3 — Сертификаты и PIN (Cert Discovery & Guard)**:
  - Автоматическое обнаружение валидного сертификата терминала в `LocalMachine\MY`.
  - Повторное использование сертификата (`cert.reused = true`) без обращений к CA.
  - Игнорирование лишнего `--pin` с предупреждением `cert_reused_pin_ignored` при валидном сертификате.
  - Поддержка принудительного перевыпуска через `--force-reissue --pin <PIN>`.
  - Автономный режим ожидания сети: при недоступности CA после 3 попыток PIN шифруется в `pending_pin.json` (DPAPI Local Machine, ACL SYSTEM + Administrators, TTL 72 ч), статус `ready_for_online` (код `11`).
  - Интерактивный GUI-диалог ввода PIN или режим ожидания PIN `standby_waiting_pin` (код `10`).
  - Сигнализация службе `L4Superv` (`SERVICE_CONTROL 128`) и мониторинг перехода в `ready` за 15 секунд.
- **Фаза 4–5 — Оркестрация и Smoke-тестирование**:
  - Запуск служб в строгом порядке: `Leo4Proxy` → `mosquitto` → `L4Con` → `L4Superv`.
  - Probes: проверка эндпоинта `/_leo4/info` по WinHTTP (критично), проверка сокета `127.0.0.1:1883` (критично), проверка сессии пользователя и запуска `l4desk.exe`, тестовый кадр `ffmpeg -f gdigrab`, статус блокировки рабочего стола.
  - Генерация отчета `install_summary.json` (UTF-8 без BOM, атомарно) и точечный патч `state.json` (`installed_version`, `installer_summary_path`).
- **Идемпотентность**:
  - Пропуск фаз дренажа, распаковки и регистрации служб при совпадении `installed_version == 1.6.0` и наличии файлов утилит (выполнение только фаз 3–5).
