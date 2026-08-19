# nginx-mutual-legacy (standalone `iot-processing.ru` host)

Reverse proxy for the DNS cutover of the legacy terminal domain
`iot-processing.ru`, running on a **dedicated host** (currently `87.242.100.34`,
Ubuntu 24.04) - fully independent from the `iot-rpc-rest-app` repo/CI/image
build pipeline and from the original `nginx-mutual` instance on
`176.108.247.249` (`dev.leo4.ru:4443`, used for `/api/*` new-stack testing).

## Why a separate host and a separate config source-of-truth

Port 443 on the original nginx-mutual VM (`176.108.247.249`) is already owned
by the `nginx-jwt` service (`dev.leo4.ru`). Since Docker cannot bind two
containers to the same host port, and DNS cutover for `iot-processing.ru`
needs port 443, this runs on its own host instead. It reuses the **already
published** `nginx-mutual` Docker image from `iot-rpc-rest-app`
(`ghcr.io/oleglebedevru/iot-rpc-rest-app/nginx-mutual:sha-f78556a` - public,
no login needed) via a plain `docker compose`, with a completely different,
self-contained nginx config **bind-mounted over** the one baked into the
image (see `docker-compose.yml`). No `iot-rpc-rest-app` build/CI step is
involved for this host at all - it is fully autonomous, so the config lives
here in `etranprocessing` (the actual project this migration belongs to),
not in `iot-rpc-rest-app`.

## What this config does NOT do

Unlike the original `dev.leo4.ru:4443` nginx-mutual config, this one carries
**no `/api/*` routes** (those are unrelated to `iot-processing.ru` legacy
terminal traffic and stay on the original host/domain). It only proxies:

- The 5 legacy raw-path terminal endpoints (Certificates/Payment/TechGate/
  GateGauge/licensebilling) directly to the real legacy IIS server's public
  IP (`46.38.51.114`), over plain HTTP (see "Legacy server SSL is disabled"
  below).
- `/api/ListMenuFile` - also proxied directly to the legacy server, to the
  real production consumer: the legacy Windows service
  `PlaterraApiFrontService` (see
  `D:\repo\platerra\PlaterraSoft\Processing\PlaterraApiServices\PlaterraApiFrontService`),
  **not** the new Python MenuBuilder stack. This service authenticates via a
  plain `Authorization: clientCertificate <base64>` header that the terminal
  already sends itself (see `Platerra.Terminal.BLL/Classes/clsMenuCreator.cs`
  and `PlaterraApiFrontService/HostApi/APIKeyAuthenticationMiddleware.cs`),
  so no `X-Client-Cert-*` header forwarding is needed for this route - nginx
  passes the `Authorization`/`hostName` headers through untouched by default.
  `PlaterraApiFrontService` was moved from its original binding
  (`https://+:443/api/`, sharing port 443 with IIS - broken, see below) to
  `http://+:80/api/` (sharing port 80 with IIS instead, the same way the
  other 5 endpoints already do) specifically to make this proxy route work.

## Legacy server SSL is disabled

The legacy IIS server's HTTPS binding(s) are currently non-functional
(TLS handshake resets immediately after ClientHello, confirmed from
multiple source hosts/TLS versions/paths - root cause not fully identified
despite verifying the HTTP.sys sslcert binding, IIS site binding, SCHANNEL
protocol settings, and firewall rules all looking correct). Plain HTTP
(port 80) works reliably, so every `proxy_pass` in `legacy_ssl.conf` targets
`http://46.38.51.114/...`, not `https://`.

## Deployment (already done once, for reference/future redeploys)

```bash
# On the host (87.242.100.34):
mkdir -p /home/user1/nginx-mutual-legacy/crt /home/user1/nginx-mutual-legacy/nginx-configs
cd /home/user1/nginx-mutual-legacy/crt
openssl req -x509 -nodes -newkey rsa:2048 \
  -keyout iot-processing-ru-selfsigned.key \
  -out iot-processing-ru-selfsigned.crt \
  -days 3650 -subj '/C=RU/ST=Moscow/L=Moscow/O=Leo4/OU=IT/CN=iot-processing.ru'

# Copy docker-compose.yml and nginx-configs/legacy_ssl.conf (this directory)
# to /home/user1/nginx-mutual-legacy/ on the host, then:
cd /home/user1/nginx-mutual-legacy
sudo docker compose up -d
sudo docker compose exec nginx-mutual nginx -t
```

To redeploy after editing `legacy_ssl.conf`:

```bash
scp nginx-configs/legacy_ssl.conf user1@87.242.100.34:/home/user1/nginx-mutual-legacy/nginx-configs/legacy_ssl.conf
ssh user1@87.242.100.34 "cd /home/user1/nginx-mutual-legacy && sudo docker compose exec nginx-mutual nginx -t && sudo docker compose exec nginx-mutual nginx -s reload"
```

The self-signed certificate is intentional (terminals do not validate it
strictly - a known, accepted factor for this migration, per product
decision). A real Let's Encrypt certificate can be added later if needed.
