from collections.abc import Mapping
from datetime import UTC, datetime
from typing import Any


def parse_gauge_pack(raw: str) -> dict[str, Any]:
    result: dict[str, Any] = {}
    if not raw:
        return result
    for pair in raw.split(";"):
        pair = pair.strip()
        if "=" in pair:
            key, value = pair.split("=", 1)
            result[key.strip()] = value.strip()
    return result


def _parse_ts(value: datetime | str | None) -> datetime | None:
    if value is None:
        return None
    if isinstance(value, datetime):
        return value if value.tzinfo is not None else value.replace(tzinfo=UTC)
    try:
        parsed = datetime.fromisoformat(value)
        return parsed if parsed.tzinfo is not None else parsed.replace(tzinfo=UTC)
    except ValueError, TypeError:
        return None


def update_slots_bitmask(
    current_mask: int,
    last_epoch_tick: int,
    current_epoch_tick: int,
    last_updated_at: datetime | str | None = None,
    current_updated_at: datetime | str | None = None,
) -> int:
    last_dt = _parse_ts(last_updated_at)
    current_dt = _parse_ts(current_updated_at)
    if last_dt is not None and current_dt is not None:
        elapsed_sec = (current_dt - last_dt).total_seconds()
        if elapsed_sec < 300:
            delta = 0
        else:
            delta = round(elapsed_sec / 600.0)
    elif last_epoch_tick <= 0:
        return 0x001
    else:
        delta = current_epoch_tick - last_epoch_tick

    if delta <= 0:
        return (current_mask & 0x0FFF) | 0x001
    if delta < 12:
        return ((current_mask << delta) & 0x0FFF) | 0x001
    return 0x001


def decode_slots_bitmask(
    bitmask: int,
    last_epoch_tick: int,
    now_epoch_tick: int | None = None,
    updated_at: datetime | str | None = None,
    now_dt: datetime | None = None,
) -> list[bool]:
    if bitmask == 0:
        return [False] * 12

    last_dt = _parse_ts(updated_at)
    if now_dt is not None and now_dt.tzinfo is None:
        now_dt = now_dt.replace(tzinfo=UTC)
    if last_dt is not None:
        current_dt = now_dt or datetime.now(UTC)
        elapsed_sec = max(0.0, (current_dt - last_dt).total_seconds())
        idle_ticks = 0 if elapsed_sec <= 660.0 else int((elapsed_sec - 60.0) // 600.0)
    else:
        if last_epoch_tick <= 0:
            return [False] * 12
        if now_epoch_tick is None:
            now_epoch_tick = int(datetime.now(UTC).timestamp() // 600)
        diff = now_epoch_tick - last_epoch_tick
        idle_ticks = max(0, diff - 1) if diff > 0 else 0

    if idle_ticks >= 12:
        effective_mask = 0
    elif idle_ticks > 0:
        effective_mask = (bitmask << idle_ticks) & 0x0FFF
    else:
        effective_mask = bitmask & 0x0FFF
    return [bool((effective_mask >> (11 - index)) & 1) for index in range(12)]


def calculate_lastnumconn(slots: list[bool]) -> int:
    lastnumconn = 0
    for slot in reversed(slots):
        if slot:
            break
        lastnumconn += 1
    return lastnumconn


def create_or_update_snapshot(
    device_id: int,
    sn: str,
    gauge_data: Mapping[Any, Any],
    existing_snapshot: dict[str, Any] | None = None,
    now_ts: datetime | None = None,
    enrichment_update: dict[str, Any] | None = None,
) -> dict[str, Any]:
    if now_ts is None:
        now_ts = datetime.now(UTC)
    elif now_ts.tzinfo is None:
        now_ts = now_ts.replace(tzinfo=UTC)
    current_epoch_tick = int(now_ts.timestamp() // 600)
    old_mask = 0
    last_epoch_tick = 0
    last_updated_at = None
    enrichment: dict[str, Any] = {
        "last_payment_at": None,
        "last_inkass_at": None,
        "license_expires_at": None,
        "iot_is_online": True,
    }
    if existing_snapshot:
        old_mask = int(existing_snapshot.get("slots_bitmask", 0))
        last_epoch_tick = int(existing_snapshot.get("last_tick_epoch", 0))
        last_updated_at = existing_snapshot.get("updated_at")
        existing_enrichment = existing_snapshot.get("internal_enrichment")
        if isinstance(existing_enrichment, dict):
            enrichment.update(existing_enrichment)
    if enrichment_update:
        enrichment.update(enrichment_update)
    new_bitmask = update_slots_bitmask(
        old_mask,
        last_epoch_tick,
        current_epoch_tick,
        last_updated_at=last_updated_at,
        current_updated_at=now_ts,
    )
    return {
        "device_id": device_id,
        "sn": sn,
        "updated_at": now_ts.isoformat(),
        "last_tick_epoch": current_epoch_tick,
        "slots_bitmask": new_bitmask,
        "gauge": {str(key): value for key, value in gauge_data.items()},
        "internal_enrichment": enrichment,
    }
