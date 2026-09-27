#!/usr/bin/env bash
# Run in the extracted, checksum-verified Git archive on the approved build host.
set -euo pipefail
: "${SOURCE_REVISION:?published Git commit required}"
: "${REGISTRY_PREFIX:?approved private registry namespace required}"
[[ "$SOURCE_REVISION" =~ ^[0-9a-f]{40}$ ]]
[[ "$REGISTRY_PREFIX" != *"@"* && "$REGISTRY_PREFIX" != *"://"* ]]
backend="$REGISTRY_PREFIX/menubuilder-backend:$SOURCE_REVISION"
frontend="$REGISTRY_PREFIX/menubuilder-frontend:$SOURCE_REVISION"
sudo docker build --target verification \
  -f MenuBuilder/backend/Dockerfile \
  --label "org.opencontainers.image.revision=$SOURCE_REVISION" \
  -t "menubuilder-verification:$SOURCE_REVISION" .
sudo docker build --target release \
  -f MenuBuilder/backend/Dockerfile \
  --label "org.opencontainers.image.revision=$SOURCE_REVISION" -t "$backend" .
sudo docker build -f MenuBuilder/frontend/Dockerfile \
  --label "org.opencontainers.image.revision=$SOURCE_REVISION" \
  -t "$frontend" MenuBuilder/frontend
# Runtime images must not include private config, keys or test fixtures.
sudo docker run --rm --entrypoint python "$backend" -c '
from pathlib import Path
root = Path("/workspace")
bad = [str(p) for p in root.rglob("*") if p.is_file() and ".venv" not in p.parts and (p.name.startswith(".env") or p.suffix in {".pem", ".key", ".pfx", ".p12"} or "tests" in p.parts)]
assert not bad, "Unexpected private/test files in runtime image"
print("Backend runtime file scan: passed")
'
sudo docker push "$backend"
sudo docker push "$frontend"
sudo docker image inspect "$backend" "$frontend" \
  --format '{{json .RepoDigests}}'
