# FFmpeg Media Engine Package for Leo4

## 1. Overview

`ffmpeg` is the low-latency media capture and RTP streaming engine for the Leo4 terminal remote desktop ecosystem. It is invoked on-demand by `l4desk.exe` in the active interactive user session to stream the kiosk display back to administrative operators via `leo4proxy` and `l4media`.

### Architecture & Process Hierarchy
```
Windows Service Manager (Session 0)
  └─ l4superv.exe (Supervisor & Watchdog)
       └─ [Job Object: KILL_ON_JOB_CLOSE]
            └─ l4desk.exe (Interactive Console Session)
                 └─ ffmpeg.exe (RTP screen/display capture)
```

- **No Windows Service**: FFmpeg is never registered as a standalone Windows service. It runs only when streaming is active.
- **Process Tree Isolation**: `l4superv` places `l4desk.exe` inside a Windows **Job Object** with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`. When `l4desk` spawns `ffmpeg.exe`, the OS automatically places FFmpeg in the same Job Object. If the user logs off, the session changes, or the supervisor terminates, all processes in the tree (`l4desk` and `ffmpeg`) are guaranteed to terminate with zero orphan processes.
- **Grace Period**: When stopping services, `l4superv` signals `Global\L4Desk_Stop_<SN>` and grants a grace period of **≥ 8000 ms** for `l4desk` and `ffmpeg` to close streaming sockets cleanly before terminating.

---

## 2. Terminal Layout & Contracts

- **Binary Location**: `C:\l4tools\ffmpeg\ffmpeg.exe`
- **Shared Libraries**: None (Zero-Dependency static executable)
- **Logs**: `C:\l4tools\ffmpeg\log\ffmpeg_<stream_id>.log`
- **State File**: `C:\l4tools\l4desk\state\ffmpeg_state.json`

### `ffmpeg_state.json` Schema
```json
{
  "pid": 12345,
  "creation_time": 133500000000000000,
  "stream_instance_id": "stream_123",
  "session_id": 1,
  "mode": "desktop",
  "source_id": "disp:1a2b3c4d",
  "state": "running",
  "owner": "l4desk",
  "sn": "TERM001"
}
```

---

## 3. Package Structure (`ffmpeg.zip`)

The archive `tools/dist/ffmpeg.zip` (~25.2 MB) is structured for multi-architecture compatibility with `l4install`:

```
ffmpeg\
  x64\
    ffmpeg.exe        (64-bit static executable, ~33 MB)
    LICENSE           (GPL v3)
  x86\
    ffmpeg.exe        (32-bit static executable, ~26 MB)
    LICENSE           (GPL v3)
  ffmpeg.sha256       (SHA-256 integrity manifest for extracted files)
  VERSION.txt         (Architectural build details)
  SOURCES.md          (Source containers and build scripts)
  README.md
```

---

## 4. System Prerequisites

### Windows 10 / Windows 11 / Windows Server 2016–2025
- No additional runtime packages required. Universal CRT is native to the OS.

### Windows 7 SP1 (x86 & x64) / Windows 7 Embedded / POSReady 7 (x86)
- Built against Microsoft Universal CRT (`ucrtbase.dll`).
- **Prerequisite**: Ensure Universal CRT update (**KB2999226** or Convenience Rollup **KB3125574**) or Visual C++ 2015–2022 Redistributable is installed on the terminal.

---

## 5. Safe & Atomic Installation via `l4install`

`l4install` executes safe and atomic installations:
1. **Active Stream Check**: Inspects `C:\l4tools\l4desk\state\ffmpeg_state.json`. If a running PID is detected, installation halts immediately to prevent stream disruption.
2. **Staging & Validation**: Unpacks files into `__ffmpeg_staging\`, verifies all SHA-256 hashes against `ffmpeg.sha256`.
3. **Atomic Replacement**:
   - Renames existing `C:\l4tools\ffmpeg` to `ffmpeg.old`.
   - Renames staged `ffmpeg.new` to `ffmpeg`.
   - Cleans up `ffmpeg.old`.
   - If a sharing violation occurs, rolls back cleanly to the original directory.
4. **Log Directory Creation**: Ensures `C:\l4tools\ffmpeg\log\` is created with proper permissions.

---

## 6. Building the Package

Run:
```cmd
tools\ffmpeg\pack_ffmpeg.cmd
```
Artifacts generated:
- `tools\dist\ffmpeg.zip` (~25.2 MB)
- `tools\dist\ffmpeg.zip.sha256`
