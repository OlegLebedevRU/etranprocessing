"""Unit and integration tests for timezone support in MenuBuilder backend."""

from datetime import UTC, datetime, timedelta
from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from httpx import ASGITransport, AsyncClient

from app.auth import create_access_token
from app.database import get_db
from app.main import app
from app.models import Org
from app.user_store import get_user_store
from app.utils.timezone import (
    get_date_range_bounds_utc,
    get_local_datetime,
    get_local_int_day,
    resolve_tz,
)


@pytest.fixture(autouse=True)
def reset_dependency_overrides():
    app.dependency_overrides.clear()
    yield
    app.dependency_overrides.clear()


@pytest.fixture
def anyio_backend():
    return "asyncio"


class TestTimezoneUtils:
    """Test utility functions in app.utils.timezone."""

    def test_resolve_tz_valid_and_fallbacks(self):
        ref_dt = datetime(2026, 9, 1, 12, 0, 0, tzinfo=UTC)

        tz_msk = resolve_tz("Europe/Moscow")
        assert ref_dt.astimezone(tz_msk).utcoffset() == timedelta(hours=3)

        tz_ykt = resolve_tz("Asia/Yekaterinburg")
        assert ref_dt.astimezone(tz_ykt).utcoffset() == timedelta(hours=5)

        tz_vlad = resolve_tz("Asia/Vladivostok")
        assert ref_dt.astimezone(tz_vlad).utcoffset() == timedelta(hours=10)

        # Fallbacks to Europe/Moscow
        assert ref_dt.astimezone(resolve_tz(None)).utcoffset() == timedelta(hours=3)
        assert ref_dt.astimezone(resolve_tz("")).utcoffset() == timedelta(hours=3)
        assert ref_dt.astimezone(
            resolve_tz("Nonexistent/Zone")
        ).utcoffset() == timedelta(hours=3)

    def test_get_local_datetime(self):
        # 2026-08-31 22:00:00 UTC
        utc_dt = datetime(2026, 8, 31, 22, 0, 0, tzinfo=UTC)

        # In Yekaterinburg (UTC+5), this is 2026-09-01 03:00:00
        local_dt = get_local_datetime(utc_dt, "Asia/Yekaterinburg")
        assert local_dt.year == 2026
        assert local_dt.month == 9
        assert local_dt.day == 1
        assert local_dt.hour == 3
        assert local_dt.minute == 0

    def test_get_local_int_day(self):
        # 2026-08-31 20:30:00 UTC
        utc_dt = datetime(2026, 8, 31, 20, 30, 0, tzinfo=UTC)
        # In Moscow (UTC+3) -> 23:30 on 2026-08-31
        assert get_local_int_day(utc_dt, "Europe/Moscow") == 20260831
        # In Yekaterinburg (UTC+5) -> 01:30 on 2026-09-01
        assert get_local_int_day(utc_dt, "Asia/Yekaterinburg") == 20260901

    def test_get_date_range_bounds_utc(self):
        # Date range 2026-09-01 to 2026-09-01 in Tyumen (Asia/Yekaterinburg, UTC+5)
        dt_from, dt_to = get_date_range_bounds_utc(
            "2026-09-01", "2026-09-01", "Asia/Yekaterinburg"
        )
        assert dt_from is not None and dt_to is not None
        # Start: 2026-09-01 00:00:00 +05:00 -> 2026-08-31 19:00:00 UTC
        assert dt_from == datetime(2026, 8, 31, 19, 0, 0, tzinfo=UTC)
        # End: 2026-09-02 00:00:00 +05:00 -> 2026-09-01 19:00:00 UTC
        assert dt_to == datetime(2026, 9, 1, 19, 0, 0, tzinfo=UTC)


@pytest.mark.anyio
class TestAuthAndTimezone:
    """Test timezone returned in /api/auth/me and switch tenant."""

    async def test_me_returns_org_timezone(self):
        token = create_access_token(
            {"sub": "manager424", "org_id": 424, "role": "user", "token_type": "tenant"}
        )
        headers = {"Authorization": f"Bearer {token}"}

        mock_session = AsyncMock()
        mock_result = MagicMock()
        mock_result.first.return_value = ("Тюмень Орг", "Asia/Yekaterinburg")
        mock_session.execute = AsyncMock(return_value=mock_result)

        mock_cm = AsyncMock()
        mock_cm.__aenter__.return_value = mock_session
        mock_cm.__aexit__.return_value = None

        with patch("app.routers.auth.async_session", return_value=mock_cm):
            async with AsyncClient(
                transport=ASGITransport(app=app), base_url="http://test"
            ) as client:
                resp = await client.get("/api/auth/me", headers=headers)
                assert resp.status_code == 200
                data = resp.json()
                assert data["org_id"] == 424
                assert data["org_name"] == "Тюмень Орг"
                assert data["timezone"] == "Asia/Yekaterinburg"

    async def test_switch_tenant_returns_timezone(self):
        su_token = create_access_token(
            {
                "sub": "admin",
                "role": "superuser",
                "is_superuser": True,
                "token_type": "master",
            }
        )
        headers = {"Authorization": f"Bearer {su_token}"}

        store = get_user_store()
        await store.create_session(
            user_id=1,
            refresh_token="test_refresh_tz_switch",
        )

        mock_session = AsyncMock()
        mock_org = Org(
            org_id=424,
            org_name="Tyumen Org",
            name="Тюмень",
            status=1,
            is_active=True,
            timezone="Asia/Yekaterinburg",
        )
        mock_result = MagicMock()
        mock_result.scalar_one_or_none.return_value = mock_org
        mock_session.execute = AsyncMock(return_value=mock_result)

        mock_cm = AsyncMock()
        mock_cm.__aenter__.return_value = mock_session
        mock_cm.__aexit__.return_value = None

        with patch("app.routers.admin_tenants.async_session", return_value=mock_cm):
            async with AsyncClient(
                transport=ASGITransport(app=app), base_url="http://test"
            ) as client:
                resp = await client.post(
                    "/api/admin/tenants/switch",
                    json={"org_id": 424},
                    cookies={"refreshToken": "test_refresh_tz_switch"},
                    headers=headers,
                )
                assert resp.status_code == 200
                data = resp.json()
                assert data["org_id"] == 424
                assert data["org_name"] == "Tyumen Org"
                assert data["timezone"] == "Asia/Yekaterinburg"


@pytest.mark.anyio
class TestAdminOrgAndTimezone:
    """Test timezone in admin organizations endpoints."""

    async def test_admin_org_crud_timezone(self):
        su_token = create_access_token(
            {
                "sub": "admin",
                "role": "superuser",
                "is_superuser": True,
                "token_type": "master",
            }
        )
        headers = {"Authorization": f"Bearer {su_token}"}
        mock_db = AsyncMock()

        # Mock ID lookup
        id_res = MagicMock()
        id_res.scalars.return_value.all.return_value = [1, 2]
        # Mock name lookup
        name_res = MagicMock()
        name_res.scalar_one_or_none.return_value = None

        mock_db.execute.side_effect = [id_res, name_res]

        async def mock_refresh(instance):
            pass

        mock_db.refresh = AsyncMock(side_effect=mock_refresh)

        app.dependency_overrides[get_db] = lambda: mock_db

        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.post(
                "/api/admin/organizations",
                json={
                    "org_name": "Vladivostok Group",
                    "name": "Владивосток",
                    "timezone": "Asia/Vladivostok",
                },
                headers=headers,
            )
            assert resp.status_code == 201
            data = resp.json()
            assert data["timezone"] == "Asia/Vladivostok"


@pytest.mark.anyio
class TestReportsTimezone:
    """Test payments report with timezone projection."""

    async def test_payments_report_local_datetime_and_tz_response(self):
        token = create_access_token(
            {"sub": "user424", "org_id": 424, "role": "user", "token_type": "tenant"}
        )
        headers = {"Authorization": f"Bearer {token}"}

        mock_session = AsyncMock()

        # 1. Org timezone query -> "Asia/Yekaterinburg"
        tz_result = MagicMock()
        tz_result.scalar_one_or_none.return_value = "Asia/Yekaterinburg"

        # 2. Count query -> 1
        count_result = MagicMock()
        count_result.scalar.return_value = 1

        # 3. Payments row query
        # Stored in UTC: 2026-08-31 21:30:00 -> In Yekaterinburg: 2026-09-01 02:30:00
        paym_dt_utc = datetime(2026, 8, 31, 21, 30, 0, tzinfo=UTC)
        rows_result = MagicMock()
        rows_result.fetchall.return_value = [
            (
                1001,  # paym_id
                paym_dt_utc,  # paym_datetime
                35000,  # amount
                "0348_010926_02300001",  # ext_id
                7001,  # tsp_code
                2,  # state
                1,  # pay_type_id
                348,  # device_id
                "SN348",  # sn
                None,  # snapshot_id
            )
        ]

        # 4. Bindings / tsp / params queries
        empty_res = MagicMock()
        empty_res.fetchall.return_value = []

        async def mock_execute(stmt, params=None):
            sql_str = str(stmt)
            if "FROM orgs" in sql_str:
                return tz_result
            if "SELECT count(*)" in sql_str:
                return count_result
            if "FROM payments p" in sql_str:
                return rows_result
            return empty_res

        mock_session.execute = AsyncMock(side_effect=mock_execute)

        mock_cm = AsyncMock()
        mock_cm.__aenter__.return_value = mock_session
        mock_cm.__aexit__.return_value = None

        with patch("app.routers.reports.async_session", return_value=mock_cm):
            async with AsyncClient(
                transport=ASGITransport(app=app), base_url="http://test"
            ) as client:
                resp = await client.get(
                    "/api/reports/payments?date_from=2026-09-01&date_to=2026-09-01",
                    headers=headers,
                )
                assert resp.status_code == 200
                data = resp.json()
                assert data["timezone"] == "Asia/Yekaterinburg"
                assert len(data["items"]) == 1
                item = data["items"][0]
                assert item["paym_id"] == 1001
                # Backend must return the absolute UTC instant with a 'Z' suffix,
                # NOT a pre-localized naive string (that caused double conversion
                # on the frontend, which applies its own UTC -> tenant tz shift).
                assert item["paym_datetime"] == "2026-08-31T21:30:00Z"
