"""Exercise the actual Classic dependency and XML renderer at fixed time.

The finite matrix covers each license/admin branch without mocking its result.
No L4Desk profile or subscription query may enter this legacy contract.
"""

import xml.etree.ElementTree as ET
from datetime import UTC, datetime, timedelta
from unittest.mock import AsyncMock, MagicMock

import pytest
from httpx import ASGITransport, AsyncClient

from app import dependencies
from app.database import get_db
from app.dependencies import get_current_terminal
from app.main import app
from app.models import License, OrgStatus, Terminal

AT = datetime(2026, 10, 5, 12, tzinfo=UTC)


class FixedDatetime(datetime):
    @classmethod
    def now(cls, tz=None):
        return AT.astimezone(tz) if tz is not None else AT.replace(tzinfo=None)


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.anyio
@pytest.mark.parametrize("active", [False, True])
@pytest.mark.parametrize("org_status", [None, "active", "inactive", "blocked"])
@pytest.mark.parametrize("period", ["missing", "past", "exact", "future"])
async def test_classic_state_and_xml_matrix(monkeypatch, active, org_status, period):
    monkeypatch.setattr(dependencies, "datetime", FixedDatetime)
    terminal = Terminal(
        id=1, device_id=773, sn="matrix-terminal", org_id=1, is_active=active
    )
    license_ = None
    if period != "missing":
        delta = {"past": -1, "exact": 0, "future": 1}[period]
        license_ = License(
            id=1,
            terminal_id=1,
            org_id=1,
            expires_at=AT + timedelta(microseconds=delta),
            balance=97685,
        )
    org = OrgStatus(org_id=1, status=org_status) if org_status is not None else None
    db = AsyncMock()
    db.execute.side_effect = [
        MagicMock(scalar_one_or_none=MagicMock(return_value=org)),
        MagicMock(scalar_one_or_none=MagicMock(return_value=license_)),
    ]
    previous = app.dependency_overrides.copy()
    app.dependency_overrides[get_current_terminal] = lambda: terminal
    app.dependency_overrides[get_db] = lambda: db
    try:
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            response = await client.get("/api/licensebilling")
    finally:
        app.dependency_overrides.clear()
        app.dependency_overrides.update(previous)

    expected = (
        "ok"
        if active and org_status != "blocked" and period in ("exact", "future")
        else "error"
    )
    assert response.status_code == 200
    assert "application/xml" in response.headers["content-type"]
    root = ET.fromstring(response.text)
    assert root.findtext("Result") == "OK"
    assert root.findtext("state") == expected
    assert root.findtext("balance") == ("97685" if license_ is not None else "0")
    assert db.execute.await_count == 2
    assert [
        call.args[0].column_descriptions[0]["entity"]
        for call in db.execute.await_args_list
    ] == [
        OrgStatus,
        License,
    ]
