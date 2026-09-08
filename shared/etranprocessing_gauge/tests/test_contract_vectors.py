from datetime import UTC, datetime, timedelta

from etranprocessing_gauge import (
    create_or_update_snapshot,
    decode_slots_bitmask,
    parse_gauge_pack,
    update_slots_bitmask,
)


def test_parse_gauge_pack_contract_vector():
    assert parse_gauge_pack("102=1; 109=2500;broken;130=1.02") == {
        "102": "1",
        "109": "2500",
        "130": "1.02",
    }


def test_mask_contract_vectors_cover_duplicate_gap_and_reset():
    assert update_slots_bitmask(0, 0, 100) == 0b1
    assert update_slots_bitmask(0b101, 100, 100) == 0b101
    assert update_slots_bitmask(0b101, 100, 102) == 0b10101
    assert update_slots_bitmask(0b101, 100, 112) == 0b1


def test_decode_contract_vector_honors_grace_and_expiry():
    updated_at = datetime(2026, 8, 31, 12, 0, tzinfo=UTC)

    within_grace = decode_slots_bitmask(
        0b1,
        0,
        updated_at=updated_at,
        now_dt=updated_at + timedelta(minutes=11),
    )
    expired = decode_slots_bitmask(
        0b1,
        0,
        updated_at=updated_at,
        now_dt=updated_at + timedelta(hours=2, minutes=1),
    )

    assert within_grace[-1] is True
    assert expired == [False] * 12


def test_snapshot_contract_preserves_enrichment_and_normalizes_keys():
    snapshot = create_or_update_snapshot(
        773,
        "term-001",
        {102: "1"},
        existing_snapshot={
            "slots_bitmask": 1,
            "last_tick_epoch": 100,
            "updated_at": "2026-08-31T12:00:00+00:00",
            "internal_enrichment": {"last_payment_at": "2026-08-31T11:00:00+00:00"},
        },
        now_ts=datetime(2026, 8, 31, 12, 10, tzinfo=UTC),
        enrichment_update={"iot_is_online": False},
    )

    assert snapshot["gauge"] == {"102": "1"}
    assert snapshot["internal_enrichment"]["last_payment_at"] is not None
    assert snapshot["internal_enrichment"]["iot_is_online"] is False
