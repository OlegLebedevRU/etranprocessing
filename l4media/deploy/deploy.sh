#!/usr/bin/env bash
# ==============================================================================
# deploy.sh - Idempotent deployment script for l4media stack
# Target host: 87.242.100.34
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
L4MEDIA_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

SERVER_HOST="${SERVER_HOST:-87.242.100.34}"
SERVER_USER="${SERVER_USER:-user1}"
SSH_KEY="${SSH_KEY:-d:/.ssh/id_ed25519}"
REMOTE_DEST="${SERVER_USER}@${SERVER_HOST}:/home/${SERVER_USER}/l4media"

echo "=== [1/6] Synchronizing l4media codebase to ${SERVER_USER}@${SERVER_HOST} ==="

deploy_remote_commands() {
cat << 'EOF'
set -euo pipefail
cd /home/user1/l4media

echo "=== [2/6] Checking / creating .env and server TLS certificate ==="
if [ ! -f .env ]; then
    cp .env.example .env
    echo "Created .env from .env.example"
fi

# Ensure new variables from .env.example are present in .env
for var in JANUS_HOST JANUS_ADMIN_PORT JANUS_ADMIN_SECRET L4MEDIA_SERVICE_TOKEN \
           L4MEDIA_ARCHIVE_WORKER_ENABLED L4MEDIA_ARCHIVE_DRY_RUN L4MEDIA_ARCHIVE_VOLUME_ROOT L4MEDIA_HOT_TELEMETRY_DIR; do
    if ! grep -q "^${var}=" .env; then
        grep "^${var}=" .env.example >> .env || true
        echo "Added ${var} to .env"
    fi
done

echo "=== [3/6] Validating mounted CA-issued dev.leo4.ru certificate ==="
# Paths come from Compose/private .env; never generate self-signed replacements.
# Validate the selected release before stopping the currently running service.
sudo docker compose -p l4media pull
sudo docker compose -p l4media run --rm --no-deps \
    --entrypoint /docker-entrypoint.d/40-verify-media-certificate.sh nginx

echo "=== [4/6] Recording existing Docker containers state ==="
sudo docker ps --format '{{.Names}} {{.Status}}' > /tmp/docker_ps_before_l4media.txt
echo "Active containers before deploy recorded."

echo "=== [5/6] Pulling and starting l4media stack ==="
sudo docker compose -p l4media up -d --no-build

echo "=== [6/6] Verifying service statuses and external container isolation ==="
sudo docker compose -p l4media ps

echo "--- Recent logs: l4media-nginx ---"
sudo docker logs --tail 15 l4media-nginx || true

echo "--- Recent logs: l4media-ingress ---"
sudo docker logs --tail 15 l4media-ingress || true

echo "--- Recent logs: l4media-janus ---"
sudo docker logs --tail 15 l4media-janus || true

echo "--- Verifying unaffected external containers ---"
sudo docker ps --format '{{.Names}} {{.Status}}' > /tmp/docker_ps_after_l4media.txt

python3 - << 'PYEOF'
with open('/tmp/docker_ps_before_l4media.txt') as f:
    before = dict(line.strip().split(' ', 1) for line in f if line.strip())

with open('/tmp/docker_ps_after_l4media.txt') as f:
    after = dict(line.strip().split(' ', 1) for line in f if line.strip())

external_before = {k: v for k, v in before.items() if not k.startswith('l4media')}
external_after = {k: v for k, v in after.items() if not k.startswith('l4media')}

print(f"External containers before: {len(external_before)}, after: {len(external_after)}")
mismatches = []
for name, status in external_before.items():
    if name not in external_after:
        mismatches.append(f"MISSING: {name}")
    else:
        if "Up Less than a second" in external_after[name] or "Up 1 second" in external_after[name]:
            mismatches.append(f"RESTARTED: {name} (was: {status}, now: {external_after[name]})")

if mismatches:
    print("WARNING: external container state changed:")
    for m in mismatches:
        print("  - " + m)
    exit(1)
else:
    print("SUCCESS: ALL external containers remained untouched and running!")
PYEOF
EOF
}

# Check if running locally on remote server or from dev machine
if [[ "$(id -un 2>/dev/null || true)" == "${SERVER_USER}" && -f "/home/${SERVER_USER}/l4media/compose.yaml" ]]; then
    echo "Running directly on remote server."
    deploy_remote_commands | bash
else
    echo "Deploying from remote client via SSH..."
    ssh -n -i "${SSH_KEY}" -o BatchMode=yes "${SERVER_USER}@${SERVER_HOST}" "mkdir -p /home/${SERVER_USER}/l4media/crt"
    scp -i "${SSH_KEY}" -r \
        "${L4MEDIA_DIR}/compose.yaml" \
        "${L4MEDIA_DIR}/pyproject.toml" \
        "${L4MEDIA_DIR}/.env.example" \
        "${L4MEDIA_DIR}/README.md" \
        "${L4MEDIA_DIR}/ARCHITECTURE.md" \
        "${L4MEDIA_DIR}/archive" \
        "${L4MEDIA_DIR}/tests" \
        "${L4MEDIA_DIR}/nginx" \
        "${L4MEDIA_DIR}/ingress" \
        "${L4MEDIA_DIR}/janus" \
        "${L4MEDIA_DIR}/deploy" \
        "${SERVER_USER}@${SERVER_HOST}:/home/${SERVER_USER}/l4media/"
    deploy_remote_commands | ssh -n -i "${SSH_KEY}" -o BatchMode=yes "${SERVER_USER}@${SERVER_HOST}" bash
fi

echo "=== l4media deployment completed successfully! ==="
