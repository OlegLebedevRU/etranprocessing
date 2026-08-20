from datetime import UTC, datetime
from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from httpx import ASGITransport, AsyncClient

from app.auth import get_current_user
from app.database import get_db
from app.main import app


@pytest.fixture(autouse=True)
def cleanup_overrides():
    yield
    app.dependency_overrides.clear()


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.anyio
async def test_monitoring_pagination_and_query_scoping():
    """Verify get_monitoring applies pagination, org_id filtering, and queries sub-records only for page items."""
    app.dependency_overrides[get_current_user] = lambda: {
        "username": "admin",
        "org_id": 1,
    }

    mock_session = AsyncMock()

    # Terminal rows: id, device_id, sn, org_id, is_active, cert_serial, cert_not_valid_after, address, note, terminal_type_id, tt.name, created_at
    mock_terminal_row = (
        1,
        1001,
        "SN1001",
        1,
        True,
        "CERT123",
        datetime(2027, 1, 1, tzinfo=UTC),
        "Test Address",
        "Test Note",
        0,
        "Стандартный",
        datetime(2026, 1, 1, tzinfo=UTC),
    )

    executed_queries = []

    async def mock_execute(stmt, params=None):
        sql_str = str(stmt)
        executed_queries.append((sql_str, params))
        result = MagicMock()
        if "payments" in sql_str:
            result.fetchall.return_value = [
                (1, datetime(2026, 8, 20, 12, 0, tzinfo=UTC))
            ]
        elif "DISTINCT ON (terminal_id) terminal_id, expires_at" in sql_str:
            result.fetchall.return_value = [(1, datetime(2026, 9, 1, 0, 0, tzinfo=UTC))]
        elif "DISTINCT ON (device_id) device_id, gauge_data" in sql_str:
            result.fetchall.return_value = [
                (1001, {"102": "0", "109": "5000", "121": "0"})
            ]
        elif "gate_gauge_records" in sql_str:
            result.fetchall.return_value = [(1001, datetime.now(UTC))]
        elif "FROM terminals t" in sql_str:
            result.fetchall.return_value = [mock_terminal_row]
        else:
            result.fetchall.return_value = []
        return result

    mock_session.scalar.return_value = 42
    mock_session.execute = AsyncMock(side_effect=mock_execute)

    mock_cm = AsyncMock()
    mock_cm.__aenter__.return_value = mock_session
    mock_cm.__aexit__.return_value = None

    with patch("app.main.async_session", return_value=mock_cm):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.get("/api/monitoring?page=2&page_size=10&search=1001")
            assert resp.status_code == 200
            data = resp.json()
            assert data["total"] == 42
            assert data["page"] == 2
            assert data["page_size"] == 10
            assert len(data["items"]) == 1
            item = data["items"][0]
            assert item["terminal_id"] == 1
            assert item["device_id"] == 1001
            assert item["cash_amount"] == 5000
            assert item["license_expires_at"] is not None

    # Verify query params for terminals table
    term_query = next(q for q in executed_queries if "FROM terminals t" in q[0])
    assert term_query[1]["limit"] == 10
    assert term_query[1]["offset"] == 10
    assert term_query[1]["org_id"] == 1
    assert term_query[1]["search"] == "%1001%"

    # Verify sub-queries used terminal_ids and device_ids scoped to page items
    payment_query = next(q for q in executed_queries if "payments" in q[0])
    assert payment_query[1]["t_ids"] == [1]

    gauge_query = next(q for q in executed_queries if "DISTINCT ON (device_id)" in q[0])
    assert gauge_query[1]["d_ids"] == [1001]


@pytest.mark.anyio
async def test_monitoring_empty_page():
    """Verify empty page returns items=[] and skips subqueries."""
    app.dependency_overrides[get_current_user] = lambda: {
        "username": "admin",
        "org_id": 1,
    }

    mock_session = AsyncMock()
    mock_session.scalar.return_value = 0

    executed_queries = []

    async def mock_execute(stmt, params=None):
        sql_str = str(stmt)
        executed_queries.append((sql_str, params))
        result = MagicMock()
        result.fetchall.return_value = []
        return result

    mock_session.execute = AsyncMock(side_effect=mock_execute)

    mock_cm = AsyncMock()
    mock_cm.__aenter__.return_value = mock_session
    mock_cm.__aexit__.return_value = None

    with patch("app.main.async_session", return_value=mock_cm):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.get("/api/monitoring?page=1&page_size=20")
            assert resp.status_code == 200
            data = resp.json()
            assert data["total"] == 0
            assert data["page"] == 1
            assert data["items"] == []

    # Subqueries for payments or gauge records should NOT be executed when page is empty
    assert not any("payments" in q[0] for q in executed_queries)
    assert not any("gate_gauge_records" in q[0] for q in executed_queries)


@pytest.mark.anyio
async def test_terminal_bindings_list_terminals():
    """Verify list_terminals applies pagination, org_id filtering, and active license filter."""
    app.dependency_overrides[get_current_user] = lambda: {
        "username": "admin",
        "org_id": 1,
    }

    mock_db = AsyncMock()
    mock_db.scalar.return_value = 15

    mock_row = (
        1,
        1001,
        "SN1001",
        1,
        True,
        5,
        10,
        "Default Menu",
        "Main St 1",
        "Note",
        0,
        "Стандартный",
        datetime(2026, 1, 1, tzinfo=UTC),
    )

    executed_queries = []

    async def mock_execute(stmt, params=None):
        sql_str = str(stmt)
        executed_queries.append((sql_str, params))
        result = MagicMock()
        result.fetchall.return_value = [mock_row]
        return result

    mock_db.execute = AsyncMock(side_effect=mock_execute)
    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        resp = await client.get("/api/terminals?page=1&page_size=20&search=1001")
        assert resp.status_code == 200
        data = resp.json()
        assert data["total"] == 15
        assert data["page"] == 1
        assert data["page_size"] == 20
        assert len(data["items"]) == 1
        item = data["items"][0]
        assert item["terminal_id"] == 1
        assert item["device_id"] == 1001
        assert item["menu_variant_name"] == "Default Menu"

    # Verify query had WHERE clause with licenses filter and org_id
    term_query = next(q for q in executed_queries if "FROM terminals t" in q[0])
    assert "l.renewal_enabled = true" in term_query[0]
    assert term_query[1]["limit"] == 20
    assert term_query[1]["org_id"] == 1
    assert term_query[1]["search"] == "%1001%"
