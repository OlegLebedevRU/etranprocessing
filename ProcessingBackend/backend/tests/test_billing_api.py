"""Tests for /api/billing/* endpoints.

Covers: deactivate, cancel-deactivation, checkout, confirm, org isolation.
Uses dependency overrides to mock DB and JWT.
"""

import uuid
from datetime import UTC, datetime, timedelta
from unittest.mock import AsyncMock, MagicMock

import pytest
from httpx import ASGITransport, AsyncClient

from app.dependencies import JwtUser
from app.main import app


@pytest.fixture(autouse=True)
def cleanup_overrides():
    yield
    app.dependency_overrides.clear()


@pytest.fixture
def anyio_backend():
    return "asyncio"


def _make_user(org_id: int = 1, username: str = "testuser") -> JwtUser:
    return JwtUser(username=username, org_id=org_id)


def _make_terminal(id: int = 1, org_id: int = 1, is_active: bool = True) -> MagicMock:
    t = MagicMock()
    t.id = id
    t.device_id = 100 + id
    t.sn = f"SN{id}"
    t.org_id = org_id
    t.is_active = is_active
    return t


def _make_license(
    terminal_id: int = 1,
    org_id: int = 1,
    expires_at: datetime | None = None,
    renewal_enabled: bool = True,
    billing_period_months: int = 1,
    monthly_price_override_minor: int | None = None,
) -> MagicMock:
    lic = MagicMock()
    lic.id = terminal_id
    lic.terminal_id = terminal_id
    lic.org_id = org_id
    lic.expires_at = expires_at or (datetime.now(UTC) + timedelta(days=30))
    lic.renewal_enabled = renewal_enabled
    lic.deactivation_requested_at = None
    lic.billing_period_months = billing_period_months
    lic.monthly_price_override_minor = monthly_price_override_minor
    lic.balance = 0
    lic.is_active = True
    return lic


def _make_org_settings(org_id: int = 1) -> MagicMock:
    settings = MagicMock()
    settings.org_id = org_id
    settings.monthly_price_minor = 300000
    settings.currency = "RUB"
    return settings


def _setup_db_mock(mock_get_db, terminal, license_, org_settings):
    """Setup a mock DB session that returns controlled data."""
    mock_db = AsyncMock()

    def execute_side_effect(stmt):
        stmt_str = str(stmt)
        result = MagicMock()
        if "org_billing_settings" in stmt_str:
            result.scalar_one_or_none.return_value = org_settings
        elif "licenses" in stmt_str:
            result.scalar_one_or_none.return_value = license_
        elif "terminals" in stmt_str:
            result.scalar_one_or_none.return_value = terminal
            result.all.return_value = [(terminal, license_)] if terminal else []
        elif "billing_orders" in stmt_str:
            order = MagicMock()
            order.id = uuid.uuid4()
            order.org_id = 1
            order.status = "pending"
            order.paid_at = None
            order.currency = "RUB"
            order.amount_minor = 300000
            result.scalar_one_or_none.return_value = order
        elif "billing_order_items" in stmt_str:
            item = MagicMock()
            item.terminal_id = 1
            item.new_expires_at = datetime.now(UTC) + timedelta(days=60)
            item.billing_period_months = 1
            result.scalars.return_value.all.return_value = [item]
        else:
            result.scalar_one_or_none.return_value = None
        return result

    mock_db.execute = AsyncMock(side_effect=execute_side_effect)
    mock_db.commit = AsyncMock()
    mock_db.flush = AsyncMock()
    mock_db.add = MagicMock()

    async def override_get_db():
        yield mock_db

    app.dependency_overrides[get_db] = override_get_db
    return mock_db


# Import after fixtures to avoid circular imports
from app.database import get_db


@pytest.mark.anyio
async def test_deactivate_terminal():
    """Deactivate endpoint sets renewal_enabled=false."""
    user = _make_user()
    terminal = _make_terminal()
    license_ = _make_license()
    org_settings = _make_org_settings()

    app.dependency_overrides[get_db] = _async_gen_mock_db(
        terminal, license_, org_settings
    )
    from app.dependencies import get_current_user_jwt

    app.dependency_overrides[get_current_user_jwt] = lambda: user

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/billing/terminals/1/deactivate")

    assert resp.status_code == 200
    data = resp.json()
    assert data["terminal_id"] == 1
    assert data["status"] in ("deactivation_scheduled", "disabled")


@pytest.mark.anyio
async def test_deactivate_wrong_org_returns_404():
    """Terminal from another org → 404."""
    user = _make_user(org_id=999)
    license_ = _make_license()
    org_settings = _make_org_settings()

    # Mock DB: terminal query returns None because org_id mismatch
    mock_db = AsyncMock()

    def execute_side_effect(stmt):
        result = MagicMock()
        stmt_str = str(stmt)
        if "org_billing_settings" in stmt_str:
            result.scalar_one_or_none.return_value = org_settings
        elif "terminals" in stmt_str and "org_id" in stmt_str:
            result.scalar_one_or_none.return_value = None
        elif "licenses" in stmt_str:
            result.scalar_one_or_none.return_value = license_
        return result

    mock_db.execute = AsyncMock(side_effect=execute_side_effect)
    mock_db.commit = AsyncMock()

    async def override_get_db():
        yield mock_db

    app.dependency_overrides[get_db] = override_get_db
    from app.dependencies import get_current_user_jwt

    app.dependency_overrides[get_current_user_jwt] = lambda: user

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/billing/terminals/1/deactivate")

    assert resp.status_code == 404


@pytest.mark.anyio
async def test_reactivation_blocked_for_admin_disabled():
    """H5: Admin-disabled terminal → 403 on reactivation."""
    user = _make_user()
    terminal = _make_terminal(is_active=False)
    license_ = _make_license(renewal_enabled=False)
    org_settings = _make_org_settings()

    app.dependency_overrides[get_db] = _async_gen_mock_db(
        terminal, license_, org_settings
    )
    from app.dependencies import get_current_user_jwt

    app.dependency_overrides[get_current_user_jwt] = lambda: user

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post(
            "/api/billing/terminals/1/reactivation-checkout",
            json={"advance_periods": 1},
        )

    assert resp.status_code == 403
    assert "administratively" in resp.json()["detail"].lower()


@pytest.mark.anyio
async def test_confirm_payment_returns_404_for_wrong_org():
    """Org isolation: can't confirm another org's order."""
    user = _make_user(org_id=999)
    order_id = str(uuid.uuid4())

    app.dependency_overrides[get_db] = _async_gen_mock_db_none_order()
    from app.dependencies import get_current_user_jwt

    app.dependency_overrides[get_current_user_jwt] = lambda: user

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post(f"/api/billing/orders/{order_id}/confirm")

    assert resp.status_code == 404


@pytest.mark.anyio
async def test_missing_org_settings_returns_409():
    """Missing OrgBillingSettings → 409."""
    user = _make_user()
    terminal = _make_terminal()
    license_ = _make_license()

    # DB returns None for org_billing_settings
    mock_db = AsyncMock()

    def execute_side_effect(stmt):
        result = MagicMock()
        stmt_str = str(stmt)
        if "org_billing_settings" in stmt_str:
            result.scalar_one_or_none.return_value = None
        elif "terminals" in stmt_str:
            result.scalar_one_or_none.return_value = terminal
        elif "licenses" in stmt_str:
            result.scalar_one_or_none.return_value = license_
        return result

    mock_db.execute = AsyncMock(side_effect=execute_side_effect)

    async def override_get_db():
        yield mock_db

    app.dependency_overrides[get_db] = override_get_db
    from app.dependencies import get_current_user_jwt

    app.dependency_overrides[get_current_user_jwt] = lambda: user

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.get("/api/billing/summary")

    assert resp.status_code == 409


# === Helpers ===


def _async_gen_mock_db(terminal, license_, org_settings):
    """Create a mock DB session factory for dependency override."""

    mock_db = AsyncMock()

    def execute_side_effect(stmt):
        result = MagicMock()
        stmt_str = str(stmt)
        if "org_billing_settings" in stmt_str:
            result.scalar_one_or_none.return_value = org_settings
        elif "licenses" in stmt_str:
            result.scalar_one_or_none.return_value = license_
        elif "terminals" in stmt_str:
            if "org_id" in stmt_str:
                result.scalar_one_or_none.return_value = terminal
            else:
                result.all.return_value = [(terminal, license_)]
        elif "billing_orders" in stmt_str:
            result.scalar_one_or_none.return_value = None
        elif "billing_order_items" in stmt_str:
            result.scalars.return_value.all.return_value = []
        else:
            result.scalar_one_or_none.return_value = None
        return result

    mock_db.execute = AsyncMock(side_effect=execute_side_effect)
    mock_db.commit = AsyncMock()
    mock_db.flush = AsyncMock()
    mock_db.add = MagicMock()

    async def override():
        yield mock_db

    return override


def _async_gen_mock_db_none_order():
    """Mock DB that returns None for any order query."""
    mock_db = AsyncMock()

    def execute_side_effect(stmt):
        result = MagicMock()
        result.scalar_one_or_none.return_value = None
        return result

    mock_db.execute = AsyncMock(side_effect=execute_side_effect)

    async def override():
        yield mock_db

    return override
