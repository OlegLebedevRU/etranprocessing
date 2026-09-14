# Changelog — l4superv (Leo4 Supervisor & Orchestrator Suite)

All notable changes to the `l4superv` and `l4install` suite will be documented in this file.

## [1.7.4] - 2026-09-14

### Changed & Improved
- **Адаптивное повышение прав токена (`sp_select_target_token`)**:
  - Реализован автоматический (best-effort) выбор `TokenLinkedToken` (High Integrity Level) для сессий с `TokenElevationTypeLimited` (UAC Split-Token администратора) для устранения блокировок UIPI при работе ПО киоска от имени Администратора.
  - Режим `l4desk_mode` по умолчанию переведен в `user_session_high_il`.
  - Безопасный fallback для непривилегированных учетных записей (`TokenElevationTypeDefault`) без сбоев `insufficient_integrity`.
  - В журнал сторожевого таймера (`orchestrator`) добавлено логирование фактического уровня целостности (`High`, `Medium`, `Low`) запущенного процесса `l4desk`.

## [1.7.2] - 2026-09-14

### Changed & Improved
- Версионирование синхронизировано до 1.7.2.
- Завершение службы l4superv и дочерних процессов производится через сигналы SCM и событие `Global\L4Desk_Stop_<SN>`, завершающее Job Object и дочерние процессы без использования taskkill.

## [1.7.1] - 2026-09-13

### Changed
- Включение rtp_tunnel_enabled: true по умолчанию (Lazy Connect для видеопотока L4RTP/1) и запуск службы Leo4Proxy с аргументом --rtp-tunnel.

## [1.7.0] - 2026-09-13

### Changed
- **Умный Cert Guard**: автоматическое разрешение обновления сертификата по новому PIN-коду при сроке действия <= 30 дней без флага принудительного перевыпуска (`--force` / `--force-reissue`).

## [1.6.0] - 2026-09-12

### Added & Improved
- **Active Wait Tick in Standby Mode (`S10`)**:
  - Reduced poll interval during Standby mode to 5 seconds (`standby_poll_sec`, configurable, default 5s).
  - Standby to Active transition completes and starts `l4desk` in <= 15 seconds after certificate appearance, measured and logged as `[WAIT] activation completed in N ms`.
- **Cert Discovery Integration in Orchestrator Tick**:
  - Integrated `cert_discovery` scanning `LocalMachine\MY` on each tick.
  - Added warning log `proxy_cert_mismatch` when certificate is `CERT_VALID` in Windows store but `leo4proxy` reports `certificate_found: false` for > 2 consecutive ticks.
- **Delayed PIN Provisioning (`pending_pin.json`)**:
  - Periodic check in Standby mode every 30 seconds (`pending_pin_check_sec`, default 30s).
  - Automatic validation of `schema: 1` and `expires_at` (expired files securely overwritten with zeros and deleted with `pending_pin_expired` log).
  - Decrypts `pin_dpapi` using DPAPI machine scope (`CryptUnprotectData`, `CRYPTPROTECT_UI_FORBIDDEN`).
  - Probes CA reachability with 5-second TCP connection check (`http_local` or cloud `iot-processing.ru:443`).
  - Executes `%base%\l4pin\l4pin.exe <PIN>` with 60-second timeout, piping and masking stdout (`pin=***`).
  - Immediately zeroes command line and PIN memory with `SecureZeroMemory` after `CreateProcessW`.
  - Automatically deletes file and fires force-tick on success (code 0 and `CERT_VALID`), deletes file on CA PIN rejection (code 25), and retains for retry on network/transient failures.
- **Force-Tick Control Mechanism**:
  - Added `SERVICE_CONTROL 128` handling in `HandlerEx` for immediate cycle wake-up via event signaling.
  - Added CLI command `l4superv.exe --tick` to send control 128 to running service via Windows `ControlService`.
  - Added CLI command `l4superv.exe --version` / `-v`.
- **State Schema Extension & Key Preservation (`state.json`)**:
  - Added `installed_version` (SemVer resolved from `VERSIONINFO` or preserved if written by installer), `installer_summary_path`, and `last_cert_state`.
  - Dynamic preservation of all unknown JSON keys on state update and reserialize.
- **Diagnostics Logging**:
  - Standardized wait tick log formats: `[WAIT] standby: cert_state=... pending_pin=... ca=... next=...`, `[WAIT] pending_pin found, expires_at=..., ca=reachable -> l4pin`, `[WAIT] activation completed in N ms`.

### Added & Improved
- **User Session Process Orchestration (`session_proc`)**:
  - Implemented `session_proc` helper leveraging `WTSQueryUserToken`, `DuplicateTokenEx`, `CreateEnvironmentBlock`, and `CreateProcessAsUserW` to orchestrate processes in active interactive console user sessions (`Session != 0`).
  - Added support for launching and supervising `l4desk` (Remote Input Control Agent) with hidden window flags (`CREATE_NO_WINDOW`) without stealing focus from kiosk application.
  - Watchdog tracking: detects console session changes (logoff/logon), safely stops old process and starts new in current active session.
  - Exponential backoff recovery (5s -> 10s -> 30s) on crash.
  - Graceful termination via named event `Global\L4Desk_Stop_<SN>`.
- **Status Reporting**:
  - `l4superv --status` displays `l4desk: RUNNING (pid N, session M)` or `STOPPED`.
- **Packaging & Staging**:
  - Integrated `l4desk` into `tools.zip` packaging (`pack_zip.cmd`), `tools/dist`, and Windows 7 SP1 installer.

## [1.2.0] - 2026-09-02

### Added & Documented
- **Dual-Architecture Unified Toolchain (x86 / x64)**:
  - Added full dual-target native MSVC build scripts producing genuine 32-bit PE32 and 64-bit PE32+ binaries for `l4con`, `l4superv`, `l4install`, `leo4proxy`, `l4pin`, `l4sql`, and `leo4-simple-svc-mqtt`.
- **Anti-Clone Reset & Certificate Store Documentation**:
  - Documented hardware fingerprint mismatch behavior (`auto_reset_on_clone`) in `README.md` and `terminal-tools-user-guide.md`, detailing automatic cleanup of terminal certificates (`leo4.ru`, `forpay.ru`) from `LocalMachine\MY`.
  - Added troubleshooting guidance on avoiding unexpected certificate cleanup when cloning or manually migrating tool folders across machines.
  - Clarified non-blocking shared read-only nature of Windows CryptoAPI store polling across concurrent processes.

## [1.1.0] - 2026-08-29

### Added & Improved
- **SQL Client Integration (`l4sql`)**:
  - Added `l4sql` directory staging into `tools.zip` and automated extraction to `C:\l4tools\l4sql\l4sql.exe` during `l4install`.
- **System PATH Registration**:
  - `l4install` registers `C:\l4tools`, `C:\l4tools\l4sql`, `C:\l4tools\l4pin`, `C:\l4tools\l4con`, `C:\l4tools\l4superv` in system environment variable `Path` (`HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment`) and broadcasts `WM_SETTINGCHANGE`.
- **Diagnostics Output**:
  - Added `l4sql` status verification to `l4install` final output summary.

## [1.0.1] - 2026-08-29

### Added
- **Root Distribution Documentation**: Included `terminal-tools-user-guide.md` in the root of the distribution package `tools.zip` and automated its unpacking alongside `l4install.exe`.
- **Extended Service Runtime Inspection**: Detailed runtime paths (`installed_path`, `runtime_pid`, `runtime_exe`, `config_path`, `log_path`, `path_match`, `status`) saved into `state.json` and reported via `l4superv --status` and `l4install`.
- **Foreign Service Cleanup**: Automatic detection, termination, and SCM removal of existing services registered with mismatched paths during `l4install`.
- **Log Directory Permissions**: Automated DACL/SDDL configuration on `mosquitto\log` ensuring full read/write access for all users and services.

### Changed
- **Mosquitto SCM Service Name**: Standardized service name as `mosquitto` with `run` parameter for reliable native execution.
- **Installer Distribution**: Added self-copy of `l4install.exe` to `%L4_TOOLS_BASE_PATH%` (`C:\l4tools`) for subsequent maintenance and idempotent re-execution.

---

## [1.0.0] - 2026-08-29

### Initial Release
- **Zero-Touch Windows Service Supervisor (`l4superv.exe`)**:
  - Pure C / Win32 API implementation (`/MT` static CRT, zero external runtime dependencies).
  - Background polling of `leo4proxy` HTTP discovery (`GET http://127.0.0.1:18443/_leo4/info`).
  - Dynamic transition between **Standby Mode** (isolated local broker on `:1883`) and **Active Bridge Mode** (TLS upstream bridge to `127.0.0.1:18883`).
  - Automatic `mosquitto.conf` template generation with active `SN` and MQTT role ACLs (`main_app`, `extra_service`).
  - Service lifecycle coordination: automatic restart of `mosquitto` and `l4con` upon certificate enrollment or SN changes.
  - Built-in Watchdog monitoring running state of `Leo4Proxy`, `mosquitto`, and `L4Con`.
- **Single-Click Installer & Deployment Utility (`l4install.exe`)**:
  - Embedded UAC elevation manifest (`requireAdministrator`).
  - Embedded ZIP extraction engine using `miniz.c`.
  - Automated directory hierarchy setup (`%L4_TOOLS_BASE_PATH%`, default `C:\l4tools`).
  - Windows SCM registration and startup for `Leo4Proxy`, `mosquitto`, `L4Con`, and `L4Superv`.
  - Hardware Fingerprint binding (`MachineGuid` + `VolumeSerialNumber`) for Anti-Clone protection.
- **State Management**:
  - Atomic JSON persistence in `state.json`.
  - Non-destructive CLI diagnostics (`l4superv.exe --status`, `l4superv.exe --check`).
