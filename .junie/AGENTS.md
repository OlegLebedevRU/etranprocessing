# Project Guidelines & Overview

## 1. Project Overview

**etranprocessing** is a payment processing platform and terminal management ecosystem. It handles payment requests from self-service payment kiosks/terminals, verifies client certificates and authentication, executes routing and balance accounting, and provides terminal menu configuration and administrative tools.

The platform is transitioning from a legacy ASP.NET / Microsoft SQL Server architecture to a modern asynchronous Python (FastAPI) / PostgreSQL stack with mutual TLS termination at Nginx.

---

## 2. Architecture & Request Flow

```
Payment Terminal (Mutual TLS HTTPS :4443 / legacy :443)
        │
        ▼
Nginx Reverse Proxy (SSL / Client Cert validation)
  - Validates client certificate
  - Forwards identity headers (X-Client-Cert-DN, X-Client-Cert-Serial, etc.)
        │
        ▼
ProcessingBackend (FastAPI + SQLAlchemy + asyncpg)
  - Authenticates terminal from cert headers
  - Handles payment / tech requests, terminal license checks & menus (ListMenuFile)
  - Persists payments and updates balances
        │
        ▼
PostgreSQL Database
        ▲
        │
MenuBuilder (FastAPI + React frontend + JWT auth)
  - User and Tenant Admin Web UI (:443 -> menubuilder-backend:8000)
  - Terminal menu builder, categories, items
  - User-facing billing API (/api/billing, /api/certificate-pin)
```

---

## 3. Repository Structure & Subprojects

| Directory / Subproject | Technology / Framework | Description | Entry Point / Key Files |
|---|---|---|---|
| **`ProcessingBackend/backend`** | Python 3.14, FastAPI, SQLAlchemy (asyncpg), Alembic | Core mTLS payment processing REST/XML API (payments, tech gate, license billing check, balance tracking, terminal menus `GET /api/ListMenuFile`, Alembic migrations) | `uvicorn app.main:app` |
| **`ProcessingBackend/mcp-pin-server`** | Python 3.14, FastMCP / MCP SDK | Model Context Protocol server for PIN operations & certificate tools | `python -m pin_server.server` |
| **`MenuBuilder/backend`** | Python 3.14, FastAPI, SQLAlchemy | Tenant/admin portal, terminal menu management, and **user billing API** (`/api/billing`, `/api/certificate-pin`, `/api/admin/organizations`) | `uvicorn app.main:app` |
| **`MenuBuilder/frontend`** | React, TypeScript, Vite, Tailwind CSS | Web UI for configuring terminal payment menus, license cart, organizations, and billing | `npm run build` / `npm run dev` |
| **`BACK/`** | Legacy C#, ASP.NET (.NET Framework) | Legacy processing core (`ProcessingCore/EtranDispatcher`, SOAP processors) | `Global.asax`, `EtranDispatcher.asmx` |
| **`FRONT/`** | Legacy ASP.NET | Legacy front-facing web apps (`TechGate`, `GateGauge`, `licensebilling`, `Certificates`) | Web endpoints |
| **`CommonLibs/`** | Legacy .NET Framework | Shared legacy C# utility libraries and DLLs | Visual Studio Solution |
| **`GateYandexMoney/`** | Legacy C# | External payment gateway integration for Yandex.Money | Web services |
| **`migrate/`** | Python / SQL scripts | Database migration utilities for transitioning MSSQL data to PostgreSQL | Migration scripts |
| **`stored-procedures/`** | T-SQL | Legacy MSSQL stored procedures for business logic, routing, and ledger tracking | `.sql` scripts |
| **`docs/`** | Markdown | Comprehensive architecture guides, UX requirements (`docs/billing-cart-ux-requirements.md`), DevOps runbooks | `docs/` |

---

## 4. Development & Code Quality Guidelines

### Python Standards
- **Python Version**: Target **Python 3.14** (`requires-python = "==3.14.*"`).
- **Package Manager**: Use `uv` for dependency management and running tools.
- **Linters & Formatters**: Before committing or deploying, ensure all three checks pass in Python subprojects:
  ```bash
  uv run ruff check --fix <src_dir>
  uv run ruff format <src_dir>
  uv run pyright <src_dir>
  ```
- **Async Best Practices**: Use asynchronous SQLAlchemy 2.0 sessions (`AsyncSession`), `select()` syntax, and avoid blocking synchronous I/O.

### Frontend Standards (MenuBuilder/frontend)
- Use TypeScript with strict typing.
- Run `npm run build` to verify production bundle creation.

---

## 5. Security & Secrets Policy

**NEVER** commit or hardcode secrets, passwords, database credentials, internal tokens, or production API keys.

1. **Environment Variables**:
   - Store secrets only in `.env` files (never committed to git) or pass them via environment variables.
   - Pydantic Settings classes must use `extra = "ignore"` to tolerate shared `.env` files.
   - Default values for configuration keys must remain empty strings or non-sensitive local defaults (`localhost`, `[]`).
2. **Database Credentials**:
   - Database connection URLs in code must never contain hardcoded username/password credentials.
3. **JWT Claims & Auth**:
   - `org_id` claims inside JWT tokens may arrive as strings — always cast to `int` at the authentication boundary (`get_current_user`).

---

## 6. Testing & Verification

- Run test suites using `uv run pytest` in the respective subproject directories (`ProcessingBackend/backend`, `MenuBuilder/backend`).
- When writing tests for authenticated routes:
  - Generate short-lived mock JWT tokens using the application's internal token generator (`create_access_token`).
  - Use simulated certificate headers (`X-Client-Cert-DN`, `X-Client-Cert-Serial`) when testing terminal-authenticated endpoints.
- Clean up any temporary test scripts, scratch SQL files, or test tokens after verification.

---

## 7. Deployment & Operations

- **Infrastructure & Hosts**:
  - **Primary App Server**: `176.108.247.249` (user `user1`, SSH key `d:\.ssh\free-tier-cloud_ru`). Hosts `processing-backend`, `menubuilder-backend`, `menubuilder-frontend`, `mcp-pin-server`, `postgres`, `rabbitmq`.
  - **Legacy mTLS Proxy Server**: `87.242.100.34` (user `user1`, SSH key `d:\.ssh\id_ed25519`). Hosts `nginx-mutual-legacy` handling mTLS on port 443.
- **Container Orchestration**: Docker Compose is used for deploying backend services, Nginx mutual TLS proxy, and PostgreSQL (`docker-compose.yaml`). `sudo` is required for docker commands over SSH.
- **Database Migrations (Alembic)**:
  - Migrations are defined under `ProcessingBackend/backend/alembic/versions/`.
  - In production, apply migrations via `sudo docker exec processing-backend alembic upgrade head`.
  - If schema changes affect shared models, restart `menubuilder-backend` afterwards (`sudo docker restart menubuilder-backend`).
- **Frontend Live Mounts**: `menubuilder-frontend` bind-mounts `./frontend/dist/`. Building on host updates files live without full container restart (restart only needed for `nginx.conf` changes).
- **Legacy IIS Deployments**: Legacy ASP.NET endpoints use an App_Code dynamic compilation model (no-compile deployment) as documented in `ProcessingBackend/docs/devops-runbook.md`.

---

## 8. MCP Operations Server (`server-ops`) & Task Readiness Protocol

An operations MCP server (`server-ops`) is attached to the primary application server (`176.108.247.249`). It enables system health inspection, log analysis, Nginx/SSL operations, and safe diagnostic workflows.

### Available MCP Ops Capabilities
- **System monitoring**: `mcp_server-ops_system_info`, `mcp_server-ops_memory_analysis`, `mcp_server-ops_disk_analysis`, `mcp_server-ops_service_status`
- **Log search & diagnostics**: `mcp_server-ops_log_search` (filtering by `keyword`, `level`: `error`/`warning`/`info`), `mcp_server-ops_log_read`, `mcp_server-ops_log_search_system` (`journalctl`), `mcp_server-ops_log_list`
- **Nginx & Certbot**: `mcp_server-ops_nginx_config_read`, `mcp_server-ops_nginx_config_test`, `mcp_server-ops_nginx_reload`, `mcp_server-ops_certbot_install`, `mcp_server-ops_certbot_renew`
- **Files & Safe execution**: `mcp_server-ops_file_list`, `mcp_server-ops_file_read`, `mcp_server-ops_file_search`, `mcp_server-ops_command_exec`
- **Project audit & confirmation**: `mcp_server-ops_project_overview`, `mcp_server-ops_config_audit` (auto-masks secrets), `mcp_server-ops_confirm_execute`

### Mandatory Task Readiness Verification Protocol (Признак готовности к задаче)
Before using MCP Ops capabilities in any task (debugging, deployment verification, diagnostics, or configuration change), the agent **MUST** verify readiness and establish an explicit task readiness status:

1. **Connectivity & Resource Pre-flight Check**:
   - Probe server connectivity and load: execute `mcp_server-ops_system_info(type="load")` or `mcp_server-ops_service_status(service="nginx")`.
   - Check host resource safety thresholds:
     - Available RAM > 300 MiB (`system_info` / `memory_analysis` — critical since server has 0B Swap).
     - Root disk usage < 90% (`system_info` / `disk_analysis`).
     - Load average within healthy limits (< 2.0).
2. **Readiness Status Declaration**:
   - Explicitly record the status in the task plan / context:
     - `[MCP Ops Readiness: READY]` — all checks passed; MCP tools may be used for diagnostics, log analysis, and verification.
     - `[MCP Ops Readiness: DEGRADED / UNAVAILABLE]` — MCP unreachable or resource limits exceeded; fall back to standard SSH runbook commands.
3. **Safety Guardrails**:
   - State-changing operations (`nginx_reload`, `file_write`, `file_delete`) return a `confirmationId` and MUST be explicitly confirmed via `mcp_server-ops_confirm_execute`.
   - Never inspect raw secret files directly: use `mcp_server-ops_config_audit` for inspecting `.env` configurations.
