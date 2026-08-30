from datetime import UTC, datetime
from typing import Any


def parse_gauge_pack(raw: str) -> dict[str, Any]:
    """Parse semicolon-delimited resource=value pairs into a dict with string keys."""
    result: dict[str, Any] = {}
    if not raw:
        return result
    for pair in raw.split(";"):
        pair = pair.strip()
        if "=" in pair:
            key, value = pair.split("=", 1)
            key = key.strip()
            value = value.strip()
            result[key] = value
    return result


def _parse_ts(val: datetime | str | None) -> datetime | None:
    if val is None:
        return None
    if isinstance(val, datetime):
        return val if val.tzinfo is not None else val.replace(tzinfo=UTC)
    try:
        dt = datetime.fromisoformat(val)
        return dt if dt.tzinfo is not None else dt.replace(tzinfo=UTC)
    except ValueError, TypeError:
        return None


def update_slots_bitmask(
    current_mask: int,
    last_epoch_tick: int,
    current_epoch_tick: int,
    last_updated_at: datetime | str | None = None,
    current_updated_at: datetime | str | None = None,
) -> int:
    """Update 12-bit rolling bitmask with communication activity in current 10-minute tick.

    - current_mask: 12-bit integer (0..4095)
    - last_epoch_tick: epoch_seconds // 600 of previous record
    - current_epoch_tick: epoch_seconds // 600 of current record
    - last_updated_at: optional ISO/datetime of previous request
    - current_updated_at: optional ISO/datetime of current request
    - Bit 0 is the current/newest tick (0-10 min ago)
    - Bit 11 is the oldest tick (110-120 min ago)
    """
    last_dt = _parse_ts(last_updated_at)
    curr_dt = _parse_ts(current_updated_at)

    if last_dt is not None and curr_dt is not None:
        elapsed_sec = (curr_dt - last_dt).total_seconds()
        if elapsed_sec < 0:
            delta = 0
        elif elapsed_sec < 300:
            # Frequent/duplicate request within the same active slot
            delta = 0
        else:
            delta = round(elapsed_sec / 600.0)
    elif last_epoch_tick <= 0:
        return 0x001
    else:
        delta = current_epoch_tick - last_epoch_tick

    if delta <= 0:
        return (current_mask & 0x0FFF) | 0x001
    elif 0 < delta < 12:
        return ((current_mask << delta) & 0x0FFF) | 0x001
    elif delta >= 12:
        return 0x001
    else:
        return (current_mask & 0x0FFF) | 0x001


def decode_slots_bitmask(
    bitmask: int,
    last_epoch_tick: int,
    now_epoch_tick: int | None = None,
    updated_at: datetime | str | None = None,
    now_dt: datetime | None = None,
) -> list[bool]:
    """Decode stored 12-bit bitmask into 12 boolean flags for UI from oldest (slot 0) to newest (slot 11).

    Accounts for idle elapsed ticks between last recorded tick and now.
    A slot only expires and becomes inactive (False) when its 10-minute timeout (+ grace margin) has fully elapsed.
    """
    if bitmask == 0:
        return [False] * 12

    last_dt = _parse_ts(updated_at)
    if now_dt is not None and now_dt.tzinfo is None:
        now_dt = now_dt.replace(tzinfo=UTC)

    if last_dt is not None:
        curr_dt = now_dt or datetime.now(UTC)
        elapsed_sec = max(0.0, (curr_dt - last_dt).total_seconds())
        # 11 minutes (660s): 10-minute interval + 1 minute grace period for timer jitter
        if elapsed_sec <= 660.0:
            idle_ticks = 0
        else:
            idle_ticks = int((elapsed_sec - 60.0) // 600.0)
    else:
        if last_epoch_tick <= 0:
            return [False] * 12
        if now_epoch_tick is None:
            now_epoch_tick = int(datetime.now(UTC).timestamp() // 600)

        diff = now_epoch_tick - last_epoch_tick
        # The next tick (diff == 1) is still within the active waiting window
        idle_ticks = max(0, diff - 1) if diff > 0 else 0

    if idle_ticks >= 12:
        effective_mask = 0
    elif idle_ticks > 0:
        effective_mask = (bitmask << idle_ticks) & 0x0FFF
    else:
        effective_mask = bitmask & 0x0FFF

    # Returns 12 booleans: index 0 is oldest (110-120m ago, Bit 11), index 11 is newest (now, Bit 0)
    return [bool((effective_mask >> (11 - i)) & 1) for i in range(12)]


def calculate_lastnumconn(slots: list[bool]) -> int:
    """Calculate number of trailing missed/inactive slots from the newest slot backward."""
    lastnumconn = 0
    for s in reversed(slots):
        if not s:
            lastnumconn += 1
        else:
            break
    return lastnumconn


def create_or_update_snapshot(
    device_id: int,
    sn: str,
    gauge_data: dict[str, Any],
    existing_snapshot: dict[str, Any] | None = None,
    now_ts: datetime | None = None,
    enrichment_update: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Build unified DeviceGaugeSnapshot conforming to the RabbitMQ retain contract."""
    if now_ts is None:
        now_ts = datetime.now(UTC)
    elif now_ts.tzinfo is None:
        now_ts = now_ts.replace(tzinfo=UTC)

    current_epoch_tick = int(now_ts.timestamp() // 600)

    old_mask = 0
    last_epoch_tick = 0
    last_updated_at_str = None
    enrichment: dict[str, Any] = {
        "last_payment_at": None,
        "last_inkass_at": None,
        "license_expires_at": None,
        "iot_is_online": True,
    }

    if existing_snapshot:
        old_mask = int(existing_snapshot.get("slots_bitmask", 0))
        last_epoch_tick = int(existing_snapshot.get("last_tick_epoch", 0))
        last_updated_at_str = existing_snapshot.get("updated_at")
        if "internal_enrichment" in existing_snapshot and isinstance(
            existing_snapshot["internal_enrichment"], dict
        ):
            enrichment.update(existing_snapshot["internal_enrichment"])

    if enrichment_update:
        enrichment.update(enrichment_update)

    new_bitmask = update_slots_bitmask(
        old_mask,
        last_epoch_tick,
        current_epoch_tick,
        last_updated_at=last_updated_at_str,
        current_updated_at=now_ts,
    )

    # Stringify all gauge keys
    norm_gauge = {str(k): v for k, v in gauge_data.items()}

    return {
        "device_id": device_id,
        "sn": sn,
        "updated_at": now_ts.isoformat(),
        "last_tick_epoch": current_epoch_tick,
        "slots_bitmask": new_bitmask,
        "gauge": norm_gauge,
        "internal_enrichment": enrichment,
    }
