# etranprocessing Documentation

## Overview

etranprocessing is a payment processing backend for terminals. It handles payment requests from kiosks/terminals, validates them, stores payment records in PostgreSQL, and maintains daily balance aggregations.

## Architecture

```
Terminal → Nginx (mutual TLS) → ProcessingBackend (FastAPI) → PostgreSQL
```

### Components

| Component | Technology | Purpose |
|-----------|-----------|---------|
| **Nginx** | nginx:alpine | SSL termination, client cert validation, reverse proxy |
| **ProcessingBackend** | FastAPI + SQLAlchemy + asyncpg | Payment processing API |
| **PostgreSQL** | postgres:15 | Data storage |

### Service Communication

```
Terminal (HTTPS + Client Cert)
    ↓
Nginx (iot-rpc-rest-app-nginx-mutual-1)
    - Validates client certificate
    - Forwards cert headers (X-Client-Cert-DN, X-Client-Cert-Serial)
    - Routes by URL path
    ↓
ProcessingBackend (processing-backend)
    - Extracts terminal identity from cert headers
    - Processes payment logic
    - Writes to PostgreSQL
    ↓
PostgreSQL (iot-rpc-rest-app-pg-1)
    - etranprocessing database
```

## Project Structure

```
etranprocessing/
├── ProcessingBackend/
│   ├── backend/
│   │   ├── app/
│   │   │   ├── main.py              # FastAPI app, router registration
│   │   │   ├── config.py            # Settings (from .env)
│   │   │   ├── database.py          # SQLAlchemy async engine
│   │   │   ├── dependencies.py      # Auth: cert → Terminal
│   │   │   ├── models.py            # SQLAlchemy models
│   │   │   ├── logging_config.py    # Payment logging with rotation
│   │   │   ├── routers/
│   │   │   │   ├── payment.py       # Payment endpoints
│   │   │   │   ├── tech_gate.py     # Legacy tech functions
│   │   │   │   ├── licensebilling.py
│   │   │   │   └── health.py
│   │   │   └── services/
│   │   │       └── payment_service.py  # Payment business logic
│   │   ├── alembic/                  # Database migrations
│   │   ├── .env                      # Secrets (not in git)
│   │   ├── .env.example              # Template for developers
│   │   └── pyproject.toml            # Dependencies
│   ├── docker-compose.yaml           # Service orchestration
│   ├── nginx.conf                    # Frontend nginx config
│   └── log/                          # Payment logs (persistent)
└── MenuBuilder/                      # Menu management (separate service)
```

## Database Schema

### Core Tables

#### `terminals`
Terminal identity and certificate mapping.

| Column | Type | Description |
|--------|------|-------------|
| id | PK | Internal ID |
| device_id | int | Device identifier (OU from cert) |
| sn | varchar | Serial number (CN from cert) |
| cert_serial | varchar | Certificate serial |
| org_id | int | Organization ID |
| is_active | bool | Active flag |

#### `orgs`
Organizations (multi-tenant isolation).

| Column | Type | Description |
|--------|------|-------------|
| org_id | PK | Organization ID (from legacy) |
| org_name | varchar | Code name (Latin) |
| name | varchar | Display name (Cyrillic) |
| status | int | 0=inactive, 1=active |
| is_active | bool | Active flag |

#### `tsp`
Payment systems reference.

| Column | Type | Description |
|--------|------|-------------|
| tsp_id | PK | TSP ID (from legacy) |
| tsp_code | int | TSP code (= PaymSubjTp) |
| tsp_name | varchar | Display name |

#### `payments`
Payment records.

| Column | Type | Description |
|--------|------|-------------|
| paym_id | PK, autoincrement | Payment ID |
| paym_datetime | timestamp | Payment time |
| paym_amount | bigint | Amount in kopeks |
| paym_ext_id | varchar(20) | External ID from terminal |
| paym_tsp_code | int | TSP code |
| terminal_id | FK → terminals | Terminal |
| org_id | FK → orgs | Organization |
| paym_state | int | State (default=0, API writes=2) |
| pay_type_id | int | 1=cash, 2=card, 3=SBP, 4=combo |

#### `payment_params`
Payment parameters (key-value per payment).

| Column | Type | Description |
|--------|------|-------------|
| id | PK | |
| paym_id | FK → payments | |
| param_id | FK → tsp_parameter_codes | |
| param_value | varchar(2000) | |

#### `tsp_parameter_codes`
Parameter definitions per TSP prototype.

| Column | Type | Description |
|--------|------|-------------|
| param_id | PK | |
| prototypenumber | int | Template number |
| parameter_code | int | Code (1, 2, 3, 301...) |
| code_description | varchar | "Рекв1", "Номер телефона" |

#### `balance_terminal_tsp`
Daily amount accumulation per terminal+TSP.

| Column | Type | Description |
|--------|------|-------------|
| rec_id | PK | |
| int_day | int | Day as yyyyMMdd |
| org_id | FK → orgs | |
| terminal_id | FK → terminals | |
| tsp_id | FK → tsp | |
| amount | bigint | Accumulated amount |
| count | int | Payment count |
| update_datetime | timestamp | |

### Entity Relationship

```
terminals (1) ──→ (N) payments
terminals (1) ──→ (N) balance_terminal_tsp
orgs (1) ──→ (N) payments
orgs (1) ──→ (N) terminals
tsp (1) ──→ (N) payments
tsp (1) ──→ (N) balance_terminal_tsp
payments (1) ──→ (N) payment_params
tsp_parameter_codes (1) ──→ (N) payment_params
```

## API Endpoints

### Payment Flow

#### POST `/api/payment/etran.ashx`

Main payment endpoint. Handles multiple functions via `function` parameter.

**Request Parameters:**

| Parameter | Required | Description |
|-----------|----------|-------------|
| function | Yes | `payment`, `check`, `update`, `addparams` |
| PaymExtId | Yes | External payment ID (max 20 chars) |
| PaymSubjTp | Yes | TSP code |
| Amount | Yes | Amount in kopeks |
| Params | No | Parameter string: `"1 value1;2 value2"` |
| PayTypeId | No | 1=cash, 2=card, 3=SBP, 4=combo |
| TotalSum | No | Display sum |
| Signature | No | Client signature |

**Example Request:**
```
POST /api/payment/etran.ashx
Content-Type: application/x-www-form-urlencoded

function=payment&PaymExtId=0773_160826_22564179&PaymSubjTp=1&Amount=10000&Params=1+9163213210%3b2+100&PayTypeId=2
```

**Success Response:**
```xml
<?xml version='1.0' encoding='UTF-8'?>
<Response>
  <Result>OK</Result>
  <PaymNumb>4</PaymNumb>
  <PaymState>2</PaymState>
  <PaymExtId>0773_160826_22564179</PaymExtId>
  <Description>Payment accepted.</Description>
</Response>
```

**Error Response:**
```xml
<?xml version='1.0' encoding='UTF-8'?>
<Response>
  <Result>ERROR</Result>
  <PaymExtId>0773_160826_22564179</PaymExtId>
  <Description>TSP not found for code 999</Description>
</Response>
```

### Payment Flow Details

1. **Idempotency**: If `paym_ext_id` already exists, returns existing payment
2. **Terminal Auth**: Extracts CN + cert_serial from nginx headers
3. **Parameter Parsing**: URL-decodes `Params` field (`+` = space, `%3b` = `;`)
4. **Default Prototype**: Uses 99000 if prototypenumber not found for TSP
5. **Balance Update**: Upserts `balance_terminal_tsp` (day + terminal + TSP)

### Tech Gate Functions

#### POST `/api/techgate/etran.ashx`

Legacy tech functions (stub responses).

| function | Description |
|----------|-------------|
| devicestatus | Device status check |
| inkass | Inkassation |
| closeshift / closeshift2 | Close shift |
| getshiftreport | Shift report |
| getinkassreport | Inkass report |
| kiosk | Kiosk info |
| tsplist | TSP list |

## Authentication

### Client Certificate Flow

1. Terminal connects with SSL client certificate
2. Nginx validates certificate (optional_no_ca)
3. Nginx forwards headers:
   - `X-Client-Cert-DN`: Certificate subject
   - `X-Client-Cert-Serial`: Certificate serial (hex)
4. ProcessingBackend extracts:
   - CN → `terminal.sn`
   - Serial → `terminal.cert_serial`
5. Looks up `Terminal` by `sn` + `cert_serial`

### Certificate Subject Format

```
emailAddress=1.terminal@forpay.ru,CN=A99D2F18001ECC93DF5CBE27F442C8FA,OU=773,O=1,L=1671,ST=msk,C=ru
```

| Field | Maps to |
|-------|---------|
| CN | `terminal.sn` |
| OU | `terminal.device_id` (informational) |
| O | `terminal.org_id` (informational) |
| Serial | `terminal.cert_serial` |

## Configuration

### Environment Variables (.env)

```bash
# Database connection
DATABASE_URL=postgresql+asyncpg://user:password@host:port/database

# CORS origins (JSON array)
CORS_ORIGINS=["http://localhost:8080","https://dev.leo4.ru:4443"]
```

### Docker Compose

```yaml
services:
  processing-backend:
    build: ./backend
    container_name: processing-backend
    restart: always
    env_file:
      - ./backend/.env
    volumes:
      - ./log:/app/log    # Persistent logs
    networks:
      - pg_network
      - processing_net

networks:
  pg_network:
    external: true
    name: iot-rpc-rest-app_pg_network
  processing_net:
    driver: bridge
```

## Logging

### Payment Logger

- **File**: `/app/log/payment.log`
- **Rotation**: 10 MB per file
- **Backup**: 5 files
- **Format**: `2026-08-16 23:25:00 - INFO - REQUEST: ext_id=...`

### Log Entries

```
REQUEST: ext_id=0773_160826_22564179, tsp=1, amount=10000, pay_type=2, terminal=773, org=1, params_raw=1 9163213210;2 100
SUCCESS: paym_id=4, ext_id=0773_160826_22564179
VALIDATION_ERROR: ext_id=..., error=TSP not found for code 999
ERROR: ext_id=..., error=...
```

## DevOps

### Deployment

```bash
# Build and restart
cd /home/user1/ProcessingBackend
sudo docker compose up -d --build processing-backend

# Check logs
sudo docker logs processing-backend --tail 50

# Check payment logs
sudo docker exec processing-backend cat /app/log/payment.log
```

### Database Migrations

```bash
# Run migration script
sudo docker cp migration.py processing-backend:/app/migration.py
sudo docker exec processing-backend python /app/migration.py
```

### Monitoring

```bash
# Container status
sudo docker ps | grep processing

# Payment count
sudo docker exec processing-backend python -c "import asyncio; from sqlalchemy import text; from app.database import engine; async def q(): 
    async with engine.connect() as c: 
        r = await c.execute(text('SELECT COUNT(*) FROM payments'))
        print(f'Payments: {r.scalar()}')
asyncio.run(q())"
```

## Legacy Reference

### Legacy System

- **URL**: `https://iot-processing.ru/payment/etran.ashx`
- **Stack**: ASP.NET (C#) + SQL Server 2005
- **Databases**: `Service`, `Payments`

### Legacy Flow

```
Terminal → Dispatcher (ashx) → SOAP → MessageProcessor (asmx) → SQL Server
```

### Key Legacy Files

```
D:\repo\platerra\Public\PROCESSING\BACK\ProcessingCore\
├── EtranDispatcher\src\Dispatcher.cs      # HTTP entry point
├── MessageProcessor\MessageProcessor.asmx.cs  # Business logic
├── MessageProcessor\DbInterface.cs        # DB operations
└── EtranApi\RequestMessage.cs             # Data model
```

### Legacy SP Mapping

| Legacy SP | New Implementation |
|-----------|-------------------|
| `AModule_PutPayment` | `PaymentService.create_payment()` |
| `AModule_PutPaymentParam` | `PaymentService.create_payment()` (loop) |
| `BalanceKioskTspExt` | `PaymentService._update_balance()` |
| `GetRek_20090918` | Not needed (routing removed) |
| `AModule_ReportTryExt` | Not needed (no attempt logging) |

### Legacy Data Migration

| Source (MSSQL) | Target (PostgreSQL) | Status |
|----------------|---------------------|--------|
| `Service..TspCodes` | `tsp` | Done (684 records) |
| `Service..Organizations` | `orgs` | Done (44 records) |
| `Service..Parameter_codes` | `tsp_parameter_codes` | Manual |
| `Payments..Payments` | `payments` | New records only |

## Troubleshooting

### Terminal gets 401

- Check nginx config has correct `proxy_set_header X-Client-Cert-DN`
- Verify terminal certificate is registered in `terminals` table
- Check `cert_serial` matches (hex format, no colons)

### Payment not saving params

- Check URL encoding: `+` = space, `%3b` = `;`
- Verify `tsp_code` exists in `tsp` table
- Check `tsp_parameter_codes` has entries for prototypenumber

### Container restarts

- Check logs: `sudo docker logs processing-backend --tail 50`
- Verify `.env` file exists and has correct DATABASE_URL
- Check PostgreSQL is accessible from container network
