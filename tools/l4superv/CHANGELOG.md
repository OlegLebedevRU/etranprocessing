# Changelog — l4superv (Leo4 Supervisor & Orchestrator Suite)

All notable changes to the `l4superv` and `l4install` suite will be documented in this file.

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
