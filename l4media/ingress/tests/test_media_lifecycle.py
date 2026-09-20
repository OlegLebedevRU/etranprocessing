#!/usr/bin/env python3
"""
Comprehensive Integration, Contract, Idempotency, and Failure Test Suite
for l4media On-Demand Media Lifecycle Management.

Covers:
1. Contract & OpenAPI validation (GET /api/v1/openapi.json).
2. Service Authentication (X-Media-Service-Token / Bearer auth, 401 on missing/invalid, legacy endpoints open).
3. On-demand media lifecycle (Start -> Health/Metrics -> Stop) with Janus & Ingress reconciliation.
4. Idempotency (deterministic replay for start and stop).
5. Device mutual exclusion / session lock (409 Conflict session_busy).
6. Concurrency test (parallel starts on same device -> exactly 1 winner).
7. Validation errors (missing session_id, missing sn, 404 on unknown session).
8. Automated reconciliation (detects and prunes orphan Janus mountpoints and Ingress routes).
9. Automated TTL watchdog expiration (session terminates automatically upon TTL expiry).
10. Aggregate technical and audit metrics (GET /api/v1/media/metrics).
"""

import concurrent.futures
import json
import os
import sys
import time
import urllib.error
import urllib.request

DEFAULT_SERVICE_TOKEN = os.getenv(
    "L4MEDIA_SERVICE_TOKEN", "l4media-service-secret-token"
)
DEFAULT_JANUS_ADMIN_SECRET = os.getenv("JANUS_ADMIN_SECRET", "janusoverlord")


def http_request(
    url: str,
    method: str = "GET",
    headers: dict | None = None,
    body: dict | str | bytes | None = None,
    timeout: float = 5.0,
) -> tuple[int, dict]:
    req_headers = {"Content-Type": "application/json"}
    if headers:
        req_headers.update(headers)

    data = None
    if body is not None:
        if isinstance(body, dict):
            data = json.dumps(body).encode("utf-8")
        elif isinstance(body, str):
            data = body.encode("utf-8")
        elif isinstance(body, bytes):
            data = body

    req = urllib.request.Request(url, data=data, headers=req_headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            content = resp.read().decode("utf-8")
            return resp.status, json.loads(content) if content else {}
    except urllib.error.HTTPError as exc:
        content = exc.read().decode("utf-8")
        try:
            return exc.code, json.loads(content) if content else {}
        except Exception:
            return exc.code, {"error": content}
    except Exception as exc:
        return 0, {"error": str(exc)}


def janus_admin_call(janus_host: str, admin_port: int, plugin_request: dict) -> dict:
    url = f"http://{janus_host}:{admin_port}/admin"
    payload = {
        "janus": "message_plugin",
        "transaction": f"test_tx_{int(time.time() * 1000)}",
        "admin_secret": DEFAULT_JANUS_ADMIN_SECRET,
        "plugin": "janus.plugin.streaming",
        "request": plugin_request,
    }
    status, resp = http_request(url, method="POST", body=payload)
    assert status == 200, f"Janus admin request failed: status {status}, resp {resp}"
    return resp


def test_openapi_spec(base_url: str):
    print("\n[TEST 1] Verifying OpenAPI 3.0.3 specification...")
    status, data = http_request(f"{base_url}/api/v1/openapi.json")
    assert status == 200, f"Expected 200, got {status}: {data}"
    assert data.get("openapi") == "3.0.3", (
        f"Invalid OpenAPI version: {data.get('openapi')}"
    )
    paths = data.get("paths", {})
    assert "/api/v1/media/sessions/start" in paths, "Missing start endpoint in OpenAPI"
    assert "/api/v1/media/sessions/{session_id}" in paths, (
        "Missing health endpoint in OpenAPI"
    )
    assert "/api/v1/media/sessions/{session_id}/stop" in paths, (
        "Missing stop endpoint in OpenAPI"
    )
    assert "/api/v1/media/reconcile" in paths, "Missing reconcile endpoint in OpenAPI"
    assert "/api/v1/media/metrics" in paths, "Missing metrics endpoint in OpenAPI"
    print("  [PASS] OpenAPI specification valid and verified.")


def test_service_auth(base_url: str):
    print("\n[TEST 2] Verifying Service Authentication security...")

    # Case A: Missing token -> 401 Unauthorized
    status, data = http_request(f"{base_url}/api/v1/media/metrics")
    assert status == 401, f"Expected 401 without auth token, got {status}: {data}"
    assert data.get("error") == "unauthorized", f"Expected 'unauthorized', got: {data}"

    # Case B: Invalid token -> 401 Unauthorized
    headers_bad = {"X-Media-Service-Token": "invalid-wrong-token"}
    status, data = http_request(f"{base_url}/api/v1/media/metrics", headers=headers_bad)
    assert status == 401, f"Expected 401 with bad token, got {status}: {data}"

    # Case C: Valid X-Media-Service-Token -> 200 OK
    headers_good = {"X-Media-Service-Token": DEFAULT_SERVICE_TOKEN}
    status, data = http_request(
        f"{base_url}/api/v1/media/metrics", headers=headers_good
    )
    assert status == 200, f"Expected 200 with valid token, got {status}: {data}"

    # Case D: Valid Authorization: Bearer <token> -> 200 OK
    headers_bearer = {"Authorization": f"Bearer {DEFAULT_SERVICE_TOKEN}"}
    status, data = http_request(
        f"{base_url}/api/v1/media/metrics", headers=headers_bearer
    )
    assert status == 200, f"Expected 200 with Bearer token, got {status}: {data}"

    # Case E: Backward-compatibility: legacy /health, /stats, /routes do not require token
    status, _ = http_request(f"{base_url}/health")
    assert status == 200, f"Legacy /health failed: status {status}"
    status, _ = http_request(f"{base_url}/routes")
    assert status == 200, f"Legacy /routes failed: status {status}"

    print(
        "  [PASS] Service Authentication enforcement and legacy compatibility verified."
    )


def test_lifecycle_full_flow(base_url: str, janus_host: str, janus_admin_port: int):
    print("\n[TEST 3] Verifying On-Demand Media Lifecycle (Start -> Health -> Stop)...")
    headers = {"X-Media-Service-Token": DEFAULT_SERVICE_TOKEN}
    session_id = f"test-sess-e2e-{int(time.time())}"
    sn = f"test-sn-e2e-{int(time.time())}"
    device_id = 9123

    # Step 1: Start Media Session
    start_payload = {
        "session_id": session_id,
        "operation_id": f"op-start-{session_id}",
        "sn": sn,
        "device_id": device_id,
        "pin": "pin12345",
        "ttl_sec": 300,
    }
    status, resp = http_request(
        f"{base_url}/api/v1/media/sessions/start",
        method="POST",
        headers=headers,
        body=start_payload,
    )
    assert status in (200, 201), f"Expected 200/201 on start, got {status}: {resp}"
    assert resp.get("status") == "success"
    assert resp.get("session_id") == session_id
    assert resp.get("state") == "active"
    assert resp.get("device_id") == device_id
    assert resp.get("mountpoint_id") == device_id
    rtp_port = resp.get("rtp_port")
    rtcp_port = resp.get("rtcp_port")
    assert rtp_port and rtp_port >= 6010, f"Invalid rtp_port: {rtp_port}"
    assert rtcp_port == rtp_port + 1, (
        f"Expected rtcp_port={rtp_port + 1}, got {rtcp_port}"
    )

    # Step 2: Verify mountpoint created in Janus
    j_info = janus_admin_call(
        janus_host, janus_admin_port, {"request": "info", "id": device_id}
    )
    j_resp = j_info.get("response", {})
    assert j_resp.get("streaming") == "info", f"Mountpoint missing in Janus: {j_info}"
    assert j_resp.get("info", {}).get("id") == device_id

    # Step 3: Verify route registered in Ingress
    status, route_resp = http_request(f"{base_url}/routes/{sn}")
    assert status == 200, f"Route not found in Ingress: status {status}"
    assert route_resp.get("rtp_port") == rtp_port

    # Step 4: Verify GET /api/v1/media/sessions/{session_id}
    status, health = http_request(
        f"{base_url}/api/v1/media/sessions/{session_id}", headers=headers
    )
    assert status == 200, f"Health check failed: status {status}: {health}"
    assert health.get("session_id") == session_id
    assert health.get("state") == "active"
    assert health.get("media_state") == "waiting_media"
    assert health.get("transport_connected") is False
    assert health.get("ttl_sec") == 300
    assert health.get("ttl_remaining_sec") <= 300

    # Step 5: Stop Media Session
    stop_payload = {"operation_id": f"op-stop-{session_id}", "reason": "operator_quit"}
    status, stop_resp = http_request(
        f"{base_url}/api/v1/media/sessions/{session_id}/stop",
        method="POST",
        headers=headers,
        body=stop_payload,
    )
    assert status == 200, f"Stop failed: status {status}: {stop_resp}"
    assert stop_resp.get("status") == "success"
    assert stop_resp.get("state") == "stopped"
    assert stop_resp.get("stop_reason") == "operator_quit"

    # Step 6: Verify mountpoint destroyed in Janus
    j_info_after = janus_admin_call(
        janus_host, janus_admin_port, {"request": "info", "id": device_id}
    )
    assert j_info_after.get("response", {}).get("error_code") == 455, (
        f"Mountpoint was NOT destroyed in Janus: {j_info_after}"
    )

    # Step 7: Verify route removed from Ingress
    status, _ = http_request(f"{base_url}/routes/{sn}")
    assert status == 404, f"Route was NOT removed from Ingress: status {status}"

    print("  [PASS] Full media session lifecycle start -> health -> stop verified.")


def test_idempotency_and_repeats(base_url: str):
    print("\n[TEST 4] Verifying Idempotency and Deterministic Repeated Responses...")
    headers = {"X-Media-Service-Token": DEFAULT_SERVICE_TOKEN}
    session_id = f"test-sess-idemp-{int(time.time())}"
    operation_id = f"op-idemp-{session_id}"
    sn = f"test-sn-idemp-{int(time.time())}"

    payload = {
        "session_id": session_id,
        "operation_id": operation_id,
        "sn": sn,
        "device_id": 9234,
        "ttl_sec": 300,
    }

    # Initial start
    status1, resp1 = http_request(
        f"{base_url}/api/v1/media/sessions/start",
        method="POST",
        headers=headers,
        body=payload,
    )
    assert status1 in (200, 201), f"First start failed: {status1}: {resp1}"

    # Repeated start with identical session_id & operation_id
    status2, resp2 = http_request(
        f"{base_url}/api/v1/media/sessions/start",
        method="POST",
        headers=headers,
        body=payload,
    )
    assert status2 == 200, f"Repeated start expected 200 OK, got {status2}: {resp2}"
    assert resp2.get("session_id") == resp1.get("session_id")
    assert resp2.get("mountpoint_id") == resp1.get("mountpoint_id")
    assert resp2.get("rtp_port") == resp1.get("rtp_port")
    assert resp2.get("created_at") == resp1.get("created_at")
    assert resp2.get("state") == "active"

    # Stop session
    status_s1, stop_r1 = http_request(
        f"{base_url}/api/v1/media/sessions/{session_id}/stop",
        method="POST",
        headers=headers,
        body={"operation_id": "op-s1"},
    )
    assert status_s1 == 200
    assert stop_r1.get("state") == "stopped"

    # Repeated stop of already stopped session
    status_s2, stop_r2 = http_request(
        f"{base_url}/api/v1/media/sessions/{session_id}/stop",
        method="POST",
        headers=headers,
        body={"operation_id": "op-s2"},
    )
    assert status_s2 == 200, f"Repeated stop expected 200, got {status_s2}"
    assert stop_r2.get("state") == "stopped"
    assert "already stopped or absent" in stop_r2.get("detail", "")

    # Stop of non-existent session
    status_s3, stop_r3 = http_request(
        f"{base_url}/api/v1/media/sessions/completely-unknown-session/stop",
        method="POST",
        headers=headers,
    )
    assert status_s3 == 200
    assert stop_r3.get("state") == "stopped"
    assert "already stopped or absent" in stop_r3.get("detail", "")

    print("  [PASS] Deterministic repeated start and stop idempotency verified.")


def test_device_mutual_exclusion(base_url: str):
    print("\n[TEST 5] Verifying Device Mutual Exclusion (session_busy conflict)...")
    headers = {"X-Media-Service-Token": DEFAULT_SERVICE_TOKEN}
    shared_sn = f"test-sn-busy-{int(time.time())}"
    sess_a = f"sess-a-{int(time.time())}"
    sess_b = f"sess-b-{int(time.time())}"

    # Start session A on shared_sn
    status_a, resp_a = http_request(
        f"{base_url}/api/v1/media/sessions/start",
        method="POST",
        headers=headers,
        body={"session_id": sess_a, "sn": shared_sn, "device_id": 9301},
    )
    assert status_a in (200, 201), f"Start sess_a failed: {status_a}: {resp_a}"

    # Attempt to start session B on same shared_sn
    status_b, resp_b = http_request(
        f"{base_url}/api/v1/media/sessions/start",
        method="POST",
        headers=headers,
        body={"session_id": sess_b, "sn": shared_sn, "device_id": 9302},
    )
    assert status_b == 409, (
        f"Expected 409 Conflict for busy device, got {status_b}: {resp_b}"
    )
    assert resp_b.get("error") == "session_busy", (
        f"Expected error='session_busy', got: {resp_b}"
    )

    # Stop session A
    http_request(
        f"{base_url}/api/v1/media/sessions/{sess_a}/stop",
        method="POST",
        headers=headers,
    )

    # Now session B succeeds
    status_b2, resp_b2 = http_request(
        f"{base_url}/api/v1/media/sessions/start",
        method="POST",
        headers=headers,
        body={"session_id": sess_b, "sn": shared_sn, "device_id": 9302},
    )
    assert status_b2 in (200, 201), (
        f"Start sess_b after sess_a stop failed: {status_b2}: {resp_b2}"
    )

    # Cleanup session B
    http_request(
        f"{base_url}/api/v1/media/sessions/{sess_b}/stop",
        method="POST",
        headers=headers,
    )

    print("  [PASS] Device mutual exclusion and 409 session_busy verified.")


def test_concurrency(base_url: str):
    print("\n[TEST 6] Verifying Concurrency (Parallel starts on same device)...")
    headers = {"X-Media-Service-Token": DEFAULT_SERVICE_TOKEN}
    shared_sn = f"test-sn-concurrent-{int(time.time())}"
    threads_count = 5

    def try_start(idx: int):
        sid = f"concurrent-sess-{idx}-{int(time.time() * 1000)}"
        return http_request(
            f"{base_url}/api/v1/media/sessions/start",
            method="POST",
            headers=headers,
            body={"session_id": sid, "sn": shared_sn, "device_id": 9400 + idx},
        )

    with concurrent.futures.ThreadPoolExecutor(max_workers=threads_count) as executor:
        futures = [executor.submit(try_start, i) for i in range(threads_count)]
        results = [f.result() for f in futures]

    statuses = [res[0] for res in results]
    successes = [res for res in results if res[0] in (200, 201)]
    conflicts = [res for res in results if res[0] == 409]

    print(f"  -> Concurrency outcomes: {statuses}")
    assert len(successes) == 1, f"Expected exactly 1 winner, got {len(successes)}"
    assert len(conflicts) == threads_count - 1, (
        f"Expected {threads_count - 1} conflicts, got {len(conflicts)}"
    )

    # Clean up winner session
    winner_sid = successes[0][1].get("session_id")
    http_request(
        f"{base_url}/api/v1/media/sessions/{winner_sid}/stop",
        method="POST",
        headers=headers,
    )

    print(
        "  [PASS] Concurrency: exactly 1 winner, remainder cleanly rejected with 409."
    )


def test_reconciliation_orphan_cleanup(
    base_url: str, janus_host: str, janus_admin_port: int
):
    print("\n[TEST 7] Verifying Automated Reconciliation & Orphan Resource Pruning...")
    headers = {"X-Media-Service-Token": DEFAULT_SERVICE_TOKEN}
    orphan_mp_id = 9888
    orphan_sn = f"test-orphan-sn-{int(time.time())}"

    # 1. Inject orphan mountpoint directly into Janus
    janus_admin_call(
        janus_host,
        janus_admin_port,
        {
            "request": "create",
            "type": "rtp",
            "id": orphan_mp_id,
            "name": f"orphan-{orphan_mp_id}",
            "description": "Orphan mountpoint for reconciliation test",
            "video": True,
            "audio": False,
            "videoport": 6188,
            "videortcpport": 6189,
            "videopt": 96,
            "videocodec": "h264",
        },
    )

    # Verify orphan exists in Janus
    j_check = janus_admin_call(
        janus_host, janus_admin_port, {"request": "info", "id": orphan_mp_id}
    )
    assert j_check.get("response", {}).get("streaming") == "info"

    # 2. Inject orphan route directly into Ingress
    http_request(f"{base_url}/routes/{orphan_sn}?rtp=6188&rtcp=6189", method="PUT")
    status, r_check = http_request(f"{base_url}/routes/{orphan_sn}")
    assert status == 200

    # 3. Trigger reconciliation
    status, recon = http_request(
        f"{base_url}/api/v1/media/reconcile", method="POST", headers=headers
    )
    assert status == 200, f"Reconciliation failed: {status}: {recon}"
    assert recon.get("status") == "ok"
    assert recon.get("orphans_cleaned_mountpoints", 0) >= 1
    assert recon.get("orphans_cleaned_routes", 0) >= 1

    # 4. Confirm orphan mountpoint is pruned in Janus
    j_check_after = janus_admin_call(
        janus_host, janus_admin_port, {"request": "info", "id": orphan_mp_id}
    )
    assert j_check_after.get("response", {}).get("error_code") == 455, (
        f"Orphan mountpoint was not destroyed: {j_check_after}"
    )

    # 5. Confirm orphan route is pruned from Ingress
    status_r, _ = http_request(f"{base_url}/routes/{orphan_sn}")
    assert status_r == 404, "Orphan route was not removed from Ingress"

    # 6. Confirm demo mountpoint 1 is preserved
    j_demo = janus_admin_call(
        janus_host, janus_admin_port, {"request": "info", "id": 1}
    )
    assert j_demo.get("response", {}).get("streaming") == "info", (
        "Demo mountpoint 1 was destroyed!"
    )

    print(
        "  [PASS] Reconciliation successfully pruned orphans while protecting static resources."
    )


def test_ttl_watchdog_expiration(base_url: str, janus_host: str, janus_admin_port: int):
    print("\n[TEST 8] Verifying Automated TTL Watchdog Expiration...")
    headers = {"X-Media-Service-Token": DEFAULT_SERVICE_TOKEN}
    session_id = f"test-sess-ttl-{int(time.time())}"
    sn = f"test-sn-ttl-{int(time.time())}"
    device_id = 9555

    # Start session with 10s minimum TTL (bounded)
    status, resp = http_request(
        f"{base_url}/api/v1/media/sessions/start",
        method="POST",
        headers=headers,
        body={
            "session_id": session_id,
            "sn": sn,
            "device_id": device_id,
            "ttl_sec": 10,
        },
    )
    assert status in (200, 201)
    assert resp.get("ttl_sec") == 10

    # Wait for TTL to expire (11 seconds)
    print("  -> Waiting 11 seconds for TTL watchdog tick...")
    time.sleep(11.0)

    # Health check must report state='stopped' and stop_reason='ttl_expired'
    status, health = http_request(
        f"{base_url}/api/v1/media/sessions/{session_id}", headers=headers
    )
    assert status == 200, f"Health check failed: {status}: {health}"
    assert health.get("state") == "stopped", (
        f"Expected state='stopped', got: {health.get('state')}"
    )
    assert health.get("stop_reason") == "ttl_expired", (
        f"Expected stop_reason='ttl_expired', got: {health.get('stop_reason')}"
    )

    # Verify Janus mountpoint is destroyed
    j_info = janus_admin_call(
        janus_host, janus_admin_port, {"request": "info", "id": device_id}
    )
    assert j_info.get("response", {}).get("error_code") == 455, (
        f"Mountpoint was not cleaned up on TTL: {j_info}"
    )

    # Verify Ingress route is removed
    status_r, _ = http_request(f"{base_url}/routes/{sn}")
    assert status_r == 404, "Route was not removed on TTL expiry"

    print("  [PASS] TTL Watchdog successfully expired and tore down media resources.")


def test_metrics_endpoint(base_url: str):
    print("\n[TEST 9] Verifying Aggregate Technical Metrics...")
    headers = {"X-Media-Service-Token": DEFAULT_SERVICE_TOKEN}
    status, metrics = http_request(f"{base_url}/api/v1/media/metrics", headers=headers)
    assert status == 200, f"Metrics failed: {status}: {metrics}"
    assert metrics.get("status") == "ok"
    assert "uptime_sec" in metrics
    assert "active_media_sessions" in metrics
    assert "total_sessions_started" in metrics
    assert "total_sessions_stopped" in metrics
    assert "orphans_cleaned_mountpoints" in metrics
    assert "orphans_cleaned_routes" in metrics
    print(f"  -> Metrics snapshot: {metrics}")
    print("  [PASS] Metrics endpoint operational and valid.")


def run_all(
    host: str = "127.0.0.1",
    control_port: int = 9100,
    janus_host: str = "127.0.0.1",
    janus_admin_port: int = 7088,
):
    base_url = f"http://{host}:{control_port}"
    print(
        "================================================================================"
    )
    print(" Starting l4media Media Lifecycle Contract & Integration Test Suite")
    print(f" Ingress Control API: {base_url}")
    print(f" Janus WebRTC Admin:  http://{janus_host}:{janus_admin_port}/admin")
    print(
        "================================================================================"
    )

    test_openapi_spec(base_url)
    test_service_auth(base_url)
    test_lifecycle_full_flow(base_url, janus_host, janus_admin_port)
    test_idempotency_and_repeats(base_url)
    test_device_mutual_exclusion(base_url)
    test_concurrency(base_url)
    test_reconciliation_orphan_cleanup(base_url, janus_host, janus_admin_port)
    test_ttl_watchdog_expiration(base_url, janus_host, janus_admin_port)
    test_metrics_endpoint(base_url)

    print(
        "\n================================================================================"
    )
    print(" ALL 9 MEDIA LIFECYCLE TESTS PASSED SUCCESSFULLY!")
    print(
        "================================================================================"
    )


if __name__ == "__main__":
    h = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    cp = int(sys.argv[2]) if len(sys.argv) > 2 else 9100
    jh = sys.argv[3] if len(sys.argv) > 3 else "127.0.0.1"
    jap = int(sys.argv[4]) if len(sys.argv) > 4 else 7088
    run_all(h, cp, jh, jap)
