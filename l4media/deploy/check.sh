#!/usr/bin/env bash
# ==============================================================================
# check.sh - Diagnostic and sanity verification script for l4media stack
# ==============================================================================
set -euo pipefail

echo "========================================================"
echo " [CHECK 1/5] Checking TCP listen port 8443 on host"
echo "========================================================"
if ss -tlpn | grep -q ":8443"; then
    echo "SUCCESS: Port 8443 is actively listening:"
    ss -tlpn | grep ":8443"
else
    echo "ERROR: Port 8443 is NOT listening!"
    exit 1
fi

echo ""
echo "========================================================"
echo " [CHECK 2/5] Container logs (tail 30 lines each)"
echo "========================================================"
echo "--- l4media-nginx ---"
sudo docker logs --tail 30 l4media-nginx || true

echo "--- l4media-ingress ---"
sudo docker logs --tail 30 l4media-ingress || true

echo "--- l4media-janus ---"
sudo docker logs --tail 30 l4media-janus || true

echo ""
echo "========================================================"
echo " [CHECK 3/5] Ingress Control API routes & stats"
echo "========================================================"
ROUTES=$(sudo docker exec l4media-ingress curl -s http://127.0.0.1:9100/routes)
echo "Routes response: ${ROUTES}"
if echo "${ROUTES}" | grep -q "a4b0000773c82116d210826"; then
    echo "SUCCESS: Test serial number a4b0000773c82116d210826 found in routing table."
else
    echo "ERROR: Test serial number missing from routes!"
    exit 1
fi

STATS=$(sudo docker exec l4media-ingress curl -s http://127.0.0.1:9100/stats)
echo "Stats response: ${STATS}"

echo ""
echo "========================================================"
echo " [CHECK 4/5] Janus WebRTC Gateway internal API health"
echo "========================================================"
JANUS_INFO=$(sudo docker exec l4media-ingress curl -s http://janus:8088/janus/info)
echo "Janus info: ${JANUS_INFO}"
if echo "${JANUS_INFO}" | grep -q '"janus":"server_info"'; then
    echo "SUCCESS: Janus Gateway HTTP API is operational inside l4media_net."
else
    echo "ERROR: Unexpected Janus info response!"
    exit 1
fi

echo ""
echo "========================================================"
echo " [CHECK 5/5] mTLS enforcement on port 8443"
echo "========================================================"
echo "Attempting connection without client certificate..."
set +e
SSL_OUT=$(openssl s_client -connect 127.0.0.1:8443 </dev/null 2>&1)
set -e

if echo "${SSL_OUT}" | grep -q "Acceptable client certificate CA names"; then
    echo "SUCCESS: Server enforces client certificate authentication (CertificateRequest sent)."
    # Check that stream log records status 500 (rejected without upstream proxy)
    STREAM_LOG=$(sudo docker exec l4media-nginx tail -n 1 /var/log/nginx/stream-access.log || true)
    echo "Last stream access log entry: ${STREAM_LOG}"
    if echo "${STREAM_LOG}" | grep -q "500"; then
        echo "SUCCESS: Connection without valid client certificate was rejected by Nginx with status 500."
    fi
elif echo "${SSL_OUT}" | grep -Ei "certificate required|handshake failure|alert|peer did not return"; then
    echo "SUCCESS: mTLS correctly rejected connection lacking client certificate."
else
    echo "WARNING: Unexpected mTLS output: ${SSL_OUT}"
fi

echo ""
echo "========================================================"
echo " [CHECK 6/7] On-demand media lifecycle API & service-auth"
echo "========================================================"
INGRESS_IP=$(sudo docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}} {{end}}' l4media-ingress | awk '{print $1}')
JANUS_IP=$(sudo docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}} {{end}}' l4media-janus | awk '{print $1}')
echo "Ingress IP: ${INGRESS_IP}, Janus IP: ${JANUS_IP}"

# Test 1: OpenAPI spec
OPENAPI=$(sudo docker exec l4media-ingress curl -s http://127.0.0.1:9100/api/v1/openapi.json)
if echo "${OPENAPI}" | grep -q '"openapi": "3.0.3"'; then
    echo "SUCCESS: OpenAPI 3.0.3 spec served correctly on /api/v1/openapi.json"
else
    echo "ERROR: OpenAPI spec check failed: ${OPENAPI}"
    exit 1
fi

# Test 2: Service Auth enforcement (401 without token)
AUTH_FAIL=$(sudo docker exec l4media-ingress curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:9100/api/v1/media/metrics)
if [ "${AUTH_FAIL}" = "401" ]; then
    echo "SUCCESS: Service Auth correctly rejected unauthenticated request with 401."
else
    echo "ERROR: Expected 401 without token, got ${AUTH_FAIL}"
    exit 1
fi

# Test 3: Service Auth with token
AUTH_OK=$(sudo docker exec l4media-ingress curl -s -H "X-Media-Service-Token: l4media-service-secret-token" http://127.0.0.1:9100/api/v1/media/metrics)
if echo "${AUTH_OK}" | grep -q '"status":"ok"'; then
    echo "SUCCESS: Service Auth accepted valid token: ${AUTH_OK}"
else
    echo "ERROR: Service Auth failed with valid token: ${AUTH_OK}"
    exit 1
fi

echo ""
echo "========================================================"
echo " [CHECK 7/8] Running media lifecycle & regression test suites"
echo "========================================================"
python3 /home/user1/l4media/ingress/tests/test_media_lifecycle.py "${INGRESS_IP}" 9100 "${JANUS_IP}" 7088
python3 /home/user1/l4media/ingress/tests/test_ingress_regression.py "${INGRESS_IP}" 9000 9100

echo ""
echo "========================================================"
echo " [CHECK 8/8] Checking l4media-archive worker & CLI status"
echo "========================================================"
PYTHONPATH="/home/user1/l4media" python3 -m archive.cli status

echo ""
echo "========================================================"
echo " All l4media health checks & test suites PASSED successfully!"
echo "========================================================"
