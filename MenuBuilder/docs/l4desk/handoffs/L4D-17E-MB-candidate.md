# Candidate Handoff Package: L4D-17E-MB (DETACHED_V1)

<!-- HANDOFF:H-L4D-17E-MB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-17E-MB-v1
status: CANDIDATE
contract_kinds:
  - REPORT
  - DEPLOYMENT
  - COMMERCIAL_CONTROL
producer_prompt_id: L4D-17E-MB
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-17E-MB-acceptance-2026-09-27.md
producer_branch: release/l4tools-1.8.2-beta-1
producer_commit: adadce325456a1c037eab184c9df3936c9b2ba3f
report_commit: adadce325456a1c037eab184c9df3936c9b2ba3f
accepted_at_utc: null
contract_version: 1.0.0
schema_revision: '027'
artifact_version: 1.0.0
candidate_format: DETACHED_V1
detached_candidate_approved: false
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-17E-MB-candidate.md
artifact_paths:
  - MenuBuilder/backend/app/config.py
  - MenuBuilder/backend/app/main.py
  - MenuBuilder/backend/app/routers/video_control.py
  - MenuBuilder/backend/app/services/financial_core/entitlement.py
  - MenuBuilder/backend/app/services/remote_session_metering.py
  - MenuBuilder/backend/tests/fixtures/iot_event_feed_examples_v1_1.json
  - MenuBuilder/backend/tests/test_iot_event_feed_consumer.py
  - MenuBuilder/frontend/src/routes/video-surveillance.tsx
  - MenuBuilder/docs/l4desk/handoffs/L4D-17E-MB-acceptance-2026-09-27.md
artifact_sha256:
  - 55290e86d29a898ddce84d2490c80cee6839cb8347a74b9b74a49fb9b3e977e5
  - 84d4ebf6196bb8d11abb86b2832819fe39a3c20b1e825fc98de6c93fda878554
  - c2577f1bb95c473d01603dd57aeb35b09a727b7f407e623605ff418b91ca56f4
  - 1e76a3096212676d90bde8ee91b8c2072915e67fdc3cc8b7fc66b3da737a6f7a
  - 69074f2ffda9df0823d7fd606b702704a18da8adc8727f79830d7074b90debc7
  - 771856c6cfed996aa8a9a99c122a73096afee123a089895f0089c17eb6bac1fe
  - 65591c6fc9d9131ae497016b2bf61e43e75fc90181937bb9112e9f9f6e5abcee
  - d276a49c076a4acf5e5c934fd1cc7ac0ec93dee0ed06820591ac201865a63ebb
  - b8e3c68b1eea13703e793b93a563374eff107a4f7a9e827521354574c9fb1568
compatibility:
  backward_compatible_with:
    - H-L4D-17C-VIDEO-WATCH-MB-v1
    - H-L4D-17D-MEDIA-v1
    - H-L4D-07-IOT-STOP-v1
  breaking_changes: false
  notes: 'Commercial policy remains disabled in production; opt-in quota applies only to isolated tenant 1000.'
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
  backend_source_commit: df7598c3f1830c86df585c380989e6b278b3a2b3
  backend_archive_sha256: a1eeb8268221fcf50ea8e0f6014115cbccf15d2c871c633d2e4e323effbb3150
  runtime_image: sha256:42fa63c0495d71e4e9700564595aba9a4506f3ea6cefde4698f0b822223e301b
  backend_python_file_match: '92/92; missing=0; extra=0'
  backend_suite: '518 passed, 50 warnings'
  frontend_suite: '58 passed; build exit 0; 5/5 referenced assets matched'
  financial_reconciliation: 'tenant 1000; 20-minute window; matched; mismatch=0; balance_difference=0'
  paid_admission: 'free quota 403, mock payment+two webhooks+poll one transaction, lease 201, release 204'
  production_video_smoke: 'terminal 1000005 start/move/stop; no delay or 500; session 483 closed'
  rollback_image: sha256:08b6d35b2a1322e3f709ae4864ee4308210fa3cc04c7c3ac1b7ed61352880d21
supersedes: []
known_risks:
  - 'Manual payment/storno, Hub, archive, active-stream graceful block and projection rebuild have local tests but no separate isolated runtime E2E.'
  - 'Production commercial policy and billing remain disabled; real YooKassa was not used.'
  - 'Full Compose rollout emitted admin/hash interpolation warnings; only MenuBuilder backend was recreated.'
  - 'Managed PostgreSQL connection instability is tracked outside this 17E verdict by user direction.'
consumers:
  - L4D-17F-DOCS
next_prompt_id: L4D-17F-DOCS
```
<!-- HANDOFF:H-L4D-17E-MB-v1:END -->

Контроллер сверяет `artifact_paths`/`artifact_sha256` с Git/raw
`report_commit`, подтверждает или отклоняет candidate отдельной записью.
Исторический `BLOCKED_CONTRACT` report не переписывается.
