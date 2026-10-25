"""Camera-only media expiry export; never compare its fixed coordinate to UTC."""

from __future__ import annotations

from typing import Any
from uuid import UUID

from fastapi import HTTPException


def camera_media_deadline(
    authority: dict[str, Any],
    *,
    sn: str,
    lease_id: str,
    user: dict[str, Any],
    roundtrip_ms: int,
    expected_clock_id: str | None = None,
) -> int:
    owners = {str(user[key]) for key in ("sub", "user_id", "id") if user.get(key)}
    if (
        authority.get("active") is not True
        or authority.get("sn") != sn
        or str(authority.get("lease_id") or "") != lease_id
    ):
        raise HTTPException(409, detail={"code": "lease_invalid"})
    if (
        str(authority.get("owner_user_id") or "") not in owners
        or not user.get("session_id")
        or str(authority.get("owner_session_id") or "") != str(user["session_id"])
    ):
        raise HTTPException(403, detail={"code": "lease_owner_mismatch"})
    if authority.get("scope") != "stream":
        raise HTTPException(403, detail={"code": "stream_scope_required"})
    expiry = authority.get("media_lease_expires_at")
    remaining = authority.get("media_remaining_ms")
    clock_id = authority.get("media_clock_id")
    try:
        parsed = UUID(clock_id)
        valid_clock = parsed.version == 4 and str(parsed) == clock_id
    except ValueError, TypeError, AttributeError:
        valid_clock = False
    if (
        not valid_clock
        or type(expiry) is not int
        or not 0 < expiry <= 9007199254740991
        or type(remaining) is not int
        or not 1 <= remaining <= 60000
        or type(roundtrip_ms) is not int
        or roundtrip_ms < 0
    ):
        raise HTTPException(409, detail={"code": "camera_media_clock_unavailable"})
    if expected_clock_id is not None and clock_id != expected_clock_id:
        raise HTTPException(409, detail={"code": "camera_media_clock_changed"})
    if remaining <= roundtrip_ms:
        raise HTTPException(410, detail={"code": "expired"})
    # The trusted export is already in the attempt's fixed media coordinate.
    # Returning it unchanged avoids both a live-UTC remap and a fresh TTL.
    return expiry
