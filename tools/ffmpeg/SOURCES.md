# FFmpeg Distribution Sources & Requirements for Leo4

This document defines the sources, architectural builds, and operating system requirements for the FFmpeg binaries bundled in `ffmpeg.zip` and deployed to payment terminals managed by `l4superv` and `l4desk`.

---

## 1. Architecture Requirements & Target Systems

- **Role**: Media streaming engine invoked as a child process of `l4desk` during active remote desktop/video sessions.
- **Service Model**: No separate Windows service; lifecycle is fully managed by `l4desk` within the interactive console session and supervised by `l4superv` via Windows Job Objects (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`).
- **Terminal Installation Path**: `C:\l4tools\ffmpeg\ffmpeg.exe`
- **Log Path**: `C:\l4tools\ffmpeg\log\`
- **State File**: `C:\l4tools\l4desk\state\ffmpeg_state.json`

---

## 2. Minimal Static Build Strategy (Zero-Dependency)

Both 64-bit and 32-bit binaries are built as **autonomous static executables** (`-static-libgcc -static-libstdc++`, `--disable-shared --enable-static`). All required codecs and protocols are compiled directly into `ffmpeg.exe`:
- **Zero External DLLs**: No `avcodec-*.dll`, `avformat-*.dll`, etc.
- **Targeted Feature Set**:
  - Video Encoder: `libx264` (H.264 / AVC)
  - Screen Capture: `gdigrab` (Win32 desktop grabber with mouse cursor capture)
  - Video Device Capture: `dshow` (DirectShow capture)
  - Network Protocols: `rtp`, `udp` (direct streaming to `leo4proxy` RTP tunnel)
  - Frame Processing: `swscale`, filters (`scale`, `format`, `fps`)
- **Reproducible Build Scripts**: Maintained in `ffmpeg-win32/build_ffmpeg_x86.sh` and `build_ffmpeg_x64.sh`.

---

## 3. x64 Distribution

### Source & Version
- **Release**: FFmpeg 9.0 minimal-static
- **Toolchain**: MinGW-w64 (`x86_64-w64-mingw32`, GCC 16.2.0, crosstool-NG)
- **Container**: `ghcr.io/btbn/ffmpeg-builds/base-win64:latest`
- **Source Artifact**: `ffmpeg-win32/x64/ffmpeg.exe` (~33 MB)
- **Environment Override**: `FFMPEG_SRC_X64`
- **Files Included**:
  - `ffmpeg.exe` (~33 MB static binary)
  - `LICENSE` (GNU General Public License v3)
- **Excluded Artifacts**: No external DLLs, `ffprobe.exe`, `ffplay.exe`, `include/`, `lib/`, `doc/`, `presets/`.

### OS Compatibility & Prerequisites
- **Supported Systems**: Windows 10, Windows 11, Windows Server 2016–2025 (x64), and Windows 7 SP1 x64.
- **Universal C Runtime (UCRT)**: Built against Microsoft Universal CRT (`ucrtbase.dll`, `api-ms-win-crt-*.dll`).
  - On **Windows 10/11**: UCRT is native to the base operating system.
  - On **Windows 7 SP1 x64**: The Microsoft Update for Universal C Runtime (**KB2999226** or Convenience Rollup **KB3125574**) or Visual C++ 2015–2022 Redistributable must be installed.

---

## 4. x86 (32-bit) Distribution

### Target Systems & Legacy Context
- **Target OS**: Windows 10/11 32-bit, Windows 7 Embedded Standard (x86), POSReady 7 (x86), Windows 7 SP1 32-bit.
- **Role in Kiosks**: Self-service payment terminals with 32-bit operating systems and constrained RAM (1–2 GB).

### Source & Version
- **Release**: FFmpeg 9.0 minimal-static
- **Toolchain**: MinGW-w64 (`i686-w64-mingw32`, GCC 16.2.0, crosstool-NG)
- **Container**: `ghcr.io/btbn/ffmpeg-builds/base-win32:latest`
- **Source Artifact**: `ffmpeg-win32/x86/ffmpeg.exe` (~26 MB)
- **Environment Override**: `FFMPEG_SRC_X86`
- **Files Included**:
  - `ffmpeg.exe` (~26 MB static binary)
  - `LICENSE` (GNU General Public License v3)
- **Status**: Full binary bundled in `ffmpeg.zip`. Parity with x64 streaming capabilities.
