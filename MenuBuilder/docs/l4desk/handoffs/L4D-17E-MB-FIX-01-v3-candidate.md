# Candidate Handoff Package: L4D-17E-MB-FIX-01 (DETACHED_V1)

<!-- HANDOFF:H-L4D-17E-MB-FIX-01-v1:BEGIN -->
```yaml
handoff_id: H-L4D-17E-MB-FIX-01-v1
status: CANDIDATE
contract_kinds:
  - REPORT
  - DEPLOYMENT
producer_prompt_id: L4D-17E-MB-FIX-01
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-17E-MB-FIX-01-v3-report.md
producer_branch: release/l4tools-1.8.2-beta-1
producer_commit: 9bda9ce498332960f7f706abb03cae09b0fff206
report_commit: f031b963f43d9c52fbf28aee9f978157386721f1
accepted_at_utc: null
contract_version: 1.0.0
schema_revision: '027'
artifact_version: 1.0.0
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-17E-MB-FIX-01-v3-candidate.md
artifact_paths:
  - MenuBuilder/backend/app/config.py
  - MenuBuilder/backend/app/main.py
  - MenuBuilder/backend/app/routers/video_control.py
  - MenuBuilder/backend/app/services/financial_core/entitlement.py
  - MenuBuilder/backend/app/services/remote_session_metering.py
  - MenuBuilder/backend/tests/fixtures/iot_event_feed_examples_v1_1.json
  - MenuBuilder/backend/tests/test_iot_event_feed_consumer.py
  - MenuBuilder/backend/tests/test_video_watch.py
  - MenuBuilder/frontend/src/routes/video-surveillance.tsx
  - MenuBuilder/frontend/src/utils/permissions.ts
  - MenuBuilder/frontend/src/tests/permissions.test.ts
  - MenuBuilder/docs/l4desk/handoffs/L4D-17E-MB-FIX-01-v3-report.md
artifact_sha256:
  - 55290e86d29a898ddce84d2490c80cee6839cb8347a74b9b74a49fb9b3e977e5
  - 84d4ebf6196bb8d11abb86b2832819fe39a3c20b1e825fc98de6c93fda878554
  - 21833b478203b08e13ed0d61f47b58746f2a484545ff979fd135a77da8943322
  - 1e76a3096212676d90bde8ee91b8c2072915e67fdc3cc8b7fc66b3da737a6f7a
  - 69074f2ffda9df0823d7fd606b702704a18da8adc8727f79830d7074b90debc7
  - 771856c6cfed996aa8a9a99c122a73096afee123a089895f0089c17eb6bac1fe
  - 65591c6fc9d9131ae497016b2bf61e43e75fc90181937bb9112e9f9f6e5abcee
  - 0244c099577ffb4d367bc7b84817025c785f018abc7a9e80009c247bb2fd8283
  - d276a49c076a4acf5e5c934fd1cc7ac0ec93dee0ed06820591ac201865a63ebb
  - 0c92ea7394c37cef5dccb3e512f3b2efdbee3ba71c7512f62e346084d0d07c34
  - 6026d176c5a187d81e7710a09c0600a2343c633d5f80d93c981cfea17268b48b
  - 6dfa6654b7472351da1147f9a526634d6839507f917294fe75f9781f0f2e1be0
compatibility:
  backward_compatible_with:
    - H-L4D-17D-MEDIA-CONTRACT-01-v1
    - H-L4D-17C-VIDEO-WATCH-MB-v1
    - H-L4D-06C-MB-v1
    - H-L4D-08B-MB-v1
    - H-L4D-09-MB-v1
    - H-L4D-10-MB-v1
    - H-L4D-11-MB-v1
    - H-L4D-12-MB-v1
    - H-L4D-13-MB-v1
    - H-L4D-14-MB-v1
    - H-L4D-16-MB-v1
  breaking_changes: false
  notes: 'Corrective acceptance only; runtime code and commercial activation unchanged.'
deployment_status: DEPLOYED_DISABLED
deployed_environment: dev.leo4.ru
feature_flags:
  production_registration: false
  production_billing: false
  production_policy_enforcement: false
  production_entitlement_worker: false
  production_metering_close_worker: false
  production_iot_consumer: false
  isolated_test_policy_enforcement: true
  isolated_test_tenant_ids: [1000]
  isolated_test_free_quota_seconds: 600
contract_payload:
  registration_id: R-L4D-17E-MB-FIX-01-v3
  backend_source_commit: 9bda9ce498332960f7f706abb03cae09b0fff206
  runtime_image: sha256:c70a11d88eef2697898d936278d4f8ce8066bc8cd0fa951d7ece9efda9b49e8e
  backend_suite: '519 passed; Ruff and Pyright clean'
  frontend_suite: '60 passed; build exit 0'
  browser_smoke: 'terminal 1000005 moving video and stop without 500; session 486 closed'
  financial_read_only: 'tenant 1000 posted ledger debit=credit=1000 kopecks; active sessions=0; latest stored reconciliation matched with zero mismatch'
  rollback_image: sha256:08b6d35b2a1322e3f709ae4864ee4308210fa3cc04c7c3ac1b7ed61352880d21
supersedes: []
known_risks:
  - 'Manual payment/storno, Hub/archive, active-stream graceful block and projection rebuild have local tests but no separate runtime E2E in this FIX.'
  - 'Production commercial policy and billing remain disabled; real YooKassa was not used.'
  - 'Original 17F prompt requires an addressed controller correction after this FIX is accepted.'
consumers:
  - L4D-17F-DOCS
next_prompt_id: L4D-17F-DOCS
```
<!-- HANDOFF:H-L4D-17E-MB-FIX-01-v1:END -->

This is a candidate only. The controller decides acceptance independently from the immutable report commit.
