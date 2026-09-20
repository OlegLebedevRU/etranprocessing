# Candidate Handoff Package: L4D-10-MB (DETACHED_V1)

<!-- HANDOFF:H-L4D-10-MB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-10-MB-v1
status: ACCEPTED
contract_kinds:
  - API
  - DEPLOYMENT
producer_prompt_id: L4D-10-MB
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-10-MB-report.md
producer_branch: l4desk/l4d-10-mb
producer_commit: 2d567c2262e8b37022312427e2f77ba21b61f144
report_commit: 7ebe85df726c04f9e612f0088ae0e95c1a79f532
accepted_at_utc: '2026-09-20T17:00:00Z'
contract_version: 1.0.0
schema_revision: '027'
artifact_version: 1.0.0
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-10-MB-candidate.md
artifact_paths:
  - MenuBuilder/backend/app/repositories/l4desk_repository.py
  - MenuBuilder/backend/app/routers/finance.py
  - MenuBuilder/backend/app/services/financial_core/__init__.py
  - MenuBuilder/backend/app/services/financial_core/cycles.py
  - MenuBuilder/backend/app/services/financial_core/exceptions.py
  - MenuBuilder/backend/app/services/financial_core/metering.py
  - MenuBuilder/backend/app/services/financial_core/schemas.py
  - MenuBuilder/backend/app/services/financial_core/tariffs.py
  - MenuBuilder/backend/app/services/financial_core/terminals.py
  - MenuBuilder/backend/app/services/financial_core/timezones.py
  - MenuBuilder/backend/tests/test_tariffs_and_metering.py
  - MenuBuilder/backend/tests/test_terminal_onboarding.py
  - MenuBuilder/docs/l4desk/handoffs/L4D-10-MB-report.md
artifact_sha256:
  - 78b98c9bd4d92c64e7e9161084cb2f226a796ee5bd06242c8feea6377d738085
  - 29e5e214ab32e270f3320d4cc22b32a537d3377a8b2e143dd59f543cf61eda4f
  - 73cbaa7f3784d4983331353ca11c394f96de2816736280a339d231f9f821c746
  - bc72f8e02692feba1d7294185b7511880d1fba324ba599694f0e35424189dcb8
  - 19fa02c95d11aa6c1b80eb99150652c73322835d14f6086c12713c862b6d9067
  - f4b05a9f574ea94b121400e1f493e19998d880b4b6306fc98b993af192ca4835
  - cf92379b04b71cd37d49d268a63796ad2b0504bb81f139caa9eb91c3f643123d
  - a09e6c426441e0d9dffa9ccf8389b35c36aaf5f5d1ed480f4c1b8ca87c2f8855
  - cdce9eec1abfcb91aa23b587dba7616a3daf65ce440c7dce9a412218dce49eaf
  - d0d8a075cecbc4f4cfe372e0f19340d593fa44f52abec095c48e73b7077e6935
  - 19164d31773a7bb624276d5c070f6b8bbc556b3e94e0f71c71428ddf84e639bf
  - 0ab63806bcc023406611f669f9424ff2bbad6993a7c0f62d025cc4b63db9fd8c
  - 8bca1e950bfcc9c873c07d3845e38118800586962e41f4777efcf5439bf627b7
compatibility:
  backward_compatible_with:
    - H-L4D-09-MB-v1
    - H-L4D-03-MB-v1
    - H-L4D-02-IOT-v1
  breaking_changes: false
  notes: Versioned immutable tariffs, individual anchor-based billing cycles with last existing day add_months rules, earliest terminal free privilege with forward-only deletion transfer, 10000 kopecks monthly charge on first online per cycle, and daily session metering split by local tenant midnights with rounding ceil(billable_sec/3600) and general tariff rounding invariants.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  l4desk_financial_core_enabled: true
  l4desk_billing_enabled: false
  l4desk_metering_enabled: true
contract_payload:
  tariffs:
    baseline_version: v1.0
    terminal_month_kopecks: 10000
    hourly_rate_kopecks: 100
    free_daily_seconds: 7200
  billing_cycles:
    anchor_rule: "First successful payment sets anchor; subsequent payments or device_online never shift anchor."
    month_addition: "add_months(anchor, n) with last existing calendar day rule and DST preservation."
    grace_window: "starts_at < grace_deadline < ends_at; grace_deadline = starts_at + 3 calendar days."
  terminal_privileges:
    free_terminal_rule: "Earliest existing terminal (min ordinal where deleted_at IS NULL) is free."
    deletion_transfer: "On deletion, privilege transfers forward only to next existing ordinal; closed periods not retroactively altered."
    monthly_charge: "Non-free terminal charged 10000 kopecks on first authenticated device_online in cycle with unique constraint (terminal_id, billing_cycle_id)."
  metering_engine:
    day_splitting: "Intervals partitioned at local tenant midnights into calendar days."
    concurrency_exclusion: "Console and video sessions never overlap."
    billable_seconds:
      free_terminal: "max(0, console + video - 7200)"
      paid_terminal: "console + video"
    paid_hours: "ceil(billable_seconds / 3600)"
    general_tariff_formula:
      calculated: "paid_hours * hourly_rate_kopecks"
      posted: "floor(calculated / 100) * 100"
      discarded: "calculated - posted"
      invariants: "calculated == posted + discarded; 0 <= discarded < 100; posted % 100 == 0; discarded not carried over"
    late_events_policy: "Posted daily usage rows are immutable; late events create adjustment ledger transaction with corrects_transaction_id."
  endpoints:
    tenant_profile: GET /api/v1/finance/profile
    tenant_cycles: GET /api/v1/finance/cycles
    tenant_current_tariff: GET /api/v1/finance/tariffs/current
    tenant_daily_usage: GET /api/v1/finance/usage
    tenant_monthly_charges: GET /api/v1/finance/monthly-charges
    internal_tariffs_list: GET /api/internal/v1/finance/tariffs
    internal_tariff_create: POST /api/internal/v1/finance/tariffs
    internal_metering_online: POST /api/internal/v1/finance/metering/online
    internal_metering_record_usage: POST /api/internal/v1/finance/metering/record-usage
    internal_metering_close_day: POST /api/internal/v1/finance/metering/close-day
supersedes: []
known_risks:
  - "Shadow mode active: automatic recurring user debits operate with dark posting until user billing cart UX (L4D-11-MB) is enabled."
  - "Historical late events spanning across closed days create delta adjustments that affect ledger balance without mutating past usage records."
consumers:
  - L4D-11-MB
next_prompt_id: L4D-11-MB
```
<!-- HANDOFF:H-L4D-10-MB-v1:END -->
