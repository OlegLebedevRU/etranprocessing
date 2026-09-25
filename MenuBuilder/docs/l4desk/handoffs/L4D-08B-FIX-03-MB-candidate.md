# H-L4D-08B-FIX-03-MB-v1 — Candidate (DETACHED_V1)

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
registration_id: R-L4D-08B-FIX-03-MB-v1
candidate_format: DETACHED_V1
detached_candidate_approved: false
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-08B-FIX-03-MB-candidate.md
contract_version: 1.1.0
schema_revision: '2026-09-25-v2'
artifact_version: 1.0.0
implementation_commit: 5bf03b9a689fb7cf8152f012d5d5367bc6a5bf33
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
  - 9A47CF059C0071ADE3D6C4661F6FBDFECD1EFCCCCA6598B42CA84461B16BFB95
  - F595BCDE4887C1D5F4980BCCA59DFE9C3A76F60905D3DF18601A0F29D8D13526
  - 62A18783CDDFE85A6B9B5B393CD1C85020F92FF2B9C1BFCB29134D46612D1321
  - D48984846262486F5850E8BFBE2910C3C085248A6BC8D7685047365F37E615AF
  - 30DD1A667367A08F3789AAEE99058C6841D7432DF42F66A914E4D8D786B2C5D6
  - 35056A786984170FFC11BD2A1244A1106BB78F515D486944BDBDE4C01C902DC2
  - 3E51F00DDBD2165E407F18D57904D75EC43586B6A7849CE876630528E2BB09B7
  - 74395CB69C4C9A463FF53D81045CEDD195791DE9A05FD01FBE4A938DE7C824C2
  - D1EA87B2B6EDD0B8D77E287193CF8675E13E53932C364DA6E3CBADED8F7BC9A3
  - D5331A1C07194C116C89F7FE3871C37B51A1D5C89DC915C63148AB1495937009
  - 27055D3AA47843C0211CFAB8A94F83202EA929B64FB042E852FB28314666CFAD
  - 096CAF4973D1446892B084CAEDEB570B211DBB1FCAF95BBB980B53897C029C96
  - C90DA8F486629B5A9206C23750FF8C32E754A7435033AA0984708845B82E2AB4
  - 345D348A839F56CBF0B45D31636590C95EDFC55D65CF01156A24F2E0A599CA27
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

Этот файл является candidate после UI-проверки и production smoke. Принятие handoff
контроллером пока не зафиксировано (`detached_candidate_approved: false`).
