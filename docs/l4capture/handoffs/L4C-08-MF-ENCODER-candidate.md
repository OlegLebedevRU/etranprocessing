handoff_id: H-L4C-08-v1
status: ACCEPTED
contract_kinds:
  - MF_HARDWARE_ENCODER
  - DYNAMIC_MF_LOADER
  - NV12_COLOR_CONVERTER
  - OPENH264_FALLBACK_CONTROLLER
producer_prompt_id: L4C-08-MF-ENCODER
producer_scope_project: tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-08-MF-ENCODER-report.md
producer_branch: l4capture/l4c-08-mf-encoder
producer_commit: a64b96d8e6f0a4d6362675ff6d0b6d364b2f6d25
accepted_at_utc: 2026-09-23T09:30:00Z
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-08-MF-ENCODER-report.md
  - tools/l4capture/include/l4capture/mf_encoder.h
  - tools/l4capture/src/encoder/mf_encoder.c
  - tools/l4capture/bin/x86/l4capture.exe
  - tools/l4capture/bin/x64/l4capture.exe
artifact_sha256:
  - 3183333e9c127fe3f17c5323932c3ff946bd956e1ff0f29c95b553e95e76c589
  - 29d8e626089b3d09db116ed5778f251c6ca4939f1754cb15f122df7583750059
  - 5e29ba64e1caef5c2cec918057a47e0e43b61408e8299f3f620e6dbb50ac1ead
  - 9dcbeead258ac19a874f2372a067cf9b679f4be10eeac3f8f7b4e418cd88d38a
  - 2431972cfec997be74fcda914c84caea10d340f681fc94678decf6d5aeb3b250
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
    - H-L4C-02-v1
    - H-L4C-03-v1
    - H-L4C-04-v1
    - H-L4C-05-v1
    - H-L4C-06-v1
    - H-L4C-07-v1
  breaking_changes: false
  notes: Hardware H.264 MFT encoder implemented with dynamic Media Foundation loading from System32, NV12 color conversion, Annex B/AVCC stripping, IDR cadence <= 2.0s, force-IDR coalescing, and transparent fallback to OpenH264 on hardware absence or failure.
deployment_status: LOCAL_TESTS_PASSED
deployed_environment: local_build
feature_flags:
  l4capture_mf_encoder: enabled
  l4capture_openh264_fallback: enabled
contract_payload:
  encoder_backend:
    primary: MF_HARDWARE_H264
    fallback: OPENH264_SOFTWARE
    supported_os: Windows 8, 8.1, 10, 11 (Windows 7 falls back to OpenH264)
    profile_level: Constrained_Baseline_3_1
    sdp_compatibility: 42e01f
    input_format: NV12 (BT.601 limited)
    b_frames: 0 (disabled)
    latency_mode: zero_latency_low_delay
    probe_timeout_ms: <= 2000
    idr_interval_ms: <= 2000
    force_idr_coalesce_ms: 500
  color_conversion:
    nv12_matrix: BT.601 limited range
    subsampling: 2x2 chroma averaging
  fallback_policy:
    trigger: unsupported_os_or_hardware_probe_failure_or_runtime_error
    action: transparent_switch_to_openh264_with_i420
    status_reported: L4C_FALLBACK_MFT_UNAVAILABLE
supersedes: []
known_risks:
  - R3: MFT driver hang during probe — bounded by 2.0s timeout with supervisor watchdog.
  - R4: Windows 7 Media Foundation absence — eliminated via dynamic LoadLibraryExW from System32 and graceful OpenH264 fallback.
consumers:
  - L4C-09-PROFILES-DEGRADE
  - L4C-10-WIN7-TELEMETRY
  - L4C-11-RELEASE-PACKAGE
  - ALL_FOLLOWING
next_prompt_id: L4C-09-PROFILES-DEGRADE
