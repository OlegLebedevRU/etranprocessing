FROM nginx:stable-alpine@sha256:862dc06c359bfe5d3211e4106269f040d261e269e58ebf17060d8328c45067c0

# Remove default http configurations and create directories
RUN apk add --no-cache openssl \
    && rm -rf /etc/nginx/conf.d/* && mkdir -p /etc/nginx/stream.d /crt

# Use custom nginx.conf with stream block
COPY nginx.conf /etc/nginx/nginx.conf
COPY stream.conf /etc/nginx/stream.d/stream.conf
COPY --chmod=0755 verify-media-certificate.sh /docker-entrypoint.d/40-verify-media-certificate.sh
COPY tests/test-media-certificate.sh /tmp/test-media-certificate.sh
RUN sh /tmp/test-media-certificate.sh && rm /tmp/test-media-certificate.sh

EXPOSE 8443

CMD ["nginx", "-g", "daemon off;"]
