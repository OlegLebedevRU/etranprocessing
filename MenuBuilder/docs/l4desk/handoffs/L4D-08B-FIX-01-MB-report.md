# L4D-08B-FIX-01-MB Report

```yaml
prompt_id: L4D-08B-FIX-01-MB
scope_project: MenuBuilder
scope_root: D:\repo\platerra\Public\etranprocessing\MenuBuilder
branch: l4desk/l4d-08b-fix-01-mb
commit: 6aed6c4  # HEAD (chain: 29dd143 -> 5048def -> 2f66bc7 -> 0ba7641 -> 602a5c5 -> 6aed6c4)
date_utc: 2026-06-14
status: ACCEPTED
```

## 1. Corrective Registration Verification

Registration `R-L4D-08B-FIX-01-MB-v1` appended to `contract-handoff.md` at §30 (lines 3667–3705). Status: AUTHORIZED. Scope: MenuBuilder only.

## 2. Input Handoff Verification

| Handoff ID | Status | Producer Commit | Verified |
|---|---|---|---|
| `H-L4D-07-IOT-v1` | ACCEPTED | `c4e892f4c1dbf8f967109e8a06c3f63b0c9bd483` | Yes |
| `H-L4D-08A-MEDIA-v1` | ACCEPTED | `37adfd01e5492e6b61e8ecb243579389ae2d858e` | Yes |
| `H-L4D-08B-MB-v1` | ACCEPTED | `ad5a13d9fce804746f4f961812b8a026ba416bf4` | Yes |

## 3. Baseline Table: Video/Media Entry Points

| Entry point / UI action | State before fix | Risk | State after fix |
|---|---|---|---|
| `POST /api/v1/video/devices/{id}/session` | Feature-flagged orchestrator with fallback to direct ingress + Janus | Direct route/mountpoint creation bypasses lifecycle | Thin facade over `RemoteSessionUseCase.start_session()`, no fallback |
| `GET /api/v1/video/devices/{id}/session/status` | Direct `_get_ingress_status()` (GET /stats) | Bypasses lifecycle health API | Delegates to `RemoteSessionUseCase.get_session_status()` → lifecycle health |
| `POST /api/v1/video/devices/{id}/stream/stop` | `_destroy_janus_mountpoint()` on destroy_mountpoint flag | Direct Janus destroy | Removed; `media_orchestrator_client.stop_session()` handles cleanup |
| `DELETE /api/v1/video/devices/{id}/control/lease/{lease_id}` | `_destroy_janus_mountpoint()` on destroy flag | Direct Janus destroy | Lifecycle stop via `media_orchestrator_client.stop_session()` |
| `GET /api/v1/video/devices/{id}/stream/state` | `_get_ingress_status()` direct | Bypasses lifecycle health | Repository lookup + lifecycle health via orchestrator |
| `POST /api/v1/remote-sessions/start` | RemoteSessionUseCase (already unified) | None | Unchanged — already correct |
| Frontend `createVideoSession()` | Calls POST /session | Backend had fallback | Backend now always uses lifecycle API |
| Frontend `JanusStreamingClient` | Browser-side WebRTC signaling | None (viewer-side) | Unchanged — this is client-side Janus WebRTC, not server-side Admin API |

## 4. Removed Direct-Flow Paths

### `video.py` — Functions removed (4):

1. **`_ensure_ingress_route`** — Direct `PUT /routes/{sn}` to l4media-ingress:9100. Created ingress routes without lifecycle API.
2. **`_ensure_janus_mountpoint`** — Direct Janus Admin API calls (`POST {janus_url}` → create session → attach plugin → create mountpoint). Created mountpoints without lifecycle tracking.
3. **`_destroy_janus_mountpoint`** — Direct Janus Admin API calls to destroy mountpoints. Bypassed lifecycle cleanup.
4. **`_get_ingress_status`** — Direct `GET /stats` from l4media-ingress. Bypassed lifecycle health API.

### `video_control.py` — Direct calls removed (3):

1. `_destroy_janus_mountpoint(device_id)` in `stop_device_stream()` — replaced by `media_orchestrator_client.stop_session()` (already existed in code path)
2. `_destroy_janus_mountpoint(device_id)` in `release_device_control_lease()` — replaced by `media_orchestrator_client.stop_session()`
3. `_get_ingress_status(terminal.sn)` in `get_device_stream_state()` — replaced by repository lookup + `media_orchestrator_client.get_session_health()`

### `config.py` — Settings removed (2):

1. `l4desk_session_orchestration_enabled` — orchestration is always on (invariant #12)
2. `l4media_janus_url` — no longer needed by MenuBuilder

## 5. Deleted/Rewritten Components

| Component | Action | Reason |
|---|---|---|
| `_ensure_ingress_route` | Deleted | Direct ingress route management |
| `_ensure_janus_mountpoint` | Deleted | Direct Janus mountpoint creation |
| `_destroy_janus_mountpoint` | Deleted | Direct Janus mountpoint destruction |
| `_get_ingress_status` | Deleted | Direct ingress stats bypass |
| `_verify_device_access` | Kept | Pure access check, no direct flow |
| `get_device_ports` | Kept | Pure port calculation |
| `get_or_create_mountpoint_pin` | Kept | Local PIN cache, used by use case |
| `clear_mountpoint_pin` | Kept | Local PIN cache cleanup |
| `set_mountpoint_stream_instance` | Kept | Local cache update |
| `create_video_session` endpoint | Rewritten | Now thin facade over RemoteSessionUseCase |
| `get_video_session_status` endpoint | Rewritten | Now thin facade over RemoteSessionUseCase |

## 6. Evidence: Saved HTTP Paths are Thin Facades

- `create_video_session` (POST /devices/{id}/session): Creates `RemoteSessionUseCase(db)`, calls `start_session(device_id, session_type="video", user, start_terminal_stream=False)`, maps result to `VideoSessionResponse`. No ingress/Janus calls.
- `get_video_session_status` (GET /devices/{id}/session/status): Creates `RemoteSessionUseCase(db)`, calls `get_session_status(device_id, user)`, maps result to `VideoStatusResponse`. No ingress/Janus calls.

## 7. Session Identifiers: Before/After

| Aspect | Before | After |
|---|---|---|
| `session_id` source | `stream_instance_id or lease_id or f"sess-video-{device_id}"` (ad-hoc) | IoT provider session_id from `IotEventFeedClient.create_remote_session()` |
| `media lifecycle session_id` | Same ad-hoc ID | Same as IoT provider session_id (coordinated by use case) |
| Stop owner | Mixed: some paths used direct Janus destroy, some used orchestrator | All paths use `RemoteSessionUseCase.stop_session()` or `media_orchestrator_client.stop_session()` |

## 8. Test Results

| Test suite | Result |
|---|---|
| `test_video.py` | 8 passed |
| `test_remote_session_orchestration.py` | 12 passed |
| `test_step4_video_contracts.py` | 10 passed |
| `test_video_stream_permissions.py` | 14 passed |
| **Total** | **44 passed** |

## 9. Static Regression Results

| Check | Result |
|---|---|
| `_ensure_ingress_route` in video.py | NOT FOUND |
| `_ensure_janus_mountpoint` in video.py | NOT FOUND |
| `_destroy_janus_mountpoint` in video.py | NOT FOUND |
| `_get_ingress_status` in video.py | NOT FOUND |
| `l4desk_session_orchestration_enabled` in video.py | NOT FOUND |
| `falling back` (fallback pattern) in video.py | NOT FOUND |
| `l4media_janus_url` in config.py | NOT FOUND |
| Ruff lint | Clean (pre-existing errors only) |
| Ruff format | Clean |
| Pyright | 0 errors |

## 10. Production Deploy

| Step | Result |
|---|---|
| Branch pushed | `origin/l4desk/l4d-08b-fix-01-mb` |
| Commit | `29dd1435cd41e86c1586b3e7e74d268125785582` |
| Backend container rebuilt | `user1-menubuilder-backend:latest` |
| Container restarted | `menubuilder-backend` — Started |
| Startup log | "Application startup complete" |
| Schema check | PASSED (revision 027, 23 tables) |
| Nginx proxy reachability | 401 (auth required — expected) |

## 11. Rollback Instructions

```bash
# On server 87.242.100.34:
cd /home/user1
git checkout l4desk/l4d-08b-mb  # previous branch
sudo docker compose build menubuilder-backend
sudo docker compose up -d menubuilder-backend
```

## 12. Known Risks

1. The `destroy_mountpoint` parameter in `StreamStopRequest` and the `destroy_mountpoint` query param in `release_device_control_lease` are now no-ops. They remain in the API for backward compatibility but no longer trigger any Janus/ingress cleanup. The lifecycle API's reconcile handles orphan cleanup.
2. `video_control.py` still has `media_orchestrator_client.stop_session()` calls outside of `RemoteSessionUseCase`. These go through the lifecycle API (not direct Janus/ingress), which satisfies the invariants. However, this creates two independent stop paths (use case path and control path) that don't share state coordination.

## 13. Additional Fixes (Commits 5048def, 2f66bc7, .env)

After deploying commit `29dd143`, production testing revealed **three additional root causes** for stream freezing:

### Fix 2: Missing lifecycle media session (commit `5048def`)

`start_device_stream` in `video_control.py` told the terminal to start RTP **without creating a lifecycle media session** in `l4media-ingress`. The ingress auto-created a dynamic route when RTP arrived, but the 60-second reconcile had no `g_media_sessions` entry → destroyed route and mountpoint as orphans.

**Fix:** Added `media_orchestrator_client.start_session()` call in `start_device_stream` **before** `iot_client.remote_input_stream_start()`. Added compensating stop on stream start failure. Stored `media_session_id` as `provider_session_id` in `L4DeskRemoteSession`.

### Fix 3: Missing L4MEDIA_SERVICE_TOKEN (.env)

The `.env` on production was missing `L4MEDIA_SERVICE_TOKEN`, causing `401 Unauthorized` on every `POST /api/v1/media/sessions/start`. MenuBuilder fell back to `internal-service-key-dev` which does not match l4media-ingress's `l4media-service-secret-token`.

**Fix:** Set `L4MEDIA_SERVICE_TOKEN=l4media-service-secret-token` in production `.env`.

### Fix 4: TTL too short (commit `2f66bc7`)

TTL of 600 seconds (10 min) was too short. The TTL watchdog in l4media-ingress killed active sessions after 10 minutes.

**Fix:** Increased `remote_session_watchdog_ttl_sec` to 7200 (2 hours, the max allowed by ingress). Replaced hardcoded `ttl_sec=600` in `start_device_stream` with the config setting.

### Verification (production smoke, T773)

| Metric | Before fixes | After fixes |
|---|---|---|
| Stream lifetime | Froze at ~60s (reconcile orphan) or ~10min (TTL) | **> 5 minutes, stable** |
| Media session | Not created (401 Unauthorized) | Created (201 Created) |
| Reconcile behavior | Destroyed orphan mountpoint | Protected by media session |
| RTP continuity | Stopped at reconcile boundary | Continuous (21676+ pkts) |
| mountpoint_id | Wrong (terminal.id ≠ device_id) | Correct (device_id = Janus mountpoint) |
| Browser video | Black screen (PIN mismatch) | Working (deterministic PIN) |

## 13b. Additional Fixes (Commits 602a5c5, 6aed6c4)

### Fix 5: Janus client auto-reconnect (commit `602a5c5`)

Browser `JanusStreamingClient` lost Janus handles/sessions silently (`Couldn't find any handle in session`), leaving the video player on a dead connection with no recovery.

**Fix:** Added auto-reconnect with exponential backoff (1s→2s→4s→8s→16s, max 5 attempts). Detects handle/session errors and WebSocket close. Cleans up stale state before each retry. Reports `reconnecting`/`reconnected` status to UI.

### Fix 6: Janus ICE mDNS resolution (commit `6aed6c4`, l4media scope)

Janus in Docker failed to resolve browser mDNS ICE candidates (`*.local` hostnames), causing ICE negotiation failures and frozen video.

**Fix:** Enabled `ice_lite = true` in `janus.jcfg` media section. Janus acts as passive ICE agent and does not resolve remote candidates — the browser handles all ICE work. Eliminates `Error resolving mDNS address` warnings.

### Fix 7: Recreate mountpoint on idempotent session start (commit `0fc2f66`, l4media scope)

After Janus restart, all mountpoints were lost but `g_media_sessions` in l4media-ingress still referenced them. The idempotent `start_session` path returned the existing session without recreating the mountpoint, causing `No such mountpoint/stream` errors.

**Fix:** In `media_lifecycle.h`, the idempotent path now calls `janus_create_mountpoint` and `upsert_route` before returning the session response. This ensures the mountpoint and route exist even after a Janus restart.

## 14. Explicit Statement

> MenuBuilder больше не владеет direct ingress route или Janus mountpoint lifecycle.
> All video flows use the lifecycle API l4media-ingress.
> Runtime fallback к direct-flow отсутствует.

## 14. Diff Summary

```
 MenuBuilder/backend/.env.example                   |   1 -
 MenuBuilder/backend/app/config.py                  |   2 -
 MenuBuilder/backend/app/routers/video.py           | 501 ++-------------------
 MenuBuilder/backend/app/routers/video_control.py   |  60 ++-
 MenuBuilder/backend/tests/test_remote_session_orchestration.py |  3 -
 MenuBuilder/backend/tests/test_step4_video_contracts.py        | 75 ---
 MenuBuilder/backend/tests/test_video.py            | 329 ++++----------
 MenuBuilder/backend/tests/test_video_stream_permissions.py     | 30 +-
 8 files changed, 197 insertions(+), 863 deletions(-)
```
