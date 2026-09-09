# Changelog — l4superv (Leo4 Supervisor & Orchestrator Suite)

All notable changes to the `l4superv` and `l4install` suite will be documented in this file.

## [1.3.0] - 2026-09-09

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
