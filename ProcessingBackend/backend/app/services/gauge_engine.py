"""Compatibility exports for the shared gauge engine."""

from etranprocessing_gauge import (
    calculate_lastnumconn,
    create_or_update_snapshot,
    decode_slots_bitmask,
    parse_gauge_pack,
    update_slots_bitmask,
)

__all__ = [
    "calculate_lastnumconn",
    "create_or_update_snapshot",
    "decode_slots_bitmask",
    "parse_gauge_pack",
    "update_slots_bitmask",
]
