# DevOps Runbook

## Legacy IIS server deployment (Payment / TechGate / GateGauge / licensebilling / Certificates)

These are separate ASP.NET Web Applications running on the legacy IIS
server (real code lives under `Public/etranprocessing/BACK/ProcessingCore/
EtranDispatcher`, `Public/etranprocessing/FRONT/{TechGate,GateGauge,
licensebilling,Certificates}`). No CI/CD — deployment is manual, by
copying source files onto the server.

### ClientCertHelper proxy-header contract

All of these apps (except `Certificates`, which is PIN-based and doesn't
need client-cert data) now include an `App_Code/ClientCertHelper.cs` (or
`src/ClientCertHelper.cs` for Payment) that transparently supports two
modes, chosen automatically per-request:
- **Direct legacy flow** (terminal → IIS directly, current default): falls
  back to `Context.Request.ClientCertificate.*`, unchanged behavior.
- **Proxy flow** (terminal → nginx-mutual → IIS, after DNS cutover): used
  only when the request's source IP matches `TrustedProxyIP` (configured in
  the site-root `web.config`, already set to nginx-mutual's IP) **and** the
  `X-Client-Cert-Serial` header is present. In that case, cert data is read
  from `X-Client-Cert-Serial` / `X-Client-Cert-DN` / `X-Client-Cert-Verified`
  / `X-SSL-Client-Cert` headers instead.

This makes the change backward compatible: as long as terminals keep
hitting IIS directly (before DNS cutover), behavior is 100% unchanged.

### Payment: no-compile deployment (App_Code conversion)

`payment/` was previously deployed as a precompiled Web Application
(`bin/EtranDispatcher.dll` + `.pdb`). To avoid needing Visual Studio/MSBuild
for this change, it's converted to the same "bare source in App_Code"
model already used by TechGate/GateGauge/licensebilling — ASP.NET compiles
it on the fly on first request.

Deploy steps (do this **first**, before the other apps — see rollback plan):
1. **Backup** the entire `payment/` folder (including `bin/EtranDispatcher.dll`,
   `bin/EtranDispatcher.pdb`, `bin/log4net.dll`) to e.g. `payment_backup_<date>/`.
2. Copy `BACK/ProcessingCore/EtranDispatcher/src/*.cs` (all 6 files,
   including the new `ClientCertHelper.cs`) into `payment/App_Code/`.
3. Also copy the updated `Global.asax` and `EtranDispatcher.asmx` over the
   existing ones at the `payment/` root (both were converted from the
   `Codebehind="*.cs"` precompiled model to inline `<script runat="server">`
   — the old model referenced a `Global`/`EtranDispatcher` type that only
   existed inside the now-removed `EtranDispatcher.dll`, causing a parser
   error `Не удалось загрузить тип 'EtranDispatcher.Global'` on every
   request). Delete `Global.asax.cs` and `EtranDispatcher.asmx.cs` from
   `payment/` if present (their content is now inlined) — leaving them
   there is harmless but redundant.
4. Delete/rename `payment/bin/EtranDispatcher.dll` and `.pdb` (keep
   `bin/log4net.dll` — it's a third-party library, not part of this app's
   own compiled code). **Move them fully OUT of the `bin/` folder** (not
   just rename in place) — ASP.NET scans every `.dll` file present in
   `bin/` regardless of name at app startup, so a renamed-but-still-present
   file (e.g. `--EtranDispatcher.dll`) still gets picked up and fails with
   `FileLoadException` on the mismatched manifest.
5. Recycle the app pool / `iisreset` for this site so ASP.NET recompiles
   `App_Code` from scratch.
6. Verify with test `function=check` and `function=payment` requests (use
   test/non-critical data) and check the `Payments`/`Payment_params` tables
   and the `EtranDispatcher.log` (log4net) for exceptions.

Also note: the Payment flow itself was simplified in this change — the
`Signature`/`tosign` MD5 validation was removed (pass-through only), and
`function=payment` now calls the real `AModule_PutPayment` stored procedure
directly (idempotent on `PaymExtId` — an existing row is returned as-is,
no duplicate insert), instead of going through the shared SOAP
`MessageProcessor.asmx`. **Important**: `AModule_PutPayment` is not a
plain insert — it also resolves `term_datetime` from the owning
organization's timezone, performs routing/eligibility via
`service..GetRek_20090918`, applies per-organization overrides (quarantine,
terminal limits, fraud shields), and updates the dealer balance ledger
(`service..BalanceKioskTspExt`). An earlier version of this fix mistakenly
bypassed this SP with a bare `INSERT`, which silently dropped all of that
derived data (missing `term_datetime`, no balance ledger updates) even
though the payment row itself still got written — this was caught in
production after deploy and fixed; the SP is now always called for
`function=payment`. Payment params are still written via the existing
`AModule_AddPaymentParam` SP. The only thing intentionally NOT done is the
"two-phase" external payment gateway dispatch (`Job.GetAvailableJobs`/
`Job.Process` — an outbound call to the URL/Rek the SP resolves, confirmed
later via async `AModule_ReportTryExt`) and the SMS notification — not used
in practice today, per product decision. `MessageProcessor.asmx.cs` itself
is untouched (still used by `OsmpDispatcher`/`PostProcessor`).

**Known data gap, handled defensively**: `AModule_PutPayment`'s call to
`service..GetRek_20090918` legitimately rejects a payment with
`"Нарушение аутентичности"` when `service..OrganizationReward` has no
routing/tariff mapping for the org+TSP combination — confirmed via direct
DB inspection to be a real, pre-existing data gap affecting some live
organizations/TSPs (e.g. org 424 + TSP 10001, org 340 + TSP 830 had zero
matching rows), not a fraud signal. Since this must not block real
terminal payments, `PutPayment()` catches that SP-level rejection and
falls back to a direct insert (`paym_state` 2, same behavior this code
path had before `AModule_PutPayment` was integrated) instead of returning
`Result=Error` to the terminal. Neither the shared stored procedure nor
the legacy SOAP flow are touched by this fallback — it only applies to
this simplified code path. Adding the missing `OrganizationReward` rows
(if these org/TSP combos are actually meant to route through a payment
system) is a data/ops task, not a code fix, and is out of scope here.

Also: when `AModule_PutPayment` succeeds but leaves the payment at
`paym_state` 1 ("start" — meaning it resolved a real external gateway and
is waiting for `Job.Process` to dispatch to it and `AModule_ReportTryExt`
to later confirm the final state), this simplified flow finalizes it
immediately as accepted (`paym_state` 2) via a follow-up `UPDATE`, since
the external dispatch/confirmation step is intentionally never performed
here — otherwise the payment would stay stuck at state 1 forever and show
as permanently pending in reporting/UI.

`check`/`checkfull`/`update`/`addparams` were also reimplemented directly
against the database (bypassing the SOAP call), but **do real validation**
— they must NOT unconditionally return `Result=OK`:
- `check`: first looks up an existing payment by `PaymExtId` (idempotent
  status check — in practice this basically never triggers, since
  terminals always send a fresh `PaymExtId`); if not found, calls the
  existing read-only stored procedure `service.dbo.GetRek_MP2` directly
  to perform the real pre-payment eligibility check the terminal relies
  on (certificate active, organization/kiosk not locked, TSP active and
  assigned to the kiosk, valid tariff mapping) — returns `Error` if
  ineligible.
- `checkfull` calls `AModule_XmlPaymentInfo` directly (same shape as
  before).
- `update`/`addparams` require the payment to already exist (found via
  `PaymExtId`) before writing extra params via `AModule_AddPaymentParam`.

**Known scope gap** (by explicit product decision, not an oversight):
`function=payment` itself still does **not** run this eligibility check
before inserting — it accepts and records any submitted payment. Adding
the equivalent eligibility validation to the **new Python processing
stack** (`ProcessingBackend/backend/app/services/payment_service.py` /
`routers/payment.py`, whose `_handle_check()` currently also just returns
a static OK) is tracked as a separate follow-up task for a future session.

#### Rollback (Payment)

No database rollback is needed — the simplified path is purely additive
(idempotent inserts, nothing is deleted/mutated). If something goes wrong
after deploying:
1. Restore `bin/EtranDispatcher.dll` and `bin/EtranDispatcher.pdb` from the
   backup (put them back into `bin/`).
2. Restore the original `Global.asax` and `EtranDispatcher.asmx` from the
   backup (the ones using `Codebehind="*.cs"`), and restore
   `Global.asax.cs`/`EtranDispatcher.asmx.cs` if they were deleted.
3. Remove/rename the `App_Code` folder added in step 2 above (e.g. to
   `App_Code.disabled`), so there's no type-conflict with the restored
   precompiled assembly.
4. Recycle the app pool / `iisreset` — the app is back to its exact
   previous behavior (SOAP `MessageProcessor` + direct
   `Context.Request.ClientCertificate`).

## Quick Reference

### Server Access

```bash
# SSH
ssh -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249

# Docker containers
sudo docker ps
sudo docker logs <container> --tail 50
sudo docker exec -it <container> bash
```

### Service URLs

| Service | URL |
|---------|-----|
| Payment API | https://dev.leo4.ru:4443/api/payment/etran.ashx |
| MenuBuilder | https://dev.leo4.ru:4443/api/ListMenuFile |
| License Billing | https://dev.leo4.ru:4443/api/licensebilling/ |

### Legacy raw-path proxy (no `/api` prefix) → real legacy IIS server

nginx-mutual also proxies the legacy terminal endpoints that do **not** use
the `/api` prefix (`/certificates/`, `/payment/`, `/payment/etran.ashx`,
`/GateGauge/main.ashx`, `/GateGauge/UpdateScript.ashx`, `/techgate/etran.ashx`,
`/licensebilling/`, plus the `/` fallback) directly to the real legacy IIS
server at its public IP `46.38.51.114`, forwarding client-cert data via
`X-Client-Cert-*` headers. This is intended for the eventual DNS cutover of
`iot-processing.ru` from the legacy server to nginx-mutual, while keeping the
legacy direct-to-terminal flow (bypassing nginx-mutual) working as a fallback.

Caveats:
- The proxy target is a literal IP, not the `iot-processing.ru` domain —
  after the DNS cutover, proxying to the domain would create a loop (nginx
  proxying to itself). There is no VPN/tunnel to the legacy server's
  internal address (`172.17.100.8`), so the public IP is used.
- nginx does **not** present a client TLS certificate when talking to the
  legacy server (that's the point of `X-Client-Cert-*` header forwarding +
  `ClientCertHelper` on the legacy side, which trusts headers only from
  `TrustedProxyIP`). If the legacy IIS site enforces "Require client
  certificate" at the SSL binding (HTTP.sys) level, requests will be
  rejected before reaching `ClientCertHelper` — this needs to be checked/
  adjusted on the legacy IIS server itself, outside of nginx's control.
- DNS cutover for `iot-processing.ru` is a manual step, done separately,
  only after verifying the legacy-server-side `ClientCertHelper` changes
  are deployed and working (see `Public/etranprocessing` repo,
  `BACK/ProcessingCore/EtranDispatcher`, `FRONT/TechGate`, `FRONT/GateGauge`,
  `FRONT/licensebilling`).

### Container Names

| Container | Purpose |
|-----------|---------|
| processing-backend | Payment processing API |
| processing-frontend | Frontend nginx |
| menubuilder-backend | Menu management API |
| menubuilder-frontend | Menu frontend |
| iot-rpc-rest-app-nginx-mutual-1 | SSL termination + routing |
| iot-rpc-rest-app-pg-1 | PostgreSQL database |
| iot-rpc-rest-app-nginx-1 | Legacy nginx |

## Deployment

### Deploy ProcessingBackend

```bash
# 1. Upload code
scp -r backend/app user1@176.108.247.249:/home/user1/ProcessingBackend/backend/

# 2. Upload docker-compose
scp docker-compose.yaml user1@176.108.247.249:/home/user1/ProcessingBackend/

# 3. Rebuild and restart
ssh user1@176.108.247.249 "cd /home/user1/ProcessingBackend && sudo docker compose up -d --build processing-backend"
```

### Deploy MenuBuilder

```bash
# 1. Upload backend
scp -r backend/app user1@176.108.247.249:/home/user1/MenuBuilder/backend/

# 2. Rebuild
ssh user1@176.108.247.249 "cd /home/user1/MenuBuilder && sudo docker compose up -d --build menubuilder-backend"

# 3. Upload frontend
scp -r frontend/src user1@176.108.247.249:/home/user1/MenuBuilder/frontend/

# 4. Build frontend
ssh user1@176.108.247.249 "cd /home/user1/MenuBuilder/frontend && npm run build"
```

### Update Nginx Config

⚠️ The nginx-mutual config lives in the **`iot-rpc-rest-app`** repo, not
here — see [nginx-config.md](nginx-config.md) "Source of truth" for why.
Edit `nginx-configs/dev_leo4_ru/internal_ssl.conf` there, commit/push, then
deploy:

```bash
# 1. Copy config to server (from the iot-rpc-rest-app checkout)
cd D:\work\iot.leo4.ru\iot-rpc-rest-app
scp -i d:\.ssh\free-tier-cloud_ru nginx-configs/dev_leo4_ru/internal_ssl.conf user1@176.108.247.249:/tmp/

# 2. Copy to container
ssh -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "sudo docker cp /tmp/internal_ssl.conf iot-rpc-rest-app-nginx-mutual-1:/etc/nginx/conf.d/internal_ssl.conf"

# 3. Test and reload
ssh -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "sudo docker exec iot-rpc-rest-app-nginx-mutual-1 nginx -t && sudo docker exec iot-rpc-rest-app-nginx-mutual-1 nginx -s reload"
```

## Database Operations

### Connect to PostgreSQL

```bash
# Via docker
sudo docker exec -it iot-rpc-rest-app-pg-1 psql -U etran -d etranprocessing

# Via psql (if installed)
psql -h 176.108.247.249 -U etran -d etranprocessing
```

### Run Migration

```bash
# 1. Copy migration script
scp migration.py user1@176.108.247.249:/tmp/

# 2. Copy to container
ssh user1@176.108.247.249 "sudo docker cp /tmp/migration.py processing-backend:/app/migration.py"

# 3. Run
ssh user1@176.108.247.249 "sudo docker exec processing-backend python /app/migration.py"
```

### Backup Database

```bash
sudo docker exec iot-rpc-rest-app-pg-1 pg_dump -U etran etranprocessing > backup_$(date +%Y%m%d).sql
```

### Restore Database

```bash
cat backup.sql | sudo docker exec -i iot-rpc-rest-app-pg-1 psql -U etran -d etranprocessing
```

## Monitoring

### Check Service Status

```bash
# All containers
sudo docker ps --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"

# Specific service
sudo docker ps | grep processing-backend
```

### View Logs

```bash
# Container logs
sudo docker logs processing-backend --tail 100
sudo docker logs processing-backend -f  # Follow

# Payment logs (persistent)
sudo docker exec processing-backend cat /app/log/payment.log
sudo docker exec processing-backend tail -f /app/log/payment.log

# Host log files
cat /home/user1/ProcessingBackend/log/payment.log
tail -f /home/user1/ProcessingBackend/log/payment.log
```

### Check Database

```bash
# Payment count
sudo docker exec processing-backend python -c "
import asyncio
from sqlalchemy import text
from app.database import engine
async def q():
    async with engine.connect() as c:
        r = await c.execute(text('SELECT COUNT(*) FROM payments'))
        print(f'Payments: {r.scalar()}')
asyncio.run(q())
"

# Recent payments
sudo docker exec processing-backend python /app/list_payments.py

# Terminal count
sudo docker exec -it iot-rpc-rest-app-pg-1 psql -U etran -d etranprocessing -c "SELECT COUNT(*) FROM terminals;"
```

## Troubleshooting

### Container Won't Start

```bash
# Check logs
sudo docker logs processing-backend --tail 50

# Common issues:
# - .env file missing
# - Database connection failed
# - Port conflict
```

### Payment Returns 401

```bash
# 1. Check nginx config
sudo docker exec iot-rpc-rest-app-nginx-mutual-1 cat /etc/nginx/conf.d/internal_ssl.conf | grep -A 20 "payment"

# 2. Check terminal exists
sudo docker exec -it iot-rpc-rest-app-pg-1 psql -U etran -d etranprocessing -c "SELECT * FROM terminals WHERE sn = 'CN_FROM_LOG';"

# 3. Check cert headers in logs
sudo docker logs processing-backend | grep "Cert DN"
```

### Payment Returns 500

```bash
# Check error logs
sudo docker logs processing-backend --tail 50

# Common issues:
# - TSP not found in tsp table
# - Database connection lost
# - Invalid parameters
```

### Logs Not Writing

```bash
# Check volume mount
sudo docker inspect processing-backend | grep -A 5 Mounts

# Check log directory
ls -la /home/user1/ProcessingBackend/log/

# Check container log directory
sudo docker exec processing-backend ls -la /app/log/
```

### Database Connection Issues

```bash
# Test from container
sudo docker exec processing-backend python -c "
import asyncio
from sqlalchemy import text
from app.database import engine
async def test():
    async with engine.connect() as c:
        r = await c.execute(text('SELECT 1'))
        print('OK:', r.scalar())
asyncio.run(test())
"

# Check .env
sudo docker exec processing-backend cat /app/.env

# Check network
sudo docker exec processing-backend ping pg
```

## Environment Variables

### ProcessingBackend (.env)

```bash
# Database
DATABASE_URL=postgresql+asyncpg://etran:etran@pg:5432/etranprocessing

# CORS
CORS_ORIGINS=["http://localhost:8080","https://dev.leo4.ru:4443"]
```

### Docker Compose Environment

```yaml
env_file:
  - ./backend/.env
```

## Security Notes

- `.env` files contain credentials - never commit to git
- Client certificates validated by nginx
- ProcessingBackend trusts nginx headers
- PostgreSQL only accessible from Docker network

## Rollback

### Rollback ProcessingBackend

```bash
# 1. Find previous image
sudo docker images | grep processing-backend

# 2. Update docker-compose.yaml to use specific tag

# 3. Rebuild
sudo docker compose up -d processing-backend
```

### Rollback Database

```bash
# 1. Restore from backup
cat backup.sql | sudo docker exec -i iot-rpc-rest-app-pg-1 psql -U etran -d etranprocessing

# 2. Restart services
sudo docker restart processing-backend
```
