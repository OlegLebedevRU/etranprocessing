# FastAPI & Backend Guidelines

You are an expert in Python 3.14+, FastAPI, SQLAlchemy 2.0+, PostgreSQL, and scalable async web application development. You write secure, maintainable, and performant code following modern Python, FastAPI, and database best practices.

---

## 1. Python Best Practices & Syntax

- Follow PEP 8 with a **120 character line limit**.
- Target **Python 3.14** with `requires-python = "==3.14.*"` in `pyproject.toml`.
- Use `async/await` throughout for non-blocking I/O.
- Use type hints on all function signatures and class attributes (`Mapped[T]`, modern union syntax `int | None`, PEP 649 deferred evaluation).
- Use `pydantic` (v2) for data validation and serialization.
- Use f-strings for string formatting.
- Use `ruff` for linting and formatting (replaces `black`, `flake8`, and `isort`).

### Exception Handling & `try-except-pass` in Python 3.14

- **Prefer `contextlib.suppress()` for intentional suppression**:
  Use `with contextlib.suppress(SpecificException):` instead of verbose `try...except SpecificException: pass` blocks (enforces Ruff rule `SIM105`).
  ```python
  import contextlib

  # ✅ Idiomatic modern Python:
  with contextlib.suppress(FileNotFoundError):
      os.remove(temp_file_path)

  # ❌ Avoid:
  try:
      os.remove(temp_file_path)
  except FileNotFoundError:
      pass
  ```
- **Never use bare `except:` or silent broad exception suppression**:
  Catching `except Exception: pass` silently masks unexpected bugs, syntax errors, and task cancellations. In Python 3.14 / `asyncio` task groups, broad suppression can incorrectly suppress parts of an `ExceptionGroup` (PEP 654).
- **Graceful degradation in cleanup / rollback paths**:
  If an exception must be caught during session cleanup, rollback, or secondary metric reporting, explicitly catch `Exception`, log it at `debug` level, or document the intent with `# noqa: BLE001, S110` / `with contextlib.suppress(Exception):`.
  ```python
  try:
      await db.commit()
  except Exception as e:
      with contextlib.suppress(Exception):
          await db.rollback()
      logger.warning("Transaction failed, rolled back: %s", e)
      raise
  ```

---

## 2. Code Quality & Pre-Commit Checks

Before any commit or deployment modifying Python code, all three checks must pass in the target Python subproject (skipped if changes are strictly limited to documentation, `tools/`, or legacy code without modifying `ProcessingBackend` or `MenuBuilder`):

```bash
uv run ruff check --fix <src_dir>
uv run ruff format <src_dir>
uv run pyright <src_dir>
```

### Subproject `pyproject.toml` Configuration

```toml
[tool.ruff]
line-length = 120
target-version = "py314"

[tool.ruff.lint]
extend-select = [
    "E",    # pycodestyle errors
    "F",    # Pyflakes
    "W",    # pycodestyle warnings
    "I",    # isort
    "UP",   # pyupgrade
    "B",    # flake8-bugbear
    "SIM",  # flake8-simplify
]
ignore = [
    "B008",  # FastAPI Depends() in default args is idiomatic
    "E501",  # Line length is handled by ruff format
]

[tool.pyright]
pythonVersion = "3.14"
typeCheckingMode = "standard"
```

---

## 3. Secrets and Credentials

**NEVER** hardcode secrets, API keys, passwords, tokens, database URLs, or internal URLs in any tracked file.

- **Python source** (`.py`) — no credentials in `os.getenv()` defaults, no hardcoded DB URLs.
- **Config files** (`config.py`, `settings.py`) — defaults must be empty or localhost-only (`localhost`, `[]`, `""`).
- **Docker / YAML** (`docker-compose.yaml`, `*.yml`) — use `env_file:` or `${VAR}` references, never inline credentials.
- **Documentation / skills** (`.md`) — no real credentials, passwords, or DB URLs with auth.
- **Shell scripts** (`.sh`) — read secrets from environment variables.

### Rules

- Use `pydantic-settings` (`BaseSettings`) or `os.environ.get("KEY")` for configuration.
- `.env` files must be in `.gitignore` and never committed.
- Config classes must use `extra = "ignore"` to tolerate shared `.env` files.
- Database URLs must never contain credentials in code — require the environment variable.
- If you discover a secret in code, remove it immediately and rotate the credential.

### Pre-commit / Pre-deploy Scan

```bash
# DB URLs with credentials in any tracked file
grep -rn "postgresql://.*:.*@" --include="*.py" --include="*.yaml" --include="*.yml" --include="*.toml" --include="*.md"

# Hardcoded non-empty secret defaults
grep -rn 'os\.getenv(.*,\s*"[^"]\{8,\}")' --include="*.py"

# Production URLs in code (not in .env)
grep -rn "https://dev\.\|https://api\.\|https://prod\." --include="*.py"
```

Zero matches expected. If found, move to `.env`.

---

## 4. Repository Architecture & Service Boundaries

The repository is organized as a microservice ecosystem:

```
etranprocessing/
├── shared/                        # Shared DB layer (`etranprocessing_db`)
│   ├── pyproject.toml             # Local package metadata (dependencies: sqlalchemy, asyncpg)
│   └── etranprocessing_db/
│       ├── base.py                # Base = DeclarativeBase
│       └── models/                # All 23 merged SQLAlchemy 2.0 ORM models
│           ├── org.py             # Org, OrgBillingSettings, OrgStatus
│           ├── terminal.py        # TerminalType, Terminal, License, TerminalCertHistory, TerminalCertDiscovery
│           ├── menu.py            # MenuVariant, Group, Service, TerminalMenuBinding
│           ├── billing.py         # BillingOrder, BillingOrderItem, CertificatePin
│           ├── payment.py         # Tsp, TspParameterCode, Payment, PaymentParam, BalanceTerminalTsp
│           ├── telemetry.py       # GateGaugeRecord, TechGateRecord
│           └── auth.py            # ApiToken
├── ProcessingBackend/
│   ├── backend/                   # Core mTLS payment gateway & terminal handlers
│   │   ├── app/
│   │   │   ├── main.py            # FastAPI initialization & error handlers
│   │   │   ├── config.py          # Settings via pydantic-settings
│   │   │   ├── database.py        # Async SQLAlchemy engine & sessionmaker (imports Base from etranprocessing_db)
│   │   │   ├── dependencies.py    # mTLS cert auth & terminal licensing dependencies
│   │   │   ├── models.py          # Re-exports from etranprocessing_db.models
│   │   │   ├── routers/           # payment, tech_gate, gate_gauge, licensebilling, list_menu, certificates
│   │   │   ├── schemas/           # Pydantic request/response models
│   │   │   └── services/          # ca.py, cert_billing.py, payment_service.py, sn.py
│   │   ├── alembic/               # Database migrations (Sole source of truth for migrations)
│   │   └── pyproject.toml
│   └── mcp-pin-server/            # FastMCP server for PIN operations & certificate tools
├── MenuBuilder/
│   ├── backend/                   # Tenant admin portal & User Billing API
│   │   ├── app/
│   │   │   ├── main.py            # FastAPI initialization & CORS
│   │   │   ├── auth.py            # JWT authentication & password hashing
│   │   │   ├── database.py        # Async SQLAlchemy engine & sessionmaker (imports Base from etranprocessing_db)
│   │   │   ├── models.py          # Re-exports from etranprocessing_db.models
│   │   │   ├── routers/           # billing, certificate_pin, admin_organizations, groups, services
│   │   │   ├── schemas/           # Pydantic schemas (billing.py, certificate_pin.py)
│   │   │   └── services/          # billing.py, payment_provider.py
│   │   └── pyproject.toml
│   └── frontend/                  # React 19 + TypeScript + Ant Design v6 UI
└── docs/                          # Architecture & UX documentation
```

### Protocol Guidelines: REST vs Terminal Gateways

- **User & Admin API (`MenuBuilder/backend`)**:
  - Full REST standard with JSON request/response payloads.
  - Pydantic models for validation and serialization.
  - JWT Bearer authentication with `org_id` cast to `int`.
- **Payment Processing Gateway (`ProcessingBackend/backend`)**:
  - Mutual TLS authentication via Nginx identity headers (`X-Client-Cert-DN`, `X-Client-Cert-Serial`).
  - Terminal protocols maintain legacy format compatibility:
    - `GET /api/ListMenuFile` — JSON menu tree for kiosks.
    - `POST /gate_gauge`, `POST /tech_gate` — XML payload gateways.
    - `POST /licensebilling/service.asmx` — SOAP/XML license billing emulation.
    - `POST /api/certificates/...` — CA certificate enrollment.

---

## 5. Models (SQLAlchemy 2.0 & Shared `etranprocessing_db`)

- **Single Source of Truth**: All ORM models are declared in the shared package `shared/etranprocessing_db` (`etranprocessing_db.models`). Both `ProcessingBackend` and `MenuBuilder` consume models from this package.
- **Thin DB Layer Principle**:
  - `etranprocessing_db` contains **strictly declarative models**, constraints, indexes, and relationships.
  - **No business logic, token generators, hashing, cryptography, or endpoint frameworks** may be added to `etranprocessing_db`.
  - The shared package dependencies are strictly limited to `sqlalchemy[asyncio]` and `asyncpg`.
- Use modern **SQLAlchemy 2.0 Declarative** syntax with `Mapped[T]` and `mapped_column()`.
- Use `DateTime(timezone=True)` with `server_default=func.now()` for timestamps.
- Explicitly configure foreign keys with `ForeignKey(...)` and appropriate `ondelete` actions.
- Use `relationship(back_populates=...)` for bidirectional relationships.
- Add `__table_args__` for indexes on frequently queried columns (`org_id`, `sn`, `cert_serial`, etc.).
- Respect existing database schemas: new tables should include standard timestamps, while legacy migrated tables (`orgs`, `tsp`) retain their defined primary keys (`org_id`, `tsp_id`).

### Database Migrations & Schema Evolution Regulations (Alembic)

1. **Sole Migration Authority**: All schema modifications and migrations **MUST** reside exclusively in `ProcessingBackend/backend/alembic/versions/`.
2. **No `create_all()` in Production**: `MenuBuilder` (and other services) must never call `Base.metadata.create_all()` in runtime lifespans.
3. **Autogeneration Coverage**: `alembic/env.py` imports `Base` and all models from `etranprocessing_db`, ensuring complete schema awareness for `alembic revision --autogenerate`.
4. **Applying Migrations**:
   ```bash
   sudo docker exec processing-backend alembic upgrade head
   ```
5. **Post-Migration Service Restart**: If shared models/tables are modified, restart dependent services:
   ```bash
   sudo docker restart menubuilder-backend
   ```
6. **Zero-Downtime Migration Policy (Expand / Contract)**:
   - *Phase 1 (Expand)*: Add new columns as nullable or with defaults; deploy new model code.
   - *Phase 2 (Contract)*: Remove legacy columns or constraints only after all services have transitioned to the new schema.

---

## 6. Database Queries & Async SQLAlchemy

### Connection & Session Management

- Use **async SQLAlchemy** with `asyncpg` driver for PostgreSQL.
- Create a single `async_sessionmaker` per application instance.
- Always use dependency injection (`Depends(get_db)`) to provide sessions to route handlers.
- Never hold sessions across multiple HTTP requests; create and close per-request.

```python
from collections.abc import AsyncGenerator
from sqlalchemy.ext.asyncio import AsyncSession, async_sessionmaker, create_async_engine

engine = create_async_engine(DATABASE_URL, echo=False, pool_size=20, max_overflow=0)
async_session_factory = async_sessionmaker(engine, expire_on_commit=False)

async def get_db() -> AsyncGenerator[AsyncSession, None]:
    async with async_session_factory() as session:
        yield session
```

### Query Optimization

- Use `select()` with SQLAlchemy 2.0 style syntax.
- Eagerly load related objects with `selectinload()` or `joinedload()` to eliminate N+1 queries.
- Never iterate over ORM objects in Python to perform filtering; filter inside the database query.

```python
from sqlalchemy import select
from sqlalchemy.orm import selectinload

# ✅ Eager loading: single roundtrip with relation
stmt = (
    select(Terminal)
    .where(Terminal.org_id == org_id)
    .options(selectinload(Terminal.licenses))
)
result = await db.execute(stmt)
terminals = result.scalars().all()
```

---

## 7. Views, Schemas & Authentication

### Pydantic Schemas

- Define separate Pydantic schemas for requests, responses, and updates.
- Use `ConfigDict(from_attributes=True)` for ORM object serialization.
- Never expose internal passwords or unneeded raw database columns.

```python
from datetime import datetime
from pydantic import BaseModel, ConfigDict, Field

class TerminalBase(BaseModel):
    sn: str = Field(..., min_length=1, max_length=100)
    address: str | None = Field(None, max_length=500)

class TerminalCreate(TerminalBase):
    org_id: int

class TerminalResponse(TerminalBase):
    id: int
    org_id: int
    is_active: bool
    created_at: datetime

    model_config = ConfigDict(from_attributes=True)
```

### Authentication Boundary & JWT

- User and admin routes use JWT Bearer authentication.
- Always cast `org_id` claim to `int` at the `get_current_user` boundary.

```python
def get_current_user(token: str = Depends(oauth2_scheme)) -> AuthenticatedUser:
    try:
        payload = jwt.decode(token, JWT_SECRET, algorithms=[JWT_ALGORITHM])
        user_id: str = payload.get("sub", "")
        raw_org = payload.get("org") or payload.get("org_id", 0)
        org_id = int(raw_org)
        return AuthenticatedUser(user_id=user_id, org_id=org_id)
    except Exception as exc:
        raise HTTPException(status_code=401, detail="Invalid token") from exc
```

---

## 8. Deployment & CI/CD Operations

### Docker on Production Server (`87.242.100.34`)

- `docker` and `docker compose` commands over SSH require `sudo`.
- Connect via SSH: `ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34`.
- Rebuild containers with unified compose file:
  ```bash
  sudo docker compose -f /home/user1/compose.yaml up -d --build processing-backend mcp-pin-server
  sudo docker compose -f /home/user1/compose.yaml up -d --build menubuilder-backend
  ```
- Use `ssh -n ...` when executing remote commands from Windows PowerShell to prevent stdin stream blocking.

### MCP Operations Server (`server-ops`)

Follow the Mandatory Task Readiness Verification Protocol before initiating remote operations:
1. Probe server health (`mcp_server-ops_system_info(type="load")`, `mcp_server-ops_system_info(type="memory")`, `mcp_server-ops_system_info(type="disk")`).
2. Verify thresholds: RAM > 300 MiB, Disk < 90%, Load < 2.0.
3. If MCP is degraded or unavailable, proceed non-blocking via direct SSH transport.
