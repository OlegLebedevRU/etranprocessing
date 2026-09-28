# L4C-04-RTP-SENDER — Implementation Report

**Status:** ACCEPTED  
**Branch:** `l4capture/l4c-04-rtp-sender`  
**Commit:** `bff8018`  
**Date:** 2026-09-22

---

## 1. Summary

Implemented the RTP/RTCP sender module for l4capture: RFC 6184 H.264 packetization (Single NAL + FU-A), non-blocking UDP loopback transport on 127.0.0.1:5004/5005, compound RTCP SR/SDES CNAME generation, and fail-fast drop policy with immediate force-IDR on network errors.

## 2. Files Created

| File | Description |
|---|---|
| `include/l4capture/rtp_sender.h` | Public API: config, stats, create/send_au/poll_rtcp/destroy |
| `include/l4capture/rtp_packetizer.h` | RFC 6184 packetization: callback-based, Single NAL + FU-A |
| `src/network/rtp_packetizer.c` | Packetizer: NAL splitting, FU-A fragmentation, RTP header assembly |
| `src/network/rtp_sender.c` | Socket lifecycle, non-blocking sendto, drop+force_idr, RTCP polling, BYE |
| `src/network/rtcp_sender.c` | Compound RTCP SR (PT=200) + SDES CNAME (PT=201), BYE (PT=203) |
| `tests/test_rtp_sender.c` | 12 tests covering RFC compliance, roundtrip, drop policy, E2E loopback |

## 3. Files Modified

| File | Change |
|---|---|
| `include/l4capture/types.h` | Added `L4C_ERR_NETWORK = 10` error code |
| `tests/test_runner.c` | Registered 12 new RTP/RTCP tests |
| `build.cmd` | Added `src/network/*.c` to compile and link lines |
| `test.cmd` | Added `src/network/*.c`, `test_rtp_sender.c`, OpenH264 encoder test + libs |

## 4. Build Verification

### x86 Build
```
=== Building x86 (32-bit, Win7 SP1 target) ===
=== x86 build OK ===
```
Zero warnings at /W4 level.

### x64 Build
```
=== Building x64 (64-bit) ===
=== x64 build OK ===
```
Zero warnings at /W4 level.

## 5. Test Results

```
l4capture test runner: 59 tests

59 passed, 0 failed, 59 total

=== ALL TESTS PASSED ===
```

### New Tests (12):

| # | Test | Validates |
|---|---|---|
| 1 | `test_rtp_single_nal_small` | SPS(30B) + PPS(8B) → 2 Single NAL packets, header correctness |
| 2 | `test_rtp_boundary_1200_1201` | 1200B → Single NAL (1212B), 1201B → FU-A (2 frags) |
| 3 | `test_rtp_fua_fragmentation_large` | 10000B IDR → 9 FU-A fragments, FU indicator/header bits |
| 4 | `test_rtp_marker_bit_au_boundary` | M=0 for first 6 packets, M=1 on last packet of AU |
| 5 | `test_rtp_timestamp_consistency` | All packets of one AU share identical timestamp; next AU +9000 |
| 6 | `test_rtp_sequence_monotonicity_and_wrap` | Strict monotonic seq; wrap 65534→65535→0→1 |
| 7 | `test_rtp_timestamp_wrap` | Timestamp wrap near UINT32_MAX without integer overflow |
| 8 | `test_rtcp_sr_sdes_generation` | SR (V=2, PT=200, 28B), SDES CNAME, 32-bit alignment |
| 9 | `test_rtcp_bye_generation` | BYE (V=2, PT=203), SSRC match |
| 10 | `test_rtp_fua_reassembly_roundtrip` | 7500B NAL → FU-A → reassemble → 100% byte-identical |
| 11 | `test_network_nonblocking_drop_on_error` | Callback abort mid-AU → L4C_ERR_NETWORK, transport_drops++ |
| 12 | `test_pipeline_e2e_loopback` | Packetize SPS+PPS+IDR → sendto UDP → recv → validate SPS/PPS/IDR |

## 6. Dumpbin Verification

### x86 (`bin\x86\l4capture.exe`)
```
KERNEL32.dll
USER32.dll
GDI32.dll
```

### x64 (`bin\x64\l4capture.exe`)
```
KERNEL32.dll
USER32.dll
GDI32.dll
```

No VC Runtime DLLs. WS2_32.lib is linked at build time (will appear in dumpbin when pipeline calls network functions in L4C-05).

## 7. RFC Compliance Summary

| Requirement | Status |
|---|---|
| Single NAL Unit (L ≤ 1200B) | Verified by test 1, 2 |
| FU-A fragmentation (L > 1200B, payload ≤ 1198B) | Verified by tests 2, 3, 10 |
| Marker bit M=1 on last packet of AU only | Verified by test 4 |
| Identical timestamp for all packets of one AU | Verified by test 5 |
| Sequence number monotonic + wrap modulo 2^16 | Verified by test 6 |
| Timestamp wrap modulo 2^32 | Verified by test 7 |
| RTCP SR (PT=200) + SDES CNAME (PT=201) | Verified by test 8 |
| RTCP BYE (PT=203) on destroy | Verified by test 9 |
| Non-blocking UDP, drop AU on error, force-IDR | Verified by test 11 |
| E2E loopback packet delivery | Verified by test 12 |

## 8. Artifact SHA-256 Hashes

| Artifact | SHA-256 |
|---|---|
| `docs/l4capture/handoffs/L4C-04-RTP-SENDER-report.md` | *(computed after file write)* |
| `tools/l4capture/include/l4capture/rtp_sender.h` | `757339ad278499e06de168c6a9c9ed1f0de0cf16147d945271038c768dcd5922` |
| `tools/l4capture/include/l4capture/rtp_packetizer.h` | `a9ef3deb4007c32e558901a162e7e3101d192bc5988043a68b80e0fc11d727fa` |
| `tools/l4capture/bin/x86/l4capture.exe` | `d6bbfc7e9bb5c53a768bd487e9324992839661416e7c642d94bd1ec233899138` |
| `tools/l4capture/bin/x64/l4capture.exe` | `19e4a50743f84048b5d62bd1ebb9582ddfa8bde6fab24f73e720d0b557ba7ada` |
