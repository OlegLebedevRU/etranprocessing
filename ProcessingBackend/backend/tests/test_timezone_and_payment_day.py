"""Unit and integration tests for timezone resolution and payment day calculation."""

from datetime import UTC, datetime, timedelta
from unittest.mock import AsyncMock, MagicMock

import pytest

from app.models import BalanceTerminalTsp, Terminal, Tsp
from app.services.payment_service import PaymentService
from app.utils.timezone import (
    get_local_datetime,
    get_local_int_day,
    resolve_tz,
)


@pytest.fixture
def anyio_backend():
    return "asyncio"


class TestTimezoneUtils:
    """Test timezone utility functions in app.utils.timezone."""

    def test_resolve_tz_valid_iana_offsets(self):
        ref_dt = datetime(2026, 9, 1, 12, 0, 0, tzinfo=UTC)

        tz_msk = resolve_tz("Europe/Moscow")
        offset_msk = ref_dt.astimezone(tz_msk).utcoffset()
        assert offset_msk == timedelta(hours=3)

        tz_ekt = resolve_tz("Asia/Yekaterinburg")
        offset_ekt = ref_dt.astimezone(tz_ekt).utcoffset()
        assert offset_ekt == timedelta(hours=5)

        tz_vlad = resolve_tz("Asia/Vladivostok")
        offset_vlad = ref_dt.astimezone(tz_vlad).utcoffset()
        assert offset_vlad == timedelta(hours=10)

    def test_resolve_tz_fallbacks(self):
        ref_dt = datetime(2026, 9, 1, 12, 0, 0, tzinfo=UTC)
        offset_msk = timedelta(hours=3)

        # Fallback to Moscow (+3)
        assert ref_dt.astimezone(resolve_tz(None)).utcoffset() == offset_msk
        assert ref_dt.astimezone(resolve_tz("")).utcoffset() == offset_msk
        assert ref_dt.astimezone(resolve_tz("   ")).utcoffset() == offset_msk
        assert (
            ref_dt.astimezone(resolve_tz("Invalid/Unknown_TZ")).utcoffset()
            == offset_msk
        )

    def test_get_local_datetime_conversion(self):
        # 2026-08-31 21:30:00 UTC
        utc_dt = datetime(2026, 8, 31, 21, 30, 0, tzinfo=UTC)

        # In Yekaterinburg (UTC+5), this is 2026-09-01 02:30:00
        ekt_dt = get_local_datetime(utc_dt, "Asia/Yekaterinburg")
        assert ekt_dt.year == 2026
        assert ekt_dt.month == 9
        assert ekt_dt.day == 1
        assert ekt_dt.hour == 2
        assert ekt_dt.minute == 30

        # In Moscow (UTC+3), this is 2026-09-01 00:30:00
        msk_dt = get_local_datetime(utc_dt, "Europe/Moscow")
        assert msk_dt.year == 2026
        assert msk_dt.month == 9
        assert msk_dt.day == 1
        assert msk_dt.hour == 0
        assert msk_dt.minute == 30

        # In London / UTC (+0), this is 2026-08-31 21:30:00
        london_dt = get_local_datetime(utc_dt, "UTC")
        assert london_dt.year == 2026
        assert london_dt.month == 8
        assert london_dt.day == 31
        assert london_dt.hour == 21

    def test_get_local_int_day_boundary(self):
        # 2026-08-31 20:30:00 UTC
        utc_dt_2030 = datetime(2026, 8, 31, 20, 30, 0, tzinfo=UTC)

        # In Moscow (UTC+3), 20:30 + 3h = 23:30 on 2026-08-31
        assert get_local_int_day(utc_dt_2030, "Europe/Moscow") == 20260831

        # In Yekaterinburg (UTC+5), 20:30 + 5h = 01:30 on 2026-09-01
        assert get_local_int_day(utc_dt_2030, "Asia/Yekaterinburg") == 20260901


@pytest.mark.anyio
class TestPaymentServiceTimezone:
    """Test PaymentService timezone resolution and int_day balance aggregation."""

    async def test_get_effective_timezone_terminal_override(self):
        mock_db = AsyncMock()
        service = PaymentService(mock_db)

        terminal = Terminal(
            id=1,
            device_id=101,
            org_id=424,
            sn="SN101",
            timezone="Asia/Vladivostok",
        )
        tz = await service.get_effective_timezone(terminal)
        ref_dt = datetime(2026, 9, 1, 12, 0, 0, tzinfo=UTC)
        assert ref_dt.astimezone(tz).utcoffset() == timedelta(hours=10)
        # Database query for Org should NOT have been called
        assert mock_db.execute.call_count == 0

    async def test_get_effective_timezone_from_org(self):
        mock_db = AsyncMock()
        mock_res = MagicMock()
        mock_res.scalar_one_or_none.return_value = "Asia/Yekaterinburg"
        mock_db.execute.return_value = mock_res

        service = PaymentService(mock_db)
        terminal = Terminal(
            id=2,
            device_id=102,
            org_id=424,
            sn="SN102",
            timezone=None,
        )
        tz = await service.get_effective_timezone(terminal)
        ref_dt = datetime(2026, 9, 1, 12, 0, 0, tzinfo=UTC)
        assert ref_dt.astimezone(tz).utcoffset() == timedelta(hours=5)
        assert mock_db.execute.call_count == 1

    async def test_get_effective_timezone_fallback_to_moscow(self):
        mock_db = AsyncMock()
        mock_res = MagicMock()
        mock_res.scalar_one_or_none.return_value = None
        mock_db.execute.return_value = mock_res

        service = PaymentService(mock_db)
        terminal = Terminal(
            id=3,
            device_id=103,
            org_id=999,
            sn="SN103",
            timezone=None,
        )
        tz = await service.get_effective_timezone(terminal)
        ref_dt = datetime(2026, 9, 1, 12, 0, 0, tzinfo=UTC)
        assert ref_dt.astimezone(tz).utcoffset() == timedelta(hours=3)

    async def test_create_payment_calculates_local_int_day_correctly(self):
        """Verify that a payment near midnight UTC gets recorded in the correct local day for Tyumen (UTC+5)."""
        mock_db = AsyncMock()

        # Terminal in Org 424 (Tyumen, Asia/Yekaterinburg)
        terminal = Terminal(
            id=42,
            device_id=348,
            org_id=424,
            sn="SN348",
            timezone=None,
        )
        tsp = Tsp(tsp_id=10, tsp_code=7001, tsp_name="ТСП Тюмень")

        async def mock_scalar(stmt, params=None):
            sql = str(stmt)
            if "FROM terminal_menu_bindings" in sql:
                return None
            if "FROM tsp_parameter_codes" in sql:
                return 100
            return None

        mock_db.scalar.side_effect = mock_scalar

        async def mock_execute(stmt, params=None):
            sql = str(stmt)
            res = MagicMock()
            if "FROM tsp" in sql:
                res.scalar_one_or_none.return_value = tsp
            elif "FROM orgs" in sql:
                res.scalar_one_or_none.return_value = "Asia/Yekaterinburg"
            elif "FROM balance_terminal_tsp" in sql:
                res.scalar_one_or_none.return_value = None  # New balance record
            else:
                res.scalar_one_or_none.return_value = None
            return res

        mock_db.execute.side_effect = mock_execute

        service = PaymentService(mock_db)

        # Payment at 2026-08-31 21:30:00 UTC -> 2026-09-01 02:30:00 in Yekaterinburg
        payment_dt = datetime(2026, 8, 31, 21, 30, 0, tzinfo=UTC)
        payment = await service.create_payment(
            terminal=terminal,
            tsp_code=7001,
            amount=35000,
            paym_ext_id="0348_010926_02300001",
            params={101: "9991234567"},
            payment_datetime=payment_dt,
        )

        assert payment.paym_amount == 35000
        assert payment.paym_datetime == payment_dt

        # Check balance record was created with int_day = 20260901 (NOT 20260831)
        added_objects = [call[0][0] for call in mock_db.add.call_args_list]
        balance_records = [
            obj for obj in added_objects if isinstance(obj, BalanceTerminalTsp)
        ]
        assert len(balance_records) == 1
        assert balance_records[0].int_day == 20260901
        assert balance_records[0].amount == 35000
        assert balance_records[0].org_id == 424
        assert balance_records[0].terminal_id == 42
