<!-- HANDOFF:H-L4D-14-MB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-14-MB-v1
status: CANDIDATE
contract_kinds:
  - API
  - DEPLOYMENT
producer_prompt_id: L4D-14-MB
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-14-MB-report.md
producer_branch: l4desk/l4d-14-mb
producer_commit: 877dc00ac6e5131c65375c5494659d36ff31822e
report_commit: 877dc00ac6e5131c65375c5494659d36ff31822e
contract_version: 1.0.0
schema_revision: '027'
artifact_version: 1.0.0
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-14-MB-candidate.md
artifact_paths:
  - MenuBuilder/backend/app/main.py
  - MenuBuilder/backend/app/routers/hub.py
  - MenuBuilder/backend/app/services/financial_core/hub_schemas.py
  - MenuBuilder/backend/app/services/financial_core/hub_service.py
  - MenuBuilder/backend/app/services/financial_core/reconciliation.py
  - MenuBuilder/backend/tests/test_hub_and_reconciliation.py
  - MenuBuilder/frontend/src/App.tsx
  - MenuBuilder/frontend/src/api/hub.ts
  - MenuBuilder/frontend/src/pages/AdminHubPage.tsx
  - MenuBuilder/frontend/src/routes/admin-layout.tsx
  - MenuBuilder/frontend/src/tests/l4desk-hub-and-reconciliation.test.ts
  - MenuBuilder/docs/l4desk/handoffs/L4D-14-MB-report.md
artifact_sha256:
  - a372b7571162fb7b366b7e52f15304ef7b9f8f4a0b0445d83f3f96360717125b
  - 1b83e283247f80841bc663e6f982e602d5171d5f9648ee46d53bce2525cbdc49
  - 0e4124362c89f0cf6e1d515c0b6f892b02735721e2b9fa883920650911485e2c
  - 3d3526ea19b098e0092fff6b1f874fdd666c3c7df64ba6d991c0aa8e8a5aa204
  - e2ffd67cd9d985e6be7c218c500b1e39c3cbbe1498769231f9733b866e79f364
  - 49509bf568d7c390c122892584b3cdec2cb6a426140d8e74dd0fbf85f9c20100
  - 14903c710fd7d6d52f47b16ab3f93cbb2a2f1218a70a22bd6d05f9e8ec9b16a6
  - 140d3b74c37b34611f8e68d490e5c47bf263128b1e20f0fdfae74d1b2594dd20
  - a751bc1619d5631ba32be823e2d17f1fe7470597ef11865f66edcf284b06760b
  - f73171adb60f790b556951bb955d40e9a865c51a1d4119753d1fefee8fbc4fab
  - 50deda9a76ee33d48eed55c33b4c5e8f5178d4c5c41fd781f7acd63a7cc08a3c
  - 43a419248253c609a11c0e5eaeedcef506924720f3969148054e5c1896608098
compatibility:
  backward_compatible_with:
    - H-L4D-13-MB-v1
    - H-L4D-12-MB-v1
    - H-L4D-11-MB-v1
    - H-L4D-10-MB-v1
    - H-L4D-09-MB-v1
  breaking_changes: false
  notes: "Superuser Hub and comprehensive financial subledger reconciliation contract across source facts -> usage -> ledger -> balance. End-to-end correlation drilldown with strict missing fact mismatch policy and safe code 11 manual payment / storno confirmation."
deployment_status: STAGED
deployed_environment: staging
feature_flags:
  l4desk_hub_enabled: true
  l4desk_financial_reconciliation_enabled: true
contract_payload:
  hub_endpoints:
    registrations: GET /api/v1/admin/hub/registrations
    terminals: GET /api/v1/admin/hub/terminals
    sessions: GET /api/v1/admin/hub/sessions
    usage: GET /api/v1/admin/hub/usage
    finance_overview: GET /api/v1/admin/hub/finance/overview
    payments: GET /api/v1/admin/hub/finance/payments
    notifications: GET /api/v1/admin/hub/notifications
    audit_events: GET /api/v1/admin/hub/audit-events
    correlation_drilldown: GET /api/v1/admin/hub/correlation-drilldown
    manual_payment_create: POST /api/v1/admin/hub/finance/manual-payment
    manual_payment_storno: POST /api/v1/admin/hub/finance/manual-payment/{id}/storno
    reconciliation_run: POST /api/v1/admin/hub/reconciliation/run
  mismatch_codes:
    - REGISTRATION_NOT_FOUND
    - REGISTRATION_EXPIRED_UNCONSUMED
    - TERMINAL_NOT_FOUND
    - TERMINAL_DELETED
    - PIN_PROVISIONING_MISSING
    - PIN_PROVISIONING_FAILED
    - SESSION_NOT_FOUND
    - SESSION_FAILED
    - SESSION_HASH_MISSING
    - USAGE_NOT_FOUND
    - USAGE_UNRECONCILED
    - LEDGER_POSTING_MISSING
    - LEDGER_IMBALANCED
    - CALCULATED_SUM_MISMATCH
    - DISCARDED_OUT_OF_BOUNDS
    - POSTED_NOT_MULTIPLE_OF_100
    - MISSING_USAGE_SOURCE_HASH
    - DUPLICATE_MONTHLY_CHARGE
    - DUPLICATE_PAYMENT_POSTING
  reconciliation_invariants:
    debit_equals_credit: true
    projection_rebuild: true
    calculated_equals_posted_plus_discarded: true
    posted_mod_100_is_zero: true
    source_hash_coverage: true
    unique_monthly_charge_and_payment_posting: true
  confirmation_code: "11"
supersedes: []
known_risks: []
consumers:
  - L4D-15A-DOCS
next_prompt_id: L4D-15A-DOCS
```
<!-- HANDOFF:H-L4D-14-MB-v1:END -->
