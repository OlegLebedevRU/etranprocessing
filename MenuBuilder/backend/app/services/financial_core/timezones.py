from __future__ import annotations

from datetime import UTC, tzinfo
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
