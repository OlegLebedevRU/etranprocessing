<!-- HANDOFF:H-L4D-08B-FIX-01-MB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-08B-FIX-01-MB-v1
status: CANDIDATE
contract_kinds:
  - MEDIA_SESSION_CONSUMER
  - LEGACY_FLOW_REMOVAL
  - UI_SAFE_UNIFIED_ORCHESTRATION
  - RECONCILE_REGRESSION_GUARD
producer_prompt_id: L4D-08B-FIX-01-MB
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-08B-FIX-01-MB-report.md
producer_branch: l4desk/l4d-08b-fix-01-mb
producer_commit: 0fc2f66  # HEAD (chain: 29dd143 -> 5048def -> 2f66bc7 -> 0ba7641 -> 602a5c5 -> 6aed6c4 -> 0fc2f66)
accepted_at_utc: null
contract_version: 1.1.0
schema_revision: N/A
artifact_version: 1.1.0
artifact_paths:
  - MenuBuilder/backend/app/routers/video.py
  - MenuBuilder/backend/app/routers/video_control.py
  - MenuBuilder/backend/app/services/remote_session_use_case.py
  - MenuBuilder/backend/app/config.py
  - MenuBuilder/backend/.env.example
  - MenuBuilder/backend/tests/test_video.py
  - MenuBuilder/backend/tests/test_video_stream_permissions.py
  - MenuBuilder/backend/tests/test_remote_session_orchestration.py
  - MenuBuilder/backend/tests/test_step4_video_contracts.py
  - MenuBuilder/frontend/src/api/janusClient.ts
  - l4media/janus/janus.jcfg
  - l4media/ingress/src/media_lifecycle.h
  - MenuBuilder/docs/l4desk/handoffs/L4D-08B-FIX-01-MB-report.md
  - MenuBuilder/docs/l4desk/handoffs/L4D-08B-FIX-01-MB-candidate.md
artifact_sha256:
  - E0AB48D3D9DB70A0975CA80A19BD552B2DE2B2D466E91CFC364D4B68282A585C
  - DB2F4AE1813E5BF1A291F456E98D92057BF56A819E7822F46AC17E95D5527017
  - 6854CA6A1C122EC349071B66CE39A9FF3C49DAF7A64623D5D65A4983343E2F25
  - 39392322EB8C1B637AB51068584A5CACF73F020D16BE5D78AC4D02522BAC5A16
  - 45C88CD43F3D6A46C39F8C1319D617EF45D3A10C30A5B7DD258FC4770A12D399
  - BA48C3E5FCA2D7A32C817C0B94391C6B220F8E299DE5F4E009A2C16AEBEB1060
  - BBE20DC3FB6D3CB1FB800959AA7ED05D8DD4693A5C89C919D984AACAF076AABB
  - 6A3A1BA14115AE90E50DB9B48CE6A557A27A9D838912DB622EEA495D20F469A7
  - 14FC99E026648DE5F93DD8B7330F76479C1C2FE77D670D76EAD7DEF3F4D35CD0
  - D5F7E5DB91E7542EB5C45483708262DB866D4B1A7B29178B490AD4577048ED9A
  - 8AB9A800C70F5D21DBCF77AEAE4635F8B42B5C1F2A55FF9F07E5D4BE554691EA
  - 6ED7529CF3029D9F1164384AF049E861F304A450B6F227CD632BB7A83F015912
  - D7B279F959C2C84ABEF72836C4B45271C48F1B1E3EC6BBA694DFC32D78139BD8
  - <this_candidate_sha256>
compatibility:
  backward_compatible_with:
    - H-L4D-07-IOT-v1
    - H-L4D-08A-MEDIA-v1
  breaking_changes: true
  notes: >
    Direct MenuBuilder ownership of dynamic ingress routes and Janus mountpoints
    is removed. All legacy and current video UI/API flows use the single
    RemoteSessionUseCase and l4media lifecycle API. Legacy HTTP paths, if retained
    for UI routing, are thin facades only and do not preserve direct media setup.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  direct_ingress_route_fallback: removed
  direct_janus_mountpoint_fallback: removed
  unified_media_lifecycle_required: true
contract_payload:
  ownership:
    menu_builder:
      - tenant/auth/policy/session orchestration
      - IoT session and lease coordination
      - lifecycle API consumer
    l4media_ingress:
      - dynamic ingress route lifecycle
      - Janus mountpoint lifecycle
      - media session state
      - TTL/watchdog/reconcile cleanup
  prohibited_in_menubuilder:
    - direct_route_creation
    - direct_route_deletion
    - direct_janus_mountpoint_creation
    - direct_janus_mountpoint_deletion
    - lifecycle_to_direct_fallback
  required_video_start_order:
    - iot_session_lock
    - control_lease_when_required
    - media_lifecycle_start
    - terminal_stream_start
    - local_session_active
  failure_behavior:
    media_lifecycle_failure: fail_closed_with_compensating_stop
    direct_media_fallback: prohibited
  ui_safety:
    legacy_ui_paths_delegate_to_unified_use_case: true
    false_success_on_media_failure: prohibited
    player_opened_before_confirmed_lifecycle_start: prohibited
  verification:
    long_running_reconcile_test_duration_sec: pending_production_smoke
    reconcile_intervals_survived: pending_production_smoke
    direct_route_calls_detected: false
    direct_janus_calls_detected: false
    orphan_cleanup_for_test_session_detected: false
supersedes:
  - H-L4D-08B-MB-v1
known_risks:
  - destroy_mountpoint API parameter is now a no-op (backward compatible)
  - video_control.py has parallel stop path via media_orchestrator_client outside RemoteSessionUseCase (goes through lifecycle API, not direct)
consumers:
  - L4D-09-MB
  - L4D-10-MB
  - L4D-12-MB
  - L4D-13-MB
  - L4D-14-MB
  - L4D-17E-MB
  - L4D-17F-DOCS
  - L4D-18E-MB
next_prompt_id: L4D-09-MB
```
<!-- HANDOFF:H-L4D-08B-FIX-01-MB-v1:END -->
