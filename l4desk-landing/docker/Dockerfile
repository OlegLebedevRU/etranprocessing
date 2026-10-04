# L4Desk landing — nginx + certbot in a single container
# Build context = repository root (l4desk-landing/)
FROM nginx:1.27-alpine

RUN apk add --no-cache certbot tzdata openssl \
    && mkdir -p /var/www/certbot /etc/nginx/ssl /var/log/letsencrypt

COPY docker/nginx.conf /etc/nginx/nginx.conf
COPY docker/entrypoint.sh /entrypoint.sh
COPY site/ /usr/share/nginx/html/

RUN chmod +x /entrypoint.sh \
    && chown -R nginx:nginx /usr/share/nginx/html /var/www/certbot

ENV DOMAIN=l4desk.ru \
    EMAIL= \
    STAGING=0 \
    TZ=UTC

EXPOSE 80 443

HEALTHCHECK --interval=30s --timeout=5s --start-period=40s --retries=3 \
  CMD wget -q -O /dev/null http://127.0.0.1/healthz || exit 1

ENTRYPOINT ["/entrypoint.sh"]
