# Quick Start Guide

## For Developers

### Prerequisites

- Python 3.14+
- Docker + Docker Compose
- PostgreSQL (or use Docker)

### Local Development

1. **Clone repository**
   ```bash
   git clone <repo-url>
   cd etranprocessing/ProcessingBackend
   ```

2. **Setup environment**
   ```bash
   cd backend
   cp .env.example .env
   # Edit .env with your database credentials
   ```

3. **Install dependencies**
   ```bash
   uv sync
   ```

4. **Run locally**
   ```bash
   uv run fastapi dev
   ```

5. **Test endpoint**
   ```bash
   curl http://localhost:8000/api/health
   ```

### Database Setup

1. **Start PostgreSQL**
   ```bash
   docker run -d --name pg -e POSTGRES_PASSWORD=etran -p 5432:5432 postgres:15
   ```

2. **Create database**
   ```bash
   docker exec -it pg psql -U postgres -c "CREATE DATABASE etranprocessing;"
   ```

3. **Run migrations**
   ```bash
   cd backend
   uv run alembic upgrade head
   ```

## For DevOps

### Deploy to Production

> ⚠️ **Never upload code or build images directly on the production server.**
> Deployment is done exclusively through the release contract described in
> [`ops_run-beta-ci-cd.md`](ops_run-beta-ci-cd.md) and
> [`ops_run-git-and-release-flow.md`](ops_run-git-and-release-flow.md):
> accepted full Git SHA on `main` → build/publish on the dedicated builder
> (176.108.247.249) → immutable OCI digest in the private registry
> `dev-leo4-ru.cr.cloud.ru/etran` → digest pull on production
> (87.242.100.34) → health and image ID verification. Alembic migrations are
> applied from the new ProcessingBackend image during the release
> (`alembic upgrade head`), not by copying scripts to the server.

### Check Service Health (read-only)

```bash
# Container status (on the production server)
sudo docker compose -f /home/user1/compose.yaml ps

# API health
curl -k https://87.242.100.34:3000/api/health
```

## For AI Agents

### Key Files to Read

1. `shared/etranprocessing_db` (`app/models.py`) - Database schema and models
2. `app/routers/payment.py` - Payment endpoints
3. `app/services/payment_service.py` - Business logic
4. `app/dependencies.py` - Authentication

### Common Modifications

**Add new field to Model:**
1. Edit `shared/etranprocessing_db/models/...` - add column
2. Create migration script in `alembic/versions/`
3. Apply the migration through the standard release flow (the new ProcessingBackend image runs `alembic upgrade head` during the release)

**Add new endpoint:**
1. Create/edit router in `app/routers/`
2. Register in `app/main.py`

**Modify payment logic:**
1. Edit `app/services/payment_service.py`
2. Edit `app/routers/payment.py` if API changes

### Testing Locally

```bash
# Run tests
uv run pytest

# Test specific file
uv run pytest tests/test_payment_service.py
```

### Deploy Changes

```bash
# 1. Copy files to server
scp -i d:\.ssh\id_ed25519 -r backend/app user1@87.242.100.34:/home/user1/ProcessingBackend/backend/

# 2. Rebuild/Restart
ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker compose -f /home/user1/compose.yaml restart processing-backend"
```

## Environment Variables

| Variable | Description | Example |
|----------|-------------|---------|
| `DATABASE_URL` | PostgreSQL connection | `postgresql+asyncpg://user:pass@host/db` |
| `CORS_ORIGINS` | Allowed origins (JSON) | `["http://localhost:8080"]` |

## API Examples

### Payment Request

```bash
curl -X POST https://dev.leo4.ru:4443/api/payment/etran.ashx \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "function=payment&PaymExtId=test123&PaymSubjTp=1&Amount=10000&Params=1+1234567890%3b2+100&PayTypeId=2"
```

### Success Response

```xml
<?xml version='1.0' encoding='UTF-8'?>
<Response>
  <Result>OK</Result>
  <PaymNumb>1</PaymNumb>
  <PaymState>2</PaymState>
  <PaymExtId>test123</PaymExtId>
  <Description>Payment accepted.</Description>
</Response>
```

## Troubleshooting

| Issue | Solution |
|-------|----------|
| 401 Unauthorized | Check terminal exists in `terminals` table |
| 500 Internal Error | Check container logs |
| Connection refused | Check PostgreSQL is running |
| Module not found | Copy file to container and restart |

## More Documentation

- [Documentation Index](README.md)
- [Documentation Naming Convention](etran_dev-documentation-naming-convention.md)
- [ProcessingBackend Overview](proc_pay-backend-architecture.md)
- [Architecture Overview](etran_arch-architecture-analysis.md)
- [Nginx Configuration](ops_net-nginx-config-guide.md)
- [DevOps Runbook](ops_run-devops-runbook.md)
- [AI Agent Reference](etran_dev-ai-agent-reference.md)
