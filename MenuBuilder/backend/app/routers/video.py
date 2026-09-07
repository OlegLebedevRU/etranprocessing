import contextlib
import logging
import uuid
from typing import Any

import httpx
from fastapi import APIRouter, Depends, HTTPException, status
from pydantic import BaseModel
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import require_tenant_context, resolve_org_id
from app.config import settings
from app.database import get_db
from app.models import Terminal

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/v1/video", tags=["video"])


class VideoSessionResponse(BaseModel):
    mountpoint_id: int
    sn: str
    janus_ws: str
    session_ttl_sec: int


class VideoStatusResponse(BaseModel):
    streaming: bool
    rtp_packets: int
    bytes: int
    idle_sec: float | None = None
    sn: str


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
    mountpoint_id: int, device_id: int, rtp_port: int, rtcp_port: int
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
                req_body = {
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
                elif plugindata.get("error_code") or plugindata.get("error"):
                    err_code = plugindata.get("error_code")
                    err_str = str(plugindata.get("error", ""))
                    if (
                        err_code != 456
                        and "already exists" not in err_str.lower()
                        and "occupied" not in err_str.lower()
                    ):
                        logger.error(
                            "Janus streaming plugin error: %s (%s)", err_str, err_code
                        )
                        raise HTTPException(
                            status_code=status.HTTP_502_BAD_GATEWAY,
                            detail=f"Janus plugin error: {err_str}",
                        )
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
    user: dict[str, Any] = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
) -> VideoSessionResponse:
    terminal = await _verify_device_access(device_id, user, db)
    rtp_port, rtcp_port = get_device_ports(device_id)
    mountpoint_id = device_id

    await _ensure_ingress_route(terminal.sn, rtp_port, rtcp_port)
    await _ensure_janus_mountpoint(mountpoint_id, device_id, rtp_port, rtcp_port)

    return VideoSessionResponse(
        mountpoint_id=mountpoint_id,
        sn=terminal.sn,
        janus_ws="/janus-ws",
        session_ttl_sec=600,
    )


@router.get(
    "/devices/{device_id}/session/status",
    response_model=VideoStatusResponse,
    status_code=status.HTTP_200_OK,
)
async def get_video_session_status(
    device_id: int,
    user: dict[str, Any] = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
) -> VideoStatusResponse:
    terminal = await _verify_device_access(device_id, user, db)
    stats = await _get_ingress_status(terminal.sn)
    return VideoStatusResponse(**stats)
