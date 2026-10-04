#!/bin/sh
# L4Desk landing — bootstrap nginx + certbot inside one container
set -eu

DOMAIN="${DOMAIN:-l4desk.ru}"
EMAIL="${EMAIL:-}"
STAGING="${STAGING:-0}"
LIVE_DIR="/etc/nginx/ssl/live"
WEBROOT="/var/www/certbot"
CERT_DIR="/etc/letsencrypt/live/${DOMAIN}"
ENABLED_FLAG="/etc/nginx/ssl/enabled"

log() {
  echo "[l4desk-landing] $*"
}

mkdir -p "$LIVE_DIR" "$WEBROOT" /etc/letsencrypt

# 1) Self-signed fallback so nginx can start before ACME succeeds
if [ ! -f "$LIVE_DIR/fullchain.pem" ] || [ ! -f "$LIVE_DIR/privkey.pem" ]; then
  log "generating self-signed bootstrap certificate"
  openssl req -x509 -nodes -newkey rsa:2048 -days 7 \
    -keyout "$LIVE_DIR/privkey.pem" \
    -out "$LIVE_DIR/fullchain.pem" \
    -subj "/CN=${DOMAIN}" \
    -addext "subjectAltName=DNS:${DOMAIN},DNS:www.${DOMAIN}" \
    2>/dev/null || \
  openssl req -x509 -nodes -newkey rsa:2048 -days 7 \
    -keyout "$LIVE_DIR/privkey.pem" \
    -out "$LIVE_DIR/fullchain.pem" \
    -subj "/CN=${DOMAIN}"
fi

# 2) Start nginx (HTTP + HTTPS with current certs)
log "starting nginx"
nginx

# 3) Issue or renew Let's Encrypt certificate via webroot
# Tries apex+www first; falls back to www-only if apex DNS is not on this host.
restart_nginx() {
  # Full stop/start: nginx -s reload may keep serving the previous cert
  # when switching from self-signed bootstrap to Let's Encrypt material.
  nginx -s stop >/dev/null 2>&1 || true
  for _ in 1 2 3 4 5; do
    if ! wget -q -O /dev/null --timeout=1 http://127.0.0.1/healthz 2>/dev/null; then
      break
    fi
    sleep 1
  done
  nginx
}

install_live_certs() {
  src_dir="$1"
  if [ -f "${src_dir}/fullchain.pem" ] && [ -f "${src_dir}/privkey.pem" ]; then
    cp -f "${src_dir}/fullchain.pem" "$LIVE_DIR/fullchain.pem"
    cp -f "${src_dir}/privkey.pem" "$LIVE_DIR/privkey.pem"
    touch "$ENABLED_FLAG"
    restart_nginx
    log "HTTPS certificate installed from ${src_dir}"
    return 0
  fi
  return 1
}

issue_cert() {
  EMAIL_ARGS="--register-unsafely-without-email"
  if [ -n "$EMAIL" ]; then
    EMAIL_ARGS="--email ${EMAIL}"
  fi

  STAGING_ARGS=""
  if [ "$STAGING" = "1" ]; then
    STAGING_ARGS="--staging"
  fi

  for names in "${DOMAIN},www.${DOMAIN}" "www.${DOMAIN}" "${DOMAIN}"; do
    D_ARGS=""
    old_ifs=$IFS
    IFS=','
    for d in $names; do
      D_ARGS="${D_ARGS} -d ${d}"
    done
    IFS=$old_ifs

    log "requesting Let's Encrypt certificate for: ${names}"
    # shellcheck disable=SC2086
    if certbot certonly \
      --webroot -w "$WEBROOT" \
      $D_ARGS \
      --non-interactive --agree-tos \
      $EMAIL_ARGS \
      $STAGING_ARGS \
      --cert-name "${DOMAIN}" \
      --keep-until-expiring \
      --preferred-challenges http-01; then
      if install_live_certs "${CERT_DIR}"; then
        log "certificate ready for: ${names}"
        return 0
      fi
    fi
    log "certbot failed for: ${names}"
  done

  log "all certbot attempts failed — keeping current certificates"
  return 1
}

# If we already have a live LE cert from a mounted volume, restore it
if [ -f "${CERT_DIR}/fullchain.pem" ] && [ -f "${CERT_DIR}/privkey.pem" ]; then
  log "found existing Let's Encrypt material for ${DOMAIN}"
  cp -f "${CERT_DIR}/fullchain.pem" "$LIVE_DIR/fullchain.pem"
  cp -f "${CERT_DIR}/privkey.pem" "$LIVE_DIR/privkey.pem"
  touch "$ENABLED_FLAG"
  restart_nginx
else
  issue_cert || true
fi

# 4) Renewal loop (certbot renew every 12h)
(
  while true; do
    sleep 12h
    log "running certbot renew"
    if certbot renew --webroot -w "$WEBROOT" --non-interactive --quiet; then
      if [ -f "${CERT_DIR}/fullchain.pem" ] && [ -f "${CERT_DIR}/privkey.pem" ]; then
        cp -f "${CERT_DIR}/fullchain.pem" "$LIVE_DIR/fullchain.pem"
        cp -f "${CERT_DIR}/privkey.pem" "$LIVE_DIR/privkey.pem"
        touch "$ENABLED_FLAG"
        restart_nginx
        log "certificates renewed and nginx reloaded"
      fi
    fi
  done
) &

# 5) Keep container alive; nginx may be restarted when certs are installed
log "nginx ready (domain=${DOMAIN})"
while true; do
  if ! wget -q -O /dev/null --timeout=2 http://127.0.0.1/healthz 2>/dev/null; then
    log "nginx healthz failed — starting nginx"
    nginx || true
    sleep 3
  fi
  sleep 30
done
