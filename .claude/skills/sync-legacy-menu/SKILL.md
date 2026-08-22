---
name: sync-legacy-menu
description: Synchronizes menu category trees (groups) and services from legacy MS SQL (Service DB) to the new PostgreSQL backend. Use when the user asks to "sync legacy menu", "импортируй меню", "синхронизируй меню с легаси", "обнови меню для орг", or import menu trees/services during the migration transition period.
---

# Sync Legacy Menu Skill

Imports or updates hierarchical category trees (`tb_GS_Group` -> `groups`) and services (`tb_GS_Service` -> `services`) from the legacy MS SQL database (`Service` on `172.17.100.1`) into the new PostgreSQL database on the production app server (`176.108.247.249`).

## 1. Network Topology & Isolation Constraint

> **IMPORTANT ARCHITECTURAL CONSTRAINT:**
> - The legacy MS SQL server (`172.17.100.1`) resides in a **protected private internal network**.
> - The new cloud server (`176.108.247.249`) has **NO DIRECT NETWORK ROUTE** to `172.17.100.1`.
> - Direct queries from Docker containers on `176.108.247.249` to MS SQL will fail.
>
> **Migration Pattern (ETL Bridge):**
> 1. **Extract**: Execute `migrate/sync_menu_from_mssql.py` from the local environment (which has access to the protected network).
> 2. **Transfer**: The script transfers the validated dataset via SCP to `user1@176.108.247.249`.
> 3. **Load & Verify**: The script triggers `menubuilder-backend` to apply changes into PostgreSQL and verifies via `GET /api/ListMenuFile`.

---

## 2. Prerequisites & Server Info

- **Legacy MS SQL**: Host `172.17.100.1`, Database `Service`, Port `1433`, User `ai-agent` / Password `ai-agent`.
- **Target Cloud Server**: `user1@176.108.247.249`, SSH Key `d:\.ssh\free-tier-cloud_ru`.
- **Target PostgreSQL**: Container `iot-rpc-rest-app-pg-1`, Database `etranprocessing`.
- **Backend Container**: `menubuilder-backend`.
- **Source of truth scripts**:
  - Extractor / CLI Bridge: `D:\repo\platerra\Public\etranprocessing\migrate\sync_menu_from_mssql.py`
  - Backend Importer: `D:\repo\platerra\Public\etranprocessing\MenuBuilder\backend\app\services\legacy_menu.py`
  - Documentation: `D:\repo\platerra\Public\etranprocessing\docs\infrastructure-and-migration-connections.md`

---

## 3. Step-by-step Workflows

### Workflow A: Dry-run / Inspect Differences (No Changes)

To check what groups and services exist in MS SQL for an organization before applying changes:

```bash
# For a specific organization:
uv run python migrate/sync_menu_from_mssql.py --org 424 --dry-run

# For multiple organizations:
uv run python migrate/sync_menu_from_mssql.py --orgs 1,424,477 --dry-run

# For all organizations:
uv run python migrate/sync_menu_from_mssql.py --all --dry-run
```

### Workflow B: Sync a Single Organization to Production

```bash
# 1. Extract from MS SQL, save JSON, upload to server, and apply in PostgreSQL:
uv run python migrate/sync_menu_from_mssql.py --org 424 --deploy

# 2. Verify menu generation via API:
# Query variant_id from PostgreSQL or call ListMenuFile endpoint
curl.exe -k -s "https://176.108.247.249:3000/api/ListMenuFile?variant_id=<VARIANT_ID>"
```

### Workflow C: Sync All Organizations to Production

```bash
# 1. Full extract and deploy to production:
uv run python migrate/sync_menu_from_mssql.py --all --deploy

# 2. Verify total records in PostgreSQL on server:
ssh -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "sudo docker exec iot-rpc-rest-app-pg-1 psql -U etran -d etranprocessing -c 'SELECT count(*) FROM menu_variants WHERE name = ''Легаси меню'';' -c 'SELECT count(*) FROM groups;' -c 'SELECT count(*) FROM services;'"
```

### Workflow D: Local-only Execution (Development / Testing)

```bash
# 1. Extract from MS SQL and save to local JSON files:
uv run python migrate/sync_menu_from_mssql.py --org 424

# 2. Run backend importer locally against local PostgreSQL (from MenuBuilder/backend):
cd MenuBuilder/backend
uv run python -m app.services.legacy_menu --org 424
```

---

## 4. Verification & Quality Gates

After syncing menu data:
1. Run backend unit tests to ensure ORM mapping integrity:
   ```bash
   cd MenuBuilder/backend
   uv run pytest tests/test_legacy_menu.py
   ```
2. Check that no unmapped groups remain in the output (`Unmapped: 0`).
3. Verify that terminal menu binding functions correctly and `GET /api/ListMenuFile` returns a valid hierarchical structure with `root` -> `items`.
