# Nginx Configuration Reference

## Overview

Nginx serves as the entry point for all terminal requests. It handles:
- SSL termination with mutual TLS (client certificate validation)
- Reverse proxy to backend services
- URL routing based on path prefix

## ⚠️ Source of truth

The **only** source of truth for this config is the separate repository
**`iot-rpc-rest-app`** (https://github.com/OlegLebedevRU/iot-rpc-rest-app,
locally checked out at `D:\work\iot.leo4.ru\iot-rpc-rest-app`), file
`nginx-configs/dev_leo4_ru/internal_ssl.conf`. That repository's Dockerfile
builds the actual `nginx-mutual` image deployed on the server — any copy of
this config kept in the `etranprocessing` repo WILL drift and mislead future
readers/deploys (this happened before: for a long time the running
container had legacy raw-path routes and a config drift compared to what
was committed in `iot-rpc-rest-app`, while stale duplicate copies of the
config sat in this repo under `ProcessingBackend/*.conf`, none of them
matching the real running config or each other). Those stale duplicate
files have been removed from `etranprocessing`. Do **not** recreate
copies of `internal_ssl.conf` here — edit it directly in `iot-rpc-rest-app`
and deploy from there (commit → rebuild `nginx-mutual` image → redeploy;
do not `docker cp` random config edits directly into the running container
without also committing them to `iot-rpc-rest-app`).

## Container

```
iot-rpc-rest-app-nginx-mutual-1
```

**Image**: Custom nginx with SSL configuration

**Ports**: 
- 4443 (HTTPS with client cert)

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
| `/certificates/`, `/payment/`, `/payment/etran.ashx`, `/GateGauge/main.ashx`, `/GateGauge/UpdateScript.ashx`, `/techgate/etran.ashx`, `/licensebilling/` | `https://46.38.51.114` (legacy IIS server, direct IP) | Legacy raw-path terminal endpoints (no `/api` prefix), proxied to the real legacy server with client-cert data forwarded via `X-Client-Cert-*` headers — see [devops-runbook.md](devops-runbook.md) for details on why an IP is used instead of the `iot-processing.ru` domain |
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

**Edit in the `iot-rpc-rest-app` repository, not here** — see "Source of
truth" above. Workflow:

```bash
# 1. Edit the config in its real repo
cd D:\work\iot.leo4.ru\iot-rpc-rest-app
# edit nginx-configs/dev_leo4_ru/internal_ssl.conf

# 2. Commit and push
git add nginx-configs/dev_leo4_ru/internal_ssl.conf
git commit -m "..."
git push

# 3. Deploy: copy to server and reload the running container
#    (until CI/CD rebuilds the nginx-mutual image automatically)
scp -i d:\.ssh\free-tier-cloud_ru nginx-configs/dev_leo4_ru/internal_ssl.conf user1@176.108.247.249:/tmp/
ssh -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "sudo docker cp /tmp/internal_ssl.conf iot-rpc-rest-app-nginx-mutual-1:/etc/nginx/conf.d/internal_ssl.conf && sudo docker exec iot-rpc-rest-app-nginx-mutual-1 nginx -t && sudo docker exec iot-rpc-rest-app-nginx-mutual-1 nginx -s reload"
```

### Quick edit directly on the running container (for emergency hotfixes only)

```bash
# Enter container
sudo docker exec -it iot-rpc-rest-app-nginx-mutual-1 bash

# Edit config
vi /etc/nginx/conf.d/internal_ssl.conf

# Test config
nginx -t

# Reload
nginx -s reload
```

⚠️ If you edit directly on the container, you **must** also copy the same
change back into `iot-rpc-rest-app/nginx-configs/dev_leo4_ru/internal_ssl.conf`
and commit it — otherwise the drift problem described above will recur.

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
