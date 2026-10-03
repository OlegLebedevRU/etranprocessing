"""Retired monetary API: no routes are mounted. Authentication compatibility only."""

import contextlib
from typing import Any

from fastapi import Header, HTTPException, Request, status

from app.config import settings


async def require_internal_or_superuser(
    request: Request,
    x_internal_service_key: str | None = Header(None, alias="X-Internal-Service-Key"),
) -> dict[str, Any]:
    """Authorize via X-Internal-Service-Key or superuser session."""
    expected_key = settings.internal_service_key_value
    if expected_key and x_internal_service_key == expected_key:
        return {"source": "internal_service"}

    auth_header = request.headers.get("Authorization")
    token: str | None = None
    if auth_header and auth_header.startswith("Bearer "):
        token = auth_header[7:].strip()
    elif "accessToken" in request.cookies:
        token = request.cookies["accessToken"]

    if token:
        with contextlib.suppress(Exception):
            from app.auth import decode_token

            payload = decode_token(token)
            role = str(payload.get("role", "")).lower()
            if (
                payload.get("is_superuser")
                or role in ("superuser", "admin")
                or payload.get("roleId") == 1
            ):
                return payload

    raise HTTPException(
        status_code=status.HTTP_403_FORBIDDEN,
        detail="Superuser privilege or valid X-Internal-Service-Key required",
    )
