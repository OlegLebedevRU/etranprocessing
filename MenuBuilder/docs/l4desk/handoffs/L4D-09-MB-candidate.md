# Candidate Handoff Package: L4D-09-MB (DETACHED_V1)

<!-- HANDOFF:H-L4D-09-MB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-09-MB-v1
status: ACCEPTED
contract_kinds:
  - SCHEMA
  - API
  - DEPLOYMENT
producer_prompt_id: L4D-09-MB
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-09-MB-report.md
producer_branch: l4desk/l4d-09-mb
producer_commit: 3d0dddba68df211a1d6c89844e0311b43bccd44d
report_commit: d487bc23011d90cad1ab9945abf480fca73e397a
accepted_at_utc: '2026-09-20T13:00:00Z'
contract_version: 1.0.0
schema_revision: '027'
artifact_version: 1.0.0
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-09-MB-candidate.md
artifact_paths:
  - MenuBuilder/backend/app/config.py
  - MenuBuilder/backend/app/main.py
  - MenuBuilder/backend/app/repositories/l4desk_repository.py
  - MenuBuilder/backend/app/routers/finance.py
  - MenuBuilder/backend/app/services/financial_core/__init__.py
  - MenuBuilder/backend/app/services/financial_core/accounts.py
  - MenuBuilder/backend/app/services/financial_core/exceptions.py
  - MenuBuilder/backend/app/services/financial_core/posting.py
  - MenuBuilder/backend/app/services/financial_core/projection.py
  - MenuBuilder/backend/app/services/financial_core/reconciliation.py
  - MenuBuilder/backend/app/services/financial_core/reversal.py
  - MenuBuilder/backend/app/services/financial_core/schemas.py
  - MenuBuilder/backend/tests/test_financial_core.py
  - MenuBuilder/docs/l4desk/handoffs/L4D-09-MB-report.md
artifact_sha256:
  - a5157b6de75e801a9e8c169fb40f4b0e3af2172d5ccf9282f99bb6a0ec49ba07
  - ea8e5f4bc9ff71ffd45d8aee6533be20fc2081304d73f6f5dfd66fcb2e0ac74a
  - 5cd4aa664214126f3cb8dba146d1da14400081cf551992a4a944e3d24cfabcb7
  - 2b9cfab30ae756600b413fa2b2dd2c111f22c93a712643a1d22569d33ea93974
  - 8fb7e50c8d1b8c0da185f3daf785c821d16270ccb47a71442fbc4d01055d582b
  - 731512c8cc5e10c65325b6f746c259f5a01cc0c15e39eaba39e612c8b060c2a8
  - d075e093ca2e8c33503af5a2b16f84946dbcf7b3412f6f6a9d86e31d8fda173d
  - e0033fcf38e1d0a704a3aabb9c8decd44bfb0533c3e8eac67e27879e3557a30b
  - 765a69fad521a893d7cd5fe0f7325f190d53645627aa609ae8e0297a71654af0
  - 64169deadcc64c71187ed0b3f9131a27c36ee8e8490faddd75bbcde91f83064f
  - a0c39a49c132b88f159e6dcd7c5df1e733941ffe073b1b2701a93dc8bee8c7a1
  - 6f839a54bacc139225a20164a3c22623e4e71e845fd112891a4edfb09c8984f0
  - 5b01420ff87ed7d42af838344e79c63b2087d41c39cc74ac404984dac4279aea
  - 729436d7a0ba76b601acc1261287b6264391fc48a5a44f888fc37d71ba8791b8
compatibility:
  backward_compatible_with:
    - H-L4D-08B-MB-v1
    - H-L4D-04C-MB-v1
  breaking_changes: false
  notes: Minimal double-entry fin_* subledger and fast balance projection for MenuBuilder with strict append-only constraints, whole-ruble kopecks, optimistic version locking, reversal support, and periodic reconciliation audits.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  l4desk_financial_core_enabled: true
  l4desk_billing_enabled: false
contract_payload:
  account_kinds:
    - tenant_settlement
    - payment_clearing
    - usage_revenue
  currency: RUB
  subledger_invariants:
    - "double_entry: sum(debit_kopecks) == sum(credit_kopecks) > 0"
    - "integer_only: integer kopecks strictly required, float prohibited"
    - "whole_rubles_rule: debit_kopecks % 100 == 0, credit_kopecks % 100 == 0"
    - "single_sided_entries: exactly one positive side per entry (debit xor credit)"
    - "immutability: update and delete of posted transactions and entries strictly prohibited"
    - "tenant_isolation: all transaction entries and tenant accounts must match transaction tenant_id"
    - "reversal_rule: corrections reference original via corrects_transaction_id with inverse entries"
  balance_projection_rule: "balance_kopecks = credits(tenant_settlement) - debits(tenant_settlement)"
  projection_concurrency: "optimistic locking with version increment and row locking in same DB transaction"
  reconciliation_checks:
    - ledger_balance
    - reversal_invariants
    - tenant_isolation
    - projection_consistency
    - duplicate_posting
    - corruption_detection
  endpoints:
    tenant_balance: GET /api/v1/finance/balance
    tenant_transactions: GET /api/v1/finance/transactions
    internal_post: POST /api/internal/v1/finance/post
    internal_reversal: POST /api/internal/v1/finance/reversal
    internal_rebuild: POST /api/internal/v1/finance/rebuild-projection/{tenant_id}
    internal_reconciliation: POST /api/internal/v1/finance/reconciliation
    internal_reconciliation_runs: GET /api/internal/v1/finance/reconciliation/runs
supersedes: []
known_risks:
  - "Dark deployment active: automated recurring user usage deductions remain disabled until L4D-10-MB meter engine is introduced."
  - "Optimistic concurrency conflict (HTTP 409) requires retry if multiple concurrent operations target the same tenant simultaneously."
consumers:
  - L4D-10-MB
next_prompt_id: L4D-10-MB
```
<!-- HANDOFF:H-L4D-09-MB-v1:END -->
