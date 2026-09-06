# AI Agent Reference

## Context

This document provides context for AI agents working on the etranprocessing codebase.

## Project Summary

etranprocessing is a payment processing backend for terminals/kiosks. It receives payment requests via HTTPS, validates them, stores in PostgreSQL, and maintains daily balance aggregations.

## Key Files

### Core Application

| File | Purpose | Key Functions |
|------|---------|---------------|
| `app/main.py` | FastAPI app setup | Router registration |
| `app/config.py` | Settings from .env | `DATABASE_URL`, `CORS_ORIGINS` |
| `app/database.py` | SQLAlchemy engine | `get_db()` dependency |
| `shared/etranprocessing_db` | Shared database models (re-exported in `app/models.py`) | `Terminal`, `Payment`, `Tsp`, etc. |
| `app/dependencies.py` | Auth dependencies | `get_current_terminal()` |
| `app/logging_config.py` | Payment logger | `payment_logger` |

### Routers

| File | Endpoints |
|------|-----------|
| `app/routers/payment.py` | `/api/payment/etran.ashx` |
| `app/routers/tech_gate.py` | `/api/techgate/etran.ashx` |
| `app/routers/licensebilling.py` | `/api/licensebilling/` |
| `app/routers/billing.py` | `/api/billing/*` (summary, terminals, checkout, reactivate — see `docs/etran_bill-licensing-architecture.md`) |
| `app/routers/health.py` | `/api/health` |

### Services

| File | Purpose |
|------|---------|
| `app/services/payment_service.py` | Payment business logic |
| `app/services/billing.py` | Pure license-billing calculations (status machine, period/date math, forecast) — see `docs/etran_bill-licensing-architecture.md` |
| `app/services/cert_billing.py` | Certificate PIN pricing/policy resolution |

## Common Tasks

### Add New Endpoint

1. Create/edit router in `app/routers/`
2. Add router to `app/main.py`
3. Add or update model in `shared/etranprocessing_db` (or alias in `app/models.py`) if needed
4. Create migration in `alembic/versions/`

### Add New Database Table

1. Add model to `shared/etranprocessing_db`
2. Create migration script in `alembic/versions/`
3. Run migration on server

### Modify Payment Flow

1. Edit `app/services/payment_service.py`
2. Edit `app/routers/payment.py` if API changes
3. Test with terminal or curl

## Code Patterns

### Database Query

```python
from sqlalchemy import select
from etranprocessing_db import Terminal

async def get_terminal(db: AsyncSession, sn: str) -> Optional[Terminal]:
    result = await db.execute(
        select(Terminal).where(Terminal.sn == sn)
    )
    return result.scalar_one_or_none()
```

### Error Handling

```python
try:
    payment = await service.create_payment(...)
    return success_response(payment.paym_id, paym_ext_id)
except ValueError as e:
    return error_response(paym_ext_id, str(e))
except Exception as e:
    logger.error(f"Error: {e}", exc_info=True)
    return error_response(paym_ext_id, "Internal server error")
```

### XML Response

```python
from fastapi.responses import Response

def xml_response(content: str) -> Response:
    return Response(
        content=f"<?xml version='1.0' encoding='UTF-8'?>\n{content}",
        media_type="application/xml",
    )
```

## Database Schema Summary

```
terminals ←── payments → orgs
                ↓
         payment_params → tsp_parameter_codes
                ↓
      balance_terminal_tsp → tsp
```

## Environment

- **Python**: 3.14 (requires-python = "==3.14.*")
- **FastAPI**: 0.141+
- **SQLAlchemy**: 2.0+
- **Database**: Managed PostgreSQL 18 (10.0.0.7:5432, db: `etran`)
- **Server**: 87.242.100.34 (user: `user1`, SSH: `d:\.ssh\id_ed25519`)

## Deployment

```bash
# Build and deploy
ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker compose -f /home/user1/compose.yaml up -d --build processing-backend"

# Check logs
ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker logs processing-backend --tail 50"
```

## Testing

### Manual Test

```bash
# Health check
curl https://dev.leo4.ru:4443/api/health

# Payment (requires client cert)
curl -X POST https://dev.leo4.ru:4443/api/payment/etran.ashx \
  -d "function=payment&PaymExtId=test123&PaymSubjTp=1&Amount=10000"
```

### Database Test

```bash
sudo docker exec processing-backend python -c "
import asyncio
from sqlalchemy import text
from app.database import engine
async def test():
    async with engine.connect() as c:
        r = await c.execute(text('SELECT COUNT(*) FROM payments'))
        print(f'Payments: {r.scalar()}')
asyncio.run(test())
"
```

## Important Notes

- Always use `async/await` for database operations
- Use `payment_logger` for payment-specific logging
- URL-decode request parameters (`+` = space, `%3b` = `;`)
- Default prototypenumber is 99000 if not found
- Payment API is idempotent by `paym_ext_id`
- **Documentation naming convention**: All technical docs in `docs/` must use the standard prefix `{СФЕРА}_{ФЛОУ}-{name}.md` (see `docs/etran_dev-documentation-naming-convention.md`). Always register new docs in `docs/README.md`.

## Legacy Reference

- **Legacy code**: `D:\repo\platerra\Public\PROCESSING\BACK\ProcessingCore\`
- **Legacy DB**: SQL Server 2005 at `172.17.100.1`
- **Legacy SPs**: `AModule_PutPayment`, `AModule_PutPaymentParam`, `BalanceKioskTspExt`

## File Locations

### Local Development

```
D:\repo\platerra\Public\etranprocessing\ProcessingBackend\
```

### Server

```
/home/user1/ProcessingBackend/
```

### Container

```
/app/
├── app/
├── log/
└── alembic/
```

## Common Issues

### ModuleNotFoundError

- File not copied to container
- Solution: `sudo docker cp` and restart

### 401 Unauthorized

- Terminal not found in database
- Check cert headers in nginx config

### 500 Internal Server Error

- Check container logs
- Usually database or validation error

### Logs Not Writing

- Check volume mount in docker-compose.yaml
- Verify log directory exists on host
