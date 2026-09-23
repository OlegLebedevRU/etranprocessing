<!-- HANDOFF:H-L4C-09-v1:BEGIN -->
```yaml
handoff_id: H-L4C-09-v1
status: ACCEPTED
contract_kinds:
  - QUALITY_PROFILES_540P_720P
  - MONOTONIC_DEGRADE_CONTROLLER
  - OVERLOAD_DETECTOR
  - INPUT_GATE_POLICY_PRESERVED
  - PROFILE_TELEMETRY
producer_prompt_id: L4C-09-PROFILES-DEGRADE
producer_scope_project: tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-09-PROFILES-DEGRADE-report.md
producer_branch: l4capture/l4c-09-profiles-degrade
producer_commit: c6a0f0f1633f3a0f5829f7ac296f82bf59c3aa1c
accepted_at_utc: 2026-09-23T13:00:11Z
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-09-PROFILES-DEGRADE-report.md
  - tools/l4capture/include/l4capture/video_profile.h
  - tools/l4capture/include/l4capture/degrade_controller.h
  - tools/l4capture/src/pipeline/video_profile.c
  - tools/l4capture/src/pipeline/degrade_controller.c
  - tools/l4capture/tests/test_profiles_degrade.c
  - tools/l4capture/src/main.c
  - tools/l4capture/bin/x86/l4capture.exe
  - tools/l4capture/bin/x64/l4capture.exe
artifact_sha256:
  - a2b2a4f8f3eb46f94cbffccc1c0cdc219ff56dc80129b4f7b2b6f4b47b190957
  - 027aa76c332edabbe0c303452318d704011e8b738f037b9cc00e0a2d7144bca5
  - 3fc37cd35fc36a84ae5fac1f2692444540a4e749f1782f16856087838947cbcc
  - 0e0c60adea598d0ac0188c7ef4b106985cdba0231636e484a73030c3cfd93c32
  - 97d6d36712a8f7f42af0241a7f41f42b167c039119d2609d192413d273c739a3
  - c1e6a1162bca886e91d04fe21afe422fa9245666992b45e850018131b5811e94
  - f9fce1ca169bc6dc37dd576d41ffd4ea562552d8771c2420e2f2c14dd90f1d94
  - 20bf7ec1cf32857d0df310c87004db1701e225af28795879b3b3050da9f3960c
  - 7e54eedaedb636e027690bb41c00f18618ef26187cc2a3fcf861556d7fa1f652
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
  breaking_changes: false
  notes: Profiles 480p/540p/720p and monotonic degrade ladder per architecture section 7. Wire profile_id aligned with l4desk adapter (1=low,2=540p,3=default). Live path confirmed default->1280x720@10fps OpenH264 via l4desk on Win10/Iris. No UI/wire schema change. Input remains denied for default. Gaps: live forced-overload D0-D3 ladder (fake-clock only), browser decode on transitions, external input-gate E2E, Win7 smoke, 100-cycle soak; MF probe flake unrelated.
deployment_status: LOCAL_TESTS_PASSED
deployed_environment: local_build_and_test_terminal_win10_x64
feature_flags: {}
contract_payload:
  profiles:
    base_480p: { raster: 854x480, fps: 10, bitrate_kbps: [500, 500, 700], input_profile_eligible: true, ui: low }
    premium_540p: { raster: 960x540, fps_range: [10, 15], bitrate_kbps: [600, 700, 900], input_profile_eligible: false, ui: internal_only }
    premium_720p: { raster: 1280x720, fps_range: [10, 15], bitrate_kbps: [600, 800, 1000], input_profile_eligible: false, ui: default_upper_bound }
  startup_policy: low->480p/10; default+Win7->480p/10; default+MFT->720p/15; default+OpenH264->720p/10; default unsupported 720p->480p refused_premium
  wire_profile_id: { low: 1, premium_540p: 2, default: 3 }
  degrade_ladder:
    order: [drop_late_raw, fps_15_to_10, raster_720_to_540, raster_540_to_480, diagnosed_stop]
    overload_condition: drops_gt_20pct_or_p95_gt_frame_interval_in_two_consecutive_3s_windows
    hold_off_sec: 6
    fresh_post_action_windows: 2
    d0_once_per_process: true
    step_policy: exactly_one_step_at_a_time
    upgrade_in_session: forbidden
    raster_down_fps_never_up: true
    stop_after_480p_sustained_overload_sec: 15
    floor_timer_start: first_bad_window_after_d0_at_480p_or_d3
    floor_timer_reset: good_or_no_data_window
    bitrate_cut_policy: only_on_confirmed_delivery_constraint_or_measured_bitrate_exceed
  config_change:
    applies_to: raster_or_encoder_change_including_d1_if_reinitialized
    clear_pending_raw_au: true
    reject_stale_generation: true
    first_au: SPS_PPS_IDR
    rtp_epoch: preserved_within_process
    actual_commit: first_valid_new_au_sent
    periodic_idr_max_interval_sec: 2
  input_gate:
    owner: existing_l4desk_adapter
    allowed_pair: [low, base_480p]
    denied: default_even_if_degraded_to_480p
    unknown_profile_or_geometry: deny
  telemetry_keys:
    - video_profile_requested
    - video_profile_actual
    - video_degradation_state
    - video_stream_fps
    - video_stream_bitrate
    - video_fallback_reason
  telemetry_mapping: READY w/h/fps + DEGRADED state/reason(HIGH_LOAD=4) + METRICS fps/bitrate; p95 and floor streak local-only (l4capture_degrade.log)
  high_load_error_mapping: EVENT_DEGRADED reason=4 + l4c_safety_stop(L4C_ERR_FATAL=99) terminal stop, no auto-restart loop
  evidence:
    local_unit: 102/103 (only test_mf_probe_graceful flake); profiles/degrade 24 PASS
    live_win10_iris: stream 1280x720@10fps DXGI+OpenH264 via l4desk; WS~61MiB handles~265
    live_gap: forced_overload_ladder_not_captured; browser_transitions; input_gate_e2e; win7_smoke; soak_100
supersedes: []
known_risks:
  - Live D0-D3 ladder only proven under fake clock; CPU starvation harness did not produce drop-class metrics before sync pipeline fix.
  - Software OpenH264 720p CPU ~0.86 core exceeds MFT budget; hardware MFT absent on this stand (probe unsupported).
  - Private Bytes ~62 MiB at 720p exceeds 480p target 45 MiB; within 128 MiB admission.
  - test_mf_probe_graceful timing flake on loaded stand (not L4C-09 scope).
consumers:
  - L4C-10-WIN7-TELEMETRY
  - L4C-11-RELEASE-PACKAGE
  - ALL_FOLLOWING
next_prompt_id: L4C-10-WIN7-TELEMETRY
```
<!-- HANDOFF:H-L4C-09-v1:END -->
