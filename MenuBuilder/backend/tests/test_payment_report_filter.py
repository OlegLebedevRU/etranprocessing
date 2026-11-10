from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from httpx import ASGITransport, AsyncClient

from app.auth import create_access_token
from app.main import app


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.anyio
@pytest.mark.parametrize("payment_type", [None, "all", "cash", "card"])
async def test_payment_filter_scopes_both_count_and_rows(payment_type):
    session = AsyncMock()
    timezone = MagicMock()
    timezone.scalar_one_or_none.return_value = "Europe/Moscow"
    count = MagicMock()
    count.scalar.return_value = 0
    rows = MagicMock()
    rows.fetchall.return_value = []
    session.execute.side_effect = [timezone, count, rows]
    context = AsyncMock()
    context.__aenter__.return_value = session
    token = create_access_token(
        {"sub": "user475", "org_id": 475, "role": "user", "token_type": "tenant"}
    )
    with patch("app.routers.reports.async_session", return_value=context):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            response = await client.get(
                "/api/reports/payments",
                params={"payment_type": payment_type} if payment_type else {},
                headers={"Authorization": f"Bearer {token}"},
            )
    assert response.status_code == 200
    for call in session.execute.await_args_list[1:]:
        sql, params = str(call.args[0]), call.args[1]
        assert "p.org_id = :org_id" in sql
        assert params["org_id"] == 475
        if payment_type == "cash":
            assert "p.pay_type_id IN (0, 1)" in sql
        elif payment_type == "card":
            assert "p.pay_type_id = :pay_type_id" in sql
            assert params["pay_type_id"] == 2
        else:
            assert "p.pay_type_id =" not in sql and "p.pay_type_id IN" not in sql


@pytest.mark.anyio
async def test_invalid_payment_type_rejected():
    token = create_access_token(
        {"sub": "user475", "org_id": 475, "role": "user", "token_type": "tenant"}
    )
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        response = await client.get(
            "/api/reports/payments?payment_type=unknown",
            headers={"Authorization": f"Bearer {token}"},
        )
    assert response.status_code == 422
