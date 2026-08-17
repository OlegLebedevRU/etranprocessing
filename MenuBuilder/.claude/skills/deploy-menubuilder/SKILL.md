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

Restart nginx (config is volume-mounted):

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

### Step 6: Verify

```bash
ssh user1@176.108.247.249 -i d:\.ssh\free-tier-cloud_ru "sudo docker logs menubuilder-backend --tail 5"
```

Expected: `Uvicorn running on http://0.0.0.0:8000`

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
