# DevOps Runbook

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

```bash
# 1. Copy config to server
scp nginx-mutual-ssl.conf user1@176.108.247.249:/tmp/

# 2. Copy to container
ssh user1@176.108.247.249 "sudo docker cp /tmp/nginx-mutual-ssl.conf iot-rpc-rest-app-nginx-mutual-1:/etc/nginx/conf.d/internal_ssl.conf"

# 3. Test and reload
ssh user1@176.108.247.249 "sudo docker exec iot-rpc-rest-app-nginx-mutual-1 nginx -t && sudo docker exec iot-rpc-rest-app-nginx-mutual-1 nginx -s reload"
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
