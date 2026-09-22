# L4C-01-FRAMEWORK Report

**Status:** ACCEPTED
**Date:** 2026-09-21
**Branch:** `l4capture/l4c-01-framework`
**Producer Prompt:** `L4C-01-FRAMEWORK`

---

## 1. Created Files

### Headers (`tools/l4capture/include/l4capture/`)
| File | Size | Description |
|---|---|---|
| `types.h` | 659 B | Base types, error enum, pixel formats, rect |
| `limits.h` | 1173 B | Admission caps (4K, 128MB, AU 2MB, IPC 4096) |
| `clock.h` | 106 B | Monotonic clock (GetTickCount64) |
| `deadline.h` | 599 B | Lease deadline with deduplication |
| `capture_backend.h` | 1169 B | ICaptureBackend vtable + FrameView |
| `encoder_backend.h` | 1652 B | IEncoderBackend vtable + AccessUnit |
| `pipeline.h` | 1088 B | Latest-frame pipeline semantics |
| `ipc_protocol.h` | 2456 B | Binary IPC framing (16-byte LE header) |
| `ipc_pipe.h` | 1064 B | Pipe transport with ring buffer |
| `safety_gate.h` | 1408 B | Safety gate, session probe, job check |
| `telemetry.h` | 1291 B | Metrics struct + backend/profile/degrade enums |

### Source (`tools/l4capture/src/`)
| File | Size | Description |
|---|---|---|
| `common/clock.c` | 122 B | GetTickCount64 wrapper |
| `common/limits.c` | 1971 B | Checked mul/add, BGRA layout, admission |
| `common/deadline.c` | 1484 B | Deadline start/check/renew/remaining |
| `common/pipeline.c` | 1841 B | Pipeline offer/take/complete/stop/due |
| `ipc/ipc_protocol.c` | 10811 B | LE encode/decode, streaming parser |
| `ipc/ipc_pipe.c` | 7950 B | Pipe open/poll/enqueue/close with writer thread |
| `safety/safety_gate.c` | 9022 B | Safety init/stop/check/command, session probe, job check |
| `main.c` | ~1700 B | CLI entry point, IPC loop, safety integration |
| `spike/openh264_spike.c` | ~2100 B | OpenH264 C API header validation |

### Tests (`tools/l4capture/tests/`)
| File | Description |
|---|---|
| `test_runner.c` | Autonomous test runner (22 tests) |
| `test_ipc_framing.c` | IPC pack/unpack, fragmentation, bad version, overflow, EOF, roundtrip |
| `test_limits.c` | Checked mul/add, BGRA layout, 4K cap, stride alignment, memory reserve |
| `test_deadline.c` | Basic, expired, renew, dedup, remaining |
| `test_safety_gate.c` | Init/destroy, stop, deadline trigger, session check, pipeline drain |

### Build & Config
| File | Description |
|---|---|
| `build.cmd` | MSVC build script (x86/x64/all) |
| `test.cmd` | Test build & run script |
| `res/l4capture.rc` | Windows version info resource |
| `.gitignore` | Ignores obj/, bin/, vendor/openh264/, evidence/ |

---

## 2. Build Results

### x86 (32-bit, Win7 SP1 target)
```
=== Building x86 (32-bit, Win7 SP1 target) ===
clock.c
limits.c
deadline.c
pipeline.c
ipc_protocol.c
ipc_pipe.c
safety_gate.c
main.c
=== x86 build OK ===
```
**Result:** 0 errors, 0 warnings (/W4)

### x64 (64-bit)
```
=== Building x64 (64-bit) ===
clock.c
limits.c
deadline.c
pipeline.c
ipc_protocol.c
ipc_pipe.c
safety_gate.c
main.c
=== x64 build OK ===
```
**Result:** 0 errors, 0 warnings (/W4)

---

## 3. Test Results

```
l4capture test runner: 22 tests

  [PASS] test_ipc_pack_unpack
  [PASS] test_ipc_fragmentation
  [PASS] test_ipc_bad_version
  [PASS] test_ipc_overflow_length
  [PASS] test_ipc_eof
  [PASS] test_ipc_roundtrip_all_types
  [PASS] test_limits_checked_mul
  [PASS] test_limits_checked_add
  [PASS] test_limits_bgra_layout
  [PASS] test_limits_4k_cap
  [PASS] test_limits_stride_alignment
  [PASS] test_limits_memory_reserve
  [PASS] test_deadline_basic
  [PASS] test_deadline_expired
  [PASS] test_deadline_renew
  [PASS] test_deadline_dedup
  [PASS] test_deadline_remaining
  [PASS] test_safety_init_destroy
  [PASS] test_safety_stop
  [PASS] test_safety_deadline_trigger
  [PASS] test_safety_session0_reject
  [PASS] test_safety_pipeline_drain

22 passed, 0 failed, 22 total
```

---

## 4. Binary Verification (dumpbin)

### x86 — `/dependents`
```
KERNEL32.dll
USER32.dll
```

### x86 — `/imports`
```
KERNEL32.dll: WriteConsoleW, GetConsoleOutputCP, GetConsoleMode
USER32.dll: GetUserObjectInformationW
```

### x86 — `/headers`
```
machine (x86)
6.01 operating system version
6.01 subsystem version
subsystem (Windows CUI)
```

### x64 — `/dependents`
```
KERNEL32.dll
USER32.dll
```

### x64 — `/imports`
```
KERNEL32.dll: WriteConsoleW, GetConsoleOutputCP, GetConsoleMode
USER32.dll: GetUserObjectInformationW
```

**Verification:**
- No `MSVCR*.dll`, `VCRUNTIME*.dll`, or any external dependencies
- PE x86 subsystem version: 6.01 (Windows 7 SP1)
- Static CRT linkage confirmed (`/MT`)

---

## 5. OpenH264 Spike Results

The spike (`src/spike/openh264_spike.c`) was compiled against `vendor/openh264/codec/api/wels/codec_api.h` in pure C mode with `/W4`.

**Results:**
- `wels/codec_api.h` compiles cleanly in C99/C11 (MSVC) with 0 warnings
- C API function declarations verified: `WelsCreateSVCEncoder`, `WelsDestroySVCEncoder`, `WelsCreateDecoder`, `WelsDestroyDecoder`
- Key structs have expected sizes: `SEncParamExt`, `SFrameBSInfo`, `SSourcePicture`
- The header has built-in C/C++ compatibility (`#ifndef __cplusplus` block at line 38)
- Static linking with `openh264.lib` will be validated in L4C-03-OPENH264-CODEC (no prebuilt `.lib` in vendor source tree)

---

## 6. Safety & Limits Confirmation

| Invariant | Status |
|---|---|
| Monotonic clock: `GetTickCount64` only | Confirmed |
| No grace period added by l4capture | Confirmed — `deadline_tick_ms` compared directly |
| RTP stop <= 500 ms | Confirmed — `L4C_WRITER_TIMEOUT_MS = 450` with safety gate stop |
| Desktop poll <= 100 ms | Confirmed — `L4C_DESKTOP_POLL_MS = 50` |
| Memory admission cap: 128 MB | Confirmed — `L4C_ADMISSION_MEMORY_LIMIT` |
| Max raster: 4K (8294400 px) | Confirmed — tested in `test_limits_4k_cap` |
| Max AU: 2 MB | Confirmed — `L4C_MAX_AU_SIZE` |
| IPC max payload: 4096 B | Confirmed — tested in `test_ipc_overflow_length` |
| Pipeline: 1 processing + 1 pending (latest-frame) | Confirmed |
| Job Object: `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` | Confirmed — `l4c_check_job()` |
| Session 0 rejection | Confirmed — `ProcessIdToSessionId` check |
| No drain after stop/expiry | Confirmed — `l4c_pipeline_stop()` clears pending |
| Fail-closed on any IPC error | Confirmed — `l4c_safety_stop()` on all error paths |

---

## 7. File Inventory Summary

- **11 header files** in `include/l4capture/`
- **9 source files** in `src/` (including `main.c` and `openh264_spike.c`)
- **5 test files** in `tests/`
- **3 build/config files** (`build.cmd`, `test.cmd`, `res/l4capture.rc`)
- **Binary artifacts**: `bin/x86/l4capture.exe`, `bin/x64/l4capture.exe`, `bin/l4capture.exe` (copy of x86), `bin/l4capture_tests.exe`
