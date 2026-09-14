# Changelog — l4con (Leo4 Diagnostic Console Agent)

All notable changes to the `l4con` component will be documented in this file.

## [1.7.2] - 2026-09-14

### Changed
- Версионирование компонентов синхронизировано до 1.7.2.

## [1.2.0] - 2026-09-02

### Fixed & Improved
- **Direct Shell & Executable Path Resolution**:
  - Implemented `resolve_cmd_path()`, `resolve_powershell_path()`, and `resolve_taskkill_path()` to ensure canonical resolution of `%COMSPEC%`, `%SystemRoot%\System32`, and `%SystemRoot%\Sysnative` across native 32-bit, native 64-bit, and WOW64 environments.
  - Formats quoted full path invocations (e.g. `"C:\Windows\System32\cmd.exe" /c ...` or `"C:\Windows\Sysnative\cmd.exe" /c ...`) preventing `ERROR_FILE_NOT_FOUND (error code 2)` during process spawning.
- **WOW64 Filesystem Redirection Management**:
  - Dynamically disables WOW64 file system redirection (`Wow64DisableWow64FsRedirection`) during `CreateProcessW` invocations in `command_runner_execute` and `command_runner_kill_process_tree`.
  - Fixes `Failed to create process: error code 2 (ERROR_FILE_NOT_FOUND)` when running 32-bit (x86) `l4con.exe` on 64-bit Windows machines executing `cmd.exe`, `powershell.exe`, or system utilities.
- **Robust Working Directory & PATH Resolution**:
  - Added recursive parent path detection in `command_runner_setup_environment` supporting nested `\x86`, `\x64`, and `\l4con` directory layouts.
  - Added validation of working directory existence before passing to `CreateProcessW`, falling back safely to base directory or process context.
  - Enriched extended `PATH` with architecture-specific and system directories (`C:\Windows\System32`, `C:\Windows`, `C:\Windows\System32\Wbem`, `C:\Windows\System32\WindowsPowerShell\v1.0`, `C:\Windows\Sysnative`).
- **Dual-Architecture Unified MSVC Build**:
  - Full support for native x86 (PE32) and x64 (PE32+) static builds via `build.cmd [all|x86|x64]`.

## [1.1.0] - 2026-08-29

### Added & Improved
- **Working Directory & Prompt Reporting**:
  - Automatically switches default working directory for spawned processes from `C:\Windows\system32` to the tools base path (`C:\l4tools`).
  - Streams active prompt line (`C:\l4tools> <command>`) as the initial output chunk to the remote console.
- **Automatic Environment PATH Enrichment**:
  - Implemented `command_runner_setup_environment()`: prepends `C:\l4tools`, `C:\l4tools\l4sql`, `C:\l4tools\l4con`, `C:\l4tools\l4pin`, `C:\l4tools\l4superv`, and `C:\l4tools\leo4proxy` to process `PATH`.
  - Enables zero-path execution for all bundled utilities (`l4sql`, `l4pin`, `l4superv`).
- **SCM Service Startup Hook**:
  - Integrated working directory and `PATH` setup inside `service_main()` for `L4Con` Windows Service.

## [1.0.0] - 2026-08-29

### Initial Release
- **Lightweight Diagnostic Agent**: Pure C / Win32 API implementation (`/MT` static CRT, zero external runtime dependencies).
- **MQTT Extra Service Role**:
  - Connects to local broker `127.0.0.1:1883`.
  - Implements mandatory MQTT presence flow: `dev/{SN}/svc` (`svc_online` on connect, `svc_offline` on disconnect and LWT).
  - Subscribes to `srv/{SN}/tsk` and publishes execution results to `dev/{SN}/out` and `dev/{SN}/res`.
- **Diagnostic Command RPC**:
  - Implements method `7001` (`CMD_DIAG_EXEC`) and method `7002` (`CMD_DIAG_CANCEL`).
  - Streaming stdout/stderr output chunking with sequence numbers.
- **Dynamic SN Discovery**: Queries `GET http://127.0.0.1:18443/_leo4/info` to obtain device SN upon startup.
- **Windows SCM Service Integration**: Runs as `L4Con` background service.
