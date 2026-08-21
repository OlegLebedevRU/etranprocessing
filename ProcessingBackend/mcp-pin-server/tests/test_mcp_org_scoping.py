from unittest.mock import AsyncMock

import pytest

from pin_server.reports import (
    report_balance_by_terminal,
    report_balance_by_tsp,
    report_inkass,
    report_payments,
)
from pin_server.server import _resolve_terminal


@pytest.mark.asyncio
async def test_resolve_terminal_org_scoping():
    mock_db = AsyncMock()

    # Terminal belongs to org 1
    t1 = {"id": 1, "device_id": 1001, "sn": "SN1001", "org_id": 1}

    async def mock_fetchrow(query, *args):
        if len(args) == 2 and args[1] == 1:
            return t1
        if len(args) == 2 and args[1] != 1:
            return None
        if len(args) == 1:
            return t1
        return None

    mock_db.fetchrow = AsyncMock(side_effect=mock_fetchrow)

    # Scoped to org 1 -> success
    term = await _resolve_terminal(mock_db, "1001", org_id=1)
    assert term["org_id"] == 1

    # Scoped to org 2 -> not found
    with pytest.raises(ValueError, match="Terminal not found"):
        await _resolve_terminal(mock_db, "1001", org_id=2)


@pytest.mark.asyncio
async def test_report_payments_org_scoping():
    mock_db = AsyncMock()
    mock_db.fetch = AsyncMock(return_value=[])

    await report_payments(mock_db, org_id=5)

    # Verify SQL query had t.org_id condition and param 5
    call_args = mock_db.fetch.call_args[0]
    query = call_args[0]
    params = call_args[1:]
    assert "t.org_id = $1" in query
    assert 5 in params


@pytest.mark.asyncio
async def test_report_balance_org_scoping():
    mock_db = AsyncMock()
    mock_db.fetch = AsyncMock(return_value=[])

    await report_balance_by_terminal(mock_db, org_id=3)
    call_args = mock_db.fetch.call_args[0]
    query = call_args[0]
    params = call_args[1:]
    assert "b.org_id = $1" in query
    assert 3 in params

    await report_balance_by_tsp(mock_db, org_id=4)
    call_args = mock_db.fetch.call_args[0]
    query = call_args[0]
    params = call_args[1:]
    assert "b.org_id = $1" in query
    assert 4 in params


@pytest.mark.asyncio
async def test_report_inkass_org_scoping():
    mock_db = AsyncMock()
    mock_db.fetch = AsyncMock(return_value=[])

    await report_inkass(mock_db, org_id=2)
    call_args = mock_db.fetch.call_args[0]
    query = call_args[0]
    params = call_args[1:]
    assert "t.org_id = $1" in query
    assert 2 in params
