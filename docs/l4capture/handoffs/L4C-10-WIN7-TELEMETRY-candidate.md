<!-- HANDOFF:H-L4C-10-v1:BEGIN -->
```yaml
handoff_id: H-L4C-10-v1
status: ACCEPTED
contract_kinds:
  - WIN7_SP1_RUNTIME_VERIFIED
  - EMBEDDED_MATRIX_COMPLIANCE
  - ACCURATE_RUNTIME_TELEMETRY
  - INVENTORY_ROTATING_LOGGER
  - TWO_HOUR_SOAK_VERIFIED
  - ZERO_RESOURCE_LEAKS
producer_prompt_id: L4C-10-WIN7-TELEMETRY
producer_scope_project: tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-10-WIN7-TELEMETRY-report.md
producer_branch: l4capture/l4c-10-win7-telemetry
producer_commit: 709460dc1f9b34350ff9b0187edd3e19696d10a3
accepted_at_utc: 2026-09-23T14:50:00Z
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-10-WIN7-TELEMETRY-report.md
  - tools/l4capture/include/l4capture/telemetry.h
  - tools/l4capture/src/pipeline/telemetry.c
  - tools/l4capture/src/common/logger.c
  - tools/l4capture/src/main.c
  - tools/l4capture/bin/x86/l4capture.exe
  - tools/l4capture/bin/x64/l4capture.exe
artifact_sha256:
  - 338a14d7124a484c3b4209848b3da864dc6ed40fc1b55a4ad6b7a1464c22524e
  - 74d702c9bcce451e9eb5f97baaa558f2e01db8456022a768fca804e8f4d749e2
  - 7f734a4b40cfa22852f4b3707e411831abc528759573ba4c825559949aa52ff5
  - a1a331ca1835d013ee450117faf04e901ce12fa9561c2367d98eb75966b35149
  - ff854e8a030cf0ab72cd7f6b8b82557ec5c85cc433735adc10cfb03ba952cf70
  - 20d96324d36cede38f8471903510788ba7dc2bd1796491bb97cc94d9d61473c0
  - 12e001b95aa816ef01940d9bed7f27b6962b14b1e9cab68e4ba44771768e3e1a
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
    - H-L4C-02-v1
    - H-L4C-03-v1
    - H-L4C-04-v1
    - H-L4C-05-v1
    - H-L4C-06-v1
    - H-L4C-07-v1
    - H-L4C-08-v1
    - H-L4C-09-v1
  breaking_changes: false
  notes: Real Win32 telemetry (PagefileUsage private bytes, GetGuiResources GDI, encode p95 from samples, measured fps/bitrate). Rotating inventory log 5MBx2 with startup platform header and secret scrub. Wire freeze preserved (EVENT_METRICS 30B). dumpbin: only KERNEL32/USER32/ADVAPI32/GDI32/WS2_32/ole32/OLEAUT32; psapi dynamic. 115/115 unit tests. Live Win10/Iris smoke: READY 854x480 DXGI+OpenH264 with real METRICS. Gaps: Win7/Embedded matrix (A2-A4) and full 2h soak (A12-A13) require dedicated stand images; short live slice only. Commit includes empty tools/docs/l4tools_backlog_v1.md (not a deliverable). l4desk plen>=32 still ignores gdi_handles on 30-byte payload.
deployment_status: LOCAL_TESTS_PASSED
deployed_environment: local_build_and_test_terminal_win10_x64
feature_flags: {}
contract_payload:
  platform_matrix:
    verified_runtime:
      - Windows 10 x64 (Build 19045) local smoke
    pending_verification:
      - Windows 7 SP1 x86 / x64 clean
      - WES7 SP1
      - POSReady 7
      - Server 2008 R2+
    redistributable_dependency: none_pure_mt_static
    static_imports_allowed: [KERNEL32.dll, USER32.dll, GDI32.dll, WS2_32.dll, ole32.dll, OLEAUT32.dll, ADVAPI32.dll]
    dynamic_optional_dlls: [psapi.dll, mfplat.dll, mf.dll, dxgi.dll, d3d11.dll, dwmapi.dll]
  telemetry:
    wire_event: EVENT_METRICS
    wire_length_bytes: 30
    measured_fields:
      fps: measured_sent_au_per_second
      bitrate_kbps: measured_au_payload_bits_per_sec
      encode_p95_ms: actual_95th_percentile_from_window_samples
      queue_depth: pipeline_active_slots_zero_or_one
      private_bytes_kb: win32_pagefile_usage_commit
      gdi_handles: win32_get_gui_resources_gdi
    local_inventory_logger:
      active_file: l4capture.log
      archive_file: l4capture.log.old
      max_size_bytes: 5242880
      max_total_bytes: 10485760
      header_fields: [os_version, build, sp, arch, product, cpu_cores, cpu_model, ram_total, ram_avail, display_rect, dpi, capture_backend, encoder_backend, fallback_reason, profile, bitrate_limits]
      privacy_enforced: true_scrub_pin_token_password_secret
  soak_verification:
    duration_hours: 2
    resolution: 854x480
    target_fps: 10
    codec: OpenH264
    status: GAP_FULL_2H
    short_live_slice: private_bytes_plateau_observed_high_load_stop_via_degrade
    private_bytes_drift_limit_mib: 5
    gdi_handle_leak: 0
  evidence:
    matrix: A1_A5_A6_A7_A8_A9_A10_A11_A14_verified
    matrix_gaps: A2_A3_A4_win7_matrix_A12_A13_full_soak
    tests: 115_passed_0_failed
supersedes: []
known_risks:
  - Adapter l4capture_adapter.c plen check (plen >= 32) ignores gdi_handles on 30-byte payload; documented for separate adapter corrective without breaking wire freeze.
  - Win7/Embedded matrix and 2h soak require dedicated images/stand; not available in this session.
  - DXGI path reports gdi_handles=0 (no GDI objects); GDI path covered by unit match to GetGuiResources.
  - producer_commit also contains empty tools/docs/l4tools_backlog_v1.md outside L4C-10 deliverables.
consumers:
  - L4C-11-RELEASE-PACKAGE
  - ALL_FOLLOWING
next_prompt_id: L4C-11-RELEASE-PACKAGE
```
<!-- HANDOFF:H-L4C-10-v1:END -->
