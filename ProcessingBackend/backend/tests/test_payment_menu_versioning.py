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
    assert payment.paym_amount == 5000
    assert payment.paym_tsp_code == 7001

    # Check added balance record has menu_snapshot_id == 42
    added_objects = [call[0][0] for call in mock_db.add.call_args_list]
    balance_records = [
        obj for obj in added_objects if isinstance(obj, BalanceTerminalTsp)
    ]
    assert len(balance_records) == 1
    assert balance_records[0].menu_snapshot_id == 42
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
    added_objects = [call[0][0] for call in mock_db.add.call_args_list]
    balance_records = [
        obj for obj in added_objects if isinstance(obj, BalanceTerminalTsp)
    ]
    assert len(balance_records) == 1
    assert balance_records[0].menu_snapshot_id is None
