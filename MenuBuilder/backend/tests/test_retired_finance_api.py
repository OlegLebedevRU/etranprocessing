"""Retired finance routes cannot mutate the frozen balance model."""

from unittest.mock import AsyncMock

import pytest
from httpx import ASGITransport, AsyncClient

from app.auth import get_current_user
from app.database import get_db
from app.main import app


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.anyio
@pytest.mark.parametrize(
    "method,path,expected",
    [
        ("GET", "/api/v1/finance/balance", 410),
        ("GET", "/api/v1/finance/transactions", 410),
        ("POST", "/api/v1/finance/payments", 410),
        ("POST", "/api/internal/v1/finance/post", 404),
        ("POST", "/api/internal/v1/finance/metering/close", 404),
        ("POST", "/api/internal/v1/finance/reconciliation", 404),
    ],
)
async def test_retired_finance_routes_never_touch_database(method, path, expected):
    db = AsyncMock()
    previous = app.dependency_overrides.copy()
    app.dependency_overrides[get_current_user] = lambda: {
        "org_id": 1,
        "is_superuser": True,
    }
    app.dependency_overrides[get_db] = lambda: db
    try:
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            response = await client.request(method, path, json={})
        assert response.status_code == expected
        db.execute.assert_not_awaited()
        db.commit.assert_not_awaited()
        db.add.assert_not_called()
    finally:
        app.dependency_overrides.clear()
        app.dependency_overrides.update(previous)
