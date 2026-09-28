# Candidate Handoff Blocks: L4D-06C-MB-FIX-01 & L4D-06C-MB

```yaml
<!-- HANDOFF:H-L4D-06C-MB-FIX-01-v1:BEGIN -->
handoff_id: H-L4D-06C-MB-FIX-01-v1
status: ACCEPTED
contract_kinds:
  - API
  - DEPLOYMENT
producer_prompt_id: L4D-06C-MB-FIX-01
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-06C-MB-FIX-01-report.md
producer_branch: l4desk/l4d-06c-mb
producer_commit: c91b24cc155dc0013500162c6e517897d8074a42
accepted_at_utc: 2026-09-19T14:10:00Z
contract_version: 1.0.0
schema_revision: 2026-09-19-v1
artifact_version: 1.0.0
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-06C-MB-FIX-01-candidate.md
artifact_paths:
  - MenuBuilder/backend/app/routers/settings.py
  - MenuBuilder/backend/app/services/terminal_onboarding_service.py
  - MenuBuilder/frontend/src/routes/settings/TerminalsSettingsPage.tsx
  - MenuBuilder/frontend/src/api/settings.ts
  - MenuBuilder/backend/tests/test_terminal_onboarding.py
  - MenuBuilder/docs/l4desk/handoffs/L4D-06C-MB-FIX-01-report.md
artifact_sha256:
  - f9f34368b347dc67e9481d7c319d913dfb3ef5fbb99bd41123b5fe4f34245ad4
  - 54ba3aff7c3d35a99314d7759cf2714b685deeac8287b564990a33b817606089
  - 71d4d891c6b573858c1c4a1e1005a8e11847f0d529428178bd1cb509f8706827
  - a49a54e4a2b4f98cafbc33f62b15ebc140fc60fddf16c9f4c1a93501af16c093
  - 2d710f66d97adf541ed472cf4ba9914637ffcbb89b7726e818fabcc47262954e
  - 80c9b55ae6e814a0f3c6709846029f63e32e660dd8f9b544d6cd422bb24429ca
compatibility:
  backward_compatible_with:
    - H-L4D-06A-PB-v1
    - H-L4D-06B-IOT-v1
  breaking_changes: false
  notes: "Unified terminal onboarding orchestrating H-L4D-06A-PB-v1 (ProcessingBackend PIN issuance) and H-L4D-06B-IOT-v1 (Leo4 IoT device provisioning). Timeout and row lock wait eliminated by transaction boundary isolation prior to outbound provider HTTP calls."
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  l4desk_terminal_onboarding_enabled: true
contract_payload:
  corrects_candidate: H-L4D-06C-MB-v1
  evidence_source: L4D-06C-MB-FIX-01
  endpoints:
    onboard_status: /api/settings/terminals/onboard/status
    onboard_terminal: /api/settings/terminals
    onboard_terminal_alias: /api/settings/terminals/onboard
    retry_terminal_saga: /api/settings/terminals/{terminal_id}/retry
    terminal_readiness: /api/settings/terminals/{terminal_id}/readiness
    list_terminals: /api/settings/terminals
    delete_terminal: /api/settings/terminals/{terminal_id}
  saga_steps:
    - step_a: "IoT Device Provisioning (POST /api/internal/v1/devices/provision)"
    - step_b: "Certificate PIN Issuance (POST /api/certificates/pins/issue)"
  readiness_states:
    record: ["ready", "pending", "failed"]
    certificate: ["pending", "issued", "consumed", "expired", "failed"]
    iot: ["pending", "ready", "failed"]
    online: ["online", "offline"]
  quota_rules:
    free_tier_first_terminal: "ordinal == min_active_ordinal marked is_free=true"
    deletion_transfer: "free tier automatically reassigns to next earliest active terminal upon soft deletion"
  audit_security:
    plain_pin_persistence: "never stored in database or audit logs"
    audit_masking: "***773 pattern enforced across all audit events"
    consumer_pin_visibility: "plain PIN delivered on 201 Created and active query only, hidden once consumed or expired"
  deployed_host: 87.242.100.34
  live_smoke_evidence:
    status_code: 201
    execution_time_seconds: 0.375
    pin_masked: "***468"
    pin_state: "issued"
    provisioning_state: "ready"
    last_error: null
  tests_passed: 9
  full_backend_suite: 344
  verification_status: VERIFIED_READY
supersedes: []
known_risks:
  - "IoT platform (app1) requires WEB_CONCURRENCY=1 for in-memory session tracking consistency"
  - "External Agent download URL hosted on cloud.ru generic repository"
consumers:
  - L4D-07-IOT
  - ALL_FOLLOWING
next_prompt_id: L4D-07-IOT
<!-- HANDOFF:H-L4D-06C-MB-FIX-01-v1:END -->

<!-- HANDOFF:H-L4D-06C-MB-v1:BEGIN -->
handoff_id: H-L4D-06C-MB-v1
status: ACCEPTED
contract_kinds:
  - API
  - DEPLOYMENT
producer_prompt_id: L4D-06C-MB
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-06C-MB-report.md
producer_branch: l4desk/l4d-06c-mb
producer_commit: c91b24cc155dc0013500162c6e517897d8074a42
accepted_at_utc: 2026-09-19T14:10:00Z
contract_version: 1.0.0
schema_revision: 2026-09-19-v1
artifact_version: 1.0.0
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-06C-MB-FIX-01-candidate.md
artifact_paths:
  - MenuBuilder/backend/app/routers/settings.py
  - MenuBuilder/backend/app/services/terminal_onboarding_service.py
  - MenuBuilder/frontend/src/routes/settings/TerminalsSettingsPage.tsx
  - MenuBuilder/frontend/src/api/settings.ts
  - MenuBuilder/backend/tests/test_terminal_onboarding.py
  - MenuBuilder/docs/l4desk/handoffs/L4D-06C-MB-FIX-01-report.md
artifact_sha256:
  - f9f34368b347dc67e9481d7c319d913dfb3ef5fbb99bd41123b5fe4f34245ad4
  - 54ba3aff7c3d35a99314d7759cf2714b685deeac8287b564990a33b817606089
  - 71d4d891c6b573858c1c4a1e1005a8e11847f0d529428178bd1cb509f8706827
  - a49a54e4a2b4f98cafbc33f62b15ebc140fc60fddf16c9f4c1a93501af16c093
  - 2d710f66d97adf541ed472cf4ba9914637ffcbb89b7726e818fabcc47262954e
  - 80c9b55ae6e814a0f3c6709846029f63e32e660dd8f9b544d6cd422bb24429ca
compatibility:
  backward_compatible_with:
    - H-L4D-06A-PB-v1
    - H-L4D-06B-IOT-v1
  breaking_changes: false
  notes: "Unified terminal onboarding consumer in MenuBuilder orchestrating H-L4D-06A-PB-v1 and H-L4D-06B-IOT-v1. Verified and confirmed under corrective step L4D-06C-MB-FIX-01."
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  l4desk_terminal_onboarding_enabled: true
contract_payload:
  evidence_source: L4D-06C-MB-FIX-01
  verified_by_handoff: H-L4D-06C-MB-FIX-01-v1
  endpoints:
    onboard_status: /api/settings/terminals/onboard/status
    onboard_terminal: /api/settings/terminals
    onboard_terminal_alias: /api/settings/terminals/onboard
    retry_terminal_saga: /api/settings/terminals/{terminal_id}/retry
    terminal_readiness: /api/settings/terminals/{terminal_id}/readiness
    list_terminals: /api/settings/terminals
    delete_terminal: /api/settings/terminals/{terminal_id}
  saga_steps:
    - step_a: "IoT Device Provisioning (POST /api/internal/v1/devices/provision)"
    - step_b: "Certificate PIN Issuance (POST /api/certificates/pins/issue)"
  readiness_states:
    record: ["ready", "pending", "failed"]
    certificate: ["pending", "issued", "consumed", "expired", "failed"]
    iot: ["pending", "ready", "failed"]
    online: ["online", "offline"]
  quota_rules:
    free_tier_first_terminal: "ordinal == min_active_ordinal marked is_free=true"
    deletion_transfer: "free tier automatically reassigns to next earliest active terminal upon soft deletion"
  audit_security:
    plain_pin_persistence: "never stored in database or audit logs"
    audit_masking: "***773 pattern enforced across all audit events"
    consumer_pin_visibility: "plain PIN delivered on 201 Created and active query only, hidden once consumed or expired"
  deployed_host: 87.242.100.34
  live_smoke_evidence:
    status_code: 201
    execution_time_seconds: 0.375
    pin_masked: "***468"
    pin_state: "issued"
    provisioning_state: "ready"
    last_error: null
  tests_passed: 9
  full_backend_suite: 344
  verification_status: VERIFIED_READY
supersedes: []
known_risks:
  - "IoT platform (app1) requires WEB_CONCURRENCY=1 for in-memory session tracking consistency"
  - "External Agent download URL hosted on cloud.ru generic repository"
consumers:
  - L4D-07-IOT
  - ALL_FOLLOWING
next_prompt_id: L4D-07-IOT
<!-- HANDOFF:H-L4D-06C-MB-v1:END -->
```
