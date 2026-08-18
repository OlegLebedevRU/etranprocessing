---
name: deploy-menubuilder
description: Deploys MenuBuilder (backend + frontend) to the production server. Use when the user says "deploy MenuBuilder", "задеплой менюбилдер", "обнови менюбилдер", "deploy menu builder", or asks to push MenuBuilder changes to the server. Also use when a code change to MenuBuilder backend or frontend needs to be applied on the server.
---

# Deploy MenuBuilder

Deploys MenuBuilder backend (FastAPI) and frontend (React/Vite) to the production server at `176.108.247.249`, rebuilds Docker containers.

**Source of truth: monorepo at `D:\repo\platerra\Public\etranprocessing\MenuBuilder\`**

## Prerequisites

- SSH key: `d:\.ssh\free-tier-cloud_ru`
- Server: `user1@176.108.247.249`
- Project on server: `/home/user1/MenuBuilder/`
- Docker compose file: `/home/user1/MenuBuilder/docker-compose.yaml`
- Backend container: `menubuilder-backend`
- Frontend container: `menubuilder-frontend` (nginx, serves `frontend/dist/`)
- **Monorepo (source):** `D:\repo\platerra\Public\etranprocessing\MenuBuilder\`

## Instructions

### Step 0: Pre-deploy checks (mandatory)

**0a. Code quality — all must pass with zero errors:**

```bash
cd D:\repo\platerra\Public\etranprocessing\MenuBuilder\backend
uv run ruff check --fix app/
uv run ruff format app/
uv run pyright app/
```

**0b. Secrets scan — must find zero matches:**

Check that no secrets, credentials, or internal URLs leaked into tracked files:

```bash
# DB URLs with credentials
grep -rn "postgresql://.*:.*@" MenuBuilder/ --include="*.py" --include="*.yaml" --include="*.yml" --include="*.toml"

# Hardcoded secrets/keys/tokens (non-empty defaults in getenv)
grep -rn 'os\.getenv(.*,\s*"[^"]\{8,\}")' MenuBuilder/ --include="*.py"

# Production URLs in code (not in .env)
grep -rn "https://dev\.\|https://api\.\|https://prod\." MenuBuilder/ --include="*.py"
```

If any match is found, move the value to `.env` and reference it via `os.environ` or `pydantic-settings`. Defaults must be empty or localhost only.

**0c. Deploy skill self-check:**

Verify that this SKILL.md and all `.md` files under `.claude/` contain no credentials, passwords, tokens, or DB URLs. Infrastructure references (server IP, SSH key path) are acceptable; secret values are not.

### Step 1: Upload backend (full rebuild)

Upload pyproject.toml, Dockerfile, and app code:

```bash
scp -i d:\.ssh\free-tier-cloud_ru -r "D:\repo\platerra\Public\etranprocessing\MenuBuilder\backend\pyproject.toml" "D:\repo\platerra\Public\etranprocessing\MenuBuilder\backend\Dockerfile" "D:\repo\platerra\Public\etranprocessing\MenuBuilder\backend\app" user1@176.108.247.249:/home/user1/MenuBuilder/backend/
```

### Step 2: Rebuild backend container

```bash
ssh user1@176.108.247.249 -i d:\.ssh\free-tier-cloud_ru "sudo docker compose -f /home/user1/MenuBuilder/docker-compose.yaml up -d --build menubuilder-backend"
```

### Step 3: Upload nginx config

```bash
scp -i d:\.ssh\free-tier-cloud_ru "D:\repo\platerra\Public\etranprocessing\MenuBuilder\nginx.conf" user1@176.108.247.249:/home/user1/MenuBuilder/nginx.conf
scp -i d:\.ssh\free-tier-cloud_ru -r "D:\repo\platerra\Public\etranprocessing\MenuBuilder\nginx" user1@176.108.247.249:/home/user1/MenuBuilder/
```

**Only needed if `nginx.conf` itself changed.** Restart nginx (config is
volume-mounted):

```bash
ssh user1@176.108.247.249 -i d:\.ssh\free-tier-cloud_ru "sudo docker compose -f /home/user1/MenuBuilder/docker-compose.yaml restart menubuilder-frontend"
```

### Step 4: Upload frontend source

```bash
scp -i d:\.ssh\free-tier-cloud_ru -r "D:\repo\platerra\Public\etranprocessing\MenuBuilder\frontend\src" user1@176.108.247.249:/home/user1/MenuBuilder/frontend/
```

### Step 5: Build frontend on server

```bash
ssh user1@176.108.247.249 -i d:\.ssh\free-tier-cloud_ru "cd /home/user1/MenuBuilder/frontend && npm run build"
```

If the build fails with TypeScript errors, fix the source files locally in monorepo and repeat from Step 4.

**No container restart needed after this step.** `menubuilder-frontend`'s
`docker-compose.yaml` bind-mounts `./frontend/dist` (i.e.
`/home/user1/MenuBuilder/frontend/dist` on the host) straight into
`/usr/share/nginx/html` **read-only, live**. `npm run build` overwrites
`dist/` in place on the host, and nginx serves the new files immediately —
verified by comparing file mtimes inside vs. outside the container after a
build (they match once you account for the container running in UTC vs. the
host's MSK timezone). A restart is only required for Step 3 (`nginx.conf`
changes) or JWT secret/env changes.

> Note: there is also a legacy, **unused** static path at
> `/var/www/menubuilder` on the host with its own (currently inactive)
> systemd `nginx.service` and `/etc/nginx/sites-enabled/menubuilder` config.
> Production traffic on port 3000 is served exclusively by the
> `menubuilder-frontend` **Docker** container (`docker-proxy` owns the
> `0.0.0.0:3000` listener) — don't waste time syncing files there.

### Step 6: Verify

```bash
ssh user1@176.108.247.249 -i d:\.ssh\free-tier-cloud_ru "sudo docker logs menubuilder-backend --tail 5"
```

Expected: `Uvicorn running on http://0.0.0.0:8000`

To verify an **authenticated** endpoint (e.g. one behind `get_current_user`)
without knowing a real user's plaintext password, generate a short-lived JWT
directly inside the container using the same signing function the app uses,
then curl with it — this avoids ever needing/typing real credentials:

```bash
# Write a small script that imports app.auth.create_access_token, builds a
# token for a known test user/org, and calls the endpoint(s) with it — see
# app/auth.py get_current_user for the expected claim names ("sub", "org").
# Prefer a scp'd script file over an inline `ssh ... "python -c ..."`
# one-liner: nested quoting through ssh + PowerShell reliably breaks/garbles
# multi-line Python (parentheses, dict literals). Delete the script and any
# printed token from the container/host afterwards — never leave test JWTs
# or scripts lying around.
```

### Step 7: Git commit and push

```bash
git -C "D:\repo\platerra\Public\etranprocessing" add MenuBuilder/
git -C "D:\repo\platerra\Public\etranprocessing" commit -m "<describe changes>"
git -C "D:\repo\platerra\Public\etranprocessing" push origin main
```

## Partial deploys

- **Backend only**: Steps 1-2 only
- **Frontend only**: Steps 4-5 only
- **Nginx only**: Step 3 only
- **No git push**: skip Step 7 (WIP)

## Database migration

If a migration script exists:

```bash
scp -i d:\.ssh\free-tier-cloud_ru "D:\repo\platerra\Public\etranprocessing\MenuBuilder\backend\migrate.py" user1@176.108.247.249:/home/user1/MenuBuilder/backend/migrate.py
ssh user1@176.108.247.249 -i d:\.ssh\free-tier-cloud_ru "sudo docker cp /home/user1/MenuBuilder/backend/migrate.py menubuilder-backend:/app/migrate.py"
ssh user1@176.108.247.249 -i d:\.ssh\free-tier-cloud_ru "sudo docker exec menubuilder-backend python migrate.py"
```

## Troubleshooting

- **Frontend build fails**: check TypeScript errors. Fix in monorepo, re-upload, rebuild.
- **Backend 404 on new routes**: container code is stale. Ensure Step 1 uploaded pyproject.toml + Dockerfile + app/ and Step 2 rebuilt.
- **nginx returns 502**: backend container not running. Check `docker logs menubuilder-backend`.
- **Port 3000 not responding**: frontend container not running. Check `docker logs menubuilder-frontend`.
- **Wrong Python version**: ensure Dockerfile in monorepo has correct base image (e.g. `python:3.14-slim`).
