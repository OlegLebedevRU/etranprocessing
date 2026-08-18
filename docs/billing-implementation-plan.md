# License Billing System — Implementation Plan

## Architecture Decisions

### Decision 1: Nginx Routing (`/api/billing/*` → ProcessingBackend)

**Choice: Add `location` block in MenuBuilder's `nginx.conf`**

```
location /api/billing/ {
    auth_jwt_extract_request_claims sub org_id;
    proxy_pass http://processing-backend:8000;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;
}
```

**Rationale:**
- Zero code changes in MenuBuilder backend — pure infra routing
- Nginx already validates JWT globally; we just let it through and also extract `sub`/`org_id` claims
- ProcessingBackend is already a separate FastAPI service; adding a location block is the standard nginx reverse-proxy pattern
- No need for a proxy router in MenuBuilder (which would add latency and coupling)
- The `processing-backend` hostname is available in the same Docker network as `menubuilder-backend`

### Decision 2: JWT Validation in ProcessingBackend

**Choice: ProcessingBackend validates JWT itself using `python-jose`**

**Rationale:**
- Nginx can extract claims (`sub`, `org_id`) via `auth_jwt_extract_request_claims`, but:
  - The nginx JWT module passes claims as `$jwt_claim_sub` etc., which need `proxy_set_header` to forward
  - If we ever need to validate token expiry or signature in the backend (e.g., for WebSocket upgrades), we'd need the library anyway
  - Same validation code as MenuBuilder = less surprise
- ProcessingBackend already validates mTLS certs for the terminal API — adding JWT is symmetric
- MenuBuilder's JWT payload: `{"sub": "username", "org_id": 1, "exp": ..., "iat": ...}` (HS256)
- ProcessingBackend needs `jwt_secret_hex` in its config (same value as MenuBuilder's `.env`)

**Implementation:** Add `python-jose[cryptography]` to `pyproject.toml`. Create `app/auth.py` mirroring MenuBuilder's pattern.

### Decision 3: Alembic Migration Style

**Choice: Use `op.create_table()` (clean Alembic operations)**

**Rationale:**
- 001 and 002 use `op.create_table()` — this is the established pattern
- 004 uses raw SQL only because it's idempotent (`IF NOT EXISTS`)
- 003 uses raw SQL because it modifies MenuBuilder-owned tables without ORM models
- New billing tables are ProcessingBackend-owned → use `op.create_table()` for type safety and downgrade support

### Decision 4: Service Layer Structure

**Choice: Split into two files**
- `app/services/billing_calculations.py` — Pure functions, no DB, no FastAPI. Calendar math, state machine, debt calculation, forecast.
- `app/services/billing_service.py` — DB queries, orchestration, depends on calculations module.

**Rationale:**
- Pure calculation functions are independently testable without DB fixtures
- Service layer orchestrates DB access + calculations, similar to existing `app/services/payment_service.py`
- Keeps the router thin (only request parsing and response formatting)

### Decision 5: Nginx Claim Forwarding

**Choice: Nginx extracts JWT claims and passes them as headers; ProcessingBackend reads from headers OR validates JWT itself**

**Hybrid approach:**
- Nginx validates JWT (gate-level rejection for invalid tokens)
- Nginx passes the raw `Authorization` header through to ProcessingBackend
- ProcessingBackend validates JWT itself to get `org_id` and `sub` claims
- This gives us defense-in-depth: nginx rejects garbage tokens, backend validates the actual claims

---

## Phase 1: Data Model & Migration

### Files to modify:
- `ProcessingBackend/backend/app/models.py`
- `ProcessingBackend/backend/alembic/versions/005_add_billing_tables.py`
- `ProcessingBackend/backend/alembic/env.py`

### 1.1 New/Modified Models in `models.py`

```python
# --- New model ---
class OrgBillingSettings(Base):
    __tablename__ = "org_billing_settings"

    org_id: Mapped[int] = mapped_column(Integer, primary_key=True)
    monthly_price_minor: Mapped[int] = mapped_column(BigInteger, nullable=False, default=0)
    currency: Mapped[str] = mapped_column(String(3), nullable=False, default="RUB")
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())
    updated_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now(), onupdate=func.now())
```

```python
# --- Extend License model (add these columns) ---
billing_period_months: Mapped[int] = mapped_column(SmallInteger, nullable=False, default=1)
monthly_price_override_minor: Mapped[int | None] = mapped_column(BigInteger, nullable=True)
renewal_enabled: Mapped[bool] = mapped_column(Boolean, default=True)
deactivation_requested_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), nullable=True)
```

```python
# --- New model ---
class BillingOrder(Base):
    __tablename__ = "billing_orders"

    id: Mapped[str] = mapped_column(String(36), primary_key=True)  # UUID
    org_id: Mapped[int] = mapped_column(Integer, nullable=False)
    status: Mapped[str] = mapped_column(String(20), nullable=False, default="pending")
    currency: Mapped[str] = mapped_column(String(3), nullable=False, default="RUB")
    amount_minor: Mapped[int] = mapped_column(BigInteger, nullable=False)
    payment_provider: Mapped[str | None] = mapped_column(String(50), nullable=True)
    external_payment_id: Mapped[str | None] = mapped_column(String(200), nullable=True)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())
    updated_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now(), onupdate=func.now())

    items: Mapped[list[BillingOrderItem]] = relationship(back_populates="order", cascade="all, delete-orphan")

    __table_args__ = (
        Index("idx_billing_orders_org_id", "org_id"),
        Index("idx_billing_orders_status", "status"),
    )
```

```python
# --- New model ---
class BillingOrderItem(Base):
    __tablename__ = "billing_order_items"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    order_id: Mapped[str] = mapped_column(String(36), ForeignKey("billing_orders.id", ondelete="CASCADE"), nullable=False)
    terminal_id: Mapped[int] = mapped_column(Integer, ForeignKey("terminals.id"), nullable=False)
    license_id: Mapped[int] = mapped_column(Integer, ForeignKey("licenses.id"), nullable=False)
    period_months: Mapped[int] = mapped_column(SmallInteger, nullable=False)
    amount_minor: Mapped[int] = mapped_column(BigInteger, nullable=False)

    order: Mapped[BillingOrder] = relationship(back_populates="items")

    __table_args__ = (
        Index("idx_billing_order_items_order_id", "order_id"),
    )
```

```python
# --- Add to License.__table_args__ ---
# Partial unique index: only one active license per terminal
Index("uq_license_active_terminal", "terminal_id", unique=True, postgresql_where=text("is_active = true"))
```

### 1.2 Migration `005_add_billing_tables.py`

```python
"""Add billing tables and license billing columns.

Revision ID: 005
Revises: 004
Create Date: 2026-08-18
"""
from typing import Sequence, Union
from alembic import op
import sqlalchemy as sa

revision: str = '005'
down_revision: Union[str, None] = '004'
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    # 1. Create org_billing_settings
    op.create_table('org_billing_settings',
        sa.Column('org_id', sa.Integer(), nullable=False),
        sa.Column('monthly_price_minor', sa.BigInteger(), nullable=False, server_default='0'),
        sa.Column('currency', sa.String(3), nullable=False, server_default='RUB'),
        sa.Column('created_at', sa.DateTime(timezone=True), server_default=sa.text('now()')),
        sa.Column('updated_at', sa.DateTime(timezone=True), server_default=sa.text('now()')),
        sa.PrimaryKeyConstraint('org_id'),
    )

    # 2. Add billing columns to licenses
    op.add_column('licenses', sa.Column('billing_period_months', sa.SmallInteger(), nullable=False, server_default='1'))
    op.add_column('licenses', sa.Column('monthly_price_override_minor', sa.BigInteger(), nullable=True))
    op.add_column('licenses', sa.Column('renewal_enabled', sa.Boolean(), server_default='true'))
    op.add_column('licenses', sa.Column('deactivation_requested_at', sa.DateTime(timezone=True), nullable=True))

    # 3. Partial unique index on licenses(terminal_id) WHERE is_active = true
    op.execute('CREATE UNIQUE INDEX uq_license_active_terminal ON licenses(terminal_id) WHERE is_active = true')

    # 4. Create billing_orders
    op.create_table('billing_orders',
        sa.Column('id', sa.String(36), nullable=False),
        sa.Column('org_id', sa.Integer(), nullable=False),
        sa.Column('status', sa.String(20), nullable=False, server_default='pending'),
        sa.Column('currency', sa.String(3), nullable=False, server_default='RUB'),
        sa.Column('amount_minor', sa.BigInteger(), nullable=False),
        sa.Column('payment_provider', sa.String(50), nullable=True),
        sa.Column('external_payment_id', sa.String(200), nullable=True),
        sa.Column('created_at', sa.DateTime(timezone=True), server_default=sa.text('now()')),
        sa.Column('updated_at', sa.DateTime(timezone=True), server_default=sa.text('now()')),
        sa.PrimaryKeyConstraint('id'),
    )
    op.create_index('idx_billing_orders_org_id', 'billing_orders', ['org_id'])
    op.create_index('idx_billing_orders_status', 'billing_orders', ['status'])

    # 5. Create billing_order_items
    op.create_table('billing_order_items',
        sa.Column('id', sa.Integer(), autoincrement=True, nullable=False),
        sa.Column('order_id', sa.String(36), nullable=False),
        sa.Column('terminal_id', sa.Integer(), nullable=False),
        sa.Column('license_id', sa.Integer(), nullable=False),
        sa.Column('period_months', sa.SmallInteger(), nullable=False),
        sa.Column('amount_minor', sa.BigInteger(), nullable=False),
        sa.PrimaryKeyConstraint('id'),
        sa.ForeignKeyConstraint(['order_id'], ['billing_orders.id'], ondelete='CASCADE'),
        sa.ForeignKeyConstraint(['terminal_id'], ['terminals.id']),
        sa.ForeignKeyConstraint(['license_id'], ['licenses.id']),
    )
    op.create_index('idx_billing_order_items_order_id', 'billing_order_items', ['order_id'])


def downgrade() -> None:
    op.drop_table('billing_order_items')
    op.drop_table('billing_orders')
    op.execute('DROP INDEX IF EXISTS uq_license_active_terminal')
    op.drop_column('licenses', 'deactivation_requested_at')
    op.drop_column('licenses', 'renewal_enabled')
    op.drop_column('licenses', 'monthly_price_override_minor')
    op.drop_column('licenses', 'billing_period_months')
    op.drop_table('org_billing_settings')
```

### 1.3 Update `alembic/env.py` imports

Add to the import block:
```python
from app.models import (
    Terminal, OrgStatus, License, GateGaugeRecord, TechGateRecord,
    Org, Tsp, TspParameterCode, Payment, PaymentParam, BalanceTerminalTsp,
    CertificatePin, ApiToken,  # existing, were missing
    OrgBillingSettings, BillingOrder, BillingOrderItem,  # new
)
```

---

## Phase 2: Config & Auth

### Files to modify:
- `ProcessingBackend/backend/app/config.py`
- `ProcessingBackend/backend/app/auth.py` (new file)
- `ProcessingBackend/backend/pyproject.toml`

### 2.1 Config additions

```python
class Settings(BaseSettings):
    database_url: str = ""
    cors_origins: list[str] = []

    # JWT (shared secret with MenuBuilder)
    jwt_secret_hex: str = ""
    jwt_algorithm: str = "HS256"

    # Billing
    payment_provider: str = ""  # empty = not configured (return 501)
    billing_currency: str = "RUB"

    model_config = {"env_file": ".env", "extra": "ignore"}

    @property
    def jwt_secret_bytes(self) -> bytes:
        return bytes.fromhex(self.jwt_secret_hex)
```

### 2.2 New `app/auth.py`

```python
from fastapi import Depends, HTTPException
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from jose import JWTError, jwt
from app.config import settings

security_scheme = HTTPBearer()


def decode_token(token: str) -> dict:
    try:
        return jwt.decode(token, settings.jwt_secret_bytes, algorithms=[settings.jwt_algorithm])
    except JWTError:
        raise HTTPException(status_code=401, detail="Invalid or expired token")


async def get_current_user(
    credentials: HTTPAuthorizationCredentials = Depends(security_scheme),
) -> dict:
    payload = decode_token(credentials.credentials)
    username: str = payload.get("sub", "")
    org_id = payload.get("org_id")
    if not username:
        raise HTTPException(status_code=401, detail="Invalid token payload")
    if org_id is None:
        raise HTTPException(status_code=403, detail="No org_id in token")
    return {"username": username, "org_id": org_id}
```

### 2.3 pyproject.toml dependency

Add `python-jose[cryptography]>=3.3.0` to dependencies.

---

## Phase 3: Billing Calculations (Pure Functions)

### New file: `app/services/billing_calculations.py`

This module contains **zero side effects** — pure functions only. No DB, no HTTP.

```python
"""Pure billing calculation functions. No side effects."""
from dataclasses import dataclass
from datetime import UTC, datetime
from dateutil.relativedelta import relativedelta


TERMINAL_STATES = (
    "active", "due_soon", "overdue",
    "deactivation_scheduled", "disabled", "admin_disabled",
)


@dataclass(frozen=True)
class PeriodInfo:
    """One billing period's worth of information."""
    period_start: datetime
    period_end: datetime
    period_months: int
    monthly_price_minor: int
    period_price_minor: int


@dataclass(frozen=True)
class TerminalBillingState:
    """Computed billing state for a single terminal."""
    terminal_id: int
    state: str  # one of TERMINAL_STATES
    license_expires_at: datetime | None
    renewal_enabled: bool
    current_period: PeriodInfo | None
    debt_minor: int  # total outstanding amount in minor units
    forecast: list[PeriodInfo]  # next 3 periods
    days_until_expiry: int | None
    deactivation_requested_at: datetime | None


def get_period_start(dt: datetime, period_months: int) -> datetime:
    """Get the start of the billing period containing `dt`."""
    # Normalize to first day of period
    month_offset = (dt.month - 1) % period_months
    return dt.replace(day=1, hour=0, minute=0, second=0, microsecond=0) - relativedelta(months=month_offset)


def get_period_end(period_start: datetime, period_months: int) -> datetime:
    """Get the exclusive end of a billing period."""
    return period_start + relativedelta(months=period_months)


def calculate_period_price(monthly_price_minor: int, period_months: int) -> int:
    """Calculate total price for a billing period."""
    return monthly_price_minor * period_months


def calculate_debt(
    license_expires_at: datetime,
    billing_period_months: int,
    monthly_price_minor: int,
    as_of: datetime,
) -> int:
    """
    Calculate total outstanding debt for unpaid periods.
    Uses calendar month arithmetic (not per-day).
    Debt = sum of all fully elapsed unpaid periods after license expiry.
    """
    if license_expires_at >= as_of:
        return 0  # not expired yet

    debt = 0
    period_start = get_period_start(license_expires_at, billing_period_months)
    cursor = period_start

    while cursor < as_of:
        period_end = get_period_end(cursor, billing_period_months)
        # Only count periods that have fully elapsed
        if period_end <= as_of:
            debt += calculate_period_price(monthly_price_minor, billing_period_months)
        cursor = period_end

    return debt


def generate_forecast(
    license_expires_at: datetime | None,
    billing_period_months: int,
    monthly_price_minor: int,
    as_of: datetime,
    count: int = 3,
) -> list[PeriodInfo]:
    """Generate the next `count` billing periods from as_of."""
    if license_expires_at is None:
        start = get_period_start(as_of, billing_period_months)
    else:
        start = get_period_start(max(license_expires_at, as_of), billing_period_months)

    result = []
    cursor = start
    for _ in range(count):
        period_end = get_period_end(cursor, billing_period_months)
        result.append(PeriodInfo(
            period_start=cursor,
            period_end=period_end,
            period_months=billing_period_months,
            monthly_price_minor=monthly_price_minor,
            period_price_minor=calculate_period_price(monthly_price_minor, billing_period_months),
        ))
        cursor = period_end
    return result


def compute_terminal_state(
    is_terminal_active: bool,
    license_expires_at: datetime | None,
    renewal_enabled: bool,
    deactivation_requested_at: datetime | None,
    as_of: datetime,
) -> str:
    """
    Determine terminal billing state.

    State machine:
    - admin_disabled: terminal.is_active = false (set by admin, overrides everything)
    - disabled: license expired AND deactivation_requested_at is past or license was disabled
    - deactivation_scheduled: deactivation_requested_at is set, not yet expired
    - overdue: license expired, renewal_enabled=True, within grace (not yet disabled)
    - due_soon: license expires within 30 days
    - active: everything OK
    """
    if not is_terminal_active:
        return "admin_disabled"

    if license_expires_at is None:
        return "active"  # no license = no billing concern (edge case)

    now = as_of

    # Check deactivation flow
    if deactivation_requested_at is not None:
        if license_expires_at <= now:
            return "disabled"  # license expired, deactivation was requested
        return "deactivation_scheduled"

    # Check expiry
    if license_expires_at <= now:
        # Expired — if renewal disabled, it's effectively disabled
        if not renewal_enabled:
            return "disabled"
        return "overdue"

    # Due soon: within 30 days
    days_until = (license_expires_at - now).days
    if days_until <= 30:
        return "due_soon"

    return "active"


def build_terminal_billing_state(
    terminal_id: int,
    is_terminal_active: bool,
    license_expires_at: datetime | None,
    billing_period_months: int,
    monthly_price_minor: int,
    renewal_enabled: bool,
    deactivation_requested_at: datetime | None,
    as_of: datetime,
) -> TerminalBillingState:
    """Build the complete billing state for a terminal."""
    state = compute_terminal_state(
        is_terminal_active, license_expires_at, renewal_enabled,
        deactivation_requested_at, as_of,
    )

    debt = 0
    current_period = None
    if license_expires_at:
        debt = calculate_debt(license_expires_at, billing_period_months, monthly_price_minor, as_of)
        period_start = get_period_start(as_of, billing_period_months)
        current_period = PeriodInfo(
            period_start=period_start,
            period_end=get_period_end(period_start, billing_period_months),
            period_months=billing_period_months,
            monthly_price_minor=monthly_price_minor,
            period_price_minor=calculate_period_price(monthly_price_minor, billing_period_months),
        )

    forecast = generate_forecast(license_expires_at, billing_period_months, monthly_price_minor, as_of)

    days_until = None
    if license_expires_at:
        delta = license_expires_at - as_of
        days_until = delta.days

    return TerminalBillingState(
        terminal_id=terminal_id,
        state=state,
        license_expires_at=license_expires_at,
        renewal_enabled=renewal_enabled,
        current_period=current_period,
        debt_minor=debt,
        forecast=forecast,
        days_until_expiry=days_until,
        deactivation_requested_at=deactivation_requested_at,
    )
```

---

## Phase 4: Billing Service (DB Orchestration)

### New file: `app/services/billing_service.py`

```python
"""Billing service — DB queries + orchestration."""
from datetime import UTC, datetime
from uuid import uuid4

from sqlalchemy import func, select, update
from sqlalchemy.ext.asyncio import AsyncSession

from app.models import (
    BillingOrder, BillingOrderItem, License, OrgBillingSettings, Terminal,
)
from app.services.billing_calculations import (
    TerminalBillingState, build_terminal_billing_state,
)


async def get_org_billing_settings(org_id: int, db: AsyncSession) -> OrgBillingSettings | None:
    result = await db.execute(
        select(OrgBillingSettings).where(OrgBillingSettings.org_id == org_id)
    )
    return result.scalar_one_or_none()


async def get_effective_monthly_price(
    org_id: int,
    license_: License,
    db: AsyncSession,
) -> int:
    """Get the effective monthly price: override on license or org default."""
    if license_.monthly_price_override_minor is not None:
        return license_.monthly_price_override_minor
    settings = await get_org_billing_settings(org_id, db)
    if settings:
        return settings.monthly_price_minor
    return 0


async def get_terminal_billing_state(
    terminal: Terminal,
    license_: License,
    db: AsyncSession,
    as_of: datetime | None = None,
) -> TerminalBillingState:
    """Build billing state for one terminal."""
    now = as_of or datetime.now(UTC)
    monthly_price = await get_effective_monthly_price(terminal.org_id, license_, db)

    return build_terminal_billing_state(
        terminal_id=terminal.id,
        is_terminal_active=terminal.is_active,
        license_expires_at=license_.expires_at,
        billing_period_months=license_.billing_period_months,
        monthly_price_minor=monthly_price,
        renewal_enabled=license_.renewal_enabled,
        deactivation_requested_at=license_.deactivation_requested_at,
        as_of=now,
    )


async def get_billing_summary(org_id: int, db: AsyncSession, as_of: datetime | None = None) -> dict:
    """Get org-level billing summary."""
    now = as_of or datetime.now(UTC)

    # Get all terminals with active licenses for this org
    result = await db.execute(
        select(Terminal, License)
        .join(License, License.terminal_id == Terminal.id)
        .where(Terminal.org_id == org_id, License.is_active == True)
    )
    rows = result.all()

    state_counts: dict[str, int] = {}
    total_debt = 0

    for terminal, license_ in rows:
        bs = await get_terminal_billing_state(terminal, license_, db, now)
        state_counts[bs.state] = state_counts.get(bs.state, 0) + 1
        total_debt += bs.debt_minor

    org_settings = await get_org_billing_settings(org_id, db)

    return {
        "org_id": org_id,
        "total_terminals": len(rows),
        "state_counts": state_counts,
        "total_debt_minor": total_debt,
        "currency": org_settings.currency if org_settings else "RUB",
        "monthly_price_minor": org_settings.monthly_price_minor if org_settings else 0,
    }


async def list_terminals_billing(
    org_id: int,
    db: AsyncSession,
    page: int = 1,
    page_size: int = 20,
    state_filter: str | None = None,
    sort_by: str = "device_id",
    sort_order: str = "asc",
    search: str | None = None,
    as_of: datetime | None = None,
) -> dict:
    """List terminals with billing info, paginated and filterable."""
    now = as_of or datetime.now(UTC)

    # Base query
    query = (
        select(Terminal, License)
        .join(License, License.terminal_id == Terminal.id)
        .where(Terminal.org_id == org_id, License.is_active == True)
    )

    if search:
        search_pattern = f"%{search}%"
        query = query.where(
            Terminal.sn.ilike(search_pattern) | Terminal.device_id.cast(str).ilike(search_pattern)
        )

    # Sorting
    sort_column = {
        "device_id": Terminal.device_id,
        "sn": Terminal.sn,
        "expires_at": License.expires_at,
        "balance": License.balance,
    }.get(sort_by, Terminal.device_id)

    if sort_order == "desc":
        query = query.order_by(sort_column.desc())
    else:
        query = query.order_by(sort_column.asc())

    # Count
    count_query = select(func.count()).select_from(
        query.subquery()
    )
    total = (await db.execute(count_query)).scalar() or 0

    # Paginate
    query = query.offset((page - 1) * page_size).limit(page_size)
    result = await db.execute(query)
    rows = result.all()

    # Build items with billing state
    items = []
    for terminal, license_ in rows:
        bs = await get_terminal_billing_state(terminal, license_, db, now)
        if state_filter and bs.state != state_filter:
            continue
        items.append({
            "terminal_id": terminal.id,
            "device_id": terminal.device_id,
            "sn": terminal.sn,
            "is_active": terminal.is_active,
            "license_expires_at": license_.expires_at.isoformat(),
            "billing_state": bs.state,
            "debt_minor": bs.debt_minor,
            "days_until_expiry": bs.days_until_expiry,
            "renewal_enabled": license_.renewal_enabled,
            "deactivation_requested_at": license_.deactivation_requested_at.isoformat() if license_.deactivation_requested_at else None,
            "monthly_price_minor": bs.current_period.monthly_price_minor if bs.current_period else 0,
        })

    return {
        "total": total,
        "page": page,
        "page_size": page_size,
        "items": items,
    }


async def request_deactivation(terminal_id: int, org_id: int, db: AsyncSession) -> None:
    """Mark a terminal's license for deactivation."""
    result = await db.execute(
        select(License).where(
            License.terminal_id == terminal_id,
            License.org_id == org_id,
            License.is_active == True,
        )
    )
    license_ = result.scalar_one_or_none()
    if not license_:
        raise ValueError("No active license found for this terminal")

    if license_.deactivation_requested_at is not None:
        raise ValueError("Deactivation already requested")

    license_.deactivation_requested_at = datetime.now(UTC)
    await db.commit()


async def cancel_deactivation(terminal_id: int, org_id: int, db: AsyncSession) -> None:
    """Cancel a pending deactivation request."""
    result = await db.execute(
        select(License).where(
            License.terminal_id == terminal_id,
            License.org_id == org_id,
            License.is_active == True,
        )
    )
    license_ = result.scalar_one_or_none()
    if not license_:
        raise ValueError("No active license found")

    if license_.deactivation_requested_at is None:
        raise ValueError("No deactivation request pending")

    license_.deactivation_requested_at = None
    await db.commit()


async def create_checkout_order(
    org_id: int,
    terminal_ids: list[int],
    period_months: int,
    db: AsyncSession,
) -> BillingOrder:
    """Create a billing order for specified terminals."""
    now = datetime.now(UTC)
    order_id = str(uuid4())
    total_amount = 0

    order = BillingOrder(
        id=order_id,
        org_id=org_id,
        status="pending",
        amount_minor=0,  # will update below
    )
    db.add(order)

    for tid in terminal_ids:
        result = await db.execute(
            select(Terminal, License)
            .join(License, License.terminal_id == Terminal.id)
            .where(Terminal.id == tid, Terminal.org_id == org_id, License.is_active == True)
        )
        row = result.one_or_none()
        if not row:
            raise ValueError(f"Terminal {tid} not found or has no active license")

        terminal, license_ = row
        monthly_price = await get_effective_monthly_price(org_id, license_, db)
        item_amount = monthly_price * period_months

        item = BillingOrderItem(
            order_id=order_id,
            terminal_id=tid,
            license_id=license_.id,
            period_months=period_months,
            amount_minor=item_amount,
        )
        db.add(item)
        total_amount += item_amount

    order.amount_minor = total_amount
    await db.commit()
    await db.refresh(order)
    return order


async def create_reactivation_order(
    terminal_id: int,
    org_id: int,
    period_months: int,
    db: AsyncSession,
) -> BillingOrder:
    """Create a reactivation order for a single disabled/overdue terminal."""
    result = await db.execute(
        select(Terminal, License)
        .join(License, License.terminal_id == Terminal.id)
        .where(Terminal.id == terminal_id, Terminal.org_id == org_id, License.is_active == True)
    )
    row = result.one_or_none()
    if not row:
        raise ValueError("Terminal not found or has no active license")

    terminal, license_ = row
    monthly_price = await get_effective_monthly_price(org_id, license_, db)

    # Calculate amount: debt + new period
    from app.services.billing_calculations import calculate_debt
    debt = calculate_debt(license_.expires_at, license_.billing_period_months, monthly_price, datetime.now(UTC))
    new_period_amount = monthly_price * period_months
    total = debt + new_period_amount

    order_id = str(uuid4())
    order = BillingOrder(
        id=order_id,
        org_id=org_id,
        status="pending",
        amount_minor=total,
    )
    db.add(order)

    item = BillingOrderItem(
        order_id=order_id,
        terminal_id=terminal_id,
        license_id=license_.id,
        period_months=period_months,
        amount_minor=total,
    )
    db.add(item)

    await db.commit()
    await db.refresh(order)
    return order
```

---

## Phase 5: Terminal API Refactoring

### Files to modify:
- `ProcessingBackend/backend/app/routers/licensebilling.py`
- `ProcessingBackend/backend/app/dependencies.py`

### 5.1 Refactored `licensebilling.py`

**Key changes:**
- Expiry returns `Result=OK` with `state=error` (not `Result=ERROR`)
- `renewal_enabled=false` alone does NOT cause `state=error`
- Separate business state from exceptions

```python
from fastapi import APIRouter, Depends, Request
from fastapi.responses import Response
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.dependencies import get_current_terminal
from app.models import Terminal
from app.services.billing_calculations import compute_terminal_state
from app.services.billing_service import get_effective_monthly_price

router = APIRouter()


def xml_response(content: str) -> Response:
    return Response(
        content=f"<?xml version='1.0' encoding='UTF-8'?>\n{content}",
        media_type="application/xml",
    )


def _build_xml(result: str, state: str, balance: int = 0, description: str = "") -> str:
    parts = [
        "<Response>",
        f"<Result>{result}</Result>",
        f"<state>{state}</state>",
        f"<balance>{balance}</balance>",
    ]
    if description:
        parts.append(f"<description>{description}</description>")
    parts.append("</Response>")
    return "".join(parts)


@router.get("")
@router.post("")
@router.get("/check")
@router.post("/check")
async def license_check(
    request: Request,
    terminal: Terminal = Depends(get_current_terminal),
    db: AsyncSession = Depends(get_db),
):
    """
    License billing check endpoint.
    Compatible with legacy licensebilling gate.ashx format.

    Response rules:
    - Result=OK always (even for expired licenses)
    - state=ok only when terminal can process payments
    - state=error when terminal cannot process (expired, disabled, blocked)
    - renewal_enabled=false alone does NOT cause state=error
    """
    try:
        # Get active license
        from sqlalchemy import select
        from app.models import License, OrgStatus

        result = await db.execute(
            select(License).where(
                License.terminal_id == terminal.id,
                License.is_active == True,
            )
        )
        license_ = result.scalar_one_or_none()

        if not license_:
            return xml_response(_build_xml(
                "OK", "error", description="No active license"
            ))

        # Check org status
        org_result = await db.execute(
            select(OrgStatus).where(OrgStatus.org_id == terminal.org_id)
        )
        org_status = org_result.scalar_one_or_none()
        if org_status and org_status.status == "blocked":
            return xml_response(_build_xml(
                "OK", "error", description="Organization is blocked"
            ))

        # Compute terminal state
        from datetime import UTC, datetime
        billing_state = compute_terminal_state(
            is_terminal_active=terminal.is_active,
            license_expires_at=license_.expires_at,
            renewal_enabled=license_.renewal_enabled,
            deactivation_requested_at=license_.deactivation_requested_at,
            as_of=datetime.now(UTC),
        )

        # Map billing state to XML response
        if billing_state in ("active", "due_soon"):
            return xml_response(_build_xml("OK", "ok", balance=license_.balance))
        else:
            # overdue, deactivation_scheduled, disabled, admin_disabled
            return xml_response(_build_xml(
                "OK", "error", balance=license_.balance,
                description=f"Terminal state: {billing_state}",
            ))

    except Exception as e:  # noqa: BLE001
        # Only truly unexpected errors reach here
        return xml_response(_build_xml(
            "ERROR", "error", description=str(e)
        ))
```

### 5.2 Dependencies cleanup

The existing `check_license` and `check_org_status` in `dependencies.py` stay unchanged (other routers may use them). The new billing router uses `compute_terminal_state` directly instead of raising HTTP exceptions.

---

## Phase 6: User API Router

### New file: `ProcessingBackend/backend/app/routers/billing.py`

```python
"""User-facing billing API (JWT-authenticated, JSON)."""
from datetime import UTC, datetime

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, Field
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import get_current_user
from app.config import settings
from app.database import get_db
from app.services import billing_service

router = APIRouter()


# --- Request/Response schemas ---

class BillingSummaryResponse(BaseModel):
    org_id: int
    total_terminals: int
    state_counts: dict[str, int]
    total_debt_minor: int
    currency: str
    monthly_price_minor: int


class TerminalBillingItem(BaseModel):
    terminal_id: int
    device_id: int
    sn: str
    is_active: bool
    license_expires_at: str
    billing_state: str
    debt_minor: int
    days_until_expiry: int | None
    renewal_enabled: bool
    deactivation_requested_at: str | None
    monthly_price_minor: int


class TerminalBillingListResponse(BaseModel):
    total: int
    page: int
    page_size: int
    items: list[TerminalBillingItem]


class DeactivateRequest(BaseModel):
    pass  # no body needed


class CheckoutRequest(BaseModel):
    terminal_ids: list[int] = Field(..., min_length=1)
    period_months: int = Field(..., ge=1, le=12)


class ReactivationCheckoutRequest(BaseModel):
    period_months: int = Field(..., ge=1, le=12)


class OrderResponse(BaseModel):
    order_id: str
    status: str
    amount_minor: int
    currency: str
    payment_url: str | None = None


# --- Endpoints ---

@router.get("/summary", response_model=BillingSummaryResponse)
async def billing_summary(
    user: dict = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    return await billing_service.get_billing_summary(user["org_id"], db)


@router.get("/terminals", response_model=TerminalBillingListResponse)
async def list_terminals(
    page: int = 1,
    page_size: int = 20,
    state: str | None = None,
    sort_by: str = "device_id",
    sort_order: str = "asc",
    search: str | None = None,
    user: dict = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    return await billing_service.list_terminals_billing(
        org_id=user["org_id"],
        db=db,
        page=page,
        page_size=page_size,
        state_filter=state,
        sort_by=sort_by,
        sort_order=sort_order,
        search=search,
    )


@router.post("/terminals/{terminal_id}/deactivate")
async def deactivate_terminal(
    terminal_id: int,
    user: dict = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    try:
        await billing_service.request_deactivation(terminal_id, user["org_id"], db)
    except ValueError as e:
        raise HTTPException(status_code=400, detail=str(e))
    return {"status": "deactivation_requested"}


@router.post("/terminals/{terminal_id}/cancel-deactivation")
async def cancel_deactivation(
    terminal_id: int,
    user: dict = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    try:
        await billing_service.cancel_deactivation(terminal_id, user["org_id"], db)
    except ValueError as e:
        raise HTTPException(status_code=400, detail=str(e))
    return {"status": "deactivation_cancelled"}


@router.post("/checkout", response_model=OrderResponse)
async def checkout(
    body: CheckoutRequest,
    user: dict = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    if not settings.payment_provider:
        raise HTTPException(status_code=501, detail="Payment provider not configured")

    try:
        order = await billing_service.create_checkout_order(
            org_id=user["org_id"],
            terminal_ids=body.terminal_ids,
            period_months=body.period_months,
            db=db,
        )
    except ValueError as e:
        raise HTTPException(status_code=400, detail=str(e))

    # TODO: Integrate with actual payment provider
    # For now, return order info with no payment_url
    return OrderResponse(
        order_id=order.id,
        status=order.status,
        amount_minor=order.amount_minor,
        currency=order.currency,
        payment_url=None,
    )


@router.post("/terminals/{terminal_id}/reactivation-checkout", response_model=OrderResponse)
async def reactivation_checkout(
    terminal_id: int,
    body: ReactivationCheckoutRequest,
    user: dict = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    if not settings.payment_provider:
        raise HTTPException(status_code=501, detail="Payment provider not configured")

    try:
        order = await billing_service.create_reactivation_order(
            terminal_id=terminal_id,
            org_id=user["org_id"],
            period_months=body.period_months,
            db=db,
        )
    except ValueError as e:
        raise HTTPException(status_code=400, detail=str(e))

    return OrderResponse(
        order_id=order.id,
        status=order.status,
        amount_minor=order.amount_minor,
        currency=order.currency,
        payment_url=None,
    )
```

### Register in `main.py`:

```python
from app.routers import billing
app.include_router(billing.router, prefix="/api/billing", tags=["billing"])
```

---

## Phase 7: Frontend

### Files to create:
- `MenuBuilder/frontend/src/api/billing.ts`
- `MenuBuilder/frontend/src/routes/billing.tsx`

### Files to modify:
- `MenuBuilder/frontend/src/App.tsx` (add route)
- `MenuBuilder/frontend/src/routes/layout.tsx` (add menu item)

### 7.1 API module: `api/billing.ts`

```typescript
import client from "./client";

// --- Types ---

export interface BillingSummary {
  org_id: number;
  total_terminals: number;
  state_counts: Record<string, number>;
  total_debt_minor: number;
  currency: string;
  monthly_price_minor: number;
}

export interface TerminalBillingItem {
  terminal_id: number;
  device_id: number;
  sn: string;
  is_active: boolean;
  license_expires_at: string;
  billing_state: string;
  debt_minor: number;
  days_until_expiry: number | null;
  renewal_enabled: boolean;
  deactivation_requested_at: string | null;
  monthly_price_minor: number;
}

export interface TerminalBillingListResponse {
  total: number;
  page: number;
  page_size: number;
  items: TerminalBillingItem[];
}

export interface OrderResponse {
  order_id: string;
  status: string;
  amount_minor: number;
  currency: string;
  payment_url: string | null;
}

// --- API calls ---

export const getBillingSummary = () =>
  client.get<BillingSummary>("/billing/summary");

export const getBillingTerminals = (params: {
  page?: number;
  page_size?: number;
  state?: string;
  sort_by?: string;
  sort_order?: string;
  search?: string;
}) => client.get<TerminalBillingListResponse>("/billing/terminals", { params });

export const deactivateTerminal = (terminalId: number) =>
  client.post(`/billing/terminals/${terminalId}/deactivate`);

export const cancelDeactivation = (terminalId: number) =>
  client.post(`/billing/terminals/${terminalId}/cancel-deactivation`);

export const createCheckout = (data: { terminal_ids: number[]; period_months: number }) =>
  client.post<OrderResponse>("/billing/checkout", data);

export const createReactivationCheckout = (terminalId: number, periodMonths: number) =>
  client.post<OrderResponse>(`/billing/terminals/${terminalId}/reactivation-checkout`, {
    period_months: periodMonths,
  });
```

### 7.2 Page component: `routes/billing.tsx`

Structure:
- Summary cards at top (total terminals, debt, state distribution)
- Tabbed terminal table (All / Active / Due Soon / Overdue / Disabled)
- Each row: device_id, SN, expiry, state badge, debt, action buttons
- Deactivate button → confirmation modal
- Cancel deactivation button (if applicable)
- Checkout button → modal with period selector
- Reactivation button for disabled terminals

### 7.3 Route registration

In `App.tsx`, add:
```tsx
import BillingPage from "./routes/billing";
// ... inside RequireAuth routes:
<Route path="billing" element={<BillingPage />} />
```

In `layout.tsx`, add to Menu items:
```tsx
{ key: "billing", icon: <DollarOutlined />, label: "Биллинг" },
```

---

## Phase 8: Nginx Configuration

### File to modify: `MenuBuilder/nginx.conf`

Add **before** the generic `/api/` location:

```nginx
location /api/billing/ {
    proxy_pass http://processing-backend:8000;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;
    # JWT is validated globally by auth_jwt_enabled on;
    # ProcessingBackend validates JWT itself for claim extraction
}
```

**Note:** The `processing-backend` hostname must be resolvable in the Docker network. If using docker-compose, ensure ProcessingBackend service is named `processing-backend` and is on the same network.

---

## Test Strategy

### Unit tests (no DB)

**File:** `ProcessingBackend/backend/tests/test_billing_calculations.py`

```python
# Test pure functions from billing_calculations.py

def test_get_period_start_monthly():
    """Period start for monthly billing aligns to 1st of month."""
    ...

def test_get_period_start_quarterly():
    """Period start for quarterly billing."""
    ...

def test_calculate_debt_no_debt():
    """No debt when license not expired."""
    ...

def test_calculate_debt_one_period():
    """Debt = 1 period when exactly one period past expiry."""
    ...

def test_calculate_debt_partial_period():
    """No debt for partially elapsed period (only fully elapsed count)."""
    ...

def test_compute_terminal_state_active():
    ...

def test_compute_terminal_state_due_soon():
    """Within 30 days of expiry."""
    ...

def test_compute_terminal_state_overdue():
    """Expired, renewal enabled."""
    ...

def test_compute_terminal_state_disabled():
    """Expired, renewal disabled."""
    ...

def test_compute_terminal_state_admin_disabled():
    """Terminal.is_active = false."""
    ...

def test_compute_terminal_state_deactivation_scheduled():
    ...

def test_renewal_enabled_false_not_error():
    """renewal_enabled=false + not expired → state=active (not error)."""
    ...

def test_forecast_generation():
    ...

def test_forecast_from_expired():
    """Forecast starts from current period when license expired."""
    ...
```

### Integration tests (with DB)

**File:** `ProcessingBackend/backend/tests/test_billing_api.py`

```python
# Terminal API tests

async def test_license_check_active_terminal():
    """Active terminal returns Result=OK, state=ok, balance=N."""
    ...

async def test_license_check_expired_returns_ok():
    """Expired terminal returns Result=OK (not ERROR), state=error."""
    ...

async def test_license_check_blocked_org():
    """Blocked org returns Result=OK, state=error."""
    ...

async def test_license_check_renewal_disabled_not_error():
    """renewal_enabled=false + not expired → state=ok."""
    ...

# User API tests

async def test_billing_summary_requires_auth():
    ...

async def test_billing_summary_returns_data():
    ...

async def test_list_terminals_pagination():
    ...

async def test_list_terminals_state_filter():
    ...

async def test_deactivate_terminal():
    ...

async def test_cancel_deactivation():
    ...

async def test_checkout_501_without_provider():
    """Returns 501 when payment_provider not configured."""
    ...
```

---

## Implementation Order

### Phase 1: Foundation (no behavior change)
1. Add `python-jose[cryptography]` to `pyproject.toml`
2. Add JWT settings to `config.py`
3. Create `app/auth.py`
4. Create migration `005_add_billing_tables.py`
5. Update `alembic/env.py` imports
6. Add new models to `models.py`
7. Run migration

### Phase 2: Pure calculations
1. Create `app/services/__init__.py` (if not exists)
2. Create `app/services/billing_calculations.py`
3. Write unit tests for all pure functions
4. Run tests: `pytest tests/test_billing_calculations.py`

### Phase 3: Service layer
1. Create `app/services/billing_service.py`
2. Test with integration tests (requires DB)

### Phase 4: Terminal API refactoring
1. Refactor `routers/licensebilling.py`
2. Test: expired terminal returns `Result=OK` with `state=error`
3. Test: `renewal_enabled=false` + not expired → `state=ok`

### Phase 5: User API
1. Create `routers/billing.py`
2. Register in `main.py`
3. Test all endpoints

### Phase 6: Nginx + Frontend
1. Add nginx location block
2. Create `api/billing.ts`
3. Create `routes/billing.tsx`
4. Update `App.tsx` and `layout.tsx`
5. E2E test

### Phase 7: Polish
1. Run `ruff check --fix` and `ruff format` on all Python files
2. Run `pyright` on ProcessingBackend
3. Manual smoke test

---

## Risk Areas & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Partial unique index on licenses may fail if existing data has multiple active licenses per terminal | Migration fails | Pre-migration: check for duplicates with `SELECT terminal_id, COUNT(*) FROM licenses WHERE is_active GROUP BY terminal_id HAVING COUNT(*) > 1`. Fix data before migration. |
| `processing-backend` hostname not resolvable from MenuBuilder nginx | 402/502 errors | Verify Docker network config; add `depends_on` if needed |
| JWT secret mismatch between MenuBuilder and ProcessingBackend | Auth failures | Both read from `.env`; document that `JWT_SECRET_HEX` must be identical |
| `dateutil` not in dependencies | ImportError | Add `python-dateutil>=2.9.0` to `pyproject.toml` |
| Calendar month arithmetic edge cases (Jan 31 + 1 month) | Wrong debt calculation | Use `dateutil.relativedelta` which handles this correctly; write edge-case tests |
| `compute_terminal_state` called in hot path (every billing check) | Performance | Pure function, no I/O, sub-microsecond. DB queries are the bottleneck; use eager loading if needed |
| Existing `check_license` raises HTTP 403 on expiry | Breaking change if other callers depend on it | Keep `check_license` unchanged; the new billing router uses `compute_terminal_state` directly |
| `paym_state` in existing Payment model vs new BillingOrder.status | Confusion | Clear naming: Payment is for terminal payment transactions, BillingOrder is for billing subscription orders |
| Nginx JWT module passes claims as headers, but we validate JWT in backend too | Redundant but safe | Defense-in-depth: nginx rejects garbage, backend validates claims. Acceptable tradeoff |

---

## Dependency Additions

### ProcessingBackend `pyproject.toml`:
```
python-jose[cryptography]>=3.3.0
python-dateutil>=2.9.0
```

### No new frontend dependencies needed:
- React 19, Ant Design 5, Axios already present
- All needed components (Table, Card, Tag, Modal, Tabs, DatePicker, Button) are in Ant Design
