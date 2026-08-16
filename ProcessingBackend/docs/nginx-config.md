# Nginx Configuration Reference

## Overview

Nginx serves as the entry point for all terminal requests. It handles:
- SSL termination with mutual TLS (client certificate validation)
- Reverse proxy to backend services
- URL routing based on path prefix

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
| `/*` | iot-processing.ru | Legacy fallback |

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

### Edit on Server

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

### Copy from Local

```bash
# Copy to server
scp -i ~/.ssh/free-tier-cloud_ru nginx.conf user1@176.108.247.249:/tmp/

# Copy to container
sudo docker cp /tmp/nginx.conf iot-rpc-rest-app-nginx-mutual-1:/etc/nginx/conf.d/internal_ssl.conf

# Test and reload
sudo docker exec iot-rpc-rest-app-nginx-mutual-1 nginx -t
sudo docker exec iot-rpc-rest-app-nginx-mutual-1 nginx -s reload
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
