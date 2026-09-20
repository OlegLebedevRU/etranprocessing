# Handoff Report: L4D-08B-MB — Unified Console / Video Orchestration Consumer

**Prompt ID:** `L4D-08B-MB`  
**Prompt Type:** `full-stack-consumer`  
**Scope Project:** `MenuBuilder`  
**Scope Root:** `D:\repo\platerra\Public\etranprocessing\MenuBuilder`  
**Required Handoff IDs:** `[H-L4D-07-IOT-v1, H-L4D-08A-MEDIA-v1]`  
**Output Handoff ID:** `H-L4D-08B-MB-v1`  
**Next Prompt ID:** `L4D-09-MB`  
**Branch:** `l4desk/l4d-08b-mb`  
**Report Path:** `MenuBuilder/docs/l4desk/handoffs/L4D-08B-MB-report.md`  
**Architecture Sections:** `[1, 2, 3, 4, 5, 6, 8, 11, 12, 13, 16, 17]`  

---

## 1. Executive Summary & Goals

In accordance with prompt `L4D-08B-MB` and L4Desk Architecture sections 1, 2, 3, 4, 5, 6, 8, 11, 12, 13, 16, 17, `MenuBuilder` has unified remote session orchestration across existing MenuBuilder users and the new L4Desk profile:

1. **Both Sequence Gates Passed:** Input contracts `H-L4D-07-IOT-v1` (IoT external session contract) and `H-L4D-08A-MEDIA-v1` (l4media on-demand media session lifecycle API) verified and confirmed against immutable fixtures.
2. **Unified `RemoteSessionUseCase`:**
   - Enforces strict tenant isolation and role-based permissions (Superuser, L4Desk owner/user role 5, Operator, Viewer).
   - Provides an explicit policy seam (`RemoteSessionPolicy` protocol) with `PermissiveLegacyPolicy` for 100% backward compatibility of existing users, and `L4DeskEntitlementPolicy` controlled by feature flag (`settings.l4desk_policy_enforcement_enabled = False` in L4D-08B, preparing the seam for L4D-12 billing).
   - Coordinates with `IotEventFeedClient` (`iot-rpc-rest-app`) for acquiring the durable technical session lock.
   - Coordinates with `MediaOrchestratorClient` (`l4media-ingress:9100`) for on-demand port allocation, Janus WebRTC mountpoint creation, Ingress route mapping, and session health telemetry.
   - Executes atomic **compensating stop / rollback** upon partial provider failures (e.g. media setup error or FFmpeg stream launch failure triggers compensating session termination and lease release on IoT).
3. **Mutual Exclusion & Strict Lock Ownership:**
   - Technical session lock is exclusively anchored in IoT (`iot-rpc-rest-app`).
   - If any session (console or video) is already active on a terminal, starting another session is strictly rejected with HTTP `409 Conflict` (`code: session_busy`).
   - Automatic cross-switching between console and video is strictly forbidden.
4. **Existing Components Reused & Hardened:**
   - Existing React components and routes (`DeviceConsoleTab.tsx`, `video-surveillance.tsx`, `player/`, `input/`, `console/`) are reused without duplication.
   - Standardized error reasons (`session_busy`, `lease_taken`, `offline`, `source_unavailable`, `ffmpeg_missing`, `terminal_timeout`, `policy_denied`).
   - Hardened connection timeouts and graceful resource cleanup.
5. **Zero-Regression & Live Verification:**
   - All 10 new orchestration tests and all 344 existing backend test suite cases passed (354/354 passed).
   - Code formatting (`ruff format`), linting (`ruff check`), and static typing (`pyright app`) passed with 0 errors.
   - Frontend production bundle built cleanly with Vite/TypeScript (`npm run build`).
   - Deployed under feature flags to production server `87.242.100.34`, with live verification of old UX and hidden L4Desk API.

---

## 2. Verification of Input Contract Gates

### Gate 1: `H-L4D-07-IOT-v1` (IoT External Contract Provider)
- **Status in `contract-handoff.md`:** `ACCEPTED` (Section 14).
- **Producer:** `L4D-07-IOT` (`iot-rpc-rest-app`, commit `7ad98a9cba29e925b3992015df365d95e03e4811`).
- **Consumers:** `[L4D-08A-MEDIA, L4D-08B-MB]`. Current prompt `L4D-08B-MB` is addressed.
- **Contract Surface:** Durable lock, session states (`starting`, `active`, `stopping`, `closed`, `failed`), idempotent replay, 409 conflict code `session_busy`.
- **Outcome:** **PASSED**.

### Gate 2: `H-L4D-08A-MEDIA-v1` (l4media On-Demand Media Lifecycle Provider)
- **Status in `contract-handoff.md`:** `ACCEPTED` (Section 15).
- **Producer:** `L4D-08A-MEDIA` (`l4media`, commit `37adfd01e5492e6b61e8ecb243579389ae2d858e`).
- **Consumers:** `[L4D-08B-MB]`. Current prompt `L4D-08B-MB` is addressed.
- **Contract Surface:** Service-authenticated API on port 9100 (`/api/v1/media/sessions/start`, `/sessions/{session_id}`, `/sessions/{session_id}/stop`, `/reconcile`, `/metrics`), OpenAPI specification, mountpoint PIN protection, automatic TTL watchdog.
- **Outcome:** **PASSED**.

---

## 3. Architecture & Unified Contract Surface

```
Browser UI (Console Tab / Video Surveillance / L4Desk)
        │
        ▼ (HTTPS :3000 / :443)
Nginx Reverse Proxy
  - /api/v1/remote-sessions/ ──► menubuilder-backend:8000
  - /api/v1/video/           ──► menubuilder-backend:8000
  - /janus-ws                ──► l4media-janus:8188
        │
        ▼
MenuBuilder Backend (RemoteSessionUseCase)
  ├── 1. Tenant boundary & role matrix verification
  ├── 2. RemoteSessionPolicy seam (Legacy permissive vs L4Desk flag)
  ├── 3. Local session table reservation (l4desk_remote_sessions)
  ├── 4. Mutual exclusion check (reject 409 session_busy if active)
  ├── 5. IoT Session Adapter (iot-rpc-rest-app: lock + control lease)
  ├── 6. Media Orchestrator Client (l4media-ingress:9100: on-demand session)
  └── 7. Compensating rollback on partial startup failure
```

### Endpoints Implemented

| Endpoint | Method | Role Guard | Description |
| :--- | :--- | :--- | :--- |
| `/api/v1/remote-sessions/start` | `POST` | Tenant User / Superuser / L4Desk | Start or idempotent replay of remote session (`session_type: "console" \| "video"`). |
| `/api/v1/remote-sessions/stop` | `POST` | Tenant User / Superuser / L4Desk | Graceful termination of session by `device_id` or `session_id`, cleaning up stream, media, IoT lock, and lease. |
| `/api/v1/remote-sessions/{session_id}/stop` | `POST` | Tenant User / Superuser / L4Desk | Path-based session termination. |
| `/api/v1/remote-sessions/devices/{device_id}/active` | `GET` | Tenant User / Superuser / L4Desk | Active session details and media streaming telemetry (`streaming`, `rtp_packets`, `bytes`, `idle_sec`). |
| `/api/remote-sessions/*` | `*` | Tenant User / Superuser / L4Desk | Non-v1 path alias for complete routing compatibility. |

---

## 4. Key Implementation Details

### 4.1. Unified `RemoteSessionUseCase`
- **File:** `MenuBuilder/backend/app/services/remote_session_use_case.py`
- Coordinates tenant boundary checks, role permissions (superuser, role 5 L4Desk owner, operator, viewer), and policy evaluation.
- Implements strict mutual exclusion: querying active sessions in `l4desk_remote_sessions` and rejecting new starts with HTTP 409 `session_busy` if any other session is active.
- Integrates compensating rollback: if media or stream launch fails, it initiates compensating stop calls to `iot-rpc-rest-app` and releases any acquired control lease before re-raising the error.

### 4.2. Media Orchestrator Client
- **File:** `MenuBuilder/backend/app/services/media_orchestrator_client.py`
- Implements `MediaOrchestratorClient` communicating with `l4media-ingress:9100` via service token (`X-Media-Service-Token`).
- Provides typed exceptions (`MediaSessionConflictError`, `MediaSessionNotFoundError`, `MediaJanusError`, `MediaPortExhaustionError`).
- Provides `start_session`, `get_session_health`, `stop_session`, `reconcile`, and `get_metrics`.

### 4.3. Remote Session Policy Seam
- **File:** `MenuBuilder/backend/app/services/remote_session_policy.py`
- Defines `RemoteSessionPolicy` protocol and `PolicyDecision`.
- Implements `PermissiveLegacyPolicy` (unconditional access for legacy operations).
- Implements `L4DeskEntitlementPolicy` guarded by `settings.l4desk_policy_enforcement_enabled` (disabled by default in L4D-08B), ready for L4D-12 billing integration.

### 4.4. Frontend Component Hardening
- **Files:** `MenuBuilder/frontend/src/api/video.ts`, `MenuBuilder/frontend/src/routes/video-surveillance.tsx`, `MenuBuilder/frontend/src/routes/devices/DeviceConsoleTab.tsx`
- Added TypeScript interfaces `RemoteSessionInfo` and API helpers `startRemoteSession`, `stopRemoteSession`, `getActiveRemoteSession`.
- Expanded operator recognition to include `user.role_id === 5` (L4Desk user).
- Standardized error mapping in `formatVideoError` to explicitly recognize and format `session_busy` conflict reasons and guidance.
- Enhanced console lease conflict reporting in `DeviceConsoleTab.tsx` for multi-session locking.

---

## 5. Verification & Test Evidence

### Backend Test Suite
- **New Test File:** `MenuBuilder/backend/tests/test_remote_session_orchestration.py`
- **Execution:** `uv run pytest tests/test_remote_session_orchestration.py -v`
- **Result:** 10 passed in 3.09s:
  1. `test_consumer_gates_fixtures_validation`: Verification of input gates and schemas against fixtures.
  2. `test_tenant_access_and_isolation`: Cross-tenant access rejected (403); superuser allowed across tenants.
  3. `test_role_matrix_console_and_video`: Role 5 allowed for console and video; Viewer (role 4) rejected for console (403).
  4. `test_mutual_exclusion_and_no_auto_switch`: Active video rejects console with 409 `session_busy`.
  5. `test_idempotent_session_start_replay`: Deterministic replay returns identical active session details.
  6. `test_compensating_stop_on_media_failure`: Media startup error triggers compensating stop on IoT and marks session failed.
  7. `test_compensating_stop_on_stream_start_failure`: Stream launch error triggers compensating stop on media and IoT.
  8. `test_graceful_stop_flow`: Graceful session stop cleans up stream, media, IoT lock, and lease.
  9. `test_unified_api_remote_sessions_lifecycle`: Full HTTP API lifecycle on `/api/v1/remote-sessions/start`, `/devices/{id}/active`, `/stop`.
  10. `test_legacy_video_session_endpoint_regression`: Existing `/api/v1/video/devices/{id}/session` continues to work with media orchestrator fallback.
- **Full Backend Suite:** 354 passed, 0 failed in 96s (`uv run pytest`).

### Code Quality Checks
- `uv run ruff check app tests` -> All checks passed (0 errors).
- `uv run ruff format --check app tests` -> 0 unformatted files.
- `uv run pyright app` -> 0 errors, 0 warnings.
- `npm --prefix MenuBuilder/frontend run build` -> Production bundle built successfully (0 errors, 42.8s).

---

## 6. Deployment & Live Verification Evidence

- **Deploy Host:** `87.242.100.34` (`user1@87.242.100.34`, key `d:\.ssh\id_ed25519`).
- **Feature Flags:**
  - `L4DESK_SESSION_ORCHESTRATION_ENABLED=true`
  - `L4DESK_POLICY_ENFORCEMENT_ENABLED=false`
- **Container Rebuild:** `sudo docker compose build --no-cache menubuilder-backend && sudo docker compose up -d menubuilder-backend` executed successfully.
- **Frontend Live Mount:** Delivered built artifacts to `/home/user1/MenuBuilder/frontend/dist/`.
- **Nginx Config:** Added `location /api/v1/remote-sessions/` in `nginx-configs/port_3000.conf` and executed `nginx -s reload`.
- **Live Smoke Verification Output:**
  ```text
  1. Frontend SPA Delivery:
     GET https://127.0.0.1:3000/ -> HTTP 200 OK
  2. Legacy Video Status Endpoint:
     GET https://127.0.0.1:3000/api/v1/video/devices/1/session/status -> HTTP 401 Unauthorized (Auth guard verified)
  3. Unified Remote Sessions Endpoint:
     GET https://127.0.0.1:3000/api/v1/remote-sessions/devices/1/active -> HTTP 401 Unauthorized (Auth guard verified)
  4. OpenAPI Schema Verification:
     GET http://172.19.0.4:8000/openapi.json -> 16 occurrences of remote-sessions paths registered
  5. Media Ingress Health:
     GET http://172.19.0.10:9100/health -> {"status":"ok","routes":1,"active_media_sessions":12}
  ```

---

## 7. Candidate Handoff Block

<!-- HANDOFF:H-L4D-08B-MB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-08B-MB-v1
status: CANDIDATE
contract_kinds:
  - API
  - DEPLOYMENT
producer_prompt_id: L4D-08B-MB
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-08B-MB-report.md
producer_branch: l4desk/l4d-08b-mb
accepted_at_utc: null
contract_version: 1.0.0
schema_revision: 1.0.0
artifact_version: 1.0.0
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-08B-MB-candidate.md
artifact_paths:
  - MenuBuilder/backend/app/services/remote_session_use_case.py
  - MenuBuilder/backend/app/services/media_orchestrator_client.py
  - MenuBuilder/backend/app/services/remote_session_policy.py
  - MenuBuilder/backend/app/routers/remote_sessions.py
  - MenuBuilder/backend/app/routers/video_control.py
  - MenuBuilder/backend/app/routers/video.py
  - MenuBuilder/backend/app/repositories/l4desk_repository.py
  - MenuBuilder/backend/app/config.py
  - MenuBuilder/backend/app/main.py
  - MenuBuilder/frontend/src/api/video.ts
  - MenuBuilder/frontend/src/routes/video-surveillance.tsx
  - MenuBuilder/frontend/src/routes/devices/DeviceConsoleTab.tsx
  - MenuBuilder/backend/tests/test_remote_session_orchestration.py
  - nginx-configs/port_3000.conf
artifact_sha256:
  - e83af253732b11d89e63c18562b563420ae6b228492f40eee74d27de07df2389
  - 65b07e3b35eccbfa412ec75a8b1362f1cdf682107af51385e5e06e4d8652ed8a
  - 688e4d3d31ab4c623d4bc85f602ede31bb935ee39394e88fd3de08c5444e5ca5
  - f6612a7f679e6e1c73d74128911dcbe608778fb9d8212ac2f93096777e94dd72
  - 3b00494d61e655a7025a29888e1d6d5e0957cc972acf64a366d1acd1b63d2b47
  - ac7856605aa42cc24db1ec8657d5f5959e6496a35e5940ff68dbc614f78f03bc
  - 29479b3b537c1b3e67bf4855d283010dce5420541ade3d5d5b6fe7d841658e5e
  - 31782a1eb607a3f84375ef798ed000e614de33c09161e7353d0855b6db6a3116
  - ae38c22884ad8246cbcaefb70aa49be5b42687dfa477f7b19860b0224c789ea7
  - 87f8dbc8e396bad3d6c35005d1672cc9a48815afbb894323378bf9e095b72d14
  - d8b93b0ab0285781f33efba06dfca6d3469ddf2872ec48ec8b3333e7fc7a1cf5
  - 20f20692276bf91ba4c379a797fc416b0fd4e73b78da216f2a25bc3fc62a3387
compatibility:
  backward_compatible_with:
    - H-L4D-07-IOT-v1
    - H-L4D-08A-MEDIA-v1
  breaking_changes: false
  notes: Unified RemoteSessionUseCase for console and video session orchestration across legacy MenuBuilder users and L4Desk commercial profile. Enforces mutual exclusion (session_busy 409, no auto-switch), compensating stop upon partial provider failures, and provides a policy seam with disabled entitlement flag.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  l4desk_session_orchestration_enabled: true
  l4desk_policy_enforcement_enabled: false
contract_payload:
  endpoints:
    start_session: /api/v1/remote-sessions/start
    stop_session: /api/v1/remote-sessions/stop
    stop_session_by_id: /api/v1/remote-sessions/{session_id}/stop
    device_active_session: /api/v1/remote-sessions/devices/{device_id}/active
  policy_seam:
    interface: RemoteSessionPolicy
    legacy_implementation: PermissiveLegacyPolicy
    commercial_implementation: L4DeskEntitlementPolicy (disabled in 08B)
  error_mapping:
    400: invalid_request
    401: unauthorized
    403: tenant_forbidden, permission_denied, policy_denied
    404: terminal_not_found
    409: session_busy (active session conflict, automatic switch forbidden), lease_conflict
    502: iot_gateway_error, media_gateway_error
  invariants:
    - Exactly one active remote session per terminal device across both console and video
    - Automatic cross-switching between console and video is strictly forbidden
    - Replay with identical operation_id returns active session parameters idempotently
    - Partial failure during media or stream launch executes compensating stop on IoT and releases control lease
supersedes: []
known_risks: []
consumers:
  - L4D-09-MB
next_prompt_id: L4D-09-MB
```
<!-- HANDOFF:H-L4D-08B-MB-v1:END -->
