from datetime import UTC, datetime
from unittest.mock import AsyncMock

import pytest
from httpx import ASGITransport, AsyncClient

from app.database import get_db
from app.dependencies import get_current_terminal
from app.main import app
from app.models import Terminal
from app.services.gauge_bus import GaugeStore, gauge_store
from app.services.gauge_engine import (
    calculate_lastnumconn,
    create_or_update_snapshot,
    decode_slots_bitmask,
    parse_gauge_pack,
    update_slots_bitmask,
)


@pytest.fixture
def anyio_backend():
    return "asyncio"


def test_parse_gauge_pack():
    raw = "102=0;109=14500;112=CCNET;120=128;121=OK;124=0;130=1.42"
    res = parse_gauge_pack(raw)
    assert res["102"] == "0"
    assert res["109"] == "14500"
    assert res["112"] == "CCNET"
    assert res["120"] == "128"
    assert res["121"] == "OK"
    assert res["124"] == "0"
    assert res["130"] == "1.42"


def test_parse_gauge_pack_empty_or_malformed():
    assert parse_gauge_pack("") == {}
    assert parse_gauge_pack(";;no_equal_sign;102=1") == {"102": "1"}


def test_update_slots_bitmask_deltas():
    # Initial record
    mask = update_slots_bitmask(0, 0, 100)
    assert mask == 0x001  # Bit 0 set

    # Same tick (delta == 0)
    mask = update_slots_bitmask(mask, 100, 100)
    assert mask == 0x001

    # Next tick (delta == 1)
    mask = update_slots_bitmask(mask, 100, 101)
    assert mask == 0x003  # 0b000000000011 (Bit 1 and Bit 0 set)

    # 2 ticks later (delta == 2)
    mask = update_slots_bitmask(mask, 101, 103)
    assert mask == 0x00D  # (0b11 << 2) | 1 = 0b1101

    # 12 ticks later (all expired)
    mask = update_slots_bitmask(mask, 103, 115)
    assert mask == 0x001

    # Timestamp-based deltas
    t0 = datetime(2026, 8, 30, 21, 21, 38, tzinfo=UTC)
    t1 = datetime(2026, 8, 30, 21, 31, 38, tzinfo=UTC)  # 10m later
    m1 = update_slots_bitmask(0x001, 0, 0, last_updated_at=t0, current_updated_at=t1)
    assert m1 == 0x003

    t2 = datetime(2026, 8, 30, 21, 51, 39, tzinfo=UTC)  # 20m later (1 missed)
    m2 = update_slots_bitmask(m1, 0, 0, last_updated_at=t1, current_updated_at=t2)
    assert m2 == 0x00D


def test_decode_slots_bitmask():
    # 12-bit mask with activity in newest tick only
    # last_tick_epoch = 100, now = 100
    slots = decode_slots_bitmask(0x001, 100, 100)
    assert len(slots) == 12
    assert slots == [False] * 11 + [True]

    # Active waiting window: now = 101 (next 10-min tick started, but interval not yet expired)
    slots = decode_slots_bitmask(0x001, 100, 101)
    assert slots == [False] * 11 + [True]

    # Idle for 1 expired tick: now = 102
    slots = decode_slots_bitmask(0x001, 100, 102)
    assert slots == [False] * 10 + [True, False]

    # Idle for 12 ticks: all False
    slots = decode_slots_bitmask(0x001, 100, 113)
    assert slots == [False] * 12

    # Timestamp-based decode test:
    t_up = datetime(2026, 8, 30, 21, 31, 38, tzinfo=UTC)
    # 8.5 minutes later (across the :40:00 boundary) -> still GREEN (True)
    t_within = datetime(2026, 8, 30, 21, 40, 5, tzinfo=UTC)
    slots_within = decode_slots_bitmask(0x001, 0, updated_at=t_up, now_dt=t_within)
    assert slots_within == [False] * 11 + [True]

    # 12 minutes later -> timeout expired -> shifts to RED (False)
    t_expired = datetime(2026, 8, 30, 21, 43, 38, tzinfo=UTC)
    slots_expired = decode_slots_bitmask(0x001, 0, updated_at=t_up, now_dt=t_expired)
    assert slots_expired == [False] * 10 + [True, False]


def test_calculate_lastnumconn():
    assert calculate_lastnumconn([True] * 12) == 0
    assert calculate_lastnumconn([True] * 11 + [False]) == 1
    assert calculate_lastnumconn([True] * 10 + [False, False]) == 2
    assert calculate_lastnumconn([False] * 12) == 12


def test_create_or_update_snapshot():
    now = datetime(2026, 8, 30, 17, 8, 0, tzinfo=UTC)
    snap = create_or_update_snapshot(
        device_id=6209,
        sn="a4b0006209c67756d020626",
        gauge_data={"102": "0", "109": "14500"},
        now_ts=now,
    )
    assert snap["device_id"] == 6209
    assert snap["sn"] == "a4b0006209c67756d020626"
    assert snap["slots_bitmask"] == 1
    assert snap["gauge"]["102"] == "0"
    assert snap["gauge"]["109"] == "14500"
    assert snap["internal_enrichment"]["iot_is_online"] is True


def test_gauge_store_operations():
    store = GaugeStore()
    snap = {
        "device_id": 123,
        "sn": "SN123",
        "slots_bitmask": 5,
        "last_tick_epoch": 100,
        "gauge": {"102": "0"},
    }
    store.set_snapshot(snap)
    assert store.get_by_device_id(123) == snap
    assert store.get_by_sn("SN123") == snap
    assert store.get_by_device_id(999) is None

    store.update_enrichment(123, {"last_payment_at": "2026-08-30T17:00:00Z"})
    updated = store.get_by_device_id(123)
    assert updated is not None
    assert updated["internal_enrichment"]["last_payment_at"] == "2026-08-30T17:00:00Z"

    store.clear()
    assert store.get_by_device_id(123) is None


@pytest.mark.anyio
async def test_post_gategauge_endpoint():
    gauge_store.clear()

    mock_terminal = Terminal(
        id=1,
        device_id=6209,
        sn="a4b0006209c67756d020626",
        org_id=1,
        is_active=True,
    )

    app.dependency_overrides[get_current_terminal] = lambda: mock_terminal
    app.dependency_overrides[get_db] = lambda: AsyncMock()

    body = "GaugePack=102=0;109=15000;112=CCNET;121=OK;130=1.42".encode("windows-1251")

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        resp = await client.post(
            "/api/gategauge",
            content=body,
            headers={"Content-Type": "application/x-www-form-urlencoded"},
        )
        assert resp.status_code == 200
        assert "<Result>OK</Result>" in resp.text

    snap = gauge_store.get_by_device_id(6209)
    assert snap is not None
    assert snap["sn"] == "a4b0006209c67756d020626"
    assert snap["gauge"]["102"] == "0"
    assert snap["gauge"]["109"] == "15000"
    assert snap["gauge"]["112"] == "CCNET"
    assert snap["slots_bitmask"] == 1


@pytest.mark.anyio
async def test_post_gategauge_missing_pack():
    mock_terminal = Terminal(
        id=1,
        device_id=6209,
        sn="a4b0006209c67756d020626",
        org_id=1,
        is_active=True,
    )

    app.dependency_overrides[get_current_terminal] = lambda: mock_terminal
    app.dependency_overrides[get_db] = lambda: AsyncMock()

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        resp = await client.post(
            "/api/gategauge",
            content=b"Foo=Bar",
            headers={"Content-Type": "application/x-www-form-urlencoded"},
        )
        assert resp.status_code == 200
        assert "<Result>ERROR</Result>" in resp.text
        assert "Missing GaugePack parameter" in resp.text
