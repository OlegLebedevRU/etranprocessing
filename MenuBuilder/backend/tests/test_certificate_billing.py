"""Tests for organizational PIN-driven certificate billing in MenuBuilder.

Covers: cert_billing service (pure functions + PIN generation), the tenant
certificate-pin endpoint, and confirm_payment's cert_pin branch.
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
from app.services.cert_billing import (
    CertBillingMode,
    CertOperationType,
    CertPolicy,
    build_cert_policy_snapshot,
    compute_pin_expiry,
    generate_pin,
    generate_unique_pin,
    is_operation_billable,
    mask_pin,
    resolve_cert_policy,
    resolve_effective_price,
    resolve_operation_type,
)


@pytest.fixture(autouse=True)
def cleanup_overrides():
    yield
    app.dependency_overrides.clear()


@pytest.fixture
def anyio_backend():
    return "asyncio"


# === Unit tests: pure functions ===


def test_generate_pin_is_numeric_and_correct_length():
    pin = generate_pin()
    assert len(pin) == 6
    assert pin.isdigit()


def test_mask_pin_hides_all_but_last_digits():
    assert mask_pin("773773") == "***773"
    assert mask_pin("") == ""


def _org_settings(**overrides) -> MagicMock:
    settings = MagicMock()
    settings.cert_billing_mode = overrides.get("cert_billing_mode", "none")
    settings.cert_price_minor = overrides.get("cert_price_minor")
    settings.currency = overrides.get("currency", "RUB")
    settings.tenant_pin_creation_enabled = overrides.get(
        "tenant_pin_creation_enabled", False
    )
    settings.cert_charge_primary_issue = overrides.get(
        "cert_charge_primary_issue", True
    )
    settings.cert_charge_reissue = overrides.get("cert_charge_reissue", True)
    return settings


def test_resolve_cert_policy_none_mode():
    policy = resolve_cert_policy(_org_settings())
    assert policy.mode == CertBillingMode.NONE
    assert policy.price_minor is None


def test_resolve_operation_type_primary_vs_reissue():
    assert resolve_operation_type(None) == CertOperationType.PRIMARY_ISSUE
    assert resolve_operation_type("ABC123") == CertOperationType.REISSUE


def test_effective_price_zero_when_mode_none():
    policy = resolve_cert_policy(_org_settings(cert_billing_mode="none"))
    assert resolve_effective_price(policy, CertOperationType.PRIMARY_ISSUE) == 0


def test_effective_price_zero_when_price_zero():
    policy = resolve_cert_policy(
        _org_settings(cert_billing_mode="per_operation", cert_price_minor=0)
    )
    assert resolve_effective_price(policy, CertOperationType.PRIMARY_ISSUE) == 0


def test_effective_price_positive_when_paid():
    policy = resolve_cert_policy(
        _org_settings(cert_billing_mode="per_operation", cert_price_minor=500000)
    )
    assert resolve_effective_price(policy, CertOperationType.PRIMARY_ISSUE) == 500000
    assert resolve_effective_price(policy, CertOperationType.REISSUE) == 500000


def test_effective_price_zero_when_operation_not_charged():
    policy = resolve_cert_policy(
        _org_settings(
            cert_billing_mode="per_operation",
            cert_price_minor=500000,
            cert_charge_reissue=False,
        )
    )
    assert resolve_effective_price(policy, CertOperationType.REISSUE) == 0
    assert resolve_effective_price(policy, CertOperationType.PRIMARY_ISSUE) == 500000


def test_is_operation_billable_false_for_none_mode():
    policy = resolve_cert_policy(_org_settings(cert_billing_mode="none"))
    assert not is_operation_billable(policy, CertOperationType.PRIMARY_ISSUE)
    assert not is_operation_billable(policy, CertOperationType.REISSUE)


def test_is_operation_billable_respects_per_operation_flags():
    policy = resolve_cert_policy(
        _org_settings(
            cert_billing_mode="per_operation",
            cert_charge_primary_issue=True,
            cert_charge_reissue=False,
        )
    )
    assert is_operation_billable(policy, CertOperationType.PRIMARY_ISSUE)
    assert not is_operation_billable(policy, CertOperationType.REISSUE)


def test_build_cert_policy_snapshot():
    policy = CertPolicy(
        mode=CertBillingMode.PER_OPERATION,
        price_minor=500000,
        currency="RUB",
        tenant_pin_creation_enabled=True,
        charge_primary_issue=True,
        charge_reissue=True,
    )
    snapshot = build_cert_policy_snapshot(
        policy, CertOperationType.PRIMARY_ISSUE, 500000
    )
    assert snapshot == {
        "cert_billing_mode": "per_operation",
        "cert_price_minor": 500000,
        "currency": "RUB",
        "operation": "primary_issue",
    }


def test_compute_pin_expiry_adds_24h_by_default():
    base = datetime(2026, 8, 20, 12, 0, tzinfo=UTC)
    expiry = compute_pin_expiry(base)
    assert expiry == datetime(2026, 8, 21, 12, 0, tzinfo=UTC)


# === Async unit tests: unique PIN generation ===


@pytest.mark.anyio
async def test_generate_unique_pin_retries_on_collision():
    db = AsyncMock()
    # 1st call returns existing row (collision), 2nd call returns None (unique)
    first_result = MagicMock()
    first_result.scalar_one_or_none.return_value = 1  # collision
    second_result = MagicMock()
    second_result.scalar_one_or_none.return_value = None  # unique

    db.execute.side_effect = [first_result, second_result]

    pin = await generate_unique_pin(db)
    assert len(pin) == 6
    assert db.execute.call_count == 2


@pytest.mark.anyio
async def test_generate_unique_pin_raises_when_all_collide():
    db = AsyncMock()
    colliding = MagicMock()
    colliding.scalar_one_or_none.return_value = 1
    db.execute.return_value = colliding

    with pytest.raises(RuntimeError, match="Unable to generate"):
        await generate_unique_pin(db)


# === API tests: POST /api/billing/terminals/{id}/certificate-pin ===


def _make_user(org_id: int = 1, username: str = "testuser") -> BillingUser:
    return BillingUser(username=username, org_id=org_id)


def _make_terminal(
    id: int = 1,
    device_id: int = 773,
    org_id: int = 1,
    cert_serial: str | None = None,
) -> MagicMock:
    t = MagicMock()
    t.id = id
    t.device_id = device_id
    t.sn = f"SN{id}"
    t.org_id = org_id
    t.cert_serial = cert_serial
    t.cert_not_valid_after = None
    t.is_active = True
    return t


def _cert_pin_mock_db(
    terminal,
    org_settings,
    existing_pending_pin=None,
    existing_pending_order=None,
):
    """Mock DB specifically for certificate-pin router calls."""
    mock_db = AsyncMock()

    def execute_side_effect(stmt):
        stmt_str = str(stmt)
        result = MagicMock()
        if "org_billing_settings" in stmt_str:
            result.scalar_one_or_none.return_value = org_settings
        elif "terminals" in stmt_str:
            result.scalar_one_or_none.return_value = terminal
        elif "billing_orders" in stmt_str:
            result.scalar_one_or_none.return_value = existing_pending_order
        elif "certificate_pins" in stmt_str:
            # First execute in create_certificate_pin looks for existing pending PIN.
            # Later execute in generate_unique_pin checks collision (returns None).
            result.scalar_one_or_none.return_value = existing_pending_pin
        elif "licenses" in stmt_str:
            result.scalar_one_or_none.return_value = None
        else:
            result.scalar_one_or_none.return_value = None
        return result

    mock_db.execute = AsyncMock(side_effect=execute_side_effect)
    mock_db.commit = AsyncMock()
    mock_db.flush = AsyncMock()
    mock_db.add = MagicMock()

    async def override():
        yield mock_db

    return mock_db, override


def _override_auth(user: BillingUser):
    app.dependency_overrides[get_current_billing_user] = lambda: user
    return get_db


@pytest.mark.anyio
async def test_certificate_pin_free_creates_pin_immediately():
    """mode=none + tenant_pin_creation_enabled → 200 pin_ready, no payment required."""
    user = _make_user()
    terminal = _make_terminal()
    org_settings = _org_settings(
        cert_billing_mode="none", tenant_pin_creation_enabled=True
    )

    get_db = _override_auth(user)
    mock_db, override = _cert_pin_mock_db(terminal, org_settings)
    app.dependency_overrides[get_db] = override

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/billing/terminals/1/certificate-pin")

    assert resp.status_code == 200
    data = resp.json()
    assert data["status"] == "pin_ready"
    assert data["payment_required"] is False
    assert data["terminal_id"] == 1
    assert len(data["pin"]) == 6
    assert data["pin"].isdigit()
    assert mock_db.commit.called


@pytest.mark.anyio
async def test_certificate_pin_paid_creates_order_and_returns_payment_required():
    """mode=per_operation with positive price → returns payment_required with order details."""
    user = _make_user()
    terminal = _make_terminal()
    org_settings = _org_settings(
        cert_billing_mode="per_operation",
        cert_price_minor=500000,
        tenant_pin_creation_enabled=True,
    )

    get_db = _override_auth(user)
    mock_db, override = _cert_pin_mock_db(terminal, org_settings)
    app.dependency_overrides[get_db] = override

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/billing/terminals/1/certificate-pin")

    assert resp.status_code == 200
    data = resp.json()
    assert data["status"] == "payment_required"
    assert data["payment_required"] is True
    assert data["terminal_id"] == 1
    assert data["amount_minor"] == 500000
    assert data["currency"] == "RUB"
    assert "order_id" in data
    assert mock_db.commit.called


@pytest.mark.anyio
async def test_certificate_pin_reissue_free_when_reissue_charge_disabled():
    """Terminal has existing cert_serial, org charges primary but not reissue → free."""
    user = _make_user()
    terminal = _make_terminal(cert_serial="OLD12345")
    org_settings = _org_settings(
        cert_billing_mode="per_operation",
        cert_price_minor=500000,
        cert_charge_primary_issue=True,
        cert_charge_reissue=False,
        tenant_pin_creation_enabled=True,
    )

    get_db = _override_auth(user)
    _, override = _cert_pin_mock_db(terminal, org_settings)
    app.dependency_overrides[get_db] = override

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/billing/terminals/1/certificate-pin")

    assert resp.status_code == 200
    data = resp.json()
    assert data["status"] == "pin_ready"
    assert data["payment_required"] is False


@pytest.mark.anyio
async def test_certificate_pin_wrong_org_returns_404():
    """Tenant isolation: terminal belonging to another org → 404."""
    user = _make_user(org_id=999)
    org_settings = _org_settings(tenant_pin_creation_enabled=True)
    get_db = _override_auth(user)
    _, override = _cert_pin_mock_db(None, org_settings)
    app.dependency_overrides[get_db] = override

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/billing/terminals/1/certificate-pin")

    assert resp.status_code == 404


@pytest.mark.anyio
async def test_certificate_pin_disabled_flag_returns_403():
    """tenant_pin_creation_enabled=false → self-service disabled for this org."""
    user = _make_user()
    terminal = _make_terminal()
    org_settings = _org_settings(tenant_pin_creation_enabled=False)
    get_db = _override_auth(user)
    _, override = _cert_pin_mock_db(terminal, org_settings)
    app.dependency_overrides[get_db] = override

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/billing/terminals/1/certificate-pin")

    assert resp.status_code == 403


@pytest.mark.anyio
async def test_certificate_pin_idempotent_returns_existing_pending_pin():
    """A repeated request while a pending PIN is still valid returns the same PIN."""
    user = _make_user()
    terminal = _make_terminal()
    org_settings = _org_settings(
        cert_billing_mode="none", tenant_pin_creation_enabled=True
    )
    existing_pin = MagicMock()
    existing_pin.pin = "112233"
    existing_pin.expires_at = datetime.now(UTC) + timedelta(hours=1)

    get_db = _override_auth(user)
    _, override = _cert_pin_mock_db(
        terminal, org_settings, existing_pending_pin=existing_pin
    )
    app.dependency_overrides[get_db] = override

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/billing/terminals/1/certificate-pin")

    assert resp.status_code == 200
    data = resp.json()
    assert data["status"] == "pin_ready"
    assert data["pin"] == "112233"


@pytest.mark.anyio
async def test_certificate_pin_idempotent_returns_existing_pending_order():
    """A repeated request while a paid order is still pending returns the same order."""
    user = _make_user()
    terminal = _make_terminal()
    org_settings = _org_settings(
        cert_billing_mode="per_operation",
        cert_price_minor=500000,
        tenant_pin_creation_enabled=True,
    )
    existing_order = MagicMock()
    existing_order.id = uuid.uuid4()
    existing_order.amount_minor = 500000
    existing_order.currency = "RUB"
    existing_order.payment_url = "/api/billing/orders/x/confirm"

    get_db = _override_auth(user)
    _, override = _cert_pin_mock_db(
        terminal, org_settings, existing_pending_order=existing_order
    )
    app.dependency_overrides[get_db] = override

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/billing/terminals/1/certificate-pin")

    assert resp.status_code == 200
    data = resp.json()
    assert data["status"] == "payment_required"
    assert data["order_id"] == str(existing_order.id)


# === API tests: confirm_payment cert_pin branch ===


@pytest.mark.anyio
async def test_confirm_payment_cert_pin_creates_pin_not_license():
    """Confirming a cert_pin order creates a CertificatePin and does not touch licenses."""
    user = _make_user()
    order_id = uuid.uuid4()

    order = MagicMock()
    order.id = order_id
    order.org_id = 1
    order.status = "pending"
    order.paid_at = None

    item = MagicMock()
    item.id = 42
    item.terminal_id = 7
    item.operation = "cert_pin"

    mock_db = AsyncMock()

    def execute_side_effect(stmt):
        stmt_str = str(stmt)
        result = MagicMock()
        if "billing_orders" in stmt_str:
            result.scalar_one_or_none.return_value = order
        elif "billing_order_items" in stmt_str:
            result.scalars.return_value.all.return_value = [item]
        elif "certificate_pins" in stmt_str:
            result.scalar_one_or_none.return_value = None
        else:
            result.scalar_one_or_none.return_value = None
        return result

    mock_db.execute = AsyncMock(side_effect=execute_side_effect)
    mock_db.commit = AsyncMock()
    mock_db.add = MagicMock()

    async def override():
        yield mock_db

    app.dependency_overrides[get_db] = override
    app.dependency_overrides[get_current_billing_user] = lambda: user

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post(f"/api/billing/orders/{order_id}/confirm")

    assert resp.status_code == 200
    data = resp.json()
    assert data["status"] == "paid"
    assert data["items_updated"] == 1

    added_types = [type(call.args[0]).__name__ for call in mock_db.add.call_args_list]
    assert "License" not in added_types
    assert "CertificatePin" in added_types
