# Changelog — l4con (Leo4 Diagnostic Console Agent)

All notable changes to the `l4con` component will be documented in this file.

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
