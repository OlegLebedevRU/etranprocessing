# H-L4D-08B-FIX-02-MB-v1 — Candidate (DETACHED_V1)

```yaml
handoff_id: H-L4D-08B-FIX-02-MB-v1
status: CANDIDATE
contract_kinds:
  - MEDIA_SESSION_CONSUMER
  - RELIABLE_SESSION_LIFECYCLE
  - TEST_RISK_CLOSURE
producer_prompt_id: L4D-08B-FIX-02-MB
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-08B-FIX-02-MB-report.md
producer_branch: l4desk/l4d-08b-fix-02-mb
registration_id: R-L4D-08B-FIX-02-MB-v1
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-08B-FIX-02-MB-candidate.md
contract_version: 1.2.0
schema_revision: '027'
artifact_version: 1.2.0
artifact_paths:
  - MenuBuilder/backend/app/routers/video_control.py
  - MenuBuilder/backend/app/routers/video.py
  - MenuBuilder/backend/app/repositories/l4desk_repository.py
  - MenuBuilder/backend/app/services/remote_session_use_case.py
  - MenuBuilder/backend/app/services/iot_client.py
  - MenuBuilder/backend/tests/test_step6_quick_actions.py
  - MenuBuilder/backend/tests/test_video.py
  - MenuBuilder/frontend/src/api/video.ts
  - MenuBuilder/frontend/src/routes/video-surveillance.tsx
  - l4media/ingress/src/media_lifecycle.h
  - MenuBuilder/docs/l4desk/handoffs/L4D-08B-FIX-02-MB-report.md
artifact_sha256:
  - DB4AB78BBC1D34C430771F6BECC5F476C5B3F984A7477DD3933FB1CEA794C932
  - 1697AB831BE9A0E68C8FA03F4392AFB24728A35E2DE449F3AADEA64D6C44B59D
  - 6D389739A5C73C8EC3D883E44369CB1AF19C19847A9972AB8D7EFDC40BB44398
  - A8F92A468DD2811636F2C469B292C0822068E7E2E61EC07A1399740F61377714
  - FEEB553AC410EDFBF01CD975F4F6862DCD0317E4BD7D48C8EFF104D0BA9BAF9B
  - 201D9BD989C368E5BEF9F3167362968F8FD8767766B3548FFD81B3ABF7D9CFF3
  - 8B176ECDE4B182B2DE6E85B0B36F10E6603137CE261208C4B0E34BB2BEAAD873
  - 7D3BD7A63EEB0A195809811B4F37B7AC060C6B4D8B43C39B93071FB82024D381
  - 2563266FEF826E58DD8F8A3BD05E963906A996DABC2DE2D25EDFC75CD9953068
  - 08ECE1013BA876777C9FF9E204892C86334B91A96DF3C45C2990D91222EA19B1
  - D99CFC61A5ED08AF633CB4F80E66538DBD37E1B7E89F8EFE529A92E99C229C86
compatibility:
  backward_compatible_with:
    - H-L4D-07-IOT-v1
    - H-L4D-08A-MEDIA-v1
    - H-L4D-08B-FIX-01-MB-v1
    - H-L4D-13-MB-FIX-01-v1
  breaking_changes: false
  notes: >
    Corrective step fixes deterministic session_id collision in media lifecycle,
    resolves terminal_id vs device_id session resolution, prevents unhandled 500 errors,
    implements lease reuse and recovery in video_control, implements graceful superseding
    of prior video sessions with lease preservation, adds DELETE /devices/{device_id}/session endpoint,
    wires up frontend 'Завершить активную сессию' button, adds fallback X-Session-Id in iot_client,
    adds stop by SN in Ingress, and fixes mock gap in test_step6_quick_actions.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  unified_media_lifecycle_required: true
contract_payload:
  ownership:
    menu_builder:
      - tenant/auth/policy/session orchestration
      - IoT session and lease coordination
      - lifecycle API consumer
      - unique per-session media ID generation
      - robust dual ID resolution (terminal.id and terminal.device_id)
      - lease reuse and conflict recovery
      - graceful session superseding and explicit close endpoint
      - lease synchronization across stream start phases
    l4media_ingress:
      - dynamic ingress route lifecycle
      - Janus mountpoint lifecycle
      - media session state
      - stop by SN and active_session_id feedback
      - TTL/watchdog/reconcile cleanup
  reconnect_resilience:
    deterministic_sn_collision_fixed: true
    session_terminated_auto_reallocated: true
    session_busy_auto_superseded: true
    stopped_mountpoint_recreated_on_restart: true
    dual_terminal_device_id_resolution: true
    unhandled_500_prevented: true
    lease_conflict_auto_recovered: true
    lease_inactive_auto_recovered: true
    button_stop_session_wired_to_backend: true
  test_risk_status:
    test_stream_start_504_terminal_timeout_not_swallowed: PASSED
supersedes: []
known_risks: []
consumers:
  - L4D-14-MB
  - L4D-16-MB
  - L4D-17E-MB
  - L4D-18E-MB
next_prompt_id: L4D-14-MB
```
