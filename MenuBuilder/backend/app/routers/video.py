import contextlib
import logging
import secrets
import uuid
from typing import Any

import httpx
from fastapi import APIRouter, Depends, HTTPException, status
from pydantic import BaseModel
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import resolve_org_id
from app.config import settings
from app.database import get_db
from app.models import Terminal
from app.security.permissions import PERMISSION_VIDEO_VIEW, require_permission
from app.services.iot_client import iot_client

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/v1/video", tags=["video"])


class VideoSessionResponse(BaseModel):
    mountpoint_id: int
    sn: str
    janus_ws: str
    session_ttl_sec: int
    pin: str | None = None


class VideoStatusResponse(BaseModel):
    streaming: bool
    rtp_packets: int
    bytes: int
    idle_sec: float | None = None
    sn: str


_mountpoint_pins: dict[int, dict[str, Any]] = {}


def get_or_create_mountpoint_pin(
    mountpoint_id: int, stream_instance_id: str | None = None
) -> str:
    cached = _mountpoint_pins.get(mountpoint_id)
    if (
        cached
        and cached.get("stream_instance_id") == stream_instance_id
        and cached.get("pin")
    ):
        return str(cached["pin"])
    new_pin = secrets.token_hex(8)
    _mountpoint_pins[mountpoint_id] = {
        "pin": new_pin,
        "stream_instance_id": stream_instance_id,
    }
    return new_pin


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


async def _ensure_ingress_route(sn: str, rtp_port: int, rtcp_port: int) -> None:
    url = f"{settings.l4media_ingress_url.rstrip('/')}/routes/{sn}"
    try:
        async with httpx.AsyncClient(timeout=5.0) as client:
            resp = await client.put(url, params={"rtp": rtp_port, "rtcp": rtcp_port})
            if resp.status_code >= 400:
                logger.error(
                    "l4media-ingress route error for %s: %d %s",
                    sn,
                    resp.status_code,
                    resp.text,
                )
                raise HTTPException(
                    status_code=status.HTTP_502_BAD_GATEWAY,
                    detail=f"Ошибка настройки маршрута ingress: {resp.text}",
                )
    except httpx.RequestError as exc:
        logger.error("Failed to connect to l4media-ingress at %s: %s", url, exc)
        raise HTTPException(
            status_code=status.HTTP_502_BAD_GATEWAY,
            detail=f"Недоступен сервис l4media-ingress: {exc}",
        ) from exc


async def _ensure_janus_mountpoint(
    mountpoint_id: int,
    device_id: int,
    rtp_port: int,
    rtcp_port: int,
    pin: str | None = None,
) -> None:
    janus_url = settings.l4media_janus_url.rstrip("/")
    try:
        async with httpx.AsyncClient(timeout=5.0) as client:
            tx1 = uuid.uuid4().hex
            resp1 = await client.post(
                janus_url, json={"janus": "create", "transaction": tx1}
            )
            if resp1.status_code != 200:
                logger.error(
                    "Janus create session error: %d %s", resp1.status_code, resp1.text
                )
                raise HTTPException(
                    status_code=status.HTTP_502_BAD_GATEWAY,
                    detail=f"Ошибка создания сессии Janus: {resp1.text}",
                )
            data1 = resp1.json()
            session_id = data1.get("data", {}).get("id")
            if not session_id:
                raise HTTPException(
                    status_code=status.HTTP_502_BAD_GATEWAY,
                    detail="Не получен session_id от Janus",
                )

            try:
                tx2 = uuid.uuid4().hex
                resp2 = await client.post(
                    f"{janus_url}/{session_id}",
                    json={
                        "janus": "attach",
                        "plugin": "janus.plugin.streaming",
                        "transaction": tx2,
                    },
                )
                if resp2.status_code != 200:
                    logger.error(
                        "Janus attach plugin error: %d %s",
                        resp2.status_code,
                        resp2.text,
                    )
                    raise HTTPException(
                        status_code=status.HTTP_502_BAD_GATEWAY,
                        detail=f"Ошибка подключения плагина Janus streaming: {resp2.text}",
                    )
                data2 = resp2.json()
                handle_id = data2.get("data", {}).get("id")
                if not handle_id:
                    raise HTTPException(
                        status_code=status.HTTP_502_BAD_GATEWAY,
                        detail="Не получен handle_id плагина Janus streaming",
                    )

                tx3 = uuid.uuid4().hex
                req_body: dict[str, Any] = {
                    "request": "create",
                    "type": "rtp",
                    "id": mountpoint_id,
                    "name": f"device-{device_id}",
                    "description": f"Camera stream for device {device_id}",
                    "video": True,
                    "audio": False,
                    "videoport": rtp_port,
                    "videortcpport": rtcp_port,
                    "videopt": 96,
                    "videocodec": "h264",
                    "videofmtp": "profile-level-id=42e01f;packetization-mode=1",
                }
                if pin:
                    req_body["pin"] = pin
                resp3 = await client.post(
                    f"{janus_url}/{session_id}/{handle_id}",
                    json={"janus": "message", "transaction": tx3, "body": req_body},
                )
                if resp3.status_code != 200:
                    logger.error(
                        "Janus create mountpoint error: %d %s",
                        resp3.status_code,
                        resp3.text,
                    )
                    raise HTTPException(
                        status_code=status.HTTP_502_BAD_GATEWAY,
                        detail=f"Ошибка вызова Janus create mountpoint: {resp3.text}",
                    )
                data3 = resp3.json()
                plugindata = data3.get("plugindata", {}).get("data", {})
                err_code = plugindata.get("error_code") if plugindata else None
                err_str = str(plugindata.get("error", "")) if plugindata else ""
                if data3.get("janus") == "error":
                    err_code = data3.get("error", {}).get("code")
                    err_reason = str(data3.get("error", {}).get("reason", ""))
                    if (
                        "already exists" not in err_reason.lower()
                        and "occupied" not in err_reason.lower()
                    ):
                        raise HTTPException(
                            status_code=status.HTTP_502_BAD_GATEWAY,
                            detail=f"Janus error ({err_code}): {err_reason}",
                        )
                elif err_code or err_str:
                    is_already_exists = (
                        err_code == 456
                        or "already exists" in err_str.lower()
                        or "occupied" in err_str.lower()
                    )
                    if not is_already_exists:
                        logger.error(
                            "Janus streaming plugin error: %s (%s)", err_str, err_code
                        )
                        raise HTTPException(
                            status_code=status.HTTP_502_BAD_GATEWAY,
                            detail=f"Janus plugin error: {err_str}",
                        )
                    if pin:
                        logger.info(
                            "Mountpoint %d already exists; recreating with pin",
                            mountpoint_id,
                        )
                        with contextlib.suppress(Exception):
                            tx_dest_mp = uuid.uuid4().hex
                            await client.post(
                                f"{janus_url}/{session_id}/{handle_id}",
                                json={
                                    "janus": "message",
                                    "transaction": tx_dest_mp,
                                    "body": {"request": "destroy", "id": mountpoint_id},
                                },
                            )
                            tx_recreate = uuid.uuid4().hex
                            await client.post(
                                f"{janus_url}/{session_id}/{handle_id}",
                                json={
                                    "janus": "message",
                                    "transaction": tx_recreate,
                                    "body": req_body,
                                },
                            )
                    else:
                        logger.info(
                            "Mountpoint %d already exists in Janus, reusing",
                            mountpoint_id,
                        )
            finally:
                with contextlib.suppress(Exception):
                    tx_dest = uuid.uuid4().hex
                    await client.post(
                        f"{janus_url}/{session_id}",
                        json={"janus": "destroy", "transaction": tx_dest},
                    )
    except httpx.RequestError as exc:
        logger.error("Failed to connect to l4media-janus at %s: %s", janus_url, exc)
        raise HTTPException(
            status_code=status.HTTP_502_BAD_GATEWAY,
            detail=f"Недоступен сервис l4media-janus: {exc}",
        ) from exc


async def _get_ingress_status(sn: str) -> dict[str, Any]:
    url = f"{settings.l4media_ingress_url.rstrip('/')}/stats"
    try:
        async with httpx.AsyncClient(timeout=5.0) as client:
            resp = await client.get(url)
            if resp.status_code != 200:
                logger.error(
                    "l4media-ingress stats error: %d %s", resp.status_code, resp.text
                )
                raise HTTPException(
                    status_code=status.HTTP_502_BAD_GATEWAY,
                    detail=f"Ошибка получения статистики ingress: {resp.text}",
                )
            data = resp.json()
    except httpx.RequestError as exc:
        logger.error("Failed to connect to l4media-ingress stats at %s: %s", url, exc)
        raise HTTPException(
            status_code=status.HTTP_502_BAD_GATEWAY,
            detail=f"Недоступен сервис l4media-ingress: {exc}",
        ) from exc

    raw_sessions = data.get("sessions", [])
    matching_sessions: list[dict[str, Any]] = []
    if isinstance(raw_sessions, list):
        matching_sessions = [
            s for s in raw_sessions if isinstance(s, dict) and s.get("sn") == sn
        ]
    elif isinstance(raw_sessions, dict) and sn in raw_sessions:
        matching_sessions = [raw_sessions[sn]]

    if not matching_sessions:
        return {
            "streaming": False,
            "rtp_packets": 0,
            "bytes": 0,
            "idle_sec": None,
            "sn": sn,
        }

    best = min(
        matching_sessions,
        key=lambda s: (
            s.get("idle_sec") is None,
            s.get("idle_sec") if s.get("idle_sec") is not None else 999999,
        ),
    )
    idle = best.get("idle_sec")
    rtp_pkts = int(best.get("rtp_packets", 0))
    bytes_count = int(best.get("bytes", 0))
    is_streaming = (idle is not None and idle <= 10 and rtp_pkts > 0) or (
        best.get("state") == "streaming" and idle is not None and idle <= 10
    )

    return {
        "streaming": bool(is_streaming),
        "rtp_packets": rtp_pkts,
        "bytes": bytes_count,
        "idle_sec": float(idle) if idle is not None else None,
        "sn": sn,
    }


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
    terminal = await _verify_device_access(device_id, user, db)
    org_id = terminal.org_id if user.get("is_superuser") else resolve_org_id(user)

    # Verify that caller holds active lease on app1
    status_data = await iot_client.remote_input_status(
        terminal.sn, org_id=org_id, user=user
    )
    lease_info = status_data.get("lease") or {}
    if not lease_info.get("active"):
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Требуется активная аренда терминала для подключения к видеопотоку",
        )

    # Check lease ownership
    owner_user_id = str(lease_info.get("owner_user_id") or "")
    user_sub = str(user.get("sub") or "")
    user_id_str = str(user.get("user_id") or "")
    role_id = int(user.get("role_id", 3))
    is_su = bool(
        user.get("is_superuser", False)
        or user.get("role") in ("superuser", "admin")
        or role_id == 1
    )

    if not is_su and owner_user_id not in (user_sub, user_id_str):
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Подключение к видеосессии доступно только держателю активной аренды",
        )

    lease_scope = lease_info.get("scope")
    if role_id == 4:
        if lease_scope != "view":
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail="Для роли наблюдателя требуется аренда с уровнем view",
            )
    elif lease_scope not in ("view", "stream", "input", "console"):
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Недостаточный уровень аренды для подключения к видеопотоку",
        )

    stream_instance_id = (
        str(lease_info.get("stream_instance_id"))
        if lease_info.get("stream_instance_id")
        else None
    )
    pin = get_or_create_mountpoint_pin(device_id, stream_instance_id)

    rtp_port, rtcp_port = get_device_ports(device_id)
    mountpoint_id = device_id

    await _ensure_ingress_route(terminal.sn, rtp_port, rtcp_port)
    await _ensure_janus_mountpoint(
        mountpoint_id, device_id, rtp_port, rtcp_port, pin=pin
    )

    return VideoSessionResponse(
        mountpoint_id=mountpoint_id,
        sn=terminal.sn,
        janus_ws="/janus-ws",
        session_ttl_sec=600,
        pin=pin,
    )


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
    terminal = await _verify_device_access(device_id, user, db)
    stats = await _get_ingress_status(terminal.sn)
    return VideoStatusResponse(**stats)
