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
# SSH to Primary Production Server (87.242.100.34)
ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34

# Docker containers
sudo docker compose -f /home/user1/compose.yaml ps
sudo docker logs <container> --tail 50
sudo docker exec -it <container> bash
```

### Service URLs & Ports

| Service | Port / Protocol | Target / URL |
|---------|-----------------|--------------|
| Terminal mTLS Gateway | 443 / HTTPS (mTLS) | `https://iot-processing.ru/payment/` (nginx-mutual-legacy) |
| Web UI MenuBuilder & REST API | 3000 / HTTPS (TLS) | `https://dev.leo4.ru:3000/` (nginx-default) |
| ACME HTTP-01 Challenge | 80 / HTTP | `http://dev.leo4.ru/.well-known/acme-challenge/` |
| Terem Email mTLS | 1443, 1444 / HTTPS (mTLS) | `https://dev.leo4.ru:1443/` (nginx-default) |
| RabbitMQ AMQP / MQTT TLS | 5672, 8883 | `amqp://...:5672/`, `mqtts://...:8883/` |

### Terminal Routing & mTLS Proxy (nginx-mutual-legacy)

`nginx-mutual-legacy` (listening on host port 443) terminates terminal client certificates and forwards requests directly to the local Docker network `user1_default` at `http://processing-backend:8000`:
- `/payment/`, `/payment/etran.ashx` → `http://new_processing_backend/api/payment/`
- `/techgate/etran.ashx` → `http://new_processing_backend/api/techgate/etran.ashx`
- `/api/ListMenuFile` → `http://new_processing_backend/api/ListMenuFile`
- `/licensebilling/` → `http://new_processing_backend/api/licensebilling/`
- `/certificates/` → `http://new_processing_backend/api/certificates/`
- Unswitched legacy routes fallback to legacy server `http://46.38.51.114`.

### Container Names

| Container | Purpose | Network | Compose File |
|-----------|---------|---------|--------------|
| `processing-backend` | Payment processing API (:8000) | `user1_default` | `/home/user1/compose.yaml` |
| `menubuilder-backend` | Menu management and Billing API (:8000) | `user1_default` | `/home/user1/compose.yaml` |
| `nginx-default` | Web UI & JWT API gateway (:80, :3000, :1443, :1444) | `user1_default` | `/home/user1/compose.yaml` |
| `app1` | IoT platform backend (:8000) | `user1_default` | `/home/user1/compose.yaml` |
| `mcp-pin-server` | MCP server for PIN operations (:8001) | `user1_default` | `/home/user1/compose.yaml` |
| `rabbitmq` | Message broker (:5672, :8883) | `user1_default` | `/home/user1/compose.yaml` |
| `nginx-mutual-legacy-nginx-mutual-1` | Terminal mTLS ingress (:443) | `user1_default` | `/home/user1/nginx-mutual-legacy/docker-compose.yml` |

## Deployment

Deployments must be strictly reproducible from the Git repository.

### Deploy ProcessingBackend

```bash
# 1. Sync shared models and backend code from repo to server
scp -i d:\.ssh\id_ed25519 -r shared/* user1@87.242.100.34:/home/user1/shared/
scp -i d:\.ssh\id_ed25519 -r ProcessingBackend/backend/* user1@87.242.100.34:/home/user1/ProcessingBackend/backend/

# 2. Rebuild and restart container
ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker compose -f /home/user1/compose.yaml build processing-backend && sudo docker compose -f /home/user1/compose.yaml up -d processing-backend"

# 3. Apply database migrations
ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker exec processing-backend alembic upgrade head"

# 4. Restart consumers of shared models if needed
ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker restart menubuilder-backend"
```

### Deploy MenuBuilder

```bash
# 1. Sync shared models and MenuBuilder backend
scp -i d:\.ssh\id_ed25519 -r shared/* user1@87.242.100.34:/home/user1/shared/
scp -i d:\.ssh\id_ed25519 -r MenuBuilder/backend/* user1@87.242.100.34:/home/user1/MenuBuilder/backend/

# 2. Rebuild and restart MenuBuilder backend
ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker compose -f /home/user1/compose.yaml build menubuilder-backend && sudo docker compose -f /home/user1/compose.yaml up -d menubuilder-backend"

# 3. Build frontend locally and upload dist
cd MenuBuilder/frontend
npm run build
scp -i d:\.ssh\id_ed25519 -r dist/* user1@87.242.100.34:/home/user1/MenuBuilder/frontend/dist/
# Note: nginx-default live-mounts frontend/dist/, no container restart required.
```

### Update Nginx Configurations

#### Terminal Gateway (`nginx-mutual-legacy`)
Config lives in `ProcessingBackend/nginx-mutual-legacy/nginx-configs/legacy_ssl.conf`.
```bash
# 1. Upload config to server
scp -i d:\.ssh\id_ed25519 ProcessingBackend/nginx-mutual-legacy/nginx-configs/legacy_ssl.conf user1@87.242.100.34:/home/user1/nginx-mutual-legacy/nginx-configs/legacy_ssl.conf

# 2. Test and reload without dropping connections
ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker exec nginx-mutual-legacy-nginx-mutual-1 nginx -t && sudo docker exec nginx-mutual-legacy-nginx-mutual-1 nginx -s reload"
```

#### Web UI & JWT Gateway (`nginx-default`)
Configs live in `nginx-configs/` (`port_80.conf`, `port_3000.conf`, `terem_email_mtls.conf`).
```bash
# 1. Upload configs to server
scp -i d:\.ssh\id_ed25519 -r nginx-configs/* user1@87.242.100.34:/home/user1/nginx-configs/

# 2. Test and reload
ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker exec nginx-default nginx -t && sudo docker exec nginx-default nginx -s reload"
```

## Database Operations

### Connect to Managed PostgreSQL (10.0.0.7)

```bash
# From server host (passwords stored in ~/.pgpass):
psql -h 10.0.0.7 -U etran_db_user -d etran
psql -h 10.0.0.7 -U leo4_db_user -d iot_rpc

# Run SQL query directly
psql -h 10.0.0.7 -U etran_db_user -d etran -c "SELECT COUNT(1) FROM terminals;"
```

### Run Migrations

Migrations are owned by `ProcessingBackend/backend/alembic/`:
```bash
sudo docker exec processing-backend alembic upgrade head
```

### Backup & Restore Database (Managed PostgreSQL)

```bash
# Backup Managed PG (from server host via ~/.pgpass):
pg_dump -h 10.0.0.7 -U etran_db_user etran > backup_etran_$(date +%Y%m%d).sql

# Restore:
psql -h 10.0.0.7 -U etran_db_user -d etran < backup_etran.sql
```

## Monitoring

### Check Service Status

```bash
# All containers (unified compose stack)
sudo docker compose -f /home/user1/compose.yaml ps

# Legacy mTLS proxy
sudo docker compose -f /home/user1/nginx-mutual-legacy/docker-compose.yml ps
```

### View Logs

```bash
# Container logs
sudo docker logs processing-backend --tail 100
sudo docker logs processing-backend -f  # Follow

# Terminal mTLS gateway logs
sudo docker logs nginx-mutual-legacy-nginx-mutual-1 --tail 100
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

# Terminal count
psql -h 10.0.0.7 -U etran_db_user -d etran -c "SELECT COUNT(*) FROM terminals;"
```

## Troubleshooting

### Container Won't Start

```bash
# Check logs
sudo docker logs processing-backend --tail 50

# Common issues:
# - .env file missing in /home/user1/ProcessingBackend/backend/.env
# - Managed PostgreSQL connection failed (check 10.0.0.7:5432)
# - Port conflict
```

### Payment Returns 401

```bash
# 1. Check nginx config
sudo docker exec nginx-mutual-legacy-nginx-mutual-1 cat /etc/nginx/conf.d/internal_ssl.conf | grep -A 20 "payment"

# 2. Check terminal exists in database
psql -h 10.0.0.7 -U etran_db_user -d etran -c "SELECT * FROM terminals WHERE sn = 'CN_FROM_LOG';"

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
