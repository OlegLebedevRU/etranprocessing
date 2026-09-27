#!/bin/sh
set -eu
umask 077
secret=${JANUS_ADMIN_SECRET:-}
case "$secret" in
  ''|*[!a-zA-Z0-9_-]*) echo 'Private JANUS_ADMIN_SECRET must be configured with safe ASCII characters' >&2; exit 1 ;;
esac
if [ "${#secret}" -lt 32 ] || [ "${#secret}" -gt 127 ]; then
  echo 'Private JANUS_ADMIN_SECRET must contain 32..127 characters' >&2
  exit 1
fi
mkdir -p /run/l4media-janus
cp -R /usr/local/etc/janus/. /run/l4media-janus/
template=/usr/local/etc/janus/janus.jcfg
if ! grep -q '@JANUS_ADMIN_SECRET@' "$template"; then
  echo 'Janus configuration must contain the private-secret placeholder' >&2
  exit 1
fi
awk '{gsub(/@JANUS_ADMIN_SECRET@/, ENVIRON["JANUS_ADMIN_SECRET"]); print}' \
  "$template" > /run/l4media-janus/janus.jcfg
chmod 600 /run/l4media-janus/janus.jcfg
exec "$@"
