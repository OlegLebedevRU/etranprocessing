---
name: deploy-processing
description: Deploys ProcessingBackend (backend + mcp-pin-server) to the production server. Use when the user says "deploy processing", "задеплой processing", "обнови processing backend", or asks to push ProcessingBackend changes to the server.
---

# Deploy ProcessingBackend

Deploys ProcessingBackend (FastAPI) and mcp-pin-server to the production server at `176.108.247.249`, rebuilds Docker containers.

**Source of truth: monorepo at `D:\repo\platerra\Public\etranprocessing\ProcessingBackend\`**

## Prerequisites

- SSH key: `d:\.ssh\free-tier-cloud_ru`
- Server: `user1@176.108.247.249`
- Project on server: `/home/user1/ProcessingBackend/`
- Docker compose file: `/home/user1/ProcessingBackend/docker-compose.yaml`
- Backend container: `processing-backend`
- MCP container: `mcp-pin-server`

## Instructions

### Step 0: Pre-deploy checks (mandatory)

**0a. Code quality — all must pass with zero errors:**

```bash
cd D:\repo\platerra\Public\etranprocessing\ProcessingBackend\backend
uv run ruff check --fix app/
uv run ruff format app/
uv run pyright app/
```

```bash
cd D:\repo\platerra\Public\etranprocessing\ProcessingBackend\mcp-pin-server
uv run ruff check --fix src/
uv run ruff format src/
uv run pyright src/
```

**0b. Secrets scan — must find zero matches:**

Check that no secrets, credentials, or internal URLs leaked into tracked files:

```bash
# DB URLs with credentials
grep -rn "postgresql://.*:.*@" ProcessingBackend/ --include="*.py" --include="*.yaml" --include="*.yml" --include="*.toml"

# Hardcoded secrets/keys/tokens (non-empty defaults in getenv)
grep -rn 'os\.getenv(.*,\s*"[^"]\{8,\}")' ProcessingBackend/ --include="*.py"

# Production URLs in code (not in .env)
grep -rn "https://dev\.\|https://api\.\|https://prod\." ProcessingBackend/ --include="*.py"
```

If any match is found, move the value to `.env` and reference it via `os.environ` or `pydantic-settings`. Defaults must be empty or localhost only.

**0c. Deploy skill self-check:**

Verify that this SKILL.md and all `.md` files under `.claude/` contain no credentials, passwords, tokens, or DB URLs. Infrastructure references (server IP, SSH key path) are acceptable; secret values are not.

### Step 1: Upload backend

```bash
scp -i d:\.ssh\free-tier-cloud_ru -r "D:\repo\platerra\Public\etranprocessing\ProcessingBackend\backend\pyproject.toml" "D:\repo\platerra\Public\etranprocessing\ProcessingBackend\backend\Dockerfile" "D:\repo\platerra\Public\etranprocessing\ProcessingBackend\backend\app" "D:\repo\platerra\Public\etranprocessing\ProcessingBackend\backend\alembic" "D:\repo\platerra\Public\etranprocessing\ProcessingBackend\backend\alembic.ini" user1@176.108.247.249:/home/user1/ProcessingBackend/backend/
```

### Step 2: Upload mcp-pin-server

```bash
scp -i d:\.ssh\free-tier-cloud_ru -r "D:\repo\platerra\Public\etranprocessing\ProcessingBackend\mcp-pin-server\pyproject.toml" "D:\repo\platerra\Public\etranprocessing\ProcessingBackend\mcp-pin-server\Dockerfile" "D:\repo\platerra\Public\etranprocessing\ProcessingBackend\mcp-pin-server\src" user1@176.108.247.249:/home/user1/ProcessingBackend/mcp-pin-server/
```

### Step 3: Upload docker-compose and rebuild

```bash
scp -i d:\.ssh\free-tier-cloud_ru "D:\repo\platerra\Public\etranprocessing\ProcessingBackend\docker-compose.yaml" user1@176.108.247.249:/home/user1/ProcessingBackend/docker-compose.yaml
ssh user1@176.108.247.249 -i d:\.ssh\free-tier-cloud_ru "sudo docker compose -f /home/user1/ProcessingBackend/docker-compose.yaml up -d --build processing-backend mcp-pin-server"
```

### Step 4: Verify

```bash
ssh user1@176.108.247.249 -i d:\.ssh\free-tier-cloud_ru "sudo docker logs processing-backend --tail 5"
ssh user1@176.108.247.249 -i d:\.ssh\free-tier-cloud_ru "sudo docker logs mcp-pin-server --tail 5"
```

Expected: `Uvicorn running on http://0.0.0.0:8000` and `MCP server connected to database`

### Step 5: Git commit and push

```bash
git -C "D:\repo\platerra\Public\etranprocessing" add ProcessingBackend/
git -C "D:\repo\platerra\Public\etranprocessing" commit -m "<describe changes>"
git -C "D:\repo\platerra\Public\etranprocessing" push origin main
```

## Partial deploys

- **Backend only**: Steps 1, 3 (backend only), 4
- **MCP only**: Steps 2, 3 (mcp only), 4
- **No git push**: skip Step 5 (WIP)

## Troubleshooting

- **Backend fails to start**: check `docker logs processing-backend`. Common: missing .env, DB connection refused.
- **MCP fails to start**: check `docker logs mcp-pin-server`. Common: DATABASE_URL wrong, pg container not on same network.
- **Port 8000 not responding**: container not running. Check `docker ps`.
- **Wrong Python version**: ensure Dockerfile has correct base image (`python:3.14-slim`).
