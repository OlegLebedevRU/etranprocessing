import hashlib
import logging
import time
from typing import Any

from fastapi import APIRouter, Depends, HTTPException, status
from pydantic import BaseModel
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import resolve_org_id
from app.config import settings
from app.database import get_db
from app.models import Terminal
from app.security.permissions import PERMISSION_VIDEO_VIEW, require_permission

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/v1/video", tags=["video"])


class VideoSessionResponse(BaseModel):
    mountpoint_id: int
    sn: str
    janus_ws: str
    session_ttl_sec: int
    pin: str | None = None
    lease_id: str | None = None


class VideoStatusResponse(BaseModel):
    streaming: bool
    rtp_packets: int
    bytes: int
    idle_sec: float | None = None
    sn: str
    last_rtp_at: float | int | str | None = None
    last_activity: float | int | str | None = None
    transport_connected: bool | None = None
    fresh_rtp: bool | None = None
    media_state: str | None = None


_mountpoint_pins: dict[int, dict[str, Any]] = {}


def get_or_create_mountpoint_pin(
    mountpoint_id: int,
    stream_instance_id: str | None = None,
    lease_id: str | None = None,
    owner_user_id: str | None = None,
) -> str:
    # Deterministic PIN derived from secret + device_id
    # Survives restarts, matches Janus mountpoint PIN
    from app.config import settings

    secret = settings.l4media_effective_token or "l4media-default-secret"
    pin = hashlib.sha256(f"{secret}:{mountpoint_id}".encode()).hexdigest()[:16]

    _mountpoint_pins.setdefault(mountpoint_id, {})
    cached = _mountpoint_pins[mountpoint_id]
    cached["pin"] = pin
    if lease_id and not cached.get("lease_id"):
        cached["lease_id"] = lease_id
    if stream_instance_id and not cached.get("stream_instance_id"):
        cached["stream_instance_id"] = stream_instance_id
    if owner_user_id and not cached.get("owner_user_id"):
        cached["owner_user_id"] = owner_user_id
    cached.setdefault("created_at", time.time())
    return pin


def set_mountpoint_stream_instance(mountpoint_id: int, stream_instance_id: str) -> None:
    if mountpoint_id in _mountpoint_pins:
        _mountpoint_pins[mountpoint_id]["stream_instance_id"] = stream_instance_id


def clear_mountpoint_pin(
    mountpoint_id: int,
    lease_id: str | None = None,
    stream_instance_id: str | None = None,
) -> None:
    cached = _mountpoint_pins.get(mountpoint_id)
    if not cached:
        return
    if lease_id is not None:
        if cached.get("lease_id") == lease_id:
            _mountpoint_pins.pop(mountpoint_id, None)
        return
    if stream_instance_id is not None:
        if cached.get("stream_instance_id") == stream_instance_id:
            _mountpoint_pins.pop(mountpoint_id, None)
        return
    _mountpoint_pins.pop(mountpoint_id, None)


def get_device_ports(device_id: int) -> tuple[int, int]:
    # NOTE (Alpha): Collisions are possible with (% VIDEO_PORT_SLOTS) in alpha stage
    slot = device_id % settings.video_port_slots
    rtp_port = settings.video_port_base + 2 * slot
    rtcp_port = rtp_port + 1
    return rtp_port, rtcp_port


async def _verify_device_access(
    device_id: int, user: dict[str, Any], db: AsyncSession
) -> Terminal:
    terminal = await db.scalar(select(Terminal).where(Terminal.device_id == device_id))
    if not terminal:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail=f"Устройство {device_id} не найдено",
        )

    org_id = resolve_org_id(user)
    is_su = bool(user.get("is_superuser", False))
    if not is_su and terminal.org_id != org_id:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail=f"Доступ к устройству {device_id} запрещен",
        )
    return terminal


@router.post(
    "/devices/{device_id}/session",
    response_model=VideoSessionResponse,
    status_code=status.HTTP_200_OK,
)
async def create_video_session(
    device_id: int,
    user: dict[str, Any] = Depends(require_permission(PERMISSION_VIDEO_VIEW)),
    db: AsyncSession = Depends(get_db),
) -> VideoSessionResponse:
    from app.services.remote_session_use_case import RemoteSessionUseCase

    use_case = RemoteSessionUseCase(db)
    result = await use_case.start_session(
        device_id=device_id,
        session_type="video",
        user=user,
        start_terminal_stream=False,
    )
    await db.commit()
    return VideoSessionResponse(
        mountpoint_id=result.mountpoint_id or device_id,
        sn=result.sn,
        janus_ws=result.janus_ws or "/janus-ws",
        session_ttl_sec=result.ttl_sec,
        pin=result.pin,
        lease_id=result.lease_id if isinstance(result.lease_id, str) else None,
    )


@router.delete(
    "/devices/{device_id}/session",
    response_model=dict[str, Any],
    status_code=status.HTTP_200_OK,
)
async def stop_video_session(
    device_id: int,
    user: dict[str, Any] = Depends(require_permission(PERMISSION_VIDEO_VIEW)),
    db: AsyncSession = Depends(get_db),
) -> dict[str, Any]:
    """Force close an active video session on device."""
    from app.services.remote_session_use_case import RemoteSessionUseCase

    use_case = RemoteSessionUseCase(db)
    res = await use_case.stop_session(
        device_id=device_id,
        reason="operator_closed",
        user=user,
    )
    await db.commit()
    return res


@router.get(
    "/devices/{device_id}/session/status",
    response_model=VideoStatusResponse,
    status_code=status.HTTP_200_OK,
)
async def get_video_session_status(
    device_id: int,
    user: dict[str, Any] = Depends(require_permission(PERMISSION_VIDEO_VIEW)),
    db: AsyncSession = Depends(get_db),
) -> VideoStatusResponse:
    from app.services.remote_session_use_case import RemoteSessionUseCase

    use_case = RemoteSessionUseCase(db)
    result = await use_case.get_session_status(device_id, user)
    return VideoStatusResponse(
        streaming=result.streaming,
        rtp_packets=result.rtp_packets,
        bytes=result.bytes,
        idle_sec=result.idle_sec,
        sn=result.sn,
        last_rtp_at=None,
        last_activity=None,
        transport_connected=result.transport_connected,
        fresh_rtp=result.fresh_rtp,
        media_state=result.media_state,
    )
