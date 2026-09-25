# H-L4D-08B-FIX-03-MB-v1 — Candidate (DETACHED_V1)

<!-- HANDOFF:H-L4D-08B-FIX-03-MB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-08B-FIX-03-MB-v1
status: CANDIDATE
contract_kinds:
  - RELIABLE_SESSION_LIFECYCLE
  - IOT_REMOTE_SESSION_CONSUMER
  - MEDIA_SESSION_CONSUMER
producer_prompt_id: L4D-08B-FIX-03-MB
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-08B-FIX-03-MB-report.md
producer_branch: l4desk/l4d-08b-fix-03-mb
producer_commit: 5bf03b9a689fb7cf8152f012d5d5367bc6a5bf33
report_commit: 4f28187b4f2b6e176f31bfd4b8de4a8533af8d0f
accepted_at_utc: null
registration_id: R-L4D-08B-FIX-03-MB-v1
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-08B-FIX-03-MB-candidate.md
contract_version: 1.1.0
schema_revision: '2026-09-25-v2'
artifact_version: 1.0.0
artifact_paths:
  - MenuBuilder/backend/app/repositories/l4desk_repository.py
  - MenuBuilder/backend/app/routers/video_control.py
  - MenuBuilder/backend/app/services/financial_core/stop_outbox.py
  - MenuBuilder/backend/app/services/iot_event_feed_client.py
  - MenuBuilder/backend/app/services/remote_session_stop.py
  - MenuBuilder/backend/app/services/remote_session_use_case.py
  - MenuBuilder/backend/tests/test_l4d_12_entitlement_grace_and_notifications.py
  - MenuBuilder/backend/tests/test_remote_session_orchestration.py
  - MenuBuilder/backend/tests/test_remote_session_stop.py
  - MenuBuilder/backend/tests/test_step4_video_contracts.py
  - MenuBuilder/backend/tests/test_step6_quick_actions.py
  - MenuBuilder/backend/tests/test_video.py
  - MenuBuilder/backend/tests/test_video_stream_permissions.py
  - MenuBuilder/docs/l4desk/handoffs/L4D-08B-FIX-03-MB-report.md
artifact_sha256:
  - 6c90ccd80b3f5a8bead34347905fc3006f5a5cb5e43211a0d4a90394ccda5dca
  - 0225ef93ce27d2f02d5cce47264011c94a2527797d34cb656ef1d5372901dec4
  - 053604b9381f872dfc8293b787a164615790ccf82bea6ea3b5760e1106462f33
  - 1065a5807d614ece571eabf93fff9646db0c2e53d01f691425d4558330b3015c
  - 30dd1a667367a08f3789aaee99058c6841d7432df42f66a914e4d8d786b2c5d6
  - 4743c7b71a1bcc7f62e0a1a640f9621394be48c825ab2a0855f8e457d87848cf
  - fcf22e75d74ceed9f7fc73c97ae0a5c0aad440135c86a900183471d9b56c4714
  - b70db0d0513733857083fb66a565d2216f06e9125a429d8d5ac097fa92e2f1c7
  - d1ea87b2b6edd0b8d77e287193cf8675e13e53932c364da6e3cbaded8f7bc9a3
  - 216954cf36f5f5c8efe7856cd3809f579566e4051cdf74e8ff2b11b363ded1ac
  - 9e03fcec978394ce68eb179c9b29396e140287d5b1ba94756ba8efeecacb85c8
  - ded20d91f962b385bd1dddb34b6aed8115bae30cbbaefa740d6d5aa9b2b11717
  - c90da8f486629b5a9206c23750ff8c32e754a7435033aa0984708845b82e2ab4
  - 00fce332bd8206a8d7e648f66c48c78ae78d6021a4b1d1f5dfd139b64ac355e0
compatibility:
  backward_compatible_with:
    - H-L4D-07-IOT-STOP-v1
    - H-L4D-08B-FIX-02-MB-v1
  breaking_changes: false
  notes: >
    Both stop HTTP paths converge on one durable exact-ID operation. New IoT and media
    sessions share one ID. Existing ambiguous IDs require explicit reconciliation.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  exact_session_stop_required: true
contract_payload:
  stop_intent_state: stop_requested
  stop_operation_id_pattern: 'stop-mb-{local_session_id}'
  provider_identity_guards:
    - session_id
    - tenant_id
    - sn
  media_identity_guards:
    - exact GET session_id
    - exact GET sn
    - exact stop response session_id
  local_closed_requires:
    - IoT exact terminal state
    - media exact stopped state for video
  retry_owner: MenuBuilder billing stop worker
  tests: 478 passed; ruff passed; pyright app passed; frontend build passed
  production_smoke: start; exact-ID stop; restart without reload; exact-ID stop
supersedes: []
known_risks:
  - historical media/lease IDs in provider_session_id need manual reconciliation
  - production stop_requested age and mismatch metrics unverified
consumers:
  - L4D-14-MB
next_prompt_id: L4D-14-MB
```
<!-- HANDOFF:H-L4D-08B-FIX-03-MB-v1:END -->

Этот файл является candidate после UI-проверки и production smoke. Принятие handoff
контроллером пока не зафиксировано. `detached_candidate_approved: true` подтверждает
разрешённый формат DETACHED_V1 в регистрации, а не принятие результата.
