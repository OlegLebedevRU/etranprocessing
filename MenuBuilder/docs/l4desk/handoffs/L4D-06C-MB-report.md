# Handoff Report: L4D-06C-MB — Terminal Onboarding Consumer

**Prompt ID:** `L4D-06C-MB`  
**Prompt Type:** `implementation-consumer`  
**Scope Project:** `MenuBuilder`  
**Scope Root:** `D:\repo\platerra\Public\etranprocessing\MenuBuilder`  
**Required Handoff IDs:** `[H-L4D-06A-PB-v1, H-L4D-06B-IOT-v1]`  
**Output Handoff ID:** `H-L4D-06C-MB-v1`  
**Next Prompt ID:** `L4D-07-IOT`  
**Branch:** `l4desk/l4d-06c-mb`  
**Report Path:** `MenuBuilder/docs/l4desk/handoffs/L4D-06C-MB-report.md`  

---

## 1. Executive Summary & Goals

In accordance with prompt `L4D-06C-MB` and L4Desk Architecture sections 1, 2, 3, 4, 5, 6, 7, 11, 13, 14, 16, 17, `MenuBuilder` has implemented the unified terminal onboarding consumer orchestrating both published provider contracts (`H-L4D-06A-PB-v1` for certificate PIN issuance and `H-L4D-06B-IOT-v1` for IoT device provisioning).

Key achievements:
1. **Contract Gates & Mapping:** Both gates (`H-L4D-06A-PB-v1` and `H-L4D-06B-IOT-v1`) verified and passed against `contract-handoff.md`.
2. **Business Terminal Lifecycle:** Single local database transaction creates `Terminal` and `L4DeskTerminal` records with monotonic tenant ordering (`ordinal`). The earliest active terminal of a tenant is granted the free tier marker (`is_free = True`). On terminal deletion, the free tier privilege automatically transfers to the next earliest active terminal without retroactive billing recalculations.
3. **Outbox Saga Pattern:** Idempotent post-commit saga coordinates IoT device provisioning and ProcessingBackend PIN request. Partial provider failures never roll back the committed business terminal record. Retry logic allows idempotent recovery using persistent `operation_id`.
4. **Security & Audit Masking:** Plaintext PIN is delivered to the user interface on initial creation and active replay only, and is never displayed again once consumed or expired per provider semantics. Plaintext PIN is strictly masked (`***773`) in all audit events (`L4DeskAuditEvent`), application logs, and database tables.
5. **UI Settings & Readiness Indicators:** MenuBuilder frontend (`TerminalsSettingsPage.tsx`) provides terminal creation modal, PIN delivery dialog with copy action, official Agent release download link (`https://l4tools-generic.ar.cloud.ru/l4tools/1.7.7/l4setup.exe`), and four exact readiness badges: `record` (database), `certificate` (PIN/cert lifecycle), `iot` (Leo4 IoT provisioning), and `online` (network presence).
6. **Zero-Regression Verification:** All 9 new onboarding unit/integration tests and all 344 existing backend test suite cases passed; frontend bundle built successfully with Vite/TypeScript; live smoke test verified against deployed services on `87.242.100.34`.

---

## 2. Verification of Input Contract Gates

### Gate 1: `H-L4D-06A-PB-v1` (ProcessingBackend Certificate PIN Provider)
- **Status in `contract-handoff.md`:** `ACCEPTED` (Section 7).
- **Producer:** `L4D-06A-PB` (`ProcessingBackend`, commit `083138f223b723098e9a188803e3fc802e8a6011`).
- **Contract Version:** `1.0.0`.
- **Consumers:** `[L4D-06B-IOT, L4D-06C-MB]`. Current prompt `L4D-06C-MB` is addressed.
- **Deployment Status:** `DEPLOYED` on `87.242.100.34:8000` (`processing-backend`).
- **Outcome:** **PASSED**.

### Gate 2: `H-L4D-06B-IOT-v1` (Leo4 IoT Platform Provisioning Provider)
- **Status in `contract-handoff.md`:** `ACCEPTED` (Section 12: *«Повторная приёмка основного шага 18 L4D-06B-IOT»*).
- **Producer:** `L4D-06B-IOT` (`iot-rpc-rest-app`, commit `c1894d07011d1a66ff5d56d68da34a946b8bfd5b`).
- **Contract Version:** `1.0.0`.
- **Consumers:** `[L4D-06C-MB, L4D-17C-IOT, L4D-18C-IOT]`. Current prompt `L4D-06C-MB` is addressed.
- **Deployment Status:** `DEPLOYED` on `87.242.100.34` (`app1:8000`).
- **Outcome:** **PASSED**.

---

## 3. Identifier Mapping Across Services

| Identifier Concept | MenuBuilder (`MenuBuilder`) | ProcessingBackend (`ProcessingBackend`) | IoT Platform (`iot-rpc-rest-app`) | Semantics & Scope |
| :--- | :--- | :--- | :--- | :--- |
| **Tenant / Org ID** | `org_id` / `tenant_id` (`int`) | `org_id` (`int`) | `tenant_id` (`int`) | Multi-tenant isolation boundary. Must match across all three services. |
| **Terminal ID** | `Terminal.id` == `L4DeskTerminal.terminal_id` (`int`) | `Terminal.id` (`int`) | `terminal_id` (`int`) | Unique database primary key of business terminal. |
| **Device ID** | `Terminal.device_id` (`int`) | `Terminal.device_id` (`int`) | `device_id` (`int`) | Internal hardware device integer allocated/confirmed by IoT platform. |
| **Serial Number** | `sn` (`str`) | `sn` (`str`) | `sn` (`str`) | Hardware terminal serial number (unique per tenant). |
| **Operation ID** | `operation_id` (`UUID str`) | `operation_id` (`UUID str`) | `operation_id` (`UUID str`) | End-to-end saga idempotency key across all providers. |
| **Correlation ID** | `correlation_id` (`str`) | `correlation_id` (`str`) | `correlation_id` (`str`) | Distributed tracing correlation identifier. |

---

## 4. Business Terminal Lifecycle & Monotonic Ordering

1. **Local Atomic Transaction:**
   - Evaluates next monotonic ordinal: `SELECT COALESCE(MAX(ordinal), 0) + 1 FROM l4desk_terminals WHERE tenant_id = :tenant_id`.
   - Resolves next runtime `Terminal.device_id`.
   - Inserts runtime `Terminal` and `L4DeskTerminal` with initial `provisioning_state = "pending"` and `pin_state = "pending"`.
   - Records `L4DeskAuditEvent` with `event_type = "terminal.created"`.
   - Local transaction is committed before running external provider network calls.
2. **First-Terminal Free Tier Rule:**
   - The terminal with the minimum active ordinal (`ordinal == min_active_ordinal`) is marked with `is_free = True`.
   - All subsequent terminals have `is_free = False`.
   - Upon soft deletion (`deleted_at = now()`), the free tier privilege automatically reassigns to the next earliest active terminal without retroactive billing recalculations.

---

## 5. Outbox Saga Orchestration & Partial Failure Recovery

1. **Step A: Leo4 IoT Platform Provisioning:**
   - Calls `POST /api/internal/v1/devices/provision` with payload:
     `{"operation_id": "...", "contract_version": "1.0.0", "tenant_id": ..., "terminal_id": ..., "sn": "...", "device_id": null}`.
   - On success: `provisioning_state = "ready"`, device ID updated if allocated by IoT, audit logged.
   - On failure: `provisioning_state = "failed"`, `last_error` recorded, audit logged.
2. **Step B: ProcessingBackend PIN Issuance:**
   - Calls `POST /api/certificates/pins/issue` with payload:
     `{"operation_id": "...", "tenant_id": ..., "terminal_id": ..., "sn": "...", "ttl_seconds": 86400}`.
   - On success: `pin_state = "issued"`, `certificate_reference = pin_masked`, audit logged (with masked PIN only).
   - On failure: `pin_state = "failed"`, `last_error` recorded, audit logged.
3. **Idempotent Retry & Reconciliation:**
   - Endpoint `POST /api/settings/terminals/{terminal_id}/retry` re-executes pending/failed steps using the durable `operation_id`.
   - If steps previously succeeded, provider endpoints return idempotent cached results without duplicate records.

---

## 6. Security & Audit Masking

- **No Plaintext PIN in Database:** `L4DeskTerminal` and `Terminal` tables store only `certificate_reference` (masked PIN, e.g. `***773`).
- **No Plaintext PIN in Audit Log:** `L4DeskAuditEvent.details` strictly records `{"pin_masked": response.pin_masked, "status": response.status}`.
- **Provider Semantics for PIN Delivery:** Plaintext PIN is delivered in the HTTP response body of initial creation and active replay. Once certificate is consumed (`consumed`) or expired (`expired`), plaintext PIN is strictly returned as `None`.

---

## 7. Frontend Settings UI

- **File:** `MenuBuilder/frontend/src/routes/settings/TerminalsSettingsPage.tsx`
- **Features:**
  - Header with "Подключить терминал" action button.
  - Creation modal with anti-duplicate click guard (`onboardLoading` state).
  - Four distinct readiness badges:
    1. **Record:** `Запись: ОК` (green)
    2. **Certificate:** `PIN готов` (blue) / `Сертификат ОК` (green) / `PIN ошибка` (red) / `PIN ожидает` (orange)
    3. **IoT:** `IoT: ОК` (green) / `IoT ошибка` (red) / `IoT ожидает` (orange)
    4. **Online:** `В сети` (green) / `Офлайн` (gray)
  - Quota tag: `0 ₽ (Льгота)` (gold) for free terminal; `Платный` (default) for others.
  - PIN delivery dialog featuring large copyable PIN typography, security guidance, and direct Agent release download button (`https://l4tools-generic.ar.cloud.ru/l4tools/1.7.7/l4setup.exe`).
  - Table row actions: Edit parameters, Retry failed steps (with spin state), Soft-delete with Popconfirm explaining quota transfer.

---

## 8. Verification & Test Evidence

### Backend Python Tests
- **Test File:** `MenuBuilder/backend/tests/test_terminal_onboarding.py`
- **Execution:** `uv run pytest tests/test_terminal_onboarding.py`
- **Result:** 9 passed in 1.14s:
  - `test_terminal_onboarding_success_flow`: Happy path, 4 readiness states, PIN delivery, audit masking.
  - `test_monotonic_tenant_ordering_and_free_marker_transfer`: Monotonic ordinal increment, free marker transfer on deletion.
  - `test_partial_failure_iot_fails_pin_succeeds`: IoT network failure, DB record preserved, PIN issued.
  - `test_partial_failure_pin_fails_iot_succeeds`: PIN provider failure, DB record preserved, IoT provisioned.
  - `test_saga_retry_recovers_failed_step`: Retry recovers failed step using persistent `operation_id`.
  - `test_idempotent_duplicate_clicks`: Idempotent replay prevents duplicate terminal records.
  - `test_tenant_isolation`: Cross-tenant access blocked with 403 Forbidden.
  - `test_provider_pin_semantics_consumed_pin_hidden`: Consumed PIN returns `pin=None`.
  - `test_auth_and_role_guards`: Anonymous (401), Viewer (403), Owner/Admin (201).
- **Full Suite:** 344 passed in 52.52s.

### Code Quality Checks
- `uv run ruff check app tests` -> All checks passed (0 errors).
- `uv run ruff format --check app tests` -> 88 files already formatted.
- `uv run pyright app` -> 0 errors, 0 warnings.
- `npm --prefix MenuBuilder/frontend run build` -> Production bundle built successfully (0 errors).

---

## 9. Deployment & Live Verification Evidence

- **Deploy Server:** `87.242.100.34` (`user1@87.242.100.34`, key `d:\.ssh\id_ed25519`).
- **Feature Flag:** `L4DESK_TERMINAL_ONBOARDING_ENABLED=true` in `/home/user1/MenuBuilder/backend/.env`.
- **Container Rebuild:** `sudo docker compose build menubuilder-backend && sudo docker compose up -d menubuilder-backend` executed successfully.
- **Frontend Dist:** Delivered to `/home/user1/MenuBuilder/frontend/dist/`.
- **Live Smoke Verification Output:**
  ```text
  1. GET /api/settings/terminals/onboard/status -> HTTP 200 (0.200s)
     Response: {"enabled": true, "agent_release_url": "https://l4tools-generic.ar.cloud.ru/l4tools/1.7.7/l4setup.exe", "agent_version": "1.7.7"}
  2. GET /api/settings/terminals -> HTTP 200 (0.113s, Total count: 131)
  3. POST /api/settings/terminals -> HTTP 201 (0.375s)
     Onboard response:
       terminal_id: 3713, sn: SN-LIVE-D824E5, ordinal: 4, is_free: False
       readiness: {"record": "ready", "certificate": "issued", "iot": "ready", "online": "offline"}
       pin: 258468, pin_masked: ***468, pin_expires_at: 2026-09-20T10:57:21.405965Z
       last_error: None
  4. GET /api/settings/terminals/3713/readiness -> HTTP 200 (0.054s)
  5. DELETE /api/settings/terminals/3713 -> HTTP 200 (0.034s, earliest_free_terminal_id: 3709)
  ```

---

## 10. Candidate Handoff Block

<!-- HANDOFF:H-L4D-06C-MB-v1:BEGIN -->
```yaml
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
```
<!-- HANDOFF:H-L4D-06C-MB-v1:END -->
