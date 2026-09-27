#!/usr/bin/env bash
# Isolated synthetic test: no published ports, production data or credentials.
set -euo pipefail
umask 077
ingress=${1:?Ingress image required}
janus=${2:?Janus image required}
root=$(cd "$(dirname "$0")/.." && pwd)
runner=python:3.14-slim@sha256:51dafde81dbdb6ebde285137a295cf18a47ca95234fe388a343719cb97305b3d
read -r -a docker_cmd <<< "${DOCKER_COMMAND:-sudo docker}"
prefix="l4media-smoke-$$"
temp=$(mktemp -d)
cleanup() {
    "${docker_cmd[@]}" rm -f "$prefix-ingress" "$prefix-janus" "$prefix-reject" >/dev/null 2>&1 || true
    "${docker_cmd[@]}" network rm "$prefix" >/dev/null 2>&1 || true
    rm -f "$temp/synthetic.env"
    rmdir "$temp"
}
trap cleanup EXIT
for image in "$ingress" "$janus"; do
    result=0
    timeout 10 "${docker_cmd[@]}" run --rm --name "$prefix-reject" --network none "$image" || result=$?
    [[ "$result" == 1 ]] || { echo 'Missing credential must exit 1' >&2; exit 1; }
done
python3 - "$temp/synthetic.env" <<'PY'
import pathlib, secrets, sys
pathlib.Path(sys.argv[1]).write_text(
    'L4MEDIA_SERVICE_TOKEN=' + secrets.token_hex(32) + '\n'
    'JANUS_ADMIN_SECRET=' + secrets.token_hex(32) + '\n')
PY
"${docker_cmd[@]}" network create --internal "$prefix" >/dev/null
"${docker_cmd[@]}" run -d --name "$prefix-janus" --network "$prefix" --network-alias janus \
    --env-file "$temp/synthetic.env" \
    -v "$root/janus/janus.jcfg:/usr/local/etc/janus/janus.jcfg:ro" \
    -v "$root/janus/janus.plugin.streaming.jcfg:/usr/local/etc/janus/janus.plugin.streaming.jcfg:ro" \
    -v "$root/janus/janus.transport.http.jcfg:/usr/local/etc/janus/janus.transport.http.jcfg:ro" \
    "$janus" >/dev/null
"${docker_cmd[@]}" run -d --name "$prefix-ingress" --network "$prefix" --network-alias ingress \
    --env-file "$temp/synthetic.env" -e JANUS_HOST=janus "$ingress" >/dev/null
"${docker_cmd[@]}" run --rm --network "$prefix" "$runner" python -c '
import socket,time
for host,port in [("janus",7088),("ingress",9100)]:
 for attempt in range(30):
  try:
   with socket.create_connection((host,port),timeout=1): break
  except OSError: time.sleep(1)
 else: raise SystemExit("Service not ready: " + host)
'
"${docker_cmd[@]}" run --rm --network "$prefix" --env-file "$temp/synthetic.env" \
    -v "$root/ingress/tests:/tests:ro" "$runner" \
    python /tests/test_media_lifecycle.py ingress 9100 janus 7088
"${docker_cmd[@]}" run --rm --network "$prefix" \
    -v "$root/ingress/tests:/tests:ro" "$runner" \
    python /tests/test_ingress_regression.py ingress 9000 9100
