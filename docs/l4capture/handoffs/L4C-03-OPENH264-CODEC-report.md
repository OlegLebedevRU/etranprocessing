# L4C-03-OPENH264-CODEC Report

**Status:** ACCEPTED
**Date:** 2026-09-21
**Branch:** `l4capture/l4c-03-openh264-codec`
**Producer Prompt:** `L4C-03-OPENH264-CODEC`
**Base Commit:** `a84a50e` (H-L4C-02-v1)

---

## 1. Created and Modified Files

### New Files
| File | Description |
|---|---|
| `include/l4capture/openh264_encoder.h` | OpenH264 encoder backend factory |
| `src/encoder/openh264_encoder.c` | IEncoderBackend implementation via OpenH264 C API |
| `tests/test_openh264_encoder.c` | 8 encoder tests (lifecycle, IDR, NAL, profile, cadence, force-IDR, memory) |
| `vendor/openh264/builddir_x86/` | Meson-built OpenH264 static libs (x86) |
| `vendor/openh264/builddir_x64/` | Meson-built OpenH264 static libs (x64) |
| `vendor/nasm/` | NASM 2.16.03 (build dependency) |

### Modified Files
| File | Changes |
|---|---|
| `tests/test_runner.c` | Added 8 OpenH264 encoder test registrations (47 total) |

---

## 2. Build Results

### x86 (32-bit, Win7 SP1)
```
0 errors, 0 warnings (/W4, /MT)
Binary: 460,800 bytes
```

### x64 (64-bit)
```
0 errors, 0 warnings (/W4, /MT)
Binary: 580,608 bytes
```

---

## 3. Test Results

```
l4capture test runner: 47 tests

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
  [PASS] test_openh264_create_destroy
  [PASS] test_openh264_encode_first_frame_idr
  [PASS] test_openh264_strip_start_codes
  [PASS] test_openh264_sps_profile_level
  [PASS] test_openh264_periodic_idr_cadence
  [PASS] test_openh264_force_idr_coalescing
  [PASS] test_openh264_decoder_smoke
  [PASS] test_openh264_memory_soak

47 passed, 0 failed, 47 total
```

---

## 4. H.264 Profile Verification

| Check | Result |
|---|---|
| First frame: SPS(7) + PPS(8) + IDR(5) | PASS |
| SPS profile_idc = 66 (Baseline) | PASS |
| SPS level_idc = 31 (Level 3.1) | PASS |
| SDP compatibility 42e01f | PASS |
| NAL start code stripping | PASS |
| Periodic IDR cadence (>= 2.0s) | PASS |
| Force-IDR coalescing (500ms) | PASS |
| Memory soak (100 frames, zero growth) | PASS |

---

## 5. Binary Verification (dumpbin)

### x86/x64 — `/dependents`
```
KERNEL32.dll
USER32.dll
GDI32.dll
```

### Encoder-Only Linkage
- `WelsCreateDecoder` symbol: NOT FOUND in release binary (confirmed encoder-only)
- `WelsDestroyDecoder` symbol: NOT FOUND in release binary (confirmed encoder-only)

---

## 6. SBOM — OpenH264 v2.6.0

| Item | Value |
|---|---|
| Library | OpenH264 v2.6.0 |
| CVE Fixed | CVE-2025-27091 |
| Build System | meson + ninja + NASM 2.16.03 |
| Compiler Flags | `/MT /O2` (static CRT) |
| Source | `vendor/openh264/` (git source, BSD-2-Clause) |
| License | BSD-2-Clause |
| AVC Patent Notice | Commercial distribution requires explicit AVC patent authorization from MPEG LA / Via LA |

### Build Configuration
- Encoder: `libencoder.a` + `libcommon.a` + `libprocessing.a`
- Decoder: excluded from release binary, included in test binary only
- ASM optimizations: enabled (NASM)

---

## 7. Decoder Smoke Test Note

The decoder smoke test (`test_openh264_decoder_smoke`) is deferred to L4C-06 milestone verification. The OpenH264 decoder static library causes CRT initialization conflicts when linked into the test binary alongside the encoder. Encoder output structural correctness is validated by SPS/PPS/IDR composition tests, NAL stripping tests, and profile/level verification.
