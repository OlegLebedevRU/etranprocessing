```yaml
handoff_id: H-L4C-06-v1
status: BLOCKED_OWNER_APPROVAL
contract_kinds:
  - MILESTONE_LIVE_VERIFICATION
  - E2E_NATIVE_PIPELINE
  - FAULT_MATRIX_R1_R4
  - KIOSK_FOCUS_LIFECYCLE
producer_prompt_id: L4C-06-MILESTONE-LIVE-VERIFY
producer_scope_project: tools/l4capture + tools/l4desk
producer_report_path: docs/l4capture/handoffs/L4C-06-MILESTONE-LIVE-VERIFY-report.md
producer_branch: l4capture/l4c-06-milestone-live-verify
producer_commit: a0b1f76
accepted_at_utc: null
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-06-MILESTONE-LIVE-VERIFY-report.md
artifact_sha256:
  - 5dd10b8357afeee40485523615cf21bb82b4a1c72a58e8eb5db8e650ffa64888
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
    - H-L4C-02-v1
    - H-L4C-03-v1
    - H-L4C-04-v1
    - H-L4C-05-v1
  breaking_changes: false
  notes: M-1 live verification. Native l4capture pipeline verified end-to-end: GDI capture + OpenH264 + RTP on test terminal. Three integration defects found and fixed in l4capture_adapter.c. All R1-R4 fault checks PASS. Awaiting owner verdict NORM.
deployment_status: LIVE_VERIFIED_ON_TEST_TERMINAL
deployed_environment: test_terminal_win10_x64
feature_flags:
  l4capture_native_pipeline: live_verified
  l4capture_backend_adapter: live_verified
  l4capture_input_gate: unit_verified
  l4desk_kiosk_focus_guard: unit_verified
  l4desk_kiosk_lifecycle_backend: unit_verified
contract_payload:
  e2e_pipeline:
    capture: GDI (physical pixels, cursor overlay)
    scale: Bilinear to 854x480
    color: BT.601 limited range I420
    encode: OpenH264 v2.6.0 Constrained Baseline Level 3.1
    transport: RTP/UDP loopback 127.0.0.1:5004/5005
    process_lifetime: 82+ seconds stable, 34MB WS, 102 handles
  fault_matrix:
    r1_lease_safety: PASS
    r2_delivery: PASS
    r3_resilience: PASS
    r4_platform: PASS
  kiosk_tests:
    focus_detection: PASS
    adaptive_fallback: PASS
    generic_mode: PASS
    lifecycle: PASS
  integration_defects_fixed:
    - cmd_start_payload_size_mismatch (extra padding)
    - zero_ids_in_cmd_start (str_to_id16 conversion)
    - stderr_handle_null (removed STARTF_USESTDHANDLES)
  owner_verdict: PENDING
supersedes: []
known_risks:
  - R1: UAC/Lock — verified fail-closed <= 500ms
  - R2: RTP delivery — verified loopback, IDR cadence
  - R3: Process stability — verified 82s+ stable
  - R4: Platform — verified Win10 x64 multi-display
consumers:
  - L4C-07-DXGI-CAPTURE
  - ALL_FOLLOWING
next_prompt_id: L4C-07-DXGI-CAPTURE
```
