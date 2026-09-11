"""Tests for /api/billing/* endpoints in MenuBuilder.

Covers: deactivate, cancel-deactivation, checkout, confirm, org isolation.
Uses dependency overrides to mock DB and JWT.
"""

import uuid
from datetime import UTC, datetime, timedelta
from unittest.mock import AsyncMock, MagicMock

import pytest
from httpx import ASGITransport, AsyncClient

from app.database import get_db
from app.main import app
from app.routers.billing import BillingUser, get_current_billing_user


@pytest.fixture(autouse=True)
def cleanup_overrides():
    yield
    app.dependency_overrides.clear()


@pytest.fixture
def anyio_backend():
    return "asyncio"


def _make_user(org_id: int = 1, username: str = "testuser") -> BillingUser:
    return BillingUser(username=username, org_id=org_id)


def _make_terminal(
    id: int = 1,
    org_id: int = 1,
    is_active: bool = True,
    address: str | None = None,
    note: str | None = None,
    terminal_type_id: int = 0,
    terminal_type_name: str | None = None,
    device_id: int | None = None,
) -> MagicMock:
    t = MagicMock()
    t.id = id
    t.device_id = device_id if device_id is not None else 100 + id
    t.sn = f"SN{id}"
    t.org_id = org_id
    t.is_active = is_active
    t.cert_serial = None
    t.cert_not_valid_after = None
    t.address = address
    t.note = note
    t.terminal_type_id = terminal_type_id
    if terminal_type_name:
        tt = MagicMock()
        tt.name = terminal_type_name
        t.terminal_type = tt
    else:
        t.terminal_type = None
    t.created_at = datetime(2026, 1, 1, tzinfo=UTC)
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
    settings.billing_mode = "standard"
    settings.min_billing_periods = 1
    settings.allowed_billing_periods = None
    settings.default_selection_mode = "all_due"
    settings.cert_billing_mode = "none"
    settings.cert_price_minor = None
    settings.tenant_pin_creation_enabled = False
    settings.cert_charge_primary_issue = True
    settings.cert_charge_reissue = True
    return settings


def _setup_db_mock(terminal, license_, org_settings):
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
    app.dependency_overrides[get_current_billing_user] = lambda: user

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
    app.dependency_overrides[get_current_billing_user] = lambda: user

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/billing/terminals/1/deactivate")

    assert resp.status_code == 404


@pytest.mark.anyio
async def test_reactivation_blocked_for_admin_disabled():
    """H5: Admin-disabled terminal → 400 on reactivation."""
    user = _make_user()
    terminal = _make_terminal(is_active=False)
    license_ = _make_license(renewal_enabled=False)
    org_settings = _make_org_settings()

    app.dependency_overrides[get_db] = _async_gen_mock_db(
        terminal, license_, org_settings
    )
    app.dependency_overrides[get_current_billing_user] = lambda: user

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post(
            "/api/billing/terminals/1/reactivation-checkout",
            json={"advance_periods": 1},
        )

    assert resp.status_code == 400
    assert "reactivated" in resp.json()["detail"].lower()


@pytest.mark.anyio
async def test_confirm_payment_returns_404_for_wrong_org():
    """Org isolation: can't confirm another org's order."""
    user = _make_user(org_id=999)
    order_id = str(uuid.uuid4())

    app.dependency_overrides[get_db] = _async_gen_mock_db_none_order()
    app.dependency_overrides[get_current_billing_user] = lambda: user

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
    app.dependency_overrides[get_current_billing_user] = lambda: user

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.get("/api/billing/summary")

    assert resp.status_code == 409


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
            result.scalar_one.return_value = license_
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


def _checkout_db_mock(terminal, license_, org_settings, added: list):
    """Mock DB for /checkout: no pending orders, captures created order items."""
    mock_db = AsyncMock()

    def execute_side_effect(stmt):
        stmt_str = str(stmt)
        result = MagicMock()
        if "org_billing_settings" in stmt_str:
            result.scalar_one_or_none.return_value = org_settings
        elif "billing_orders" in stmt_str:
            result.scalar_one_or_none.return_value = None
        elif "licenses" in stmt_str:
            result.scalar_one_or_none.return_value = license_
        elif "terminals" in stmt_str:
            result.scalar_one_or_none.return_value = terminal
        else:
            result.scalar_one_or_none.return_value = None
        return result

    mock_db.execute = AsyncMock(side_effect=execute_side_effect)
    mock_db.commit = AsyncMock()
    mock_db.flush = AsyncMock()
    mock_db.add = MagicMock(side_effect=added.append)

    async def override_get_db():
        yield mock_db

    return override_get_db


def _paid_org_settings():
    org_settings = _make_org_settings()
    org_settings.cert_billing_mode = "per_operation"
    org_settings.cert_price_minor = 300000
    org_settings.tenant_pin_creation_enabled = True
    return org_settings


async def _post_checkout(user, terminal, license_, org_settings, items, added):
    app.dependency_overrides[get_db] = _checkout_db_mock(
        terminal, license_, org_settings, added
    )
    app.dependency_overrides[get_current_billing_user] = lambda: user

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        return await client.post("/api/billing/checkout", json={"items": items})


@pytest.mark.anyio
async def test_checkout_combines_license_and_cert_pin():
    """One order carries both a renewal item and a cert_pin item."""
    added: list = []
    resp = await _post_checkout(
        _make_user(),
        _make_terminal(),
        _make_license(expires_at=datetime.now(UTC) + timedelta(days=45)),
        _paid_org_settings(),
        [{"terminal_id": 1, "advance_periods": 1, "include_cert_pin": True}],
        added,
    )

    assert resp.status_code == 200
    data = resp.json()
    assert data["amount_minor"] == 600000
    operations = sorted(i["operation"] for i in data["items"])
    assert operations == ["cert_pin", "renewal"]


@pytest.mark.anyio
async def test_checkout_cert_pin_only():
    """A terminal with nothing to renew can still pay for a certificate."""
    added: list = []
    resp = await _post_checkout(
        _make_user(),
        _make_terminal(),
        _make_license(expires_at=datetime.now(UTC) + timedelta(days=45)),
        _paid_org_settings(),
        [
            {
                "terminal_id": 1,
                "advance_periods": 0,
                "include_license": False,
                "include_cert_pin": True,
            }
        ],
        added,
    )

    assert resp.status_code == 200
    data = resp.json()
    assert data["amount_minor"] == 300000
    assert [i["operation"] for i in data["items"]] == ["cert_pin"]


@pytest.mark.anyio
async def test_checkout_overdue_charges_one_period_from_today():
    """A long-overdue license costs one period and restarts today."""
    added: list = []
    resp = await _post_checkout(
        _make_user(),
        _make_terminal(),
        _make_license(expires_at=datetime.now(UTC) - timedelta(days=95)),
        _make_org_settings(),
        [{"terminal_id": 1, "advance_periods": 0}],
        added,
    )

    assert resp.status_code == 200
    data = resp.json()
    assert data["amount_minor"] == 300000
    item = data["items"][0]
    assert item["operation"] == "reactivation"
    assert item["periods_due"] == 1
    new_expires = datetime.fromisoformat(item["new_expires_at"])
    assert new_expires > datetime.now(UTC) + timedelta(days=27)


@pytest.mark.anyio
async def test_checkout_accepts_disabled_terminal():
    """Disabled terminals rejoin through the regular checkout."""
    added: list = []
    resp = await _post_checkout(
        _make_user(),
        _make_terminal(),
        _make_license(
            expires_at=datetime.now(UTC) - timedelta(days=10), renewal_enabled=False
        ),
        _make_org_settings(),
        [{"terminal_id": 1, "advance_periods": 0}],
        added,
    )

    assert resp.status_code == 200
    assert resp.json()["items"][0]["operation"] == "reactivation"


@pytest.mark.anyio
async def test_checkout_cert_pin_requires_tenant_flag():
    """Cert PIN cannot be bought when self-service is disabled for the org."""
    added: list = []
    org_settings = _paid_org_settings()
    org_settings.tenant_pin_creation_enabled = False

    resp = await _post_checkout(
        _make_user(),
        _make_terminal(),
        _make_license(expires_at=datetime.now(UTC) + timedelta(days=45)),
        org_settings,
        [
            {
                "terminal_id": 1,
                "include_license": False,
                "include_cert_pin": True,
            }
        ],
        added,
    )

    assert resp.status_code == 403


@pytest.mark.anyio
async def test_checkout_rejects_free_cert_pin():
    """A free certificate must be requested directly, not paid for."""
    added: list = []
    org_settings = _make_org_settings()
    org_settings.tenant_pin_creation_enabled = True

    resp = await _post_checkout(
        _make_user(),
        _make_terminal(),
        _make_license(expires_at=datetime.now(UTC) + timedelta(days=45)),
        org_settings,
        [
            {
                "terminal_id": 1,
                "include_license": False,
                "include_cert_pin": True,
            }
        ],
        added,
    )

    assert resp.status_code == 400
    assert "free" in resp.json()["detail"].lower()


@pytest.mark.anyio
async def test_checkout_rejects_empty_selection():
    """Neither license nor certificate selected → 400."""
    added: list = []
    resp = await _post_checkout(
        _make_user(),
        _make_terminal(),
        _make_license(),
        _make_org_settings(),
        [{"terminal_id": 1, "include_license": False, "include_cert_pin": False}],
        added,
    )

    assert resp.status_code == 400


@pytest.mark.anyio
async def test_get_billing_terminals_returns_address_and_type_fields():
    """GET /api/billing/terminals returns address, note, terminal_type_id, terminal_type_name, created_at."""
    user = _make_user()
    terminal = _make_terminal(
        address="г. Москва, ул. Ленина, д. 10",
        note="Главный вход",
        terminal_type_id=1,
        terminal_type_name="Платежный терминал",
    )
    license_ = _make_license()
    org_settings = _make_org_settings()

    mock_db = AsyncMock()

    def execute_side_effect(stmt):
        stmt_str = str(stmt)
        result = MagicMock()
        if "org_billing_settings" in stmt_str:
            result.scalar_one_or_none.return_value = org_settings
        elif "terminals" in stmt_str:
            result.all.return_value = [(terminal, license_)]
        elif "certificate_pins" in stmt_str:
            result.scalars.return_value.all.return_value = []
        else:
            result.scalar_one_or_none.return_value = None
            result.scalars.return_value = MagicMock(return_value=[])
        return result

    mock_db.execute = AsyncMock(side_effect=execute_side_effect)

    async def override_get_db():
        yield mock_db

    app.dependency_overrides[get_db] = override_get_db
    app.dependency_overrides[get_current_billing_user] = lambda: user

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.get("/api/billing/terminals")

    assert resp.status_code == 200
    data = resp.json()
    items = data["items"] if isinstance(data, dict) and "items" in data else data
    assert len(items) == 1
    t_data = items[0]
    assert t_data["address"] == "г. Москва, ул. Ленина, д. 10"
    assert t_data["note"] == "Главный вход"
    assert t_data["terminal_type_id"] == 1
    assert t_data["terminal_type_name"] == "Платежный терминал"
    assert t_data["created_at"] is not None


@pytest.mark.anyio
async def test_get_billing_terminals_pagination_sorting_search_and_new_filter():
    """GET /api/billing/terminals supports pagination, comma search, sorting and new filter."""
    user = _make_user()
    t1 = _make_terminal(
        id=1,
        device_id=101,
        address="Address 101",
        note="Note 1",
    )
    t1.cert_serial = "CERT123"
    t1.created_at = datetime(2020, 1, 1, tzinfo=UTC)
    t2 = _make_terminal(
        id=2,
        device_id=102,
        address="Address 102",
        note="Note 2",
    )
    t2.cert_serial = None
    l1 = _make_license(expires_at=datetime.now(UTC) + timedelta(days=20))
    l2 = _make_license(expires_at=datetime.now(UTC) + timedelta(days=50))
    org_settings = _make_org_settings()

    mock_db = AsyncMock()

    def execute_side_effect(stmt):
        stmt_str = str(stmt)
        result = MagicMock()
        if "org_billing_settings" in stmt_str:
            result.scalar_one_or_none.return_value = org_settings
        elif "terminals" in stmt_str:
            result.all.return_value = [(t1, l1), (t2, l2)]
        elif "certificate_pins" in stmt_str:
            result.scalars.return_value.all.return_value = []
        else:
            result.scalar_one_or_none.return_value = None
            result.scalars.return_value = MagicMock(return_value=[])
        return result

    mock_db.execute = AsyncMock(side_effect=execute_side_effect)

    async def override_get_db():
        yield mock_db

    app.dependency_overrides[get_db] = override_get_db
    app.dependency_overrides[get_current_billing_user] = lambda: user

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        # 1. Test pagination & sort by device_id desc
        resp = await client.get(
            "/api/billing/terminals?page=1&page_size=50&sort_by=device_id&sort_order=desc"
        )
        assert resp.status_code == 200
        data = resp.json()
        assert "items" in data
        assert data["total_count"] == 2
        assert data["page"] == 1
        assert data["page_size"] == 50
        assert data["items"][0]["device_id"] == 102
        assert data["items"][1]["device_id"] == 101
        assert resp.headers.get("x-total-count") == "2"

        # 2. Test comma search
        resp_search = await client.get(
            "/api/billing/terminals?search=101,105&page=1&page_size=50"
        )
        assert resp_search.status_code == 200
        data_search = resp_search.json()
        assert len(data_search["items"]) == 1
        assert data_search["items"][0]["device_id"] == 101

        # 3. Test sort by license_expires_at asc
        resp_sort_lic = await client.get(
            "/api/billing/terminals?sort_by=license_expires_at&sort_order=asc&page=1&page_size=50"
        )
        assert resp_sort_lic.status_code == 200
        items_lic = resp_sort_lic.json()["items"]
        assert items_lic[0]["device_id"] == 101

        # 4. Test filter only_new
        resp_new = await client.get(
            "/api/billing/terminals?only_new=true&page=1&page_size=50"
        )
        assert resp_new.status_code == 200
        items_new = resp_new.json()["items"]
        assert len(items_new) >= 1
        assert items_new[0]["device_id"] == 102


@pytest.mark.anyio
async def test_checkout_rejects_cert_pin_for_disabled_terminal():
    """Cert PIN cannot be bought for a disabled terminal."""
    added: list = []
    org_settings = _paid_org_settings()
    org_settings.tenant_pin_creation_enabled = True

    resp = await _post_checkout(
        _make_user(),
        _make_terminal(is_active=False),
        _make_license(expires_at=datetime.now(UTC) - timedelta(days=10)),
        org_settings,
        [
            {
                "terminal_id": 1,
                "include_license": False,
                "include_cert_pin": True,
            }
        ],
        added,
    )

    assert resp.status_code == 400
    assert "disabled" in resp.json()["detail"].lower()


@pytest.mark.anyio
async def test_calculate_endpoint():
    """POST /api/billing/calculate returns item calculation without adding DB order."""
    added: list = []
    user = _make_user()
    terminal = _make_terminal()
    license_ = _make_license(expires_at=datetime.now(UTC) + timedelta(days=45))
    org_settings = _paid_org_settings()

    app.dependency_overrides[get_db] = _checkout_db_mock(
        terminal, license_, org_settings, added
    )
    app.dependency_overrides[get_current_billing_user] = lambda: user

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post(
            "/api/billing/calculate",
            json={
                "items": [
                    {
                        "terminal_id": 1,
                        "advance_periods": 1,
                        "include_license": True,
                        "include_cert_pin": True,
                    }
                ]
            },
        )

    assert resp.status_code == 200
    data = resp.json()
    assert data["currency"] == "RUB"
    assert data["total_amount_minor"] == 600000
    assert data["license_amount_minor"] == 300000
    assert data["cert_amount_minor"] == 300000
    assert len(data["items"]) == 1
    item = data["items"][0]
    assert item["terminal_id"] == 1
    assert item["advance_periods"] == 1
    assert item["license_amount_minor"] == 300000
    assert item["cert_amount_minor"] == 300000
    assert item["total_item_amount_minor"] == 600000
    # No BillingOrder should have been added
    assert len(added) == 0


@pytest.mark.anyio
async def test_checkout_cert_linked_mode():
    """In cert_linked mode, regular license is 0 ₽ and only cert PIN is charged."""
    added: list = []
    org_settings = _paid_org_settings()
    org_settings.billing_mode = "cert_linked"

    resp = await _post_checkout(
        _make_user(),
        _make_terminal(),
        _make_license(expires_at=datetime.now(UTC) + timedelta(days=45)),
        org_settings,
        [
            {
                "terminal_id": 1,
                "advance_periods": 1,
                "include_license": True,
                "include_cert_pin": True,
            }
        ],
        added,
    )

    assert resp.status_code == 200
    data = resp.json()
    # 0 for license + 300000 for cert = 300000
    assert data["amount_minor"] == 300000


@pytest.mark.anyio
async def test_checkout_min_billing_periods_enforced():
    """Checkout enforces min_billing_periods."""
    added: list = []
    org_settings = _make_org_settings()
    org_settings.min_billing_periods = 3

    # Attempt to pay for only 1 period when 3 is min required -> 400
    resp = await _post_checkout(
        _make_user(),
        _make_terminal(),
        _make_license(expires_at=datetime.now(UTC) + timedelta(days=45)),
        org_settings,
        [{"terminal_id": 1, "advance_periods": 1, "include_license": True}],
        added,
    )
    assert resp.status_code == 400
    assert "minimum" in resp.json()["detail"].lower()

    # Paying for 3 periods -> 200
    resp_ok = await _post_checkout(
        _make_user(),
        _make_terminal(),
        _make_license(expires_at=datetime.now(UTC) + timedelta(days=45)),
        org_settings,
        [{"terminal_id": 1, "advance_periods": 3, "include_license": True}],
        added,
    )
    assert resp_ok.status_code == 200
    assert resp_ok.json()["amount_minor"] == 900000


@pytest.mark.anyio
async def test_checkout_allowed_billing_periods_enforced():
    """Checkout enforces allowed_billing_periods (e.g. 3,6,12)."""
    added: list = []
    org_settings = _make_org_settings()
    org_settings.allowed_billing_periods = "3,6,12"
    org_settings.min_billing_periods = 1

    # 2 months is not in (3,6,12) -> 400
    resp = await _post_checkout(
        _make_user(),
        _make_terminal(),
        _make_license(expires_at=datetime.now(UTC) + timedelta(days=45)),
        org_settings,
        [{"terminal_id": 1, "advance_periods": 2, "include_license": True}],
        added,
    )
    assert resp.status_code == 400
    assert "not allowed" in resp.json()["detail"].lower()

    # 6 months is in (3,6,12) -> 200
    resp_ok = await _post_checkout(
        _make_user(),
        _make_terminal(),
        _make_license(expires_at=datetime.now(UTC) + timedelta(days=45)),
        org_settings,
        [{"terminal_id": 1, "advance_periods": 6, "include_license": True}],
        added,
    )
    assert resp_ok.status_code == 200
    assert resp_ok.json()["amount_minor"] == 1800000


@pytest.mark.anyio
async def test_orders_endpoint_alias():
    """POST /api/billing/orders works as an alias for checkout."""
    added: list = []
    user = _make_user()
    terminal = _make_terminal()
    license_ = _make_license(expires_at=datetime.now(UTC) + timedelta(days=45))
    org_settings = _make_org_settings()

    app.dependency_overrides[get_db] = _checkout_db_mock(
        terminal, license_, org_settings, added
    )
    app.dependency_overrides[get_current_billing_user] = lambda: user

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post(
            "/api/billing/orders",
            json={
                "items": [
                    {"terminal_id": 1, "advance_periods": 1, "include_license": True}
                ]
            },
        )

    assert resp.status_code == 200
    assert resp.json()["amount_minor"] == 300000
