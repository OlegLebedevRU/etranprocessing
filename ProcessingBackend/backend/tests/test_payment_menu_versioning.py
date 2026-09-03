from datetime import UTC, datetime
from unittest.mock import AsyncMock, MagicMock

import pytest

from app.models import (
    BalanceTerminalTsp,
    MenuVariantSnapshot,
    Terminal,
    TerminalMenuBinding,
    Tsp,
)
from app.services.payment_service import PaymentService


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.anyio
async def test_payment_creation_records_loaded_snapshot_id():
    """Verify that PaymentService.create_payment links the payment and balance to the loaded snapshot A{x}."""
    mock_db = AsyncMock()

    terminal = Terminal(id=1, device_id=209, org_id=1, sn="SN209", is_active=True)
    tsp = Tsp(tsp_id=10, tsp_code=7001, tsp_name="Тест ТСП")
    binding = TerminalMenuBinding(
        id=1,
        device_id=209,
        menu_variant_id=5,
        loaded_version=2,
        loaded_at=datetime.now(UTC),
    )
    snapshot = MenuVariantSnapshot(
        id=42,
        menu_variant_id=5,
        version=2,
        snapshot_data={"services_by_tsp": {"7001": {"name": "Снепшот Услуга"}}},
    )

    async def mock_scalar(stmt, params=None):
        sql = str(stmt)
        if "FROM terminal_menu_bindings" in sql:
            return binding
        if "FROM menu_variant_snapshots" in sql:
            return snapshot
        if "FROM tsp_parameter_codes" in sql:
            return 100
        return None

    mock_db.scalar.side_effect = mock_scalar

    async def mock_execute(stmt, params=None):
        sql = str(stmt)
        res = MagicMock()
        if "FROM tsp" in sql:
            res.scalar_one_or_none.return_value = tsp
        elif "FROM balance_terminal_tsp" in sql:
            res.scalar_one_or_none.return_value = None  # New balance record
        else:
            res.scalar_one_or_none.return_value = None
        return res

    mock_db.execute.side_effect = mock_execute

    service = PaymentService(mock_db)
    payment = await service.create_payment(
        terminal=terminal,
        tsp_code=7001,
        amount=5000,
        paym_ext_id="0209_240826_10000000",
        params={101: "9991234567"},
    )

    assert payment.menu_snapshot_id == 42
    assert payment.menu_version == 2
    assert payment.paym_amount == 5000
    assert payment.paym_tsp_code == 7001

    # Check added balance record has menu_snapshot_id == 42 and menu_version == 2
    added_objects = [call[0][0] for call in mock_db.add.call_args_list]
    balance_records = [
        obj for obj in added_objects if isinstance(obj, BalanceTerminalTsp)
    ]
    assert len(balance_records) == 1
    assert balance_records[0].menu_snapshot_id == 42
    assert balance_records[0].menu_version == 2
    assert balance_records[0].amount == 5000


@pytest.mark.anyio
async def test_payment_creation_fallback_when_no_snapshot():
    """Verify that when terminal has no binding or loaded_version is None, menu_snapshot_id is NULL."""
    mock_db = AsyncMock()

    terminal = Terminal(id=1, device_id=209, org_id=1, sn="SN209", is_active=True)
    tsp = Tsp(tsp_id=10, tsp_code=7001, tsp_name="Тест ТСП")

    async def mock_scalar(stmt, params=None):
        sql = str(stmt)
        if "FROM terminal_menu_bindings" in sql:
            return None  # No binding
        if "FROM tsp_parameter_codes" in sql:
            return 100
        return None

    mock_db.scalar.side_effect = mock_scalar

    async def mock_execute(stmt, params=None):
        sql = str(stmt)
        res = MagicMock()
        if "FROM tsp" in sql:
            res.scalar_one_or_none.return_value = tsp
        elif "FROM balance_terminal_tsp" in sql:
            res.scalar_one_or_none.return_value = None
        else:
            res.scalar_one_or_none.return_value = None
        return res

    mock_db.execute.side_effect = mock_execute

    service = PaymentService(mock_db)
    payment = await service.create_payment(
        terminal=terminal,
        tsp_code=7001,
        amount=5000,
        paym_ext_id="0209_240826_10000000",
        params={101: "9991234567"},
    )

    assert payment.menu_snapshot_id is None
    assert payment.menu_version == 1
    added_objects = [call[0][0] for call in mock_db.add.call_args_list]
    balance_records = [
        obj for obj in added_objects if isinstance(obj, BalanceTerminalTsp)
    ]
    assert len(balance_records) == 1
    assert balance_records[0].menu_snapshot_id is None
    assert balance_records[0].menu_version == 1


@pytest.mark.anyio
async def test_payment_creation_updates_existing_balance_matching_version():
    """Verify that when balance record for same menu_version exists, it is updated and not duplicated."""
    mock_db = AsyncMock()

    terminal = Terminal(id=1, device_id=209, org_id=1, sn="SN209", is_active=True)
    tsp = Tsp(tsp_id=10, tsp_code=7001, tsp_name="Тест ТСП")
    binding = TerminalMenuBinding(
        id=1,
        device_id=209,
        menu_variant_id=5,
        loaded_version=2,
        loaded_at=datetime.now(UTC),
    )
    snapshot = MenuVariantSnapshot(
        id=42,
        menu_variant_id=5,
        version=2,
        snapshot_data={"services_by_tsp": {"7001": {"name": "Снепшот Услуга"}}},
    )
    existing_balance = BalanceTerminalTsp(
        rec_id=99,
        int_day=20260903,
        org_id=1,
        terminal_id=1,
        tsp_id=10,
        menu_version=2,
        menu_snapshot_id=42,
        amount=3000,
        count=1,
    )

    async def mock_scalar(stmt, params=None):
        sql = str(stmt)
        if "FROM terminal_menu_bindings" in sql:
            return binding
        if "FROM menu_variant_snapshots" in sql:
            return snapshot
        if "FROM tsp_parameter_codes" in sql:
            return 100
        return None

    mock_db.scalar.side_effect = mock_scalar

    async def mock_execute(stmt, params=None):
        sql = str(stmt)
        res = MagicMock()
        if "FROM tsp" in sql:
            res.scalar_one_or_none.return_value = tsp
        elif "FROM balance_terminal_tsp" in sql:
            res.scalar_one_or_none.return_value = existing_balance
        else:
            res.scalar_one_or_none.return_value = None
        return res

    mock_db.execute.side_effect = mock_execute

    service = PaymentService(mock_db)
    payment = await service.create_payment(
        terminal=terminal,
        tsp_code=7001,
        amount=5000,
        paym_ext_id="0209_240826_10000002",
        params={101: "9991234567"},
    )

    assert payment.menu_version == 2
    assert payment.menu_snapshot_id == 42
    # Check existing balance record was updated
    assert existing_balance.amount == 8000
    assert existing_balance.count == 2
    # Ensure no NEW balance record was added to db.add
    added_objects = [call[0][0] for call in mock_db.add.call_args_list]
    new_balances = [b for b in added_objects if isinstance(b, BalanceTerminalTsp)]
    assert len(new_balances) == 0


@pytest.mark.anyio
async def test_payment_creation_auto_creates_snapshot_when_missing():
    """Verify that when MenuVariantSnapshot is missing, PaymentService auto-creates it."""
    mock_db = AsyncMock()

    terminal = Terminal(id=1, device_id=209, org_id=1, sn="SN209", is_active=True)
    tsp = Tsp(tsp_id=10, tsp_code=7001, tsp_name="Тест ТСП")
    binding = TerminalMenuBinding(
        id=1,
        device_id=209,
        menu_variant_id=5,
        loaded_version=3,
        loaded_at=datetime.now(UTC),
    )
    from app.models import Group, MenuVariant, ServiceMenu

    variant = MenuVariant(id=5, org_id=1, name="Основное меню", version=3)
    group = Group(id=1, menu_variant_id=5, org_id=1, number=1, name="Мойка")
    service_item = ServiceMenu(
        id=1,
        menu_variant_id=5,
        group_id=1,
        tsp_code=7001,
        name="Премиум Мойка v3",
        printname="Премиум v3",
        price=500,
        protypenumber=99001,
    )

    async def mock_get(model, ident):
        if model == MenuVariant and ident == 5:
            return variant
        return None

    mock_db.get.side_effect = mock_get

    async def mock_scalar(stmt, params=None):
        sql = str(stmt)
        if "FROM terminal_menu_bindings" in sql:
            return binding
        if "FROM menu_variant_snapshots" in sql:
            return None  # Snapshot doesn't exist yet!
        if "FROM tsp_parameter_codes" in sql:
            return 100
        return None

    mock_db.scalar.side_effect = mock_scalar

    async def mock_execute(stmt, params=None):
        sql = str(stmt)
        res = MagicMock()
        if "FROM tsp" in sql:
            res.scalar_one_or_none.return_value = tsp
        elif "FROM groups" in sql:
            res.scalars.return_value.all.return_value = [group]
        elif "FROM services" in sql:
            res.scalars.return_value.all.return_value = [service_item]
        elif "FROM balance_terminal_tsp" in sql:
            res.scalar_one_or_none.return_value = None
        else:
            res.scalar_one_or_none.return_value = None
        return res

    mock_db.execute.side_effect = mock_execute

    service = PaymentService(mock_db)
    payment = await service.create_payment(
        terminal=terminal,
        tsp_code=7001,
        amount=5000,
        paym_ext_id="0209_240826_10000003",
        params={101: "9991234567"},
    )

    assert payment.menu_version == 3

    # Check that a MenuVariantSnapshot was created and added
    added_objects = [call[0][0] for call in mock_db.add.call_args_list]
    snapshots = [s for s in added_objects if isinstance(s, MenuVariantSnapshot)]
    assert len(snapshots) == 1
    assert snapshots[0].version == 3
    assert snapshots[0].menu_variant_id == 5
    assert "7001" in snapshots[0].snapshot_data["services_by_tsp"]
    assert (
        snapshots[0].snapshot_data["services_by_tsp"]["7001"]["name"]
        == "Премиум Мойка v3"
    )
