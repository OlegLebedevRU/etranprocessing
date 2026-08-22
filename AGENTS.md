# AGENTS.md — Project Rules

## Secrets and credentials

**NEVER** hardcode secrets, API keys, passwords, tokens, database URLs, or internal URLs in any tracked file. This includes:

- **Python source** (`.py`) — no credentials in `os.getenv()` defaults, no hardcoded DB URLs
- **Config files** (`config.py`, `settings.py`) — defaults must be empty or localhost-only
- **Docker / YAML** (`docker-compose.yaml`, `*.yml`) — use `env_file:` or `${VAR}` references, never inline credentials
- **Documentation / skills** (`.md`, `SKILL.md`) — no scripts containing real credentials, passwords, or DB URLs with auth
- **Shell scripts** (`.sh`) — no hardcoded secrets; read from env

All sensitive values must come from environment variables or `.env` files.

### Rules

- Use `os.environ.get("KEY")`, `os.environ["KEY"]`, `pydantic-settings`, or similar mechanisms
- `.env` files must be in `.gitignore` and never committed
- Config classes must use `extra = "ignore"` to tolerate shared `.env` files
- Default values in config should be empty strings or non-sensitive placeholders (e.g. `localhost`, `[]`)
- Database URLs must never contain credentials in code — require the env var
- If you discover a secret in code, remove it immediately and rotate the credential

### Pre-commit / pre-deploy scan

```bash
# DB URLs with credentials in any tracked file
grep -rn "postgresql://.*:.*@" --include="*.py" --include="*.yaml" --include="*.yml" --include="*.toml" --include="*.md"

# Hardcoded non-empty secret defaults
grep -rn 'os\.getenv(.*,\s*"[^"]\{8,\}")' --include="*.py"

# Production URLs in code (not in .env)
grep -rn "https://dev\.\|https://api\.\|https://prod\." --include="*.py"
```

Zero matches expected. If found, move to `.env`.

## Code quality

Before any commit or deployment, all three checks must pass in every Python subproject:

```bash
uv run ruff check --fix <src_dir>
uv run ruff format <src_dir>
uv run pyright <src_dir>
```

## Python version & Backend Guidelines

All projects target **Python 3.14** (`requires-python = "==3.14.*"`).

Detailed backend code standards, architecture rules, and patterns are documented in **`ProcessingBackend/GUIDELINES.md`**. Key rules include:
- **Shared DB Models (`shared/etranprocessing_db`)**: Single source of truth for all SQLAlchemy 2.0 ORM models for both `ProcessingBackend` and `MenuBuilder`.
- **Thin DB Layer Principle**: `etranprocessing_db` contains *strictly* declarative models, constraints, and relationships. No business logic, auth/crypto utilities, or framework dependencies.
- **FastAPI dependency injection**: `B008` is ignored; use `Depends()` in default argument positions.
- **SQLAlchemy 2.0 Declarative**: Use `Mapped[T] = mapped_column(...)` for models.
- **Python 3.14 Exception Handling**: Prefer `with contextlib.suppress(SpecificException):` over `try...except...pass` (`SIM105`). Never use silent broad `except Exception: pass`. For cleanup/rollback paths, explicitly catch `Exception` and log or document intent (`with contextlib.suppress(Exception):`).

## Project structure & Service Boundaries

| Subproject | Technology / Framework | Role & Service Boundary | Entry point |
|---|---|---|---|
| **`shared/` (`etranprocessing_db`)** | Python 3.14, SQLAlchemy 2.0, asyncpg | Shared thin declarative ORM model layer for both backends (23 unified models, constraints, indexes). | `import etranprocessing_db` |
| **`ProcessingBackend/backend`** | Python 3.14, FastAPI, SQLAlchemy (asyncpg), Alembic | Core mTLS payment processing gateway, terminal XML/SOAP handlers (`/api/payment`, `/api/techgate`, `/api/gategauge`, `/api/licensebilling`, `/api/certificates`, `GET /api/ListMenuFile`). Sole authority for Alembic migrations. **No user-facing JWT routes.** | `uvicorn app.main:app` |
| **`MenuBuilder/backend`** | Python 3.14, FastAPI, SQLAlchemy | Tenant & admin web portal, terminal menu management, and **user-facing billing API** (`/api/billing`, `/api/certificate-pin`, `/api/admin/organizations`, JWT authentication). | `uvicorn app.main:app` |
| **`MenuBuilder/frontend`** | React 19, TypeScript, Vite, Ant Design v6 | Web UI for tenant administrators, terminal menu builder, license cart, and admin panels (Code Splitting, Design Tokens, multi-tenant). | `npm run build` / `npm run dev` |
| **`ProcessingBackend/mcp-pin-server`** | Python 3.14, FastMCP / MCP SDK | Model Context Protocol server for PIN operations & certificate tools. | `python -m pin_server.server` |

## Infrastructure & Servers

- **Primary Application Server**: `176.108.247.249` (user: `user1`, SSH key: `d:\.ssh\free-tier-cloud_ru`)
  - Runs Docker containers: `processing-backend`, `menubuilder-backend`, `menubuilder-frontend`, `mcp-pin-server`, `postgres`, `rabbitmq`, etc.
  - Reverse proxy Nginx on port 443 routes `/api/billing/` and admin/portal routes to `menubuilder-backend:8000`, and terminal endpoints to `processing-backend:8000`.
- **Legacy mTLS Reverse Proxy Server**: `87.242.100.34` (user: `user1`, SSH key: `d:\.ssh\id_ed25519`)
  - Runs `nginx-mutual-legacy` (terminates client mTLS and forwards requests to `176.108.247.249`).

## CI/CD — lessons learned (verified in production sessions)

These are working, verified practices for deploying and verifying changes on
the production host (`176.108.247.249`). See also each subproject's
`.claude/skills/deploy-*/SKILL.md` for the full step-by-step deploy flow.

- **`docker` on the server requires `sudo`.** Plain `docker ...` / `docker
  compose ...` fails with "permission denied while trying to connect to the
  Docker daemon socket". Always prefix with `sudo docker ...` /
  `sudo docker compose ...` over SSH.
- **Database migrations (Alembic):** Alembic migrations reside in
  `ProcessingBackend/backend/alembic/`. To apply migrations in production:
  1. Upload migration files (e.g. `012_add_org_contacts_and_billing_modes.py`) to the server.
  2. Copy into the container or rebuild `processing-backend`:
     `sudo docker cp /home/user1/ProcessingBackend/backend/alembic/versions/. processing-backend:/app/alembic/versions/`
  3. Execute: `sudo docker exec processing-backend alembic upgrade head`
  4. If database schema was altered with new columns/tables, restart dependent containers (e.g. `sudo docker restart menubuilder-backend`).
- **JWT `org_id` claims travel as strings — cast to `int` at the auth
  boundary, once.** Any endpoint that binds an org id into a raw SQL query
  against an integer column will raise `asyncpg.exceptions.DataError` if it
  receives the JWT claim unconverted. Fix it in the shared
  `get_current_user()`/auth dependency, not per-endpoint.
- **Bind-mounted frontend containers don't need a restart after `npm run
  build`.** If `docker-compose.yaml` mounts `dist/` as a live volume (see
  MenuBuilder's `menubuilder-frontend`), rebuilding on the host is sufficient
  — nginx picks up the new files immediately. Only restart the container for
  `nginx.conf`/cert/env changes. Verify by comparing file mtimes inside vs.
  outside the container (watch for timezone offsets between host and
  container when eyeballing timestamps).
- **Testing authenticated endpoints without real credentials:** generate a
  short-lived JWT directly inside the target container using the app's own
  `create_access_token()` (same secret, same claim names), then curl the
  endpoint with it. This verifies a fix end-to-end post-deploy without ever
  needing/typing a real user's password.
- **SSH execution from Windows PowerShell:** When invoking remote commands via `ssh` from PowerShell, use `ssh -n ...` (e.g. `ssh -n -i d:\.ssh\free-tier-cloud_ru ...`) to prevent stdin handle blocking.
- **PowerShell → `ssh` → remote shell quoting is fragile for inline Python.**
  Multi-line `python -c "..."` one-liners with parentheses/dict literals
  reliably get mangled through nested PowerShell/ssh/remote-shell quoting.
  Prefer: write the script to a local file, `scp` it to `/tmp` on the server,
  `docker cp` it into the container, then `docker exec <container> python
  /tmp/script.py`. Delete the temp script (locally and on the server/
  container) once done — never leave test scripts or generated tokens lying
  around.
- **After any lint/build/deploy/verification pass, clean up temp
  artifacts** (scp'd test scripts, generated tokens, scratch SQL files) from
  both the server and the local session workspace.

## MCP Operations Server (`server-ops`) & DevOps Protocol

An operations MCP server (`server-ops`) is connected to the primary application server (`176.108.247.249`). It provides tools for system health inspection, log investigation, Nginx and SSL management, and safe command execution.

### Available MCP Ops Capabilities
- **System monitoring**: `mcp_server-ops_system_info`, `mcp_server-ops_memory_analysis`, `mcp_server-ops_disk_analysis`, `mcp_server-ops_service_status`
- **Log search & diagnostics**: `mcp_server-ops_log_search` (filtering by `keyword`, `level`: `error`/`warning`/`info`), `mcp_server-ops_log_read`, `mcp_server-ops_log_search_system` (`journalctl`), `mcp_server-ops_log_list`
- **Nginx & Certbot**: `mcp_server-ops_nginx_config_read`, `mcp_server-ops_nginx_config_test`, `mcp_server-ops_nginx_reload`, `mcp_server-ops_certbot_install`, `mcp_server-ops_certbot_renew`
- **Files & Command execution**: `mcp_server-ops_file_list`, `mcp_server-ops_file_read`, `mcp_server-ops_file_search`, `mcp_server-ops_command_exec` (whitelisted safe commands)
- **Project audit & confirmation**: `mcp_server-ops_project_overview`, `mcp_server-ops_config_audit` (auto-masks secrets), `mcp_server-ops_confirm_execute` (2FA confirmation for mutating actions)

### Mandatory Task Readiness Verification Protocol (Признак готовности к задаче)
Before utilizing MCP Ops capabilities in any debugging, deployment, diagnostics, or configuration task, the agent **MUST** verify readiness and establish an explicit task readiness status:

1. **Connectivity & Resource Pre-flight Check**:
   - Probe server connectivity and load: call `mcp_server-ops_system_info(type="load")` or `mcp_server-ops_service_status(service="nginx")`.
   - Check host resource safety thresholds:
     - Available RAM > 300 MiB (`system_info` / `memory_analysis` — critical since server has 0B Swap).
     - Root disk usage < 90% (`system_info` / `disk_analysis`).
     - Load average within healthy limits (< 2.0).
2. **Readiness Status Declaration & Non-Blocking Policy**:
   - Explicitly record the status in task context / plan:
     - `[MCP Ops Readiness: READY]` — all checks passed; MCP tools may be used for diagnostics, log analysis, and verification.
     - `[MCP Ops Readiness: DEGRADED / UNAVAILABLE]` — MCP unreachable or resource thresholds exceeded; fall back to standard SSH runbook commands (`user1@176.108.247.249`, key `d:\.ssh\free-tier-cloud_ru`).
   - **CRITICAL: `[MCP Ops Readiness: UNAVAILABLE]` (или `DEGRADED`) НЕ блокирует деплой (Non-Blocking)**. Отсутствие подключения или недоступность MCP Ops сервера не является причиной для отмены или задержки деплоя. Сборка, деплой, применение миграций и верификация в этом случае выполняются в штатном режиме через прямой SSH-транспорт (`scp`, `ssh sudo docker ...`) без блокировок.
3. **Safety Guardrails**:
   - State-changing operations (`nginx_reload`, `file_write`, `file_delete`) return a `confirmationId` and MUST be verified before confirmation via `mcp_server-ops_confirm_execute`.
   - Never inspect raw secret files directly: use `mcp_server-ops_config_audit` for inspecting `.env` configurations.
