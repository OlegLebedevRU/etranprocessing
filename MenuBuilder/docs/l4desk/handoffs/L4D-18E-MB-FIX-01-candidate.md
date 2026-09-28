# 18E MenuBuilder candidate — DETACHED_V1

<!-- HANDOFF:H-L4D-18E-MB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-18E-MB-v1
status: CANDIDATE
contract_kinds: [REPORT, DEPLOYMENT]
producer_prompt_id: L4D-18E-MB-FIX-01
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-18E-MB-FIX-01-report.md
producer_branch: l4desk/l4d-18e-mb
producer_commit: 4f72dc6dbe03acc7975463bd4fa92832c4684638
report_commit: c590c45476913935276a7639673380e0a53e29fc
accepted_at_utc: null
contract_version: 1.0.0
schema_revision: '027'
artifact_version: 18E-restricted-1
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-18E-MB-FIX-01-candidate.md
artifact_commit: c590c45476913935276a7639673380e0a53e29fc
artifact_paths:
  - MenuBuilder/docs/l4desk/handoffs/L4D-18E-MB-FIX-01-report.md
  - MenuBuilder/docs/l4desk/handoffs/L4D-18E-MB-FIX-01-inputs.json
  - MenuBuilder/docs/l4desk/handoffs/L4D-18E-MB-FIX-01-finance-probe.json
  - MenuBuilder/deploy/verify-finance-rollback.py
  - MenuBuilder/deploy/README.md
artifact_sha256:
  - a139ee2cb320c80c701f85cfee6c026348341e362617ab2f251bd6086a2b9653
  - d3890e5f27d71e5e61568501fc853c1db88aec48ec63f00a808aa76ae1ce147a
  - d9a51c5026b3e64667bdcaaad5684979f2ec24c6471ebcf8c17c95e6a60c0dc1
  - e16e1eb644e9729420052f62f2eaeaefa422a7572fca5630b8b6605888c75fc0
  - dcfe453040f2cf920645106a92af715528eeee159e7a88729ac7256b8928209c
compatibility:
  backward_compatible_with:
    - H-L4D-18E-MB-EVIDENCE-CONTRACT-01-v1
    - H-L4D-18E-MB-DEVICE-PIN-CONTRACT-01-v1
    - H-L4D-18E-MB-FIXTURE-CONTRACT-01-v1
  breaking_changes: false
  notes: 'MenuBuilder-only restricted rollout; Agent 1.8.2-beta-1, shared 0.1.1, PB 027, IoT 0008_org_reservations and 18D media unchanged.'
deployment_status: DEPLOYED_DISABLED
deployed_environment: production
feature_flags:
  l4desk_enabled: false
  l4desk_registration_enabled: false
  l4desk_billing_enabled: false
  l4desk_policy_enforcement_enabled: false
  l4desk_policy_shadow_mode: true
  l4desk_terminal_onboarding_enabled: true
  l4desk_financial_core_enabled: true
  l4desk_entitlement_worker_enabled: false
  l4desk_metering_close_worker_enabled: false
  effective_yookassa: false
  isolated_test_backend_running: false
contract_payload:
  registration_id: R-L4D-18E-MB-FIX-01-v3
  backend_manifest: sha256:b0c4b0c11b93697ecda63a7b505ecc349d6b6624c43a3469dc54119c86105589
  frontend_manifest: sha256:3fbed8c6104f8399e7486300c0a16e17fd96aeabe90ba4b7bcc11ddc404845d5
  build_delivery: 'Published Git/raw archive; build host; registry push; production pull by digest.'
  identities: 'device_id primary; tenant-filtered navigation; sys exactly windows/linux/esp32.'
  pin: 'Own-terminal authenticated management access; issue/replay/current; pending visible; consumed/expired plaintext absent; no commercial prerequisite.'
  tests: 'Backend 551; frontend 63; final-image network-isolated phase contracts 54; Ruff/Pyright/build passed.'
  browser: 'Moving decoded video and stop; console response; profile 200; Hub superuser 200/owner 403; responsive 600/900/1440.'
  financial_probe: 'Existing tenant 10000; 10000 kopecks once; 600-second simulated grace; mock stop; matched reconciliation; outer rollback verified.'
  current_reconciliation: 'Tenant 1000 run 12 and tenant 10000 run 13 matched, mismatch=0, difference=0 in current UTC window.'
  commercial_activation: 'Not authorized: five unresolved historical source hash gaps in tenant 1000 sessions 476-480.'
  archive: 'Status/consumer checked; worker and purge disabled; no backup-schedule claim.'
supersedes: []
known_risks:
  - 'Full September tenant 1000 reconciliation has five unresolved missing source hashes; not an accepted exception. No invented hashes or weakened invariant.'
  - 'No general commercial activation, new registration E2E, live YooKassa charge or fresh 120-minute soak is claimed.'
  - 'Current grace/stop probe uses mocked provider and rolled-back PostgreSQL rows; actual prior provider stop is historical 17F evidence.'
  - 'Approved Agent download URL unavailable; old 1.7.7 link removed, accepted 1.8.2-beta-1 unchanged.'
  - '51 backend test warnings and existing large frontend vendor chunk warning remain disclosed.'
  - 'Accidental test terminal 1000008 soft-deleted with no sessions or charges; provider/audit record retained.'
consumers: [L4D-18F-DOCS]
next_prompt_id: L4D-18F-DOCS
```
<!-- HANDOFF:H-L4D-18E-MB-v1:END -->

Candidate only. Independent controller acceptance is required. Do not open 18F automatically.
