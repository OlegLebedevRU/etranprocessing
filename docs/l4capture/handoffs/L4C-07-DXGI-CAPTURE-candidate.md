handoff_id: H-L4C-07-v1
status: ACCEPTED
contract_kinds:
  - DXGI_CAPTURE_BACKEND
  - DYNAMIC_D3D11_LOADER
  - GDI_FALLBACK_CONTROLLER
  - ROTATION_CURSOR_HANDLER
producer_prompt_id: L4C-07-DXGI-CAPTURE
producer_scope_project: tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-07-DXGI-CAPTURE-report.md
producer_branch: l4capture/l4c-07-dxgi-capture
producer_commit: a64b96d8e6f0a4d6362675ff6d0b6d364b2f6d25
accepted_at_utc: 2026-09-23T08:52:53Z
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-07-DXGI-CAPTURE-report.md
  - tools/l4capture/include/l4capture/dxgi_capture.h
  - tools/l4capture/src/capture/dxgi_capture.c
  - tools/l4capture/bin/x86/l4capture.exe
  - tools/l4capture/bin/x64/l4capture.exe
artifact_sha256:
  - da782bede4e7e2637434301596e2ba62024216ed6a5de04df4fdecea271c4a56
  - cc2df472c09f65644343796263d21fff2bca7317d307ba7f824786add4eb369c
  - 4e0bef6f694c28839052194b0c68ed32004faccd0b69f8883f3aec60873f467c
  - 1a689e3c274590fcefa63e1b0ab737dc874880ab21529910ec312892ca422319
  - 76a7b04ffb6c9e54ec767b813aa1efa0f87a36cc3fafbb3d42e854c25e7fd45a
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
    - H-L4C-02-v1
    - H-L4C-03-v1
    - H-L4C-04-v1
    - H-L4C-05-v1
    - H-L4C-06-v1
  breaking_changes: false
  notes: DXGI 1.2 Desktop Duplication backend implemented with dynamic D3D11 loading, zero-copy staging texture readback, hardware cursor composition, screen rotation support, and graceful automatic GDI fallback on ACCESS_LOST.
deployment_status: LOCAL_TESTS_PASSED
deployed_environment: local_build
feature_flags:
  l4capture_dxgi_capture: enabled
  l4capture_gdi_fallback: enabled
contract_payload:
  capture_backend:
    primary: DXGI_1_2_DESKTOP_DUPLICATION
    fallback: GDI_BITBLT
    supported_os: Windows 8, 8.1, 10, 11 (Windows 7 falls back to GDI)
    staging_format: DXGI_FORMAT_B8G8R8A8_UNORM
    rotation_handling: [IDENTITY, ROTATE90, ROTATE180, ROTATE270]
    cursor_overlay: hardware_shape_cache_single_render
  error_handling:
    access_lost_policy: fail_closed_if_session_unavailable_else_3_retries_then_gdi_fallback
    retry_delays_ms: [100, 300, 1000]
    stop_latency_ms: <= 500
    leak_prevention: unconditional_release_frame
supersedes: []
known_risks:
  - R1: UAC/Lock screen causes DXGI_ERROR_ACCESS_LOST — verified fail-closed <= 500ms without leak or hanging.
  - R3: Staging buffer leak — verified unconditional ReleaseFrame across 100 stress cycles.
  - R4: Windows 7 incompatibility — eliminated via dynamic LoadLibraryExW from System32.
consumers:
  - L4C-08-MF-ENCODER
  - L4C-09-PROFILES-DEGRADE
  - L4C-10-WIN7-TELEMETRY
  - L4C-11-RELEASE-PACKAGE
  - ALL_FOLLOWING
next_prompt_id: L4C-08-MF-ENCODER
