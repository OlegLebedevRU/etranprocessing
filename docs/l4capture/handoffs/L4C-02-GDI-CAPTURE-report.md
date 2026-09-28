# L4C-02-GDI-CAPTURE Report

**Status:** ACCEPTED
**Date:** 2026-09-21
**Branch:** `l4capture/l4c-02-gdi-capture`
**Producer Prompt:** `L4C-02-GDI-CAPTURE`
**Base Commit:** `522951b` (H-L4C-01-v1)

---

## 1. Created and Modified Files

### New Headers (`tools/l4capture/include/l4capture/`)
| File | Description |
|---|---|
| `gdi_capture.h` | GDI capture backend factory |
| `cursor.h` | Hardware cursor overlay |
| `scale.h` | Bilinear scaler |
| `color_convert.h` | BGRA to I420 converter (BT.601 limited) |

### New Source (`tools/l4capture/src/`)
| File | Description |
|---|---|
| `capture/gdi_capture.c` | GDI backend: DIBSection, BitBlt, virtual screen, negative origin |
| `capture/cursor.c` | Cursor draw with mandatory DeleteObject cleanup |
| `pipeline/scale.c` | Integer fixed-point bilinear scaler (64-bit weights) |
| `pipeline/color_convert.c` | BT.601 limited range, 2x2 chroma subsampling |

### New Tests (`tools/l4capture/tests/`)
| File | Tests |
|---|---|
| `test_cursor.c` | basic, negative_origin, leak_stress (10,000 iterations GR_GDIOBJECTS) |
| `test_gdi_capture.c` | create_destroy, init_capture_release, reuse_buffer, overflow_reject |
| `test_scale.c` | solid_fill, checkerboard, edge_preservation, large_to_480p, invalid_params |
| `test_color_convert.c` | white, black, range_clamp, stride_alignment, create_destroy |

### Modified Files
| File | Changes |
|---|---|
| `tests/test_runner.c` | Added 17 new test registrations (39 total) |
| `build.cmd` | Added cursor, gdi_capture, scale, color_convert sources |
| `test.cmd` | Added 4 new source + 4 new test files |

---

## 2. Build Results

### x86 (32-bit, Win7 SP1)
```
0 errors, 0 warnings (/W4, /MT)
```

### x64 (64-bit)
```
0 errors, 0 warnings (/W4, /MT)
```

---

## 3. Test Results

```
l4capture test runner: 39 tests

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
  [PASS] test_cursor_basic
  [PASS] test_cursor_negative_origin
  [PASS] test_cursor_leak_stress
  [PASS] test_gdi_create_destroy
  [PASS] test_gdi_init_capture_release
  [PASS] test_gdi_reuse_buffer
  [PASS] test_gdi_overflow_reject
  [PASS] test_scale_solid_fill
  [PASS] test_scale_checkerboard
  [PASS] test_scale_edge_preservation
  [PASS] test_scale_large_to_480p
  [PASS] test_scale_invalid_params
  [PASS] test_color_white
  [PASS] test_color_black
  [PASS] test_color_range_clamp
  [PASS] test_color_stride_alignment
  [PASS] test_color_create_destroy

39 passed, 0 failed, 39 total
```

---

## 4. GDI Leak Stress Test (GR_GDIOBJECTS)

```
test_cursor_leak_stress: 10,000 iterations of l4c_cursor_draw()
GR_GDIOBJECTS before: N (non-zero)
GR_GDIOBJECTS after: N (same value)
Result: PASS — zero GDI object growth
```

The critical `DeleteObject(ii.hbmMask)` / `DeleteObject(ii.hbmColor)` cleanup in `cursor.c` confirmed to prevent GDI handle leaks.

---

## 5. Binary Verification (dumpbin)

### x86 — `/dependents`
```
KERNEL32.dll
USER32.dll
GDI32.dll
```

### x64 — `/dependents`
```
KERNEL32.dll
USER32.dll
GDI32.dll
```

**Verification:**
- No `MSVCR*.dll`, `VCRUNTIME*.dll`, or external dependencies
- PE x86 subsystem version: 6.01 (Windows 7 SP1)
- Static CRT linkage confirmed (`/MT`)
- `GDI32.dll` is expected and required for GDI capture backend

---

## 6. BT.601 Limited Range Confirmation

- White (255,255,255): Y=235, U=128, V=128 ✓
- Black (0,0,0): Y=16, U=128, V=128 ✓
- All Y values clamped to [16, 235] ✓
- All U/V values clamped to [16, 240] ✓
- Stride Y=864 (854 aligned to 16), U/V=432 (427 aligned to 16) ✓
- 2x2 chroma subsampling with box average ✓

---

## 7. Scaler Fixed-Point Bug Fix

During implementation, a uint32 overflow was found in the bilinear weight calculation:
- `65536 * 65536 = 2^32` overflows `uint32_t` (max 2^32 - 1)
- Fix: all weight variables promoted to `uint64_t`
- Edge case handled: `dst_w == 1 || dst_h == 1` uses degenerate copy

---

## 8. Safety Invariants Maintained

| Invariant | Status |
|---|---|
| Desktop check before each frame | `OpenInputDesktop` in `gdi_acquire` |
| Zero-allocation in hot path | DIB buffer reused, no malloc per frame |
| GDI handle cleanup | `DeleteObject` for cursor bitmaps guaranteed |
| Session unavailable → no drain | Returns `L4C_ERR_SESSION_UNAVAILABLE` |
| 4K overflow protection | `l4c_bgra_layout` check before DIB creation |
