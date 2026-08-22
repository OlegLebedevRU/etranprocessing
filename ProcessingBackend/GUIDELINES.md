# FastAPI Guidelines

You are an expert in Python 3.14+, FastAPI, SQLAlchemy, PostgreSQL, and scalable async web application development. You write secure, maintainable, and performant code following FastAPI, Python, and database best practices.

## Python Best Practices

- Follow PEP 8 with 120 character line limit
- Use double quotes for Python strings
- Use `ruff` for linting and formatting (no `isort` — `ruff` handles import sorting)
- Use f-strings for string formatting
- Target **Python 3.14+** with `requires-python = "==3.14.*"` in `pyproject.toml`
- Use `async/await` throughout for non-blocking I/O
- Prefer type hints on all function signatures and class attributes
- Use `pydantic` for data validation and serialization

## Code Quality & Pre-Commit Checks

Before any commit, all three checks must pass:

```bash
uv run ruff check --fix <src_dir>
uv run ruff format <src_dir>
uv run pyright <src_dir>
```

Configure `pyproject.toml`:

```toml
[tool.ruff]
line-length = 120
target-version = "py314"

[tool.ruff.lint]
select = ["E", "F", "W", "I", "UP", "B", "SIM", "ARG"]
ignore = []

[tool.pyright]
pythonVersion = "3.14"
typeCheckingMode = "strict"
```

## Secrets and Credentials

**NEVER** hardcode secrets, API keys, passwords, tokens, database URLs, or internal URLs in any tracked file. This includes:

- **Python source** (`.py`) — no credentials in `os.getenv()` defaults, no hardcoded DB URLs
- **Config files** (`config.py`, `settings.py`) — defaults must be empty or localhost-only
- **Docker / YAML** (`docker-compose.yaml`, `*.yml`) — use `env_file:` or `${VAR}` references, never inline credentials
- **Documentation / skills** (`.md`) — no scripts containing real credentials, passwords, or DB URLs with auth
- **Shell scripts** (`.sh`) — no hardcoded secrets; read from env

### Rules

- Use `os.environ.get("KEY")` or `pydantic-settings` (`BaseSettings`) for configuration
- `.env` files must be in `.gitignore` and never committed
- Config classes must use `extra = "ignore"` to tolerate shared `.env` files
- Default values in config should be empty strings or non-sensitive placeholders (e.g. `localhost`, `[]`, `0`)
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

## FastAPI Best Practices

- Use **async views** for all route handlers unless there is a blocking, synchronous-only dependency
- Leverage `Depends()` for dependency injection (middleware, auth, database sessions)
- Organize routes into APIRouter blueprints by feature or domain
- Always validate and sanitize user input via Pydantic models
- Return appropriate HTTP status codes and error responses (use `HTTPException`)
- Document all endpoints with docstrings and OpenAPI decorators (`tags`, `summary`, `description`)
- Use middleware sparingly and document cross-cutting concerns
- Implement structured logging with context (request ID, user ID, etc.)

### Project Structure

```
backend/
├── app/
│   ├── __init__.py
│   ├── main.py                    # FastAPI app initialization
│   ├── config.py                  # Settings via pydantic-settings
│   ├── database.py                # SQLAlchemy engine, session factory
│   ├── api/
│   │   ├── __init__.py
│   │   ├── v1/
│   │   │   ├── __init__.py
│   │   │   ├── payment/
│   │   │   │   ├── __init__.py
│   │   │   │   ├── routes.py      # APIRouter
│   │   │   │   ├── schemas.py     # Pydantic models
│   │   │   │   ├── crud.py        # Database operations
│   │   │   │   └── models.py      # SQLAlchemy models (optional, see Models section)
│   │   │   └── auth/
│   │   │       ├── __init__.py
│   │   │       ├── routes.py
│   │   │       ├── schemas.py
│   │   │       ├── jwt.py         # JWT logic
│   │   │       └── dependencies.py # get_current_user, etc.
│   ├── models/
│   │   ├── __init__.py
│   │   ├── payment.py             # Payment model
│   │   ├── user.py
│   │   ├── organization.py
│   │   └── base.py                # Base model class
│   ├── schemas/
│   │   ├── __init__.py
│   │   └── ...                    # Pydantic request/response models
│   ├── middleware/
│   │   ├── __init__.py
│   │   └── logging.py             # Request/response logging
│   ├── utils/
│   │   ├── __init__.py
│   │   ├── errors.py              # Custom exceptions
│   │   └── logger.py              # Logging setup
│   └── deps.py                    # Common dependencies (DB session, auth)
├── tests/
│   ├── __init__.py
│   ├── conftest.py                # Pytest fixtures
│   ├── test_auth.py
│   ├── test_payment.py
│   └── ...
├── alembic/                       # Database migrations
│   ├── env.py
│   ├── script.py.mako
│   └── versions/
│       └── ...
├── pyproject.toml
├── Dockerfile
├── docker-compose.yaml
└── .env.example
```

## Models (SQLAlchemy)

- Define models in separate modules under `app/models/` or alongside routes
- Always inherit from a `Base` declarative class with common columns (`id`, `created_at`, `updated_at`)
- Use `relationship()` with `back_populates` or `backref` for bidirectional foreign keys
- Add `__repr__()` for useful debugging output
- Use type hints on all columns
- Prefer `String(length)` with reasonable limits; never use unlimited `Text` for user input
- Use `DateTime(timezone=True)` and `func.now()` for timestamps
- Enable SQL Alchemy's `__table_args__` for indexes on frequently queried columns

```python
from sqlalchemy import Column, Integer, String, DateTime, ForeignKey, func, Index
from sqlalchemy.orm import declarative_base, relationship
from datetime import datetime

Base = declarative_base()

class BaseModel(Base):
    """Base model with common columns."""
    __abstract__ = True
    
    id = Column(Integer, primary_key=True)
    created_at = Column(DateTime(timezone=True), server_default=func.now(), nullable=False)
    updated_at = Column(DateTime(timezone=True), server_default=func.now(), onupdate=func.now(), nullable=False)

class Organization(BaseModel):
    __tablename__ = "organizations"
    
    name = Column(String(255), nullable=False, unique=True)
    description = Column(String(1024), nullable=True)
    
    users = relationship("User", back_populates="organization")
    
    def __repr__(self):
        return f"<Organization(id={self.id}, name={self.name!r})>"

class User(BaseModel):
    __tablename__ = "users"
    
    org_id = Column(Integer, ForeignKey("organizations.id"), nullable=False)
    email = Column(String(255), nullable=False, unique=True)
    hashed_password = Column(String(255), nullable=False)
    is_active = Column(Boolean, default=True)
    
    organization = relationship("Organization", back_populates="users")
    
    __table_args__ = (
        Index("idx_org_id_email", "org_id", "email"),
    )
    
    def __repr__(self):
        return f"<User(id={self.id}, email={self.email!r}, org_id={self.org_id})>"
```

## Views & Route Handlers

- Write all handlers as `async def` (FastAPI auto-runs sync handlers in a thread pool, avoid this)
- Accept Pydantic models for request bodies; never parse raw JSON
- Use dependency injection via `Depends()` for sessions, auth, logging
- Handle exceptions gracefully with try/except and return appropriate HTTP status codes
- Use `HTTPException` for client errors (400, 401, 403, 404, etc.)
- Return Pydantic response models for consistent serialization
- Implement proper pagination with limit/offset or cursor-based pagination

```python
from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy.ext.asyncio import AsyncSession
from pydantic import BaseModel

router = APIRouter(prefix="/api/v1/users", tags=["users"])

class UserResponse(BaseModel):
    id: int
    email: str
    is_active: bool

@router.get("/{user_id}", response_model=UserResponse)
async def get_user(user_id: int, db: AsyncSession = Depends(get_db)):
    """Fetch a single user by ID."""
    user = await db.execute(select(User).where(User.id == user_id))
    user = user.scalar_one_or_none()
    
    if not user:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="User not found")
    
    return user

@router.get("/", response_model=list[UserResponse])
async def list_users(
    skip: int = Query(0, ge=0),
    limit: int = Query(10, ge=1, le=100),
    db: AsyncSession = Depends(get_db),
):
    """List all users with pagination."""
    users = await db.execute(select(User).offset(skip).limit(limit))
    return users.scalars().all()
```

## URL Routes

- Use descriptive route names for reverse URL lookups (e.g., `get_user`, `create_payment`)
- Organize routes by API version (e.g., `/api/v1/`, `/api/v2/`)
- Use meaningful path segments: `/api/v1/users/{user_id}`, `/api/v1/organizations/{org_id}/users`
- Always end REST collection endpoints without a trailing slash; item endpoints use ID
- Use query parameters for filtering, sorting, pagination
- Use path parameters for resource identity only

## Request/Response Schemas (Pydantic)

- Define separate Pydantic models for requests, responses, and database updates
- Use inheritance to avoid duplication (base model → create/update models)
- Never expose internal database IDs or sensitive fields in responses without intent
- Use `Field(...)` for validation rules, descriptions, and examples
- Enable `strict` mode for type validation in production

```python
from pydantic import BaseModel, Field, ConfigDict
from datetime import datetime

class UserBase(BaseModel):
    email: str = Field(..., min_length=5, max_length=255)
    is_active: bool = Field(default=True)

class UserCreate(UserBase):
    password: str = Field(..., min_length=8, max_length=255)

class UserUpdate(BaseModel):
    email: str | None = Field(None, min_length=5, max_length=255)
    is_active: bool | None = None

class UserResponse(UserBase):
    id: int
    created_at: datetime
    updated_at: datetime
    
    model_config = ConfigDict(from_attributes=True)
```

## Database & SQLAlchemy

### Connection & Session Management

- Use **async SQLAlchemy** with `asyncpg` driver for PostgreSQL
- Create a single `SessionLocal` async session factory per application instance
- Always use dependency injection to provide sessions to route handlers
- Never hold sessions across multiple HTTP requests; create and close per-request
- Use context managers (`async with`) for session cleanup

```python
from sqlalchemy.ext.asyncio import create_async_engine, async_sessionmaker, AsyncSession

DATABASE_URL = os.environ.get("DATABASE_URL")
engine = create_async_engine(DATABASE_URL, echo=False, pool_size=20, max_overflow=0)
AsyncSessionLocal = async_sessionmaker(engine, class_=AsyncSession, expire_on_commit=False)

async def get_db() -> AsyncGenerator[AsyncSession, None]:
    async with AsyncSessionLocal() as session:
        try:
            yield session
        finally:
            await session.close()
```

### Query Optimization

- Use `select()` with SQLAlchemy 2.0+ style (not the legacy ORM API)
- Eagerly load related objects with `selectinload()` or `joinedload()` to avoid N+1 queries
- Use `select(...).where(...)` for filtering; never iterate and filter in Python
- Add database indexes on frequently queried columns (foreign keys, email, org_id, etc.)
- Profile queries with `EXPLAIN ANALYZE` in PostgreSQL before deployment
- Implement query result caching for read-heavy operations (Redis, in-process cache)

```python
from sqlalchemy import select
from sqlalchemy.orm import selectinload

# ✅ Good: single query with eager loading
stmt = select(User).where(User.org_id == org_id).options(selectinload(User.organization))
result = await db.execute(stmt)
users = result.scalars().all()

# ❌ Bad: N+1 query problem
users = await db.execute(select(User).where(User.org_id == org_id))
users = users.scalars().all()
for user in users:
    org = user.organization  # Lazy load — extra query per user!
```

### Migrations (Alembic)

- Use Alembic for all database schema changes
- Never apply schema changes directly to production; always use migrations
- Write descriptive migration file names and docstrings
- Test migrations on a replica database before production deployment
- Keep migrations small and focused (one feature per migration file)

```bash
# Generate a new migration
alembic revision --autogenerate -m "add_org_contacts_table"

# Apply migration in development
alembic upgrade head

# On production server (via SSH/container)
sudo docker exec processing-backend alembic upgrade head
```

## Authentication & Authorization

- Implement JWT-based authentication for stateless API clients
- Store JWT secrets in environment variables, never in code
- Cast JWT `org_id` claims to `int` at the auth boundary, once
- Use `Depends()` for auth decorators on protected routes
- Implement role-based access control (RBAC) with clear permission checks
- Return `401 Unauthorized` for missing/invalid tokens
- Return `403 Forbidden` for valid tokens without required permissions
- Log authentication failures for security auditing

```python
from fastapi import Depends, HTTPException, status
from jose import JWTError, jwt

SECRET_KEY = os.environ.get("SECRET_KEY")
ALGORITHM = "HS256"

async def get_current_user(token: str = Depends(oauth2_scheme)) -> dict:
    """Verify JWT token and return user claims."""
    try:
        payload = jwt.decode(token, SECRET_KEY, algorithms=[ALGORITHM])
        user_id: int | None = payload.get("sub")
        org_id: str | None = payload.get("org_id")
        
        if user_id is None or org_id is None:
            raise JWTError("Missing required claims")
        
        # ✅ Cast org_id to int at auth boundary
        return {"user_id": int(user_id), "org_id": int(org_id)}
    except JWTError:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Invalid token")

async def get_current_admin(user: dict = Depends(get_current_user)) -> dict:
    """Verify user is an admin."""
    if not await is_admin(user["user_id"]):
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Admin access required")
    return user
```

## Error Handling

- Use custom exception classes for domain-specific errors
- Return structured error responses with error codes and messages
- Log all errors with full context (stack trace, request ID, user ID)
- Return `500 Internal Server Error` only for unexpected exceptions
- Return appropriate 4xx codes for client errors (invalid input, auth, not found, conflict)

```python
from fastapi import HTTPException, status
from typing import Any

class AppException(Exception):
    """Base exception for all application errors."""
    def __init__(self, status_code: int, detail: str, code: str = "INTERNAL_ERROR"):
        self.status_code = status_code
        self.detail = detail
        self.code = code

@app.exception_handler(AppException)
async def app_exception_handler(request, exc: AppException):
    return JSONResponse(
        status_code=exc.status_code,
        content={"error": exc.code, "detail": exc.detail},
    )

class UserNotFound(AppException):
    def __init__(self):
        super().__init__(status.HTTP_404_NOT_FOUND, "User not found", "USER_NOT_FOUND")

# Usage
raise UserNotFound()
```

## Logging & Monitoring

- Configure structured logging with request/response context (request ID, user ID, duration)
- Use `loguru` or `structlog` for structured logs (JSON output for production)
- Log at INFO level for important business events (auth, payment, etc.)
- Log at DEBUG level for detailed query info (dev/test only)
- Never log sensitive data (passwords, tokens, card numbers)
- Include timings for database queries and external API calls
- Implement health check endpoint (`GET /health`) for monitoring

```python
import logging
from contextlib import asynccontextmanager

logger = logging.getLogger(__name__)

@app.get("/health")
async def health():
    """Health check for monitoring."""
    return {"status": "ok", "timestamp": datetime.utcnow().isoformat()}

@app.middleware("http")
async def log_request_middleware(request: Request, call_next):
    """Log HTTP request and response."""
    request_id = request.headers.get("X-Request-ID", str(uuid.uuid4()))
    start = time.time()
    
    response = await call_next(request)
    
    duration = time.time() - start
    logger.info(
        f"{request.method} {request.url.path}",
        extra={
            "request_id": request_id,
            "status_code": response.status_code,
            "duration_ms": duration * 1000,
        },
    )
    return response
```

## Testing

- Write unit tests for business logic (models, schemas, utils)
- Write integration tests for API endpoints with a test database
- Test both positive (happy path) and negative (error) scenarios
- Use `pytest` with `pytest-asyncio` for async test functions
- Use fixtures for test data and database setup
- Mock external dependencies (email, payment gateways, etc.)
- Achieve minimum 80% code coverage for critical paths
- Test database migrations with actual PostgreSQL schema

```python
import pytest
from sqlalchemy.ext.asyncio import create_async_engine, async_sessionmaker
from httpx import AsyncClient

@pytest.fixture
async def test_db():
    """Create an in-memory SQLite test database."""
    engine = create_async_engine("sqlite+aiosqlite:///:memory:")
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
    
    SessionLocal = async_sessionmaker(engine, class_=AsyncSession)
    yield SessionLocal
    await engine.dispose()

@pytest.fixture
async def client(test_db):
    """FastAPI test client with test database."""
    app.dependency_overrides[get_db] = lambda: test_db()
    async with AsyncClient(app=app, base_url="http://test") as ac:
        yield ac

@pytest.mark.asyncio
async def test_get_user(client):
    """Test fetching a user."""
    response = await client.get("/api/v1/users/1")
    assert response.status_code == 404
```

## PostgreSQL & SQL

- Always use parameterized queries (SQLAlchemy ORM prevents SQL injection automatically)
- Use transactions for multi-step operations (Alembic handles this automatically)
- Implement row-level security (RLS) for multi-tenant applications via PostgreSQL policies
- Use `JSONB` for semi-structured data (metadata, config, etc.)
- Create indexes on foreign keys, unique constraints, and frequently filtered columns
- Use `EXPLAIN ANALYZE` to validate query plans before deployment
- Set up connection pooling (SQLAlchemy's `pool_size` and `max_overflow`)
- Use `on_delete=CASCADE` only for detail records; use `on_delete=RESTRICT` for important entities

```python
from sqlalchemy import Index

class Payment(BaseModel):
    __tablename__ = "payments"
    
    id = Column(Integer, primary_key=True)
    org_id = Column(Integer, ForeignKey("organizations.id", ondelete="RESTRICT"), nullable=False)
    user_id = Column(Integer, ForeignKey("users.id", ondelete="RESTRICT"), nullable=False)
    amount = Column(Numeric(19, 2), nullable=False)
    status = Column(String(50), default="pending")
    metadata = Column(JSONB, default={})
    
    __table_args__ = (
        Index("idx_org_id_status", "org_id", "status"),
        Index("idx_user_id_created_at", "user_id", "created_at"),
    )
```

## Nginx Configuration

- Use Nginx as a reverse proxy in front of FastAPI application servers
- Implement SSL/TLS termination in Nginx (TLS 1.2+, strong ciphers)
- Route by URL path prefix to different backend services (`/api/billing/` → `menubuilder-backend:8000`, `/api/payment/` → `processing-backend:8000`)
- Set reasonable timeouts for backend connections (30-60s depending on workload)
- Enable gzip compression for text responses
- Log access and errors with request context (user IP, status, response time)
- Implement rate limiting for public endpoints
- Use health checks to detect unhealthy backend instances

```nginx
upstream processing_backend {
    least_conn;
    server processing-backend:8000 max_fails=3 fail_timeout=30s;
}

upstream menubuilder_backend {
    least_conn;
    server menubuilder-backend:8000 max_fails=3 fail_timeout=30s;
}

server {
    listen 443 ssl http2;
    server_name api.example.com;
    
    ssl_certificate /etc/letsencrypt/live/api.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/api.example.com/privkey.pem;
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_ciphers HIGH:!aNULL:!MD5;
    
    # Gzip compression
    gzip on;
    gzip_types text/plain text/css application/json;
    gzip_min_length 1000;
    
    # Payment API → Processing Backend (mTLS)
    location /api/payment/ {
        proxy_pass http://processing_backend;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_set_header X-Request-ID $request_id;
        proxy_connect_timeout 30s;
        proxy_send_timeout 60s;
        proxy_read_timeout 60s;
    }
    
    # Billing API → MenuBuilder Backend (JWT)
    location /api/billing/ {
        proxy_pass http://menubuilder_backend;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_set_header X-Request-ID $request_id;
        proxy_connect_timeout 30s;
        proxy_send_timeout 60s;
        proxy_read_timeout 60s;
    }
    
    # Health check
    location /health {
        access_log off;
        proxy_pass http://menubuilder_backend;
    }
    
    # Access logs with context
    access_log /var/log/nginx/api_access.log combined buffer=32k flush=5s;
    error_log /var/log/nginx/api_error.log warn;
}

# Redirect HTTP to HTTPS
server {
    listen 80;
    server_name api.example.com;
    return 301 https://$server_name$request_uri;
}
```

## Docker & Deployment

- Use multi-stage builds to minimize production image size
- Run FastAPI with `uvicorn` behind Nginx (never expose uvicorn directly to the internet)
- Set `workers` to `1` for async FastAPI (use Nginx load balancing instead)
- Use environment variables for all configuration (database URL, secrets, etc.)
- Mount volumes for persistent data (migrations, logs, certificates)
- Health checks via `curl` or `HTTP` probe pointing to `/health`
- Never run containers as root; use a dedicated non-root user

```dockerfile
# Multi-stage build
FROM python:3.14-slim as builder
WORKDIR /app
COPY pyproject.toml uv.lock ./
RUN pip install uv && uv pip install --system

FROM python:3.14-slim
WORKDIR /app
COPY --from=builder /usr/local/lib/python3.14/site-packages /usr/local/lib/python3.14/site-packages
COPY app/ ./app/
COPY alembic/ ./alembic/

RUN useradd -m -u 1000 appuser && chown -R appuser:appuser /app
USER appuser

EXPOSE 8000
HEALTHCHECK --interval=30s --timeout=10s --start-period=40s --retries=3 CMD curl -f http://localhost:8000/health || exit 1
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000", "--workers", "1"]
```

## CI/CD — Verified Production Practices

These are working, verified practices for deploying changes on the production host (`176.108.247.249`).

### Docker on Production Server

- **`docker` and `docker compose` require `sudo`.** Always prefix with `sudo docker ...` / `sudo docker compose ...` over SSH.

```bash
# Deploy new image
sudo docker pull registry.example.com/processing-backend:latest
sudo docker compose -f docker-compose.prod.yaml up -d processing-backend
```

### Database Migrations (Alembic)

- Alembic migrations reside in `ProcessingBackend/backend/alembic/versions/`
- To apply migrations in production:
  1. Upload migration files to the server
  2. Copy into the container: `sudo docker cp /home/user1/ProcessingBackend/backend/alembic/versions/. processing-backend:/app/alembic/versions/`
  3. Execute migration: `sudo docker exec processing-backend alembic upgrade head`
  4. If database schema altered with new columns/tables, restart dependent containers:
     ```bash
     sudo docker restart menubuilder-backend
     ```

### Deployment Checklist

- [ ] All code passes `ruff check`, `ruff format`, and `pyright`
- [ ] All tests pass: `pytest tests/`
- [ ] Database migration tested on staging/replica
- [ ] Environment variables configured on production server
- [ ] Docker image built and pushed to registry
- [ ] `docker-compose.yaml` updated with new image tag
- [ ] Health check `/health` responds `200 OK`
- [ ] No hardcoded secrets in code or config files
- [ ] Nginx reverse proxy validated with test requests
- [ ] Monitoring/alerting configured for new endpoints
- [ ] Rollback plan documented (previous image tag)

## Dependency Management

- Use `uv` for fast, reproducible dependency management (no `pip` or `pipenv`)
- Lock all dependencies in `uv.lock` and commit to version control
- Keep dependencies up-to-date; run `uv sync` to install from `uv.lock`
- Pin major versions in `pyproject.toml`; allow minor/patch updates
- Review security advisories regularly: `uv pip audit`

```toml
[project]
requires-python = "==3.14.*"
dependencies = [
    "fastapi==0.115.0",
    "sqlalchemy==2.0.35",
    "asyncpg==0.30.0",
    "pydantic==2.10.0",
    "pydantic-settings==2.5.0",
]
```
