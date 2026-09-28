from datetime import UTC, datetime
from unittest.mock import AsyncMock

import pytest

from l4mcp.access import Principal
from l4mcp.console_tools import _preflight
from l4mcp.reports import (
    report_balance_by_terminal,
    report_balance_by_tsp,
    report_inkass,
    report_payments,
)
from l4mcp.server import _report_scope_required


@pytest.mark.asyncio
async def test_console_requires_exact_device_id_before_network_request():
    principal = Principal("Bearer test", 1, "user", 3, 3, "session")
    assert (await _preflight(principal, None))["reason"] == "device_id_required"


def test_reports_require_bounded_user_scope():
    assert _report_scope_required(None, None, None)["status"] == "filters_required"
    assert (
        _report_scope_required("2026-09-01", None, None)["status"] == "filters_required"
    )
    assert _report_scope_required(None, None, "773") is None
    with pytest.raises(ValueError, match="0–30 days"):
        _report_scope_required("2026-01-01", "2026-09-01", None)


@pytest.mark.asyncio
async def test_report_payments_org_scoping():
    mock_db = AsyncMock()
    mock_db.fetch = AsyncMock(return_value=[])
    mock_db.fetchval = AsyncMock(return_value=None)

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
    mock_db.fetchval = AsyncMock(return_value=None)

    await report_inkass(mock_db, org_id=2)
    call_args = mock_db.fetch.call_args[0]
    query = call_args[0]
    params = call_args[1:]
    assert "t.org_id = $1" in query
    assert 2 in params


@pytest.mark.asyncio
async def test_report_payments_uses_org_timezone_for_date_range_and_iso_z_output():
    """MCP must apply the same tenant-local date -> UTC contract as the HTTP API:
    - date_from/date_to are resolved via Org.timezone (not compared as raw strings)
    - paym_datetime is serialized as UTC ISO-8601 with 'Z', matching MenuBuilder HTTP.
    """
    mock_db = AsyncMock()
    mock_db.fetchval = AsyncMock(return_value="Asia/Yekaterinburg")

    paym_dt_utc = datetime(2026, 8, 31, 21, 30, 0, tzinfo=UTC)
    mock_db.fetch = AsyncMock(
        side_effect=[
            [
                {
                    "paym_id": 1001,
                    "paym_datetime": paym_dt_utc,
                    "paym_amount": 35000,
                    "paym_ext_id": "0348_010926_02300001",
                    "paym_tsp_code": 7001,
                    "paym_state": 2,
                    "pay_type_id": 1,
                    "device_id": 348,
                    "sn": "SN348",
                }
            ],
            [],
        ]
    )

    result = await report_payments(
        mock_db, date_from="2026-09-01", date_to="2026-09-01", org_id=424
    )

    # Org timezone must have been resolved (not defaulted/ignored).
    mock_db.fetchval.assert_awaited_once()

    # Date range must be converted from tenant-local calendar dates to UTC bounds,
    # never compared as raw date strings against a TIMESTAMPTZ column.
    query, org_id_param, dt_from_param, dt_to_param, *_rest = (
        mock_db.fetch.call_args_list[0][0]
    )
    assert "p.paym_datetime >= $" in query
    assert "p.paym_datetime < $" in query
    assert org_id_param == 424
    assert isinstance(dt_from_param, datetime)
    assert isinstance(dt_to_param, datetime)

    assert result["items"][0]["paym_datetime"] == "2026-08-31T21:30:00Z"
