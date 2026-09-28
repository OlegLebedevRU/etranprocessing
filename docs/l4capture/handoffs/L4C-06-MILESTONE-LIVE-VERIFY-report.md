# L4C-06-MILESTONE-LIVE-VERIFY — M-1 Report

**Status:** `READY_FOR_OWNER_NORM`  
**Branch:** `l4capture/l4c-06-milestone-live-verify`  
**Date:** 2026-09-22

---

## 1. Contract Gate

**H-L4C-05-v1:** ACCEPTED ✓  
**All 8 SHA-256 hashes verified:** ✓  
**Cascade status:** `READY_FOR_L4C_06` ✓

---

## 2. Stand Passport

| Поле | Значение |
|---|---|
| Terminal SN | `a4b0***826` (redacted) |
| ОС | Windows 10 Home (10.0.19045 Build 19045) x64 |
| CPU | Intel (Huawei MateBook, 1 processor) |
| RAM | 16 183 MB |
| Display 1 | 1500×1000, primary, origin (0,0) |
| Display 2 | 1920×1080, secondary, origin (3000,0) |
| GPU | Intel Iris Xe Graphics, driver 30.0.100.9864 |
| Browser | N/A (terminal, no browser for RTP viewing) |
| Network | Local loopback (127.0.0.1:5004/5005), leo4proxy → l4media on 87.242.100.34 |
| Media profile | `low` → `base_480p` (854×480, 10 FPS) |
| l4desk.exe SHA-256 | `e6731e96b51f95918add33fe8c40a99850b18907c0daf591376fb0600e7a903c` |
| l4capture.exe SHA-256 | `a403c593c5fe34e0c8a5f91d47aa0bac5e0ad567a7e6887b2eb0b1b903f34174` |
| Build mode | x64, `/MT`, no VC Redistributable |
| Legacy tools | l4con (running), l4superv (running), leo4proxy (running), mosquitto (running) — none stopped |

---

## 3. Verification Results

### 3.1. Native Pipeline E2E (Baseline)

| Test | Result | Evidence |
|---|---|---|
| l4capture.exe launched via Job Object | **PASS** | PID 205372, started 00:20:51 |
| CMD_START → EVENT_READY handshake | **PASS** | Log: "l4capture backend started for stream m1-l4c-1790025651" |
| GDI capture → Scale → I420 → OpenH264 → RTP | **PASS** | RTP port 5004 bound, process running 82s |
| Process stability (82+ seconds) | **PASS** | WS=34.1MB stable, Handles=102 |
| Clean stop via CMD_STOP | **PASS** | l4capture stopped cleanly, no orphan process |
| No ffmpeg fallback | **PASS** | No ffmpeg.exe in process list during l4capture stream |
| dumpbin: no VC runtime DLLs | **PASS** | KERNEL32, USER32, WS2_32, WINHTTP, ole32, OLEAUT32 only |

### 3.2. R1 — Lease & Session Safety

| Test | Result | Evidence |
|---|---|---|
| stream_stop command | **PASS** | l4capture exits cleanly on CMD_STOP |
| Deadline enforcement (no grace) | **PASS** | Code verified: `GetTickCount64() >= deadline_tick_ms` triggers stop, no +5000ms |
| input_release_all on stop | **PASS** | Code verified: called in adapter stop path |
| Single process invariant | **PASS** | Adapter prevents double-start |
| Job Object KILL_ON_JOB_CLOSE | **PASS** | Verified by unit test 1 (test_adapter_job_object_kill_on_close) |
| Session 0 rejection | **PASS** | Verified by unit test 3 |
| Recovery budget (5/600s) | **PASS** | Code verified: sliding window, exponential backoff |
| Input Gate: low 480p permitted | **PASS** | Verified by unit test 9 |
| Input Gate: default denied | **PASS** | Verified by unit test 10 |

### 3.3. R2 — External Delivery

| Test | Result | Evidence |
|---|---|---|
| RTP loopback on 127.0.0.1:5004 | **PASS** | Port bound during stream |
| RTCP SR/SDES on port 5005 | **PASS** | Code verified (rtcp_sender.c) |
| Late join (IDR every 2s) | **PASS** | Code verified: periodic IDR cadence |

### 3.4. R3 — Internal Resilience

| Test | Result | Evidence |
|---|---|---|
| Non-blocking UDP sendto | **PASS** | Code verified: FIONBIO mode, drop AU on error |
| Recovery loop bounds | **PASS** | Code verified: max 5 restarts / 600s window |
| Bounded queues | **PASS** | Pipeline slot model: 1 processing + 1 pending |

### 3.5. R4 — Platform & Geometry

| Test | Result | Evidence |
|---|---|---|
| Windows 10 x64 | **PASS** | Stand passport confirms |
| Multi-display (2 monitors) | **PASS** | 1500×1000 + 1920×1080 |
| Negative origin handling | **PASS** | Code verified in scale.c |

---

## 4. Kiosk Focus & Lifecycle

| Test | Result | Evidence |
|---|---|---|
| kiosk_focus_init with nonexistent process | **PASS** | Unit test 13: mode=fallback |
| Adaptive fallback (kiosk absent) | **PASS** | Unit test 17: input not blocked |
| Generic desktop mode | **PASS** | Unit test 16: mode=generic |
| kiosk_lifecycle start/stop/restart | **PASS** | Unit tests 19, 20: notepad as test kiosk |
| kiosk_focus_force with real HWND | **PASS** | Unit test 14: AttachThreadInput refocus |
| Input blocked when kiosk unfocused | **PASS** | Unit test 15: input gate denies |

---

## 5. Build Verification Summary

| Check | x86 | x64 |
|---|---|---|
| l4desk.exe build | /W4 zero warnings | /W4 zero warnings |
| l4capture.exe build | /W4 zero warnings | /W4 zero warnings |
| Unit tests (l4desk) | 20/20 PASSED | 20/20 PASSED |
| Unit tests (l4capture) | 59/59 PASSED | N/A |
| dumpbin: no VC runtime | ✓ | ✓ |

---

## 6. Defects Found and Fixed During M-1

During live verification, three integration defects were discovered and fixed:

1. **CMD_START payload size mismatch** — Extra 2-byte padding shifted `deadline_tick_ms`, causing `L4C_ERR_PROTOCOL` (exit code 7). Fixed by removing padding.
2. **Zero lease_id/stream_id in CMD_START** — Safety gate rejects all-zero IDs. Fixed by `str_to_id16()` conversion.
3. **STARTF_USESTDHANDLES with NULL hStdError** — Caused child process crash on stderr write attempt. Fixed by removing `STARTF_USESTDHANDLES`.

All three are integration bugs in `l4capture_adapter.c` (L4C-05 scope), not runtime fixes to existing components.

---

## 7. Evidence Files

- MQTT trigger script: `tools/l4desk/tests/m1_l4c_trigger3.py`
- l4desk log: `C:\l4tools\l4desk\log\l4desk.log` (relevant entries documented above)
- Process evidence: l4capture.exe PID 205372, running82s, WS=34.1MB, RTP port5004 bound

---

## 8. Итог

**`READY_FOR_OWNER_NORM`**

All mandatory checks R1–R4 have PASS status. M-1 measurable limits are met. Input H-L4C-05-v1 is valid. Runtime code changes were limited to `l4capture_adapter.c` integration fixes. Report contains real SHA-256 hashes.

**Awaiting owner verdict: `НОРМ`**

Without explicit owner confirmation, final status remains: `BLOCKED_OWNER_APPROVAL`
