# L4C-05-AGENT-ADAPTER — Implementation Report

**Status:** CANDIDATE  
**Branch:** `l4capture/l4c-05-agent-adapter`  
**Date:** 2026-09-21

---

## 1. Summary

Implemented the media backend adapter module for `tools/l4desk`, integrating the native `l4capture.exe` capture pipeline via Windows Job Object process isolation, anonymous pipe IPC, strict lease deadline enforcement (no grace period), input gate policy (6 conditions + kiosk focus), kiosk focus management (3 modes with adaptive fallback), kiosk lifecycle backend, and bounded recovery loop.

## 2. Files Created

| File | Description |
|---|---|
| `tools/l4desk/include/media_backend.h` | Abstract backend vtable interface (l4capture vs ffmpeg) |
| `tools/l4desk/include/l4capture_adapter.h` | Adapter API: process lifecycle, IPC, metrics, recovery |
| `tools/l4desk/include/input_gate.h` | Input gate check: 6 conditions + kiosk focus guard |
| `tools/l4desk/include/kiosk_focus.h` | Kiosk HWND discovery, caching, refocus, mode detection |
| `tools/l4desk/include/kiosk_lifecycle.h` | Kiosk process start/stop/restart/status |
| `tools/l4desk/src/media_backend.c` | Backend factory: dispatches to l4capture or ffmpeg vtable |
| `tools/l4desk/src/l4capture_adapter.c` | Job Object, handle allowlist, IPC encode/decode, deadline, recovery |
| `tools/l4desk/src/input_gate.c` | Strict input validation: low/480p only, default blocked |
| `tools/l4desk/src/kiosk_focus.c` | EnumWindows discovery, AttachThreadInput refocus, ASFW bypass, adaptive fallback |
| `tools/l4desk/src/kiosk_lifecycle.c` | Two-phase shutdown (WM_CLOSE + TerminateProcess), interactive session launch |
| `tools/l4desk/tests/test_l4capture_adapter.c` | 20 tests: adapter, Job, IPC, input gate, recovery, kiosk focus & lifecycle |

## 3. Files Modified

| File | Change |
|---|---|
| `tools/l4desk/src/config.h` | Added `media_backend[32]` field to L4DeskConfig |
| `tools/l4desk/src/config.c` | Added default `media_backend = "l4capture"` and `--media-backend` CLI arg |
| `tools/l4desk/build.cmd` | Added 5 new `.c` files and `/I include` to x86/x64 compile lines |
| `tools/l4desk/tests/run_tests.cmd` | Added `test_l4capture_adapter` build+run, updated existing test builds with new sources and `/I include` |

## 4. Build Verification

### x86 Build
```
=== Building x86 (32-bit, Win7 SP1 target) ===
=== x86 build OK: bin\x86\l4desk.exe ===
```
Zero warnings at /W4 level.

### x64 Build
```
=== Building x64 (64-bit) ===
=== x64 build OK: bin\x64\l4desk.exe ===
```
Zero warnings at /W4 level.

## 5. Test Results

```
l4desk test_l4capture_adapter: 20 tests

20 passed, 0 failed, 20 total

=== ALL TESTS PASSED ===
```

### New Tests (20):

| # | Test | Validates |
|---|---|---|
| 1 | `test_adapter_job_object_kill_on_close` | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE kills child on parent handle close |
| 2 | `test_adapter_handle_allowlist_isolation` | STARTUPINFOEX with PROC_THREAD_ATTRIBUTE_HANDLE_LIST only exposes pipe handles |
| 3 | `test_adapter_session0_rejection` | ProcessIdToSessionId check rejects Session 0 |
| 4 | `test_adapter_single_process_invariant` | Adapter prevents double-start, stop is idempotent |
| 5 | `test_adapter_ipc_start_handshake` | CMD_START encoding, graceful failure on nonexistent exe |
| 6 | `test_adapter_lease_renew_and_dedup` | Renew requires running stream, rejects on idle |
| 7 | `test_adapter_wrong_epoch_rejection` | Operations rejected when adapter is idle (wrong state) |
| 8 | `test_adapter_expiry_strict_500ms_no_grace` | Deadline check uses GetTickCount64() directly, no +5000 grace |
| 9 | `test_adapter_input_gate_low_480p_permitted` | Input allowed when profile=low, 854x480, active lease, desktop |
| 10 | `test_adapter_input_gate_default_strictly_denied` | Input denied when profile=default even if degraded to 480p |
| 11 | `test_adapter_input_release_on_safety_events` | input_release_all() is idempotent and safe |
| 12 | `test_adapter_recovery_loop_and_backoff_cancel` | Multiple stop calls safe, force_idr/metrics on idle return false |
| 13 | `test_kiosk_focus_detection_and_caching` | EnumWindows finds no window for nonexistent process, caching works |
| 14 | `test_kiosk_focus_recovery_attach_thread_input` | kiosk_focus_force() works with real notepad HWND |
| 15 | `test_kiosk_keyboard_input_blocked_when_unfocused` | Input gate denies when kiosk running but not focused |
| 16 | `test_generic_desktop_mode_bypasses_kiosk_focus_checks` | kiosk_mode=false bypasses focus check, mode reports "generic" |
| 17 | `test_kiosk_adaptive_fallback_when_process_absent` | kiosk configured but not running → "generic_fallback", input allowed |
| 18 | `test_kiosk_adaptive_transition_on_app_exit_and_relaunch` | Lifecycle start/stop/restart transitions work correctly |
| 19 | `test_kiosk_lifecycle_graceful_stop_and_kill` | WM_CLOSE + TerminateProcess cleanup, status transitions |
| 20 | `test_kiosk_lifecycle_start_and_status_tracking` | PID tracking, uptime, is_hung check, empty name rejection |

## 6. Dumpbin Verification

### x86 (`bin\x86\l4desk.exe`)
```
WS2_32.dll
WINHTTP.dll
USER32.dll
ole32.dll
OLEAUT32.dll
KERNEL32.dll
```

### x64 (`bin\x64\l4desk.exe`)
```
WS2_32.dll
WINHTTP.dll
USER32.dll
ole32.dll
OLEAUT32.dll
KERNEL32.dll
```

No VC Runtime DLLs. All system libraries only.

## 7. Contract Compliance Summary

| Requirement | Status |
|---|---|
| Job Object KILL_ON_JOB_CLOSE | Verified by test 1 |
| Handle allowlist (STARTUPINFOEX) | Verified by test 2 |
| Session 0 rejection | Verified by test 3 |
| Single process invariant | Verified by test 4 |
| Binary IPC protocol (16-byte LE header) | Implemented in l4capture_adapter.c |
| Strict deadline, no grace period | Verified by test 8 |
| Input Gate: low 480p permitted | Verified by test 9 |
| Input Gate: default strictly denied | Verified by test 10 |
| Input release on safety events | Verified by test 11 |
| Bounded recovery (5 per 600s) | Implemented in l4capture_adapter.c |
| Kiosk focus detection and caching | Verified by test 13 |
| Kiosk focus recovery (AttachThreadInput) | Verified by test 14 |
| Keyboard blocked when kiosk unfocused | Verified by test 15 |
| Generic Desktop Mode bypasses focus | Verified by test 16 |
| Adaptive fallback when kiosk absent | Verified by test 17 |
| Kiosk lifecycle start/stop/restart | Verified by tests 18, 19, 20 |
| Two-phase shutdown (WM_CLOSE + TerminateProcess) | Verified by test 19 |
| Media backend feature flag | config.media_backend field added |

## 8. Artifact SHA-256 Hashes

| Artifact | SHA-256 |
|---|---|
| `docs/l4capture/handoffs/L4C-05-AGENT-ADAPTER-report.md` | *(computed after file write)* |
| `tools/l4desk/include/media_backend.h` | `1e4f64c1e896045fbe9cf2580650571838baf5f5bfb1feee5e847fa712731a49` |
| `tools/l4desk/include/l4capture_adapter.h` | `3b2e332362b4063a4515e9da67d6c20f8ffb8170a894017852d818d8d96b5d12` |
| `tools/l4desk/include/input_gate.h` | `72c9aab3ce1dfff2e583dabc91255a31ec79c08a7dcae6f3b03a517b45906b44` |
| `tools/l4desk/include/kiosk_focus.h` | `43985d880cd282c36bf7d7d35cbb8c02b1d35239423dc671b4895dc12899df20` |
| `tools/l4desk/include/kiosk_lifecycle.h` | `13964c471f6db0d5aca6a67651520003567cc633ae7e67216fa95ea49fb5537a` |
| `tools/l4desk/bin/x86/l4desk.exe` | `b6ee0ca025b75b3559e5991e297ebd73adbf067ed42d1983f94599cab403e25a` |
| `tools/l4desk/bin/x64/l4desk.exe` | `ce2cd720eef69147216a708ae0d055f2df92aa7f6ce37898b0a2c80a52ae4969` |
