FROM debian:bookworm-slim

# Base packages
RUN <<`
  set -e
  apt-get update
  apt-get upgrade -y
  apt-get install -y \
    less nano \
    libjansson4 libjwt0 \
    curl gnupg2 ca-certificates \
    lsb-release debian-archive-keyring \
    openssl gettext-base
  rm -rf /var/lib/apt/lists/*
`

# Add nginx repo
RUN <<`
  set -e
  curl https://nginx.org/keys/nginx_signing.key | gpg --dearmor > /usr/share/keyrings/nginx-archive-keyring.gpg
  printf "deb [signed-by=/usr/share/keyrings/nginx-archive-keyring.gpg] http://nginx.org/packages/mainline/debian $(lsb_release -cs) nginx\n" > /etc/apt/sources.list.d/nginx.list
  printf "Package: *\nPin: origin nginx.org\nPin: release o=nginx\nPin-Priority: 900\n" > /etc/apt/preferences.d/99nginx
`

# Install nginx
RUN <<`
  set -e
  apt-get update
  apt-get install -y nginx=1.31.1*
  rm -rf /var/lib/apt/lists/*
`

WORKDIR /usr/lib/nginx/modules

# Install pre-built JWT module v2.5.0
RUN curl -Ls https://github.com/TeslaGov/ngx-http-auth-jwt-module/releases/download/2.5.0/ngx-http-auth-jwt-module-2.5.0_libjwt-1.18.4_nginx-1.31.1.tgz | tar -zx > ngx_http_auth_jwt_module_2.5.0.so

# Symlink for libjwt
RUN ln -sf /usr/lib/x86_64-linux-gnu/libjwt.so.0 /usr/lib/x86_64-linux-gnu/libjwt.so.2

WORKDIR /

# Main nginx.conf with JWT module loaded
COPY <<` /etc/nginx/nginx.conf
daemon off;
user  nginx;
worker_processes  auto;

error_log  /dev/stderr notice;
pid        /var/run/nginx.pid;

load_module /usr/lib/nginx/modules/ngx_http_auth_jwt_module_2.5.0.so;

events {
    worker_connections  1024;
}

http {
    include       /etc/nginx/mime.types;
    default_type  application/octet-stream;
    log_format custom '$remote_addr - $remote_user [$time_local] "$request" $status $body_bytes_sent "$http_referer" "$http_user_agent"';

    include /etc/nginx/conf.d/*.conf;
}
`

RUN nginx -t

# Entrypoint: substitutes JWT_SECRET_HEX in nginx config template
COPY nginx/entrypoint.sh /entrypoint-jwt.sh
RUN chmod +x /entrypoint-jwt.sh

EXPOSE 443
ENTRYPOINT ["/entrypoint-jwt.sh"]
CMD ["nginx"]
