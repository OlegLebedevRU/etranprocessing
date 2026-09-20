# Candidate Handoff Package: L4D-12-MB (DETACHED_V1)

<!-- HANDOFF:H-L4D-12-MB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-12-MB-v1
status: CANDIDATE
contract_kinds:
  - API
  - DEPLOYMENT
producer_prompt_id: L4D-12-MB
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-12-MB-report.md
producer_branch: l4desk/l4d-12-mb
producer_commit: 95ba8b7915c1d9e34e76169706b5aa862c2e9954
report_commit: 385155dc394f2bd4479f1545695d61a03b485b9b
contract_version: 1.0.0
schema_revision: '027'
artifact_version: 1.0.0
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-12-MB-candidate.md
artifact_paths:
  - MenuBuilder/backend/app/config.py
  - MenuBuilder/backend/app/main.py
  - MenuBuilder/backend/app/repositories/l4desk_repository.py
  - MenuBuilder/backend/app/routers/finance.py
  - MenuBuilder/backend/app/routers/video_control.py
  - MenuBuilder/backend/app/services/financial_core/__init__.py
  - MenuBuilder/backend/app/services/financial_core/entitlement.py
  - MenuBuilder/backend/app/services/financial_core/notifications.py
  - MenuBuilder/backend/app/services/financial_core/schemas.py
  - MenuBuilder/backend/app/services/financial_core/stop_outbox.py
  - MenuBuilder/backend/app/services/financial_core/worker.py
  - MenuBuilder/backend/app/services/remote_session_policy.py
  - MenuBuilder/backend/tests/test_l4d_12_entitlement_grace_and_notifications.py
  - MenuBuilder/docs/l4desk/handoffs/L4D-12-MB-report.md
artifact_sha256:
  - 71ea749442cb9c791863fc15e86633d49c3d36d87064ef9cc677b6254686f018
  - a57b8afddfbaac01772f3f05dbae4b7892a864bcf24283eddb3ce1cad1e3f091
  - d6b7c1421174016861b75375483463b74eb24336686c9f4da76922d927ec38b0
  - 5b0bf751e3149681624ae213cc2ccd4747f55093c76cf1a3ab5c8770731938b1
  - 08af1e5fcd9f7e24ac4aba869dcc84eccfb16b304ff2026f89495d1785d2e7c1
  - 1e7d2698992f0ecbaf07698279d880731b53bb10372e8096bb89e8fd831e03a1
  - c8928efcd52530c71975bbe9b009e5841b8026d0222ee9d1af114d2003976f57
  - fd414844d5f26a86607057e947f8a87aa54904deee81f6262f5b3266f8e6759b
  - 8438eda2f13c5cc9729a3398e15390a2453f2cdc55e992f1b3b20db12401b468
  - 45119132562a10f1eac9e017c54600c751ff015e67806e1c23b73a6989c127d4
  - d4e485c04a30af8471fb095476719dcd15169de91945ed232a696acb186e13d3
  - 1778f36290937ef239415a55d8581ca812c3e6517b7e6186a90ab6100fa9574e
  - 560ae3a021a1ac0943d02f0acfac8091cd14adae75ad175dac0f4ba9d54fbfbd
  - a04349eff17353edabd0b24dc8612fcd996caeb17e78c87488fd15158aee0873
compatibility:
  backward_compatible_with:
    - H-L4D-11-MB-v1
    - H-L4D-10-MB-v1
    - H-L4D-08B-MB-v1
    - H-L4D-07-IOT-v1
  breaking_changes: false
  notes: Commercial start/continue/stop decisioning, cycle-bound grace state machine, Stop Outbox retry pattern, and idempotent email notifications for L4Desk SaaS.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  l4desk_policy_enforcement_enabled: false
  l4desk_policy_shadow_mode: true
  l4desk_entitlement_worker_enabled: false
  l4desk_email_notifications_enabled: true
contract_payload:
  entitlement_states:
    - free
    - active
    - grace
    - blocked
  reason_codes:
    - entitlement_blocked
    - free_quota_exceeded
    - unpaid_secondary_terminal
    - payment_required
    - no_terminals
  notification_types:
    - cycle_minus_7
    - cycle_minus_3
    - cycle_minus_1
    - grace
    - blocked
  normative_rules:
    free_tier: "Single free terminal with 120 min/local day pooled quota before first payment; secondary terminals blocked; continuation allowed if balance > 0"
    cycle_bound_grace: "Grace is active only when balance < 0 and now < cycle_start + 3 days in tenant timezone; always bound to cycle boundary, never to online/charge events"
    late_payment: "Pays off period and preserves immutable anchor_at; balance >= 0 immediately unblocks"
    session_termination: "Video teardown executed immediately; console stops new commands, waits for running command or bounded timeout, then closes"
    stop_outbox: "Pending stops tracked in stop_requested state and retried automatically until confirmed by IoT provider"
    notification_idempotency: "Unique by (tenant_id, billing_cycle_id, notification_type); retries do not produce duplicate records or emails"
  endpoints:
    tenant_entitlement: GET /api/v1/finance/entitlement
    tenant_notifications: GET /api/v1/finance/notifications
    internal_entitlement: GET /api/internal/v1/finance/entitlement/{tenant_id}
    internal_worker_tick: POST /api/internal/v1/finance/entitlement/worker/tick
    internal_stop_outbox_process: POST /api/internal/v1/finance/stop-outbox/process
    internal_notifications_list: GET /api/internal/v1/finance/notifications
supersedes: []
known_risks:
  - "In production, policy enforcement defaults to shadow mode (l4desk_policy_enforcement_enabled=false) to ensure backward compatibility during rollout."
consumers:
  - L4D-13-MB
next_prompt_id: L4D-13-MB
```
<!-- HANDOFF:H-L4D-12-MB-v1:END -->
