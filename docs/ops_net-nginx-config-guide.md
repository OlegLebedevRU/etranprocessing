# Nginx Configuration Reference

## Overview

Nginx serves as the entry point for all terminal requests. It handles:
- SSL termination with mutual TLS (client certificate validation)
- Reverse proxy to backend services
- URL routing based on path prefix

## ⚠️ Source of truth & Architecture

Following the production consolidation to server `87.242.100.34`, Nginx ingress is handled by two dedicated Docker containers operating within the shared `user1_default` bridge network:

1. **Terminal mTLS Ingress (`nginx-mutual-legacy`)**:
   - **Host Port**: `443` (Mutual TLS termination for self-service payment kiosks).
   - **Container**: `nginx-mutual-legacy-nginx-mutual-1`.
   - **Source of truth**: `ProcessingBackend/nginx-mutual-legacy/nginx-configs/legacy_ssl.conf`.
   - **Mounted inside container**: `/etc/nginx/conf.d/internal_ssl.conf:ro`.
   - **Docker Compose**: `ProcessingBackend/nginx-mutual-legacy/docker-compose.yml`.

2. **Web UI, JWT Auth & REST API Gateway (`nginx-default`)**:
   - **Host Ports**: `80` (ACME HTTP-01 challenge & redirect), `3000` (MenuBuilder Web UI & JWT API gateway), `1443`/`1444` (Terem Email mTLS).
   - **Container**: `nginx-default`.
   - **Source of truth**: `nginx-configs/` (`port_80.conf`, `port_3000.conf`, `terem_email_mtls.conf`).
   - **Docker Compose**: `/home/user1/compose.yaml`.

## Container: nginx-mutual-legacy

```
nginx-mutual-legacy-nginx-mutual-1
```

**Image**: `ghcr.io/oleglebedevru/iot-rpc-rest-app/nginx-mutual:sha-f78556a`

**Ports**: 
- `443` (HTTPS with client cert validation)

## Key Configuration Sections

### SSL Settings

```nginx
ssl_protocols TLSv1 TLSv1.1 TLSv1.2 TLSv1.3;
ssl_ciphers DEFAULT:@SECLEVEL=0;
ssl_prefer_server_ciphers on;
listen 4443 ssl;

ssl_certificate /crt/legacy_cert_dev_leo4_ru.crt;
ssl_certificate_key /crt/server_key.pem;

ssl_verify_client optional_no_ca;
```

- `optional_no_ca`: Client cert is optional, no CA verification
- Certificates mounted from host

### Certificate Headers

Nginx forwards client certificate info to backends:

```nginx
proxy_set_header X-SSL-Client-Cert $ssl_client_escaped_cert;
proxy_set_header X-SSL-Client-Verify $ssl_client_verify;
proxy_set_header X-Client-Cert-DN $ssl_client_s_dn;
proxy_set_header X-Client-Cert-Serial $ssl_client_serial;
```

### URL Routing

| Path | Backend | Description |
|------|---------|-------------|
| `/api/payment/*` | processing-backend:8000 | Payment endpoints |
| `/api/techgate/etran.ashx` | processing-backend:8000 | Legacy tech functions |
| `/api/licensebilling/*` | processing-backend:8000 | License billing |
| `/api/gategauge/*` | processing-backend:8000 | Device telemetry |
| `/api/ListMenuFile` | menubuilder-backend:8000 | Menu files |
| `/api/debug/*` | processing-backend:8000 | Debug endpoints |
| `/certificates/`, `/payment/`, `/payment/etran.ashx`, `/GateGauge/main.ashx`, `/GateGauge/UpdateScript.ashx`, `/techgate/etran.ashx`, `/licensebilling/` | `https://46.38.51.114` (legacy IIS server, direct IP) | Legacy raw-path terminal endpoints (no `/api` prefix), proxied to the real legacy server with client-cert data forwarded via `X-Client-Cert-*` headers — see [ops_run-devops-runbook.md](ops_run-devops-runbook.md) for details on why an IP is used instead of the `iot-processing.ru` domain |
| `/*` | `https://46.38.51.114` (legacy IIS server, direct IP) | Legacy fallback |

### Payment Location

```nginx
location /api/payment/ {
    proxy_pass http://processing-backend:8000/api/payment/;
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;
    proxy_set_header X-SSL-Client-Cert $ssl_client_escaped_cert;
    proxy_set_header X-SSL-Client-Verify $ssl_client_verify;
    proxy_set_header X-Client-Cert-DN $ssl_client_s_dn;
    proxy_set_header X-Client-Cert-Serial $ssl_client_serial;
    proxy_buffering off;
}
```

### TechGate Location (Legacy)

```nginx
location ~ ^/api/techgate/etran\.ashx$ {
    proxy_pass http://processing-backend:8000/api/techgate$is_args$args;
    # ... same headers as payment
}
```

Regex matches `/api/techgate/etran.ashx` exactly.

### Double API Prefix Fix

```nginx
location ~ ^/api/api/(.*)$ {
    return 301 /api/$1$is_args$args;
}
```

Handles terminal bug that sends `/api/api/...` instead of `/api/...`.

## Runtime Config Location

```
/etc/nginx/conf.d/internal_ssl.conf
```

## Modifying Configuration

### Terminal mTLS Gateway (`nginx-mutual-legacy`)
Edit `ProcessingBackend/nginx-mutual-legacy/nginx-configs/legacy_ssl.conf`:

```bash
# 1. Upload config to server 87.242.100.34
scp -i d:\.ssh\id_ed25519 ProcessingBackend/nginx-mutual-legacy/nginx-configs/legacy_ssl.conf user1@87.242.100.34:/home/user1/nginx-mutual-legacy/nginx-configs/legacy_ssl.conf

# 2. Test and reload without downtime
ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker exec nginx-mutual-legacy-nginx-mutual-1 nginx -t && sudo docker exec nginx-mutual-legacy-nginx-mutual-1 nginx -s reload"
```

### Web UI & JWT Gateway (`nginx-default`)
Edit configs in `nginx-configs/` (`port_80.conf`, `port_3000.conf`, `terem_email_mtls.conf`):

```bash
# 1. Upload configs to server 87.242.100.34
scp -i d:\.ssh\id_ed25519 -r nginx-configs/* user1@87.242.100.34:/home/user1/nginx-configs/

# 2. Test and reload
ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker exec nginx-default nginx -t && sudo docker exec nginx-default nginx -s reload"
```

## Troubleshooting

### 502 Bad Gateway

- Backend container not running
- Check: `sudo docker ps | grep processing`

### 411 Length Required

- Nginx expects Content-Length header
- Terminal should send proper POST request

### Certificate Not Forwarded

- Check `proxy_set_header` directives
- Verify `ssl_verify_client optional_no_ca` is set

### Wrong Backend Response

- Check location order (first match wins)
- Verify `proxy_pass` URL is correct
