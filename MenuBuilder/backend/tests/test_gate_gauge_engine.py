from datetime import UTC, datetime

import pytest
from etranprocessing_gauge import (
    calculate_lastnumconn,
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


def test_update_slots_bitmask_deltas():
    # Initial record
    mask = update_slots_bitmask(0, 0, 100)
    assert mask == 0x001

    # Same tick (delta == 0)
    mask = update_slots_bitmask(mask, 100, 100)
    assert mask == 0x001

    # Next tick (delta == 1)
    mask = update_slots_bitmask(mask, 100, 101)
    assert mask == 0x003

    # 2 ticks later (delta == 2)
    mask = update_slots_bitmask(mask, 101, 103)
    assert mask == 0x00D

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
