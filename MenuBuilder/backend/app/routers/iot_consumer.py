from __future__ import annotations

import contextlib
import logging
from typing import Any

from fastapi import APIRouter, Depends, Header, HTTPException, Request, status

from app.config import settings
from app.services.iot_event_consumer import iot_event_consumer

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/internal/v1/iot-consumer", tags=["iot-consumer"])


async def require_internal_or_superuser(
    request: Request,
    x_internal_service_key: str | None = Header(None, alias="X-Internal-Service-Key"),
) -> dict[str, Any]:
    """Authorize via X-Internal-Service-Key or superuser session."""
    expected_key = settings.event_feed_effective_token
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


@router.get("/status")
async def get_iot_consumer_status(
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> dict[str, Any]:
    """Retrieve consumer checkpoint, real-time lag metrics, and processing counts."""
    return await iot_event_consumer.get_metrics()


@router.post("/poll")
async def trigger_iot_consumer_poll(
    batch_size: int | None = None,
    _auth: dict[str, Any] = Depends(require_internal_or_superuser),
) -> dict[str, Any]:
    """Trigger an on-demand single poll cycle of the IoT event feed."""
    result = await iot_event_consumer.poll_once(batch_size=batch_size)
    return {
        "events_received": result.events_received,
        "processed_count": result.processed_count,
        "duplicate_count": result.duplicate_count,
        "quarantine_count": result.quarantine_count,
        "has_more": result.has_more,
        "checkpoint_cursor": result.checkpoint_cursor,
        "remote_latest_cursor": result.remote_latest_cursor,
        "cursor_lag": result.cursor_lag,
        "last_event_occurred_at": (
            result.last_event_occurred_at.isoformat()
            if result.last_event_occurred_at
            else None
        ),
    }
