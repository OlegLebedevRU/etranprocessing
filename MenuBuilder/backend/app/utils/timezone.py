"""Timezone utilities for resolving regional timezones, converting dates, and calculating report boundaries."""

from datetime import UTC, date, datetime, time, timedelta, tzinfo
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

import dateutil.tz

DEFAULT_TIMEZONE = "Europe/Moscow"


def resolve_tz(
    tz_name: str | tzinfo | None,
    default: str = DEFAULT_TIMEZONE,
) -> tzinfo:
    """Resolve a timezone identifier or object to a valid tzinfo / ZoneInfo instance.

    Uses standard ZoneInfo when available, with robust fallback to dateutil.tz and UTC.

    Args:
        tz_name: Timezone string (e.g. 'Asia/Yekaterinburg', 'Europe/Moscow') or tzinfo.
        default: Fallback timezone name if tz_name is None, empty, or invalid.

    Returns:
        tzinfo or ZoneInfo instance.
    """
    if isinstance(tz_name, tzinfo):
        return tz_name

    if tz_name and isinstance(tz_name, str):
        cleaned = tz_name.strip()
        if cleaned:
            if cleaned.upper() == "UTC":
                return UTC
            try:
                return ZoneInfo(cleaned)
            except ZoneInfoNotFoundError, ValueError, KeyError:
                fallback_tz = dateutil.tz.gettz(cleaned)
                if fallback_tz is not None:
                    return fallback_tz

    try:
        return ZoneInfo(default)
    except ZoneInfoNotFoundError, ValueError, KeyError:
        fallback_default = dateutil.tz.gettz(default)
        if fallback_default is not None:
            return fallback_default
        return UTC


def get_timezone_name(tz: tzinfo | str | None, default: str = DEFAULT_TIMEZONE) -> str:
    """Get the string identifier of a timezone."""
    if tz is None:
        return default
    if isinstance(tz, str):
        return tz.strip() or default
    key = getattr(tz, "key", None)
    if key:
        return str(key)
    s = str(tz)
    if s.startswith("tzfile('") and s.endswith("')"):
        return s[8:-2]
    fn = getattr(tz, "_filename", None)
    if fn:
        parts = [p for p in str(fn).replace("\\", "/").split("/") if p]
        if len(parts) >= 2:
            return f"{parts[-2]}/{parts[-1]}"
    if tz is UTC:
        return "UTC"
    return s


def get_local_datetime(
    dt: datetime | None = None,
    tz: tzinfo | str | None = None,
) -> datetime:
    """Convert a UTC or aware datetime into the target local timezone.

    Args:
        dt: Datetime object. If naive, assumed to be UTC. If None, current UTC time is used.
        tz: Target timezone name or tzinfo object. Defaults to Europe/Moscow.

    Returns:
        Timezone-aware datetime in the target timezone.
    """
    target_zone = resolve_tz(tz)
    if dt is None:
        dt = datetime.now(UTC)
    elif dt.tzinfo is None:
        dt = dt.replace(tzinfo=UTC)

    return dt.astimezone(target_zone)


def get_local_int_day(
    dt: datetime | None = None,
    tz: tzinfo | str | None = None,
) -> int:
    """Calculate integer day key (YYYYMMDD) for a timestamp in the given timezone.

    Args:
        dt: Datetime object. If naive, assumed to be UTC. If None, current UTC time is used.
        tz: Target timezone name or tzinfo object. Defaults to Europe/Moscow.

    Returns:
        Integer representation of date, e.g. 20260901.
    """
    local_dt = get_local_datetime(dt=dt, tz=tz)
    return int(local_dt.strftime("%Y%m%d"))


def to_utc_iso(dt: datetime | None) -> str:
    """Serialize an absolute timestamp as UTC ISO-8601 with a 'Z' suffix.

    Args:
        dt: Timezone-aware datetime (assumed UTC if naive). None yields "".

    Returns:
        ISO-8601 string in UTC with 'Z' suffix, e.g. '2026-09-01T14:24:19Z',
        or "" if dt is None.
    """
    if dt is None:
        return ""
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=UTC)
    else:
        dt = dt.astimezone(UTC)
    return dt.isoformat().replace("+00:00", "Z")


def get_date_range_bounds_utc(
    date_from_str: str | None,
    date_to_str: str | None,
    tz: tzinfo | str | None = None,
) -> tuple[datetime | None, datetime | None]:
    """Convert local string dates (YYYY-MM-DD) into UTC range boundaries [from_utc, to_utc).

    Start date is inclusive: date_from at 00:00:00 local -> UTC.
    End date is exclusive: date_to + 1 day at 00:00:00 local -> UTC.

    Args:
        date_from_str: Start date in YYYY-MM-DD format or None.
        date_to_str: End date in YYYY-MM-DD format or None.
        tz: Target timezone name or tzinfo object.

    Returns:
        Tuple of (from_utc, to_utc), each being a timezone-aware UTC datetime or None.
    """
    target_zone = resolve_tz(tz)
    dt_from_utc: datetime | None = None
    dt_to_utc: datetime | None = None

    if date_from_str:
        try:
            d_from = date.fromisoformat(date_from_str.strip())
            local_from = datetime.combine(d_from, time.min, tzinfo=target_zone)
            dt_from_utc = local_from.astimezone(UTC)
        except ValueError:
            pass

    if date_to_str:
        try:
            d_to = date.fromisoformat(date_to_str.strip()) + timedelta(days=1)
            local_to = datetime.combine(d_to, time.min, tzinfo=target_zone)
            dt_to_utc = local_to.astimezone(UTC)
        except ValueError:
            pass

    return dt_from_utc, dt_to_utc
