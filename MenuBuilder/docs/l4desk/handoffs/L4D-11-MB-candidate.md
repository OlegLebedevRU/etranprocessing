# Candidate Handoff Package: L4D-11-MB (DETACHED_V1)

<!-- HANDOFF:H-L4D-11-MB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-11-MB-v1
status: CANDIDATE
contract_kinds:
  - API
  - DEPLOYMENT
producer_prompt_id: L4D-11-MB
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-11-MB-report.md
producer_branch: l4desk/l4d-11-mb
producer_commit: b6f793ad9880cf20489fe37366edc66af8229464
report_commit: 221f36dbbe36c581c69a3a65deeb1378b90ab123
contract_version: 1.0.0
schema_revision: '027'
artifact_version: 1.0.0
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-11-MB-candidate.md
artifact_paths:
  - MenuBuilder/backend/app/config.py
  - MenuBuilder/backend/app/repositories/l4desk_repository.py
  - MenuBuilder/backend/app/routers/finance.py
  - MenuBuilder/backend/app/services/financial_core/__init__.py
  - MenuBuilder/backend/app/services/financial_core/schemas.py
  - MenuBuilder/backend/app/services/financial_core/payments.py
  - MenuBuilder/backend/app/services/financial_core/manual_payments.py
  - MenuBuilder/backend/app/services/financial_core/yookassa.py
  - MenuBuilder/backend/tests/test_yookassa_and_manual_payments.py
  - MenuBuilder/docs/l4desk/handoffs/L4D-11-MB-report.md
artifact_sha256:
  - 8944bbd842e287f55863172cbf92afcb65a9f26f7579c8b25edecaecc9b67498
  - b8670d8abb462ae2a0a61bf6e782e82482403bf683ddc31345cc3c369e7d5714
  - afeb55ca680d92a754923a575bbc3ee7eeb509322da9ecfebb8b957e7efadb45
  - 6693513b08f168e6cb3a21f4bce6ec06773a59e3970654c2994ef78446907ae7
  - 86c06f08d3f556925d3d8a9ae44d1a514a0a27f948e4af28b8a43605aaa53576
  - 6abbe80dbc69d60e0c79abc08391a3bb0f3df13cc8a36a4c86af36394f65c401
  - dd68c81271233654ca31a64cf57a7a559718b264225191157475f559042a1950
  - aac015319eb62683c884c2a52933b26f41b49792ebc72c1272e78823dba10713
  - 655b60e3206ad5ac039f3fe794afb83254327876bf5682eb102b47e20c97098e
  - f61b7a366875c07c908161f04e65f6431197cb44f9bd1e5194fe6111be50decf
compatibility:
  backward_compatible_with:
    - H-L4D-10-MB-v1
    - H-L4D-09-MB-v1
  breaking_changes: false
  notes: YooKassa top-up flow for individuals, immutable B2B manual payment registration by superuser, double-entry reversal/storno mechanics, and atomic cycle anchor fixation on first successful payment.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  l4desk_financial_core_enabled: true
  yookassa_enabled: false
  yookassa_ip_filter_enabled: false
  yookassa_receipt_enabled: true
contract_payload:
  payment_states:
    - pending
    - waiting_for_capture
    - succeeded
    - canceled
  idempotency:
    yookassa: "Idempotence-Key header / operation_id on payment creation and replay-safe lookup"
    webhook_and_poll: "Server authoritative GET /payments/{id} verification before ledger posting; duplicate webhook or poll is a safe no-op"
  ledger_posting:
    yookassa_succeeded: "Dr payment_clearing, Cr tenant_settlement"
    manual_payment: "Dr payment_clearing, Cr tenant_settlement"
    manual_storno: "Dr tenant_settlement, Cr payment_clearing (kind=reversal, corrects_transaction_id)"
  cycle_anchor:
    rule: "First successful payment (YooKassa or manual) fixes immutable cycle anchor_at; subsequent payments never shift existing anchor"
  manual_payments:
    role_required: "Superuser only (role 1 / is_superuser)"
    immutability: "Posted manual payment records are append-only; corrections executed strictly via storno + new document"
  fiscal_configuration:
    config_keys:
      - YOOKASSA_ENABLED
      - YOOKASSA_SHOP_ID
      - YOOKASSA_SECRET_KEY
      - YOOKASSA_API_URL
      - YOOKASSA_WEBHOOK_SECRET
      - YOOKASSA_IP_FILTER_ENABLED
      - YOOKASSA_TRUSTED_IPS_RAW
      - YOOKASSA_RETURN_URL_BASE
      - YOOKASSA_RECEIPT_ENABLED
      - YOOKASSA_TAX_SYSTEM_CODE
      - YOOKASSA_VAT_CODE
      - YOOKASSA_PAYMENT_SUBJECT
      - YOOKASSA_PAYMENT_MODE
      - YOOKASSA_ITEM_DESCRIPTION
      - YOOKASSA_REQUEST_TIMEOUT_SEC
  endpoints:
    payment_create: POST /api/v1/finance/payments
    payment_get: GET /api/v1/finance/payments/{payment_id}
    payment_list: GET /api/v1/finance/payments
    payment_poll: POST /api/v1/finance/payments/{payment_id}/poll
    yookassa_webhook: POST /api/v1/finance/yookassa/webhook
    manual_payment_create: POST /api/internal/v1/finance/manual-payments
    manual_payment_storno: POST /api/internal/v1/finance/manual-payments/{manual_payment_id}/storno
    manual_payment_list: GET /api/internal/v1/finance/manual-payments
    manual_payment_get: GET /api/internal/v1/finance/manual-payments/{manual_payment_id}
supersedes: []
known_risks:
  - "Production YooKassa credentials default to disabled/sandbox until production merchant keys are injected into .env."
consumers:
  - L4D-12-MB
next_prompt_id: L4D-12-MB
```
<!-- HANDOFF:H-L4D-11-MB-v1:END -->
