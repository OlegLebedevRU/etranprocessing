from datetime import UTC, datetime
from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from httpx import ASGITransport, AsyncClient

from app.auth import create_access_token
from app.main import app


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.anyio
class TestReportsSqlGrouping:
    """Test SQL aggregation and version snapshot resolution in reports."""

    async def test_balance_by_tsp_sql_grouping_with_multiple_versions(self):
        token = create_access_token(
            {"sub": "user10", "org_id": 10, "role": "user", "token_type": "tenant"}
        )
        headers = {"Authorization": f"Bearer {token}"}

        mock_session = AsyncMock()

        # 1. Org timezone query -> "Asia/Yekaterinburg"
        tz_result = MagicMock()
        tz_result.scalar_one_or_none.return_value = "Asia/Yekaterinburg"

        # 2. SQL aggregated balance rows:
        # Columns: (tsp_code, tsp_name, menu_version, menu_snapshot_id, terminal_count, total_count, total_amount)
        balance_rows = [
            (7001, "Глобальное имя", 1, 101, 3, 15, 150000),
            (7001, "Глобальное имя", 2, 102, 2, 8, 120000),
            (7002, "Другая услуга", 1, 101, 1, 4, 40000),
        ]
        balance_result = MagicMock()
        balance_result.fetchall.return_value = balance_rows

        # 3. Snapshot query
        snapshot_rows = [
            (
                101,
                {
                    "version": 1,
                    "services_by_tsp": {
                        "7001": {"name": "Экспресс-мойка v1"},
                        "7002": {"name": "Пылесос"},
                    },
                },
            ),
            (
                102,
                {
                    "version": 2,
                    "services_by_tsp": {
                        "7001": {"name": "Мойка Люкс v2"},
                    },
                },
            ),
        ]
        snapshot_result = MagicMock()
        snapshot_result.fetchall.return_value = snapshot_rows

        empty_result = MagicMock()
        empty_result.fetchall.return_value = []

        async def mock_execute(stmt, params=None):
            sql_str = str(stmt)
            if "FROM orgs" in sql_str:
                return tz_result
            if "FROM balance_terminal_tsp" in sql_str:
                return balance_result
            if "FROM menu_variant_snapshots" in sql_str:
                return snapshot_result
            return empty_result

        mock_session.execute = AsyncMock(side_effect=mock_execute)

        mock_cm = AsyncMock()
        mock_cm.__aenter__.return_value = mock_session
        mock_cm.__aexit__.return_value = None

        with patch("app.routers.reports.async_session", return_value=mock_cm):
            async with AsyncClient(
                transport=ASGITransport(app=app), base_url="http://test"
            ) as client:
                resp = await client.get(
                    "/api/reports/balance-by-tsp?date_from=2026-09-01&date_to=2026-09-03",
                    headers=headers,
                )
                assert resp.status_code == 200
                data = resp.json()
                assert data["timezone"] == "Asia/Yekaterinburg"
                items = data["items"]
                assert len(items) == 3

                # First item: 7001 v1
                assert items[0]["tsp_code"] == 7001
                assert items[0]["version"] == 1
                assert items[0]["tsp_name"] == "Экспресс-мойка v1"
                assert items[0]["terminal_count"] == 3
                assert items[0]["total_count"] == 15
                assert items[0]["total_amount"] == 150000

                # Second item: 7001 v2
                assert items[1]["tsp_code"] == 7001
                assert items[1]["version"] == 2
                assert items[1]["tsp_name"] == "Мойка Люкс v2"
                assert items[1]["terminal_count"] == 2
                assert items[1]["total_count"] == 8
                assert items[1]["total_amount"] == 120000

                # Third item: 7002 v1
                assert items[2]["tsp_code"] == 7002
                assert items[2]["version"] == 1
                assert items[2]["tsp_name"] == "Пылесос"
                assert items[2]["terminal_count"] == 1
                assert items[2]["total_count"] == 4
                assert items[2]["total_amount"] == 40000

    async def test_payments_report_includes_menu_version_and_snapshot_name(self):
        token = create_access_token(
            {"sub": "user10", "org_id": 10, "role": "user", "token_type": "tenant"}
        )
        headers = {"Authorization": f"Bearer {token}"}

        mock_session = AsyncMock()

        tz_result = MagicMock()
        tz_result.scalar_one_or_none.return_value = "Europe/Moscow"

        count_result = MagicMock()
        count_result.scalar.return_value = 1

        paym_dt_utc = datetime(2026, 9, 3, 12, 0, 0, tzinfo=UTC)
        rows_result = MagicMock()
        rows_result.fetchall.return_value = [
            (
                1001,  # paym_id
                paym_dt_utc,  # paym_datetime
                50000,  # amount
                "0010_030926_12000001",  # ext_id
                7001,  # tsp_code
                2,  # state
                1,  # pay_type_id
                10,  # device_id
                "SN010",  # sn
                102,  # snapshot_id
                2,  # menu_version
            )
        ]

        snapshot_rows = [
            (
                102,
                {
                    "version": 2,
                    "services_by_tsp": {
                        "7001": {"name": "Мойка Люкс v2"},
                    },
                },
            ),
        ]
        snapshot_result = MagicMock()
        snapshot_result.fetchall.return_value = snapshot_rows

        empty_result = MagicMock()
        empty_result.fetchall.return_value = []

        async def mock_execute(stmt, params=None):
            sql_str = str(stmt)
            if "FROM orgs" in sql_str:
                return tz_result
            if "SELECT count(*)" in sql_str:
                return count_result
            if "FROM payments p" in sql_str:
                return rows_result
            if "FROM menu_variant_snapshots" in sql_str:
                return snapshot_result
            return empty_result

        mock_session.execute = AsyncMock(side_effect=mock_execute)

        mock_cm = AsyncMock()
        mock_cm.__aenter__.return_value = mock_session
        mock_cm.__aexit__.return_value = None

        with patch("app.routers.reports.async_session", return_value=mock_cm):
            async with AsyncClient(
                transport=ASGITransport(app=app), base_url="http://test"
            ) as client:
                resp = await client.get(
                    "/api/reports/payments?date_from=2026-09-03&date_to=2026-09-03",
                    headers=headers,
                )
                assert resp.status_code == 200
                data = resp.json()
                assert len(data["items"]) == 1
                item = data["items"][0]
                assert item["paym_id"] == 1001
                assert item["paym_tsp_code"] == 7001
                assert item["menu_version"] == 2
                assert item["tsp_name"] == "Мойка Люкс v2"
