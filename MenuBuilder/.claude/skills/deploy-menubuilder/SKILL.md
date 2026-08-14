---
name: deploy-menubuilder
description: Deploys MenuBuilder (backend + frontend) to the production server. Use when the user says "deploy MenuBuilder", "задеплой менюбилдер", "обнови менюбилдер", "deploy menu builder", or asks to push MenuBuilder changes to the server. Also use when a code change to MenuBuilder backend or frontend needs to be applied on the server.
---

# Deploy MenuBuilder

Deploys MenuBuilder backend (FastAPI) and frontend (React/Vite) to the production server at `176.108.247.249`, rebuilds Docker containers, and syncs to the monorepo.

## Prerequisites

- SSH key: `d:\.ssh\free-tier-cloud_ru`
- Server: `user1@176.108.247.249`
- Project on server: `/home/user1/MenuBuilder/`
- Docker compose file: `/home/user1/MenuBuilder/docker-compose.yaml`
- Backend container: `menubuilder-backend`
- Frontend container: `menubuilder-frontend` (nginx:alpine, serves `frontend/dist/`)
- Local project: `D:\repo\platerra\Public\MenuBuilder\`
- Monorepo: `D:\repo\platerra\Public\etranprocessing\MenuBuilder\`

## Instructions

### Step 1: Upload backend code

```bash
scp -i d:\.ssh\free-tier-cloud_ru -r "D:\repo\platerra\Public\MenuBuilder\backend\app" user1@176.108.247.249:/home/user1/MenuBuilder/backend/
```

### Step 2: Rebuild backend container

```bash
ssh user1@176.108.247.249 -i d:\.ssh\free-tier-cloud_ru "sudo docker compose -f /home/user1/MenuBuilder/docker-compose.yaml up -d --build menubuilder-backend"
```

### Step 3: Upload frontend source

```bash
scp -i d:\.ssh\free-tier-cloud_ru -r "D:\repo\platerra\Public\MenuBuilder\frontend\src" user1@176.108.247.249:/home/user1/MenuBuilder/frontend/
```

### Step 4: Build frontend on server

```bash
ssh user1@176.108.247.249 -i d:\.ssh\free-tier-cloud_ru "cd /home/user1/MenuBuilder/frontend && npm run build"
```

If the build fails with TypeScript errors, fix the source files locally and repeat from Step 3.

### Step 5: Verify

```bash
ssh user1@176.108.247.249 -i d:\.ssh\free-tier-cloud_ru "sudo docker logs menubuilder-backend --tail 5"
```

Expected: `Uvicorn running on http://0.0.0.0:8000`

Test an endpoint:
```bash
ssh user1@176.108.247.249 -i d:\.ssh\free-tier-cloud_ru "curl -s http://localhost:3000/api/menu-variants"
```

### Step 6: Sync to monorepo and push

```bash
robocopy "D:\repo\platerra\Public\MenuBuilder\backend\app" "D:\repo\platerra\Public\etranprocessing\MenuBuilder\backend\app" /E /XD __pycache__ /XF .env /PURGE
robocopy "D:\repo\platerra\Public\MenuBuilder\frontend\src" "D:\repo\platerra\Public\etranprocessing\MenuBuilder\frontend\src" /E /XD node_modules /PURGE
git -C "D:\repo\platerra\Public\etranprocessing" add MenuBuilder/
git -C "D:\repo\platerra\Public\etranprocessing" commit -m "<describe changes>"
git -C "D:\repo\platerra\Public\etranprocessing" push origin main
```

## Partial deploys

- **Backend only**: skip Steps 3-4
- **Frontend only**: skip Steps 1-2
- **No git push**: skip Step 6 (e.g. WIP changes)

## Database migration

If a migration script exists (e.g. `migrate_variants.py`), copy it to the container and run:

```bash
scp -i d:\.ssh\free-tier-cloud_ru "D:\repo\platerra\Public\MenuBuilder\backend\migrate_variants.py" user1@176.108.247.249:/home/user1/MenuBuilder/backend/migrate_variants.py
ssh user1@176.108.247.249 -i d:\.ssh\free-tier-cloud_ru "sudo docker cp /home/user1/MenuBuilder/backend/migrate_variants.py menubuilder-backend:/app/migrate_variants.py"
ssh user1@176.108.247.249 -i d:\.ssh\free-tier-cloud_ru "sudo docker exec menubuilder-backend python migrate_variants.py"
```

## Troubleshooting

- **Frontend build fails**: check TypeScript errors in the output. Fix source locally, re-upload, rebuild.
- **Backend 404 on new routes**: container code is stale. Ensure Step 1 uploaded to the correct path and Step 2 rebuilt (not just restarted).
- **nginx returns 502**: backend container not running. Check `docker logs menubuilder-backend`.
- **Port 3000 not responding**: frontend container not running. Check `docker logs menubuilder-frontend`.
