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
    from app.services.gauge_bus import gauge_store

    gauge_store.clear()
    gauge_store.set_snapshot(
        {
            "device_id": 1001,
            "sn": "SN1001",
            "updated_at": datetime.now(UTC).isoformat(),
            "last_tick_epoch": int(datetime.now(UTC).timestamp() // 600),
            "slots_bitmask": 0x001,
            "gauge": {"102": "0", "109": "5000", "121": "0"},
        }
    )

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

    with patch("app.routers.monitoring.async_session", return_value=mock_cm):
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

    # Verify sub-queries used terminal_ids scoped to page items
    payment_query = next(q for q in executed_queries if "payments" in q[0])
    assert payment_query[1]["t_ids"] == [1]

    # GateGauge is no longer queried from PostgreSQL DB
    assert not any("gate_gauge_records" in q[0] for q in executed_queries)


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

    with patch("app.routers.monitoring.async_session", return_value=mock_cm):
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
    """Verify list_terminals applies pagination, org_id filtering, and does not filter out by license."""
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
        1,
        datetime(2026, 1, 1, tzinfo=UTC),
        1,
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

    # Verify query had WHERE clause with org_id and without licenses filter
    term_query = next(q for q in executed_queries if "FROM terminals t" in q[0])
    assert "EXISTS (SELECT 1 FROM licenses" not in term_query[0]
    assert term_query[1]["limit"] == 20
    assert term_query[1]["org_id"] == 1
    assert term_query[1]["search"] == "%1001%"


@pytest.mark.anyio
async def test_monitoring_includes_all_terminals_without_license_filter():
    """Verify monitoring includes expired terminals and provides last_inkass_at."""
    app.dependency_overrides[get_current_user] = lambda: {
        "username": "admin",
        "org_id": 1,
    }

    mock_session = AsyncMock()
    mock_session.scalar.return_value = 1

    executed_queries = []

    mock_terminal = (
        1,
        209,
        "SN209",
        1,
        True,
        "SER209",
        datetime(2027, 8, 21, 12, 0, tzinfo=UTC),
        "Main Street 209",
        "Test Terminal",
        1,
        "Тип 1",
    )

    async def mock_execute(stmt, params=None):
        sql_str = str(stmt)
        executed_queries.append((sql_str, params))
        result = MagicMock()
        result.scalar.return_value = 1
        if "FROM terminals t" in sql_str:
            result.fetchall.return_value = [mock_terminal]
        elif "FROM licenses" in sql_str:
            result.fetchall.return_value = [
                (1, datetime(2025, 1, 1, 0, 0, tzinfo=UTC))  # Expired license
            ]
        elif (
            "FROM tech_gate_records" in sql_str
            and "function_name = 'inkass'" in sql_str
        ):
            result.fetchall.return_value = [
                (209, datetime(2026, 8, 21, 8, 8, 40, tzinfo=UTC))
            ]
        elif "FROM payments" in sql_str or "FROM gate_gauge_records" in sql_str:
            result.fetchall.return_value = []
        else:
            result.fetchall.return_value = []
        return result

    mock_session.execute = AsyncMock(side_effect=mock_execute)
    mock_cm = AsyncMock()
    mock_cm.__aenter__.return_value = mock_session
    mock_cm.__aexit__.return_value = None

    with patch("app.routers.monitoring.async_session", return_value=mock_cm):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.get("/api/monitoring")
            assert resp.status_code == 200
            data = resp.json()
            assert data["total"] == 1
            assert len(data["items"]) == 1
            item = data["items"][0]
            assert item["device_id"] == 209
            assert item["license_expires_at"] == "2025-01-01T00:00:00+00:00"
            assert item["last_inkass_at"] == "2026-08-21T08:08:40+00:00"
            assert item["cert_not_valid_after"] == "2027-08-21T12:00:00+00:00"

    term_query = next(q for q in executed_queries if "FROM terminals t" in q[0])
    # The WHERE clause for monitoring terminals should NOT filter out expired licenses
    assert "EXISTS (SELECT 1 FROM licenses" not in term_query[0]


@pytest.mark.anyio
async def test_inkass_report_example_calculation_and_fields():
    """Verify inkassation report data structure and payment-interval calculated sum."""
    app.dependency_overrides[get_current_user] = lambda: {
        "username": "admin",
        "org_id": 1,
    }

    mock_session = AsyncMock()
    mock_session.scalar.return_value = 1  # count

    req_data = {
        "InkassDateTime": "21.08.2026 8:08:40",
        "TotalSum": "62500",
        "TotalNoteCount": "35",
        "InkassId": "59",
        "TransactCount": "109",
        "InkassExtId": "0209_210826_08073950",
        "PaymExtId": "0209_200826_17463404",
        "Note3": "3",  # 50 rub
        "Note4": "26",  # 100 rub
        "Note5": "5",  # 500 rub
        "Note6": "1",  # 1000 rub
    }

    # Row tuple: (id, device_id, sn, created_at, request_data, org_id, terminal_id)
    inkass_row = (
        100,
        209,
        "SN209",
        datetime(2026, 8, 21, 8, 8, 40, tzinfo=UTC),
        req_data,
        1,
        10,
    )

    prev_req_data = {
        "PaymExtId": "0209_190826_12000000",
    }

    async def mock_execute(stmt, params=None):
        sql_str = str(stmt)
        result = MagicMock()
        result.scalar.return_value = 1
        if "FROM tech_gate_records r" in sql_str:
            result.fetchall.return_value = [inkass_row]
        elif "WHERE device_id = :device_id AND function_name = 'inkass'" in sql_str:
            result.fetchone.return_value = (
                prev_req_data,
                datetime(2026, 8, 19, 12, 0, 0, tzinfo=UTC),
            )
        elif "FROM payments" in sql_str and "ORDER BY paym_id DESC" in sql_str:
            if (params or {}).get("ext_id") == "0209_200826_17463404":
                result.fetchone.return_value = (
                    200,
                    datetime(2026, 8, 20, 17, 46, 34, tzinfo=UTC),
                )
            else:
                result.fetchone.return_value = (
                    150,
                    datetime(2026, 8, 19, 12, 0, 0, tzinfo=UTC),
                )
        elif "COALESCE(SUM(paym_amount)" in sql_str:
            result.scalar.return_value = 6250000  # 62500 rubles in kopecks
        return result

    async def mock_scalar(stmt, params=None):
        sql_str = str(stmt)
        if "SELECT COALESCE(SUM(paym_amount), 0) FROM payments" in sql_str:
            return 6250000  # 62500 rubles in kopecks
        return 1

    mock_session.execute = AsyncMock(side_effect=mock_execute)
    mock_session.scalar = AsyncMock(side_effect=mock_scalar)

    mock_cm = AsyncMock()
    mock_cm.__aenter__.return_value = mock_session
    mock_cm.__aexit__.return_value = None

    with patch("app.routers.reports.async_session", return_value=mock_cm):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.get("/api/reports/inkass")
            assert resp.status_code == 200
            data = resp.json()
            assert data["total"] == 1
            item = data["items"][0]
            assert item["device_id"] == 209
            assert item["total_sum"] == 62500
            assert item["calculated_sum"] == 62500
            assert item["total_note_count"] == 35
            assert item["transact_count"] == 109
            assert item["report_number"] == "59"
            assert item["inkass_ext_id"] == "0209_210826_08073950"
            assert item["paym_ext_id"] == "0209_200826_17463404"
            assert item["banknotes"] == {
                "n10": 0,
                "n50": 3,
                "n100": 26,
                "n200": 0,
                "n500": 5,
                "n1000": 1,
                "n2000": 0,
                "n5000": 0,
            }


@pytest.mark.anyio
async def test_payments_report_tsp_name_resolution():
    """Verify payments report returns resolved tsp_name from terminal bindings / org menu."""
    app.dependency_overrides[get_current_user] = lambda: {
        "username": "admin",
        "org_id": 1,
    }

    mock_session = AsyncMock()

    # (paym_id, paym_datetime, paym_amount, paym_ext_id, paym_tsp_code, paym_state, pay_type_id, device_id, sn, menu_snapshot_id)
    paym_row1 = (
        1001,
        datetime(2026, 8, 21, 10, 0, 0, tzinfo=UTC),
        50000,
        "0209_210826_10000001",
        7001,
        1,
        1,
        209,
        "SN209",
        None,
    )
    paym_row2 = (
        1002,
        datetime(2026, 8, 21, 10, 5, 0, tzinfo=UTC),
        10000,
        "0209_210826_10050001",
        9999,
        1,
        1,
        209,
        "SN209",
        None,
    )

    captured_params = []

    async def mock_execute(stmt, params=None):
        if params:
            captured_params.append(dict(params))
        sql_str = str(stmt)
        result = MagicMock()
        result.scalar.return_value = 2
        if "COUNT(*)" in sql_str:
            result.scalar.return_value = 2
        elif "FROM payments p" in sql_str:
            result.fetchall.return_value = [paym_row1, paym_row2]
        elif "FROM terminal_menu_bindings" in sql_str:
            result.fetchall.return_value = [(209, 7001, "Мобильная связь")]
        elif "FROM services s" in sql_str:
            result.fetchall.return_value = []
        elif "FROM tsp" in sql_str:
            result.fetchall.return_value = [(9999, "Неизвестный провайдер")]
        elif "FROM payment_params" in sql_str:
            result.fetchall.return_value = []
        else:
            result.fetchall.return_value = []
        return result

    mock_session.execute = AsyncMock(side_effect=mock_execute)
    mock_session.scalar = AsyncMock(return_value=2)

    mock_cm = AsyncMock()
    mock_cm.__aenter__.return_value = mock_session
    mock_cm.__aexit__.return_value = None

    with patch("app.routers.reports.async_session", return_value=mock_cm):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.get(
                "/api/reports/payments?date_from=2026-08-01&date_to=2026-08-31"
            )
            assert resp.status_code == 200
            data = resp.json()
            assert data["total"] == 2
            items = data["items"]
            assert items[0]["paym_id"] == 1001
            assert items[0]["paym_ext_id"] == "0209_210826_10000001"
            assert items[0]["tsp_name"] == "Мобильная связь"
            assert items[1]["paym_id"] == 1002
            assert items[1]["tsp_name"] == "Неизвестный провайдер"
            assert len(captured_params) > 0
            assert captured_params[0]["dt_from"] == datetime(
                2026, 7, 31, 21, 0, tzinfo=UTC
            )
            assert captured_params[0]["dt_to"] == datetime(
                2026, 8, 31, 21, 0, tzinfo=UTC
            )


@pytest.mark.anyio
async def test_balance_by_terminal_report():
    """Verify balance by terminal report."""
    app.dependency_overrides[get_current_user] = lambda: {
        "username": "admin",
        "org_id": 1,
    }

    mock_session = AsyncMock()

    # (device_id, sn, terminal_id, tsp_count, total_count, total_amount)
    row1 = (209, "SN209", 1, 3, 10, 50000)

    async def mock_execute(stmt, params=None):
        result = MagicMock()
        result.fetchall.return_value = [row1]
        return result

    mock_session.execute = AsyncMock(side_effect=mock_execute)

    mock_cm = AsyncMock()
    mock_cm.__aenter__.return_value = mock_session
    mock_cm.__aexit__.return_value = None

    with patch("app.routers.reports.async_session", return_value=mock_cm):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.get(
                "/api/reports/balance-by-terminal?date_from=2026-08-31&date_to=2026-08-31"
            )
            assert resp.status_code == 200
            data = resp.json()
            assert len(data["items"]) == 1
            assert data["items"][0]["device_id"] == 209
            assert data["items"][0]["total_amount"] == 50000


@pytest.mark.anyio
async def test_balance_by_tsp_report():
    """Verify balance by TSP report."""
    app.dependency_overrides[get_current_user] = lambda: {
        "username": "admin",
        "org_id": 1,
    }

    mock_session = AsyncMock()

    # (tsp_code, tsp_name, menu_snapshot_id, terminal_id, count, amount)
    row1 = (7001, "Мобильная связь", None, 1, 5, 25000)

    async def mock_execute(stmt, params=None):
        sql_str = str(stmt)
        result = MagicMock()
        if "FROM balance_terminal_tsp" in sql_str:
            result.fetchall.return_value = [row1]
        elif "FROM services s" in sql_str:
            result.fetchall.return_value = [(7001, "Мобильная связь")]
        else:
            result.fetchall.return_value = []
        return result

    mock_session.execute = AsyncMock(side_effect=mock_execute)

    mock_cm = AsyncMock()
    mock_cm.__aenter__.return_value = mock_session
    mock_cm.__aexit__.return_value = None

    with patch("app.routers.reports.async_session", return_value=mock_cm):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.get(
                "/api/reports/balance-by-tsp?date_from=2026-08-31&date_to=2026-08-31"
            )
            assert resp.status_code == 200
            data = resp.json()
            assert len(data["items"]) == 1
            assert data["items"][0]["tsp_code"] == 7001
            assert data["items"][0]["total_amount"] == 25000
