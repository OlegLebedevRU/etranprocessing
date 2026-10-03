from __future__ import annotations

from datetime import UTC, date, datetime, timedelta, tzinfo
from zoneinfo import ZoneInfo


def resolve_timezone(tz_name: str | None) -> tzinfo:
    """Resolve timezone by IANA name with graceful cross-platform fallback.

    Uses standard ZoneInfo when available (e.g. Linux / hosts with tzdata),
    falling back to dateutil.tz on Windows when zoneinfo database is not present.
    """
    if not tz_name or tz_name.upper() in ("UTC", "Z"):
        return UTC
    try:
        return ZoneInfo(tz_name)
    except Exception:
        from dateutil import tz

        zone = tz.gettz(tz_name)
        if zone is not None:
            return zone
        raise


def split_interval_by_local_days(
    start_utc: datetime, end_utc: datetime, tz_name: str
) -> list[tuple[date, int]]:
    """Split an interval [start_utc, end_utc) across local midnight boundaries of tz_name.

    Invariants:
    - sum(duration_seconds) == round((end_utc - start_utc).total_seconds())
    - DST safe (uses exact timezone offsets at each local midnight).
    """
    if start_utc.tzinfo is None:
        start_utc = start_utc.replace(tzinfo=UTC)
    if end_utc.tzinfo is None:
        end_utc = end_utc.replace(tzinfo=UTC)
    start_utc, end_utc = start_utc.astimezone(UTC), end_utc.astimezone(UTC)

    if start_utc >= end_utc:
        return []

    tz = resolve_timezone(tz_name)
    chunks: list[tuple[date, int]] = []
    cursor = start_utc
    assigned = 0

    while cursor < end_utc:
        curr_date = cursor.astimezone(tz).date()
        next_day = curr_date + timedelta(days=1)
        next_midnight_local = datetime(
            next_day.year, next_day.month, next_day.day, 0, 0, 0, tzinfo=tz
        )
        next_boundary = min(end_utc, next_midnight_local.astimezone(UTC))
        cumulative = round((next_boundary - start_utc).total_seconds())
        seconds = cumulative - assigned

        if seconds > 0:
            chunks.append((curr_date, seconds))

        assigned = cumulative
        cursor = next_boundary

    return chunks
