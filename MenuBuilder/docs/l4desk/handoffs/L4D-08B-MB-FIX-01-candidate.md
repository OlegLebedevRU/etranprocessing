# Candidate Handoff Package: L4D-08B-MB-FIX-01 (DETACHED_V1)

<!-- HANDOFF:H-L4D-08B-MB-FIX-01-v1:BEGIN -->
```yaml
handoff_id: H-L4D-08B-MB-FIX-01-v1
status: CANDIDATE
contract_kinds:
  - API
  - DEPLOYMENT
producer_prompt_id: L4D-08B-MB-FIX-01
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-08B-MB-FIX-01-report.md
producer_branch: l4desk/l4d-08b-mb
producer_commit: ad5a13d9fce804746f4f961812b8a026ba416bf4
report_commit: 8bd1bc52cf2614188f82745c0dcc49a8562792b6
accepted_at_utc: null
contract_version: 1.0.0
schema_revision: 1.0.0
artifact_version: 1.0.0
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-08B-MB-FIX-01-candidate.md
artifact_paths:
  - MenuBuilder/backend/app/services/remote_session_use_case.py
  - MenuBuilder/backend/app/services/media_orchestrator_client.py
  - MenuBuilder/backend/app/services/remote_session_policy.py
  - MenuBuilder/backend/app/routers/remote_sessions.py
  - MenuBuilder/backend/app/routers/video_control.py
  - MenuBuilder/backend/app/routers/video.py
  - MenuBuilder/backend/app/repositories/l4desk_repository.py
  - MenuBuilder/backend/app/config.py
  - MenuBuilder/backend/app/main.py
  - MenuBuilder/frontend/src/api/video.ts
  - MenuBuilder/frontend/src/routes/video-surveillance.tsx
  - MenuBuilder/frontend/src/routes/devices/DeviceConsoleTab.tsx
  - MenuBuilder/backend/tests/test_remote_session_orchestration.py
  - MenuBuilder/docs/l4desk/handoffs/L4D-08B-MB-FIX-01-report.md
artifact_sha256:
  - e83af253732b11d89e63c18562b563420ae6b228492f40eee74d27de07df2389
  - 65b07e3b35eccbfa412ec75a8b1362f1cdf682107af51385e5e06e4d8652ed8a
  - 688e4d3d31ab4c623d4bc85f602ede31bb935ee39394e88fd3de08c5444e5ca5
  - f6612a7f679e6e1c73d74128911dcbe608778fb9d8212ac2f93096777e94dd72
  - 0ec7212e941b825407a15e47d68fd499298b5f230e6e1e963848f94e09f1d69b
  - ac7856605aa42cc24db1ec8657d5f5959e6496a35e5940ff68dbc614f78f03bc
  - 46947572410dab6e163f7ca9f3f22644ef8c33c3629edef51709915a81aefb23
  - 31782a1eb607a3f84375ef798ed000e614de33c09161e7353d0855b6db6a3116
  - d54d19cfe79fc00a2c1d3db397cb83c44759f02c018742c0131676e716626e55
  - ae38c22884ad8246cbcaefb70aa49be5b42687dfa477f7b19860b0224c789ea7
  - 87f8dbc8e396bad3d6c35005d1672cc9a48815afbb894323378bf9e095b72d14
  - d8b93b0ab0285781f33efba06dfca6d3469ddf2872ec48ec8b3333e7fc7a1cf5
  - c34880ca65b4ac4b81a25b26034d1c2317f9f224c46a189cbcd450fcf3c030f1
  - 3d242f40722d1e8b9e65a8c7808a11c9c58ce27688534ef85bbc525afef38460
compatibility:
  backward_compatible_with:
    - H-L4D-07-IOT-v1
    - H-L4D-08A-MEDIA-v1
  breaking_changes: false
  notes: "Corrective handoff package resolving HASH_MISMATCH, schema alignment, and PostgreSQL check constraint l4desk_session_active_ck on remote session creation."
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  l4desk_session_orchestration_enabled: true
  l4desk_policy_enforcement_enabled: false
contract_payload:
  corrects_candidate: H-L4D-08B-MB-v1
  registration_id: R-L4D-08B-MB-FIX-01-v1
  evidence_source: L4D-08B-MB-FIX-01
  endpoints:
    start_session: /api/v1/remote-sessions/start
    stop_session: /api/v1/remote-sessions/stop
    stop_session_by_id: /api/v1/remote-sessions/{session_id}/stop
    device_active_session: /api/v1/remote-sessions/devices/{device_id}/active
  policy_seam:
    interface: RemoteSessionPolicy
    legacy_implementation: PermissiveLegacyPolicy
    commercial_implementation: L4DeskEntitlementPolicy (disabled in 08B)
  error_mapping:
    400: invalid_request
    401: unauthorized
    403: tenant_forbidden, permission_denied, policy_denied
    404: terminal_not_found
    409: session_busy (active session conflict, automatic switch forbidden), lease_conflict
    502: iot_gateway_error, media_gateway_error
  invariants:
    - Exactly one active remote session per terminal device across both console and video
    - Automatic cross-switching between console and video is strictly forbidden
    - Replay with identical operation_id returns active session parameters idempotently
    - Partial failure during media or stream launch executes compensating stop on IoT and releases control lease
supersedes: []
known_risks: []
consumers:
  - L4D-08B-MB
next_prompt_id: L4D-08B-MB
```
<!-- HANDOFF:H-L4D-08B-MB-FIX-01-v1:END -->

<!-- HANDOFF:H-L4D-08B-MB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-08B-MB-v1
status: CANDIDATE
contract_kinds:
  - API
  - DEPLOYMENT
producer_prompt_id: L4D-08B-MB
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-08B-MB-report.md
producer_branch: l4desk/l4d-08b-mb
producer_commit: ad5a13d9fce804746f4f961812b8a026ba416bf4
report_commit: 8bd1bc52cf2614188f82745c0dcc49a8562792b6
accepted_at_utc: null
contract_version: 1.0.0
schema_revision: 1.0.0
artifact_version: 1.0.0
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-08B-MB-FIX-01-candidate.md
artifact_paths:
  - MenuBuilder/backend/app/services/remote_session_use_case.py
  - MenuBuilder/backend/app/services/media_orchestrator_client.py
  - MenuBuilder/backend/app/services/remote_session_policy.py
  - MenuBuilder/backend/app/routers/remote_sessions.py
  - MenuBuilder/backend/app/routers/video_control.py
  - MenuBuilder/backend/app/routers/video.py
  - MenuBuilder/backend/app/repositories/l4desk_repository.py
  - MenuBuilder/backend/app/config.py
  - MenuBuilder/backend/app/main.py
  - MenuBuilder/frontend/src/api/video.ts
  - MenuBuilder/frontend/src/routes/video-surveillance.tsx
  - MenuBuilder/frontend/src/routes/devices/DeviceConsoleTab.tsx
  - MenuBuilder/backend/tests/test_remote_session_orchestration.py
  - MenuBuilder/docs/l4desk/handoffs/L4D-08B-MB-FIX-01-report.md
artifact_sha256:
  - e83af253732b11d89e63c18562b563420ae6b228492f40eee74d27de07df2389
  - 65b07e3b35eccbfa412ec75a8b1362f1cdf682107af51385e5e06e4d8652ed8a
  - 688e4d3d31ab4c623d4bc85f602ede31bb935ee39394e88fd3de08c5444e5ca5
  - f6612a7f679e6e1c73d74128911dcbe608778fb9d8212ac2f93096777e94dd72
  - 0ec7212e941b825407a15e47d68fd499298b5f230e6e1e963848f94e09f1d69b
  - ac7856605aa42cc24db1ec8657d5f5959e6496a35e5940ff68dbc614f78f03bc
  - 46947572410dab6e163f7ca9f3f22644ef8c33c3629edef51709915a81aefb23
  - 31782a1eb607a3f84375ef798ed000e614de33c09161e7353d0855b6db6a3116
  - d54d19cfe79fc00a2c1d3db397cb83c44759f02c018742c0131676e716626e55
  - ae38c22884ad8246cbcaefb70aa49be5b42687dfa477f7b19860b0224c789ea7
  - 87f8dbc8e396bad3d6c35005d1672cc9a48815afbb894323378bf9e095b72d14
  - d8b93b0ab0285781f33efba06dfca6d3469ddf2872ec48ec8b3333e7fc7a1cf5
  - c34880ca65b4ac4b81a25b26034d1c2317f9f224c46a189cbcd450fcf3c030f1
  - 3d242f40722d1e8b9e65a8c7808a11c9c58ce27688534ef85bbc525afef38460
compatibility:
  backward_compatible_with:
    - H-L4D-07-IOT-v1
    - H-L4D-08A-MEDIA-v1
  breaking_changes: false
  notes: Unified RemoteSessionUseCase for console and video session orchestration across legacy MenuBuilder users and L4Desk commercial profile. Enforces mutual exclusion (session_busy 409, no auto-switch), compensating stop upon partial provider failures, and provides a policy seam with disabled entitlement flag. Verified and confirmed under corrective step L4D-08B-MB-FIX-01.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  l4desk_session_orchestration_enabled: true
  l4desk_policy_enforcement_enabled: false
contract_payload:
  evidence_source: L4D-08B-MB-FIX-01
  verified_by_handoff: H-L4D-08B-MB-FIX-01-v1
  endpoints:
    start_session: /api/v1/remote-sessions/start
    stop_session: /api/v1/remote-sessions/stop
    stop_session_by_id: /api/v1/remote-sessions/{session_id}/stop
    device_active_session: /api/v1/remote-sessions/devices/{device_id}/active
  policy_seam:
    interface: RemoteSessionPolicy
    legacy_implementation: PermissiveLegacyPolicy
    commercial_implementation: L4DeskEntitlementPolicy (disabled in 08B)
  error_mapping:
    400: invalid_request
    401: unauthorized
    403: tenant_forbidden, permission_denied, policy_denied
    404: terminal_not_found
    409: session_busy (active session conflict, automatic switch forbidden), lease_conflict
    502: iot_gateway_error, media_gateway_error
  invariants:
    - Exactly one active remote session per terminal device across both console and video
    - Automatic cross-switching between console and video is strictly forbidden
    - Replay with identical operation_id returns active session parameters idempotently
    - Partial failure during media or stream launch executes compensating stop on IoT and releases control lease
supersedes: []
known_risks: []
consumers:
  - L4D-09-MB
next_prompt_id: L4D-09-MB
```
<!-- HANDOFF:H-L4D-08B-MB-v1:END -->
