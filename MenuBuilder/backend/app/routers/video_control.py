import asyncio
import contextlib
import json
import logging
from typing import Any, Literal

import websockets
import websockets.exceptions
from fastapi import (
    APIRouter,
    Depends,
    HTTPException,
    Query,
    Response,
    WebSocket,
    WebSocketDisconnect,
    status,
)
from pydantic import BaseModel, ConfigDict, Field
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import (
    decode_token,
    get_current_user,
    require_tenant_context,
    resolve_org_id,
)
from app.config import settings
from app.database import async_session, get_db
from app.routers.video import _get_ingress_status, _verify_device_access
from app.security.permissions import (
    ALL_PERMISSIONS,
    PERMISSION_VIDEO_VIEW,
    require_permission,
)
from app.services.iot_client import iot_client

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/v1/video", tags=["video-control"])


# ---------------------------------------------------------------------------
# Models
# ---------------------------------------------------------------------------


class ControlStatusAgent(BaseModel):
    online: bool = False
    desktop_available: bool = False
    screen: dict[str, Any] | None = None
    last_seen_at: str | None = None
    stale: bool = False
    inventory: dict[str, Any] | None = None
    stream: dict[str, Any] | None = None


class ControlStatusLease(BaseModel):
    active: bool = False
    mine: bool = False
    lease_id: str | None = None
    scope: str | None = None
    owner_role: str | None = None
    owner_masked: str | None = None
    owner_user_id: str | None = None
    expires_at: str | None = None
    stream_instance_id: str | None = None
    selected_desktop_id: str | None = None


class ControlStatusResponse(BaseModel):
    agent: ControlStatusAgent
    lease: ControlStatusLease


class LeaseAcquireRequest(BaseModel):
    scope: str = "input"
    ttl_sec: int | None = None
    session_id: str | None = None


class ScopeUpgradeRequest(BaseModel):
    scope: str


class ControlLeaseResponse(BaseModel):
    lease_id: str
    expires_at: str
    keepalive_sec: int
    ws_path: str
    scope: str | None = None
    owner_role: str | None = None
    owner_masked: str | None = None
    owner_user_id: str | None = None
    owner_session_id: str | None = None


class KeepaliveRequest(BaseModel):
    lease_id: str


class StreamStartRequest(BaseModel):
    mode: Literal["desktop", "usb-camera"]
    source_id: str
    profile: str = "default"
    lease_id: str | None = None


class StreamStopRequest(BaseModel):
    lease_id: str | None = None


class StreamStartResponse(BaseModel):
    stream_instance_id: str
    result: str
    state: str | None = None


class StreamStopResponse(BaseModel):
    result: str


class StreamStateResponse(BaseModel):
    sn: str
    stream: dict[str, Any] | None = None
    ingress: dict[str, Any] | None = None


class ControlEventRequest(BaseModel):
    lease_id: str
    type: Literal["pointer_move", "mouse_click", "key"]
    x: int | None = Field(default=None, ge=0, le=65535)
    y: int | None = Field(default=None, ge=0, le=65535)
    button: Literal["left"] = "left"
    client_ref: str | None = Field(default=None, max_length=64)
    kind: Literal["down", "up", "press"] | None = None
    vk: int | None = Field(default=None, ge=0, le=255)
    text: str | None = Field(default=None, max_length=32)

    model_config = ConfigDict(extra="forbid")


class WsInboundMove(BaseModel):
    type: Literal["pointer_move"]
    x: int = Field(ge=0, le=65535)
    y: int = Field(ge=0, le=65535)

    model_config = ConfigDict(extra="forbid")


class WsInboundClick(BaseModel):
    type: Literal["mouse_click"]
    x: int = Field(ge=0, le=65535)
    y: int = Field(ge=0, le=65535)
    button: Literal["left"] = "left"
    client_ref: str | None = Field(default=None, max_length=64)

    model_config = ConfigDict(extra="forbid")


class WsInboundKey(BaseModel):
    type: Literal["key"]
    kind: Literal["down", "up", "press"]
    vk: int = Field(ge=0, le=255)
    text: str | None = Field(default=None, max_length=32)
    client_ref: str | None = Field(default=None, max_length=64)

    model_config = ConfigDict(extra="forbid")


class WsInboundKeepalive(BaseModel):
    type: Literal["keepalive"]

    model_config = ConfigDict(extra="forbid")


class WsInboundRelease(BaseModel):
    type: Literal["release"]

    model_config = ConfigDict(extra="forbid")


# ---------------------------------------------------------------------------
# Dependencies & Helpers
# ---------------------------------------------------------------------------


async def require_operator_user(
    user: dict[str, Any] = Depends(require_tenant_context),
) -> dict[str, Any]:
    """Dependency: require active tenant context and operator role (1: superuser, 2: admin, 3: user). Blocks role 4."""
    role_id = user.get("role_id")
    is_su = bool(
        user.get("is_superuser", False)
        or user.get("role") in ("superuser", "admin")
        or role_id == 1
    )
    if not is_su and role_id not in (1, 2, 3):
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Управление доступно только операторам",
        )
    return user


async def require_remote_control_user(
    user: dict[str, Any] = Depends(require_tenant_context),
) -> dict[str, Any]:
    """Legacy alias for require_operator_user."""
    return await require_operator_user(user)


async def get_ws_user(websocket: WebSocket) -> dict[str, Any] | None:
    """Authenticate WebSocket user from cookie, Authorization header, or query param."""
    # Support test overrides
    app_instance = getattr(websocket, "app", None)
    if app_instance and hasattr(app_instance, "dependency_overrides"):
        override = app_instance.dependency_overrides.get(get_current_user)
        if override:
            res = override()
            user_override = await res if hasattr(res, "__await__") else res
            if user_override:
                return user_override

    token: str | None = None
    if "accessToken" in websocket.cookies:
        token = websocket.cookies["accessToken"]
    elif "authorization" in websocket.headers:
        auth_hdr = websocket.headers["authorization"]
        if auth_hdr.lower().startswith("bearer "):
            token = auth_hdr[7:].strip()
    elif "token" in websocket.query_params:
        token = websocket.query_params["token"]

    if not token:
        nginx_user_id = websocket.headers.get("x-user-id")
        nginx_org_id = websocket.headers.get("x-org-id")
        nginx_role = websocket.headers.get("x-user-role")
        if settings.trust_proxy_identity_headers and nginx_user_id:
            return {
                "sub": nginx_user_id,
                "user_id": int(nginx_user_id) if nginx_user_id.isdigit() else 0,
                "org_id": int(nginx_org_id)
                if nginx_org_id and nginx_org_id.isdigit()
                else 0,
                "role_id": 3,
                "role": nginx_role or "user",
                "is_superuser": False,
                "session_id": f"sess-{nginx_user_id}",
                "permissions": list(ALL_PERMISSIONS),
            }
        return None

    try:
        payload = decode_token(token)
    except Exception:  # noqa: BLE001
        return None

    raw_user_id = payload.get("userId") or payload.get("user_id") or payload.get("sub")
    raw_org_id = payload.get("orgId") or payload.get("org_id") or payload.get("org")
    raw_role_id = payload.get("roleId") or payload.get("role_id")
    role = str(payload.get("role", "user")).lower()

    try:
        user_id = (
            int(raw_user_id)
            if raw_user_id is not None and str(raw_user_id).isdigit()
            else 0
        )
    except TypeError, ValueError:
        user_id = 0

    try:
        org_id = (
            int(raw_org_id)
            if raw_org_id is not None and str(raw_org_id).isdigit()
            else None
        )
    except TypeError, ValueError:
        org_id = None

    try:
        role_id = (
            int(raw_role_id)
            if raw_role_id is not None and str(raw_role_id).isdigit()
            else 3
        )
    except TypeError, ValueError:
        role_id = 3

    role_names = {1: "superuser", 2: "admin", 3: "user", 4: "viewer"}
    if role.isdigit():
        role = role_names.get(int(role), "user")

    if role == "viewer" or role_id == 4:
        role = "viewer"
        role_id = 4

    username = payload.get("username") or payload.get("sub") or str(user_id)
    is_su = bool(
        (payload.get("is_superuser") or role in ("superuser", "admin") or role_id == 1)
        and role_id != 4
    )
    if is_su and role not in ("superuser", "admin"):
        role = "superuser"

    user_perms = list(payload.get("permissions") or [])
    if role_id in (1, 2, 3) or is_su:
        user_perms = list(ALL_PERMISSIONS)

    session_id = str(
        payload.get("session_id")
        or payload.get("sid")
        or payload.get("jti")
        or f"sess-{user_id}"
    )

    return {
        "sub": str(payload.get("sub") or username or user_id),
        "user_id": user_id,
        "username": username,
        "org_id": org_id,
        "role_id": role_id,
        "role": role,
        "is_superuser": is_su,
        "session_id": session_id,
        "permissions": user_perms,
    }


# ---------------------------------------------------------------------------
# REST Endpoints
# ---------------------------------------------------------------------------


@router.get(
    "/devices/{device_id}/control/status",
    response_model=ControlStatusResponse,
)
async def get_device_control_status(
    device_id: int,
    user: dict[str, Any] = Depends(require_permission(PERMISSION_VIDEO_VIEW)),
    db: AsyncSession = Depends(get_db),
) -> ControlStatusResponse:
    """Fetch terminal remote control status (agent presence, inventory, stream & active lease). Accessible to roles 1-3 and role 4 with video:view."""
    terminal = await _verify_device_access(device_id, user, db)
    org_id = terminal.org_id if user.get("is_superuser") else resolve_org_id(user)
    raw = await iot_client.remote_input_status(
        sn=terminal.sn,
        org_id=org_id,
        user=user,
    )
    agent_data = raw.get("agent") or {}
    lease_data = raw.get("lease") or {}
    owner_user_id = lease_data.get("owner_user_id")
    current_sub = str(user.get("sub", ""))
    user_id_str = str(user.get("user_id", ""))
    mine = bool(
        lease_data.get("active")
        and owner_user_id
        and (str(owner_user_id) == current_sub or str(owner_user_id) == user_id_str)
    )

    return ControlStatusResponse(
        agent=ControlStatusAgent(
            online=bool(agent_data.get("online", False)),
            desktop_available=bool(agent_data.get("desktop_available", False)),
            screen=agent_data.get("screen"),
            last_seen_at=agent_data.get("last_seen_at"),
            stale=bool(agent_data.get("stale", False)),
            inventory=agent_data.get("inventory"),
            stream=agent_data.get("stream"),
        ),
        lease=ControlStatusLease(
            active=bool(lease_data.get("active", False)),
            mine=mine,
            lease_id=str(lease_data.get("lease_id"))
            if lease_data.get("lease_id")
            else None,
            scope=lease_data.get("scope"),
            owner_role=lease_data.get("owner_role"),
            owner_masked=lease_data.get("owner_masked"),
            owner_user_id=str(owner_user_id) if owner_user_id else None,
            expires_at=lease_data.get("expires_at"),
            stream_instance_id=str(lease_data.get("stream_instance_id"))
            if lease_data.get("stream_instance_id")
            else None,
            selected_desktop_id=lease_data.get("selected_desktop_id"),
        ),
    )


@router.post(
    "/devices/{device_id}/control/lease",
    response_model=ControlLeaseResponse,
    status_code=status.HTTP_201_CREATED,
)
async def acquire_device_control_lease(
    device_id: int,
    body: LeaseAcquireRequest | None = None,
    user: dict[str, Any] = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
) -> ControlLeaseResponse:
    """Acquire exclusive lease for terminal according to role matrix."""
    scope = body.scope if body and body.scope else "input"
    ttl_sec = body.ttl_sec if body else None

    role_id = int(user.get("role_id", 3))
    is_strictly_superuser = bool(role_id == 1 or user.get("role") == "superuser")
    is_operator = bool(
        is_strictly_superuser
        or role_id in (1, 2, 3)
        or user.get("role") in ("superuser", "admin", "user")
    )

    if scope == "console":
        if not is_strictly_superuser:
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail="Доступ к консоли разрешён только суперадминистраторам",
            )
    elif scope in ("input", "stream"):
        if not is_operator:
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail="Управление доступно только операторам",
            )
    elif scope == "view":
        if role_id == 4:
            user_permissions = user.get("permissions") or []
            if (
                PERMISSION_VIDEO_VIEW not in user_permissions
                and "*" not in user_permissions
            ):
                raise HTTPException(
                    status_code=status.HTTP_403_FORBIDDEN,
                    detail="Доступ к данному разделу не предоставлен",
                )
    else:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail=f"Недопустимый уровень аренды: {scope}",
        )

    terminal = await _verify_device_access(device_id, user, db)
    org_id = terminal.org_id if user.get("is_superuser") else resolve_org_id(user)
    custom_user = dict(user)
    if body and body.session_id:
        custom_user["session_id"] = body.session_id

    res = await iot_client.remote_input_acquire_lease(
        sn=terminal.sn,
        scope=scope,
        ttl_sec=ttl_sec,
        org_id=org_id,
        user=custom_user,
    )
    lease_id = str(res["lease_id"])
    return ControlLeaseResponse(
        lease_id=lease_id,
        expires_at=str(res["expires_at"]),
        keepalive_sec=res.get("keepalive_sec", 15),
        ws_path=f"/api/v1/video/devices/{device_id}/control/ws/{lease_id}",
        scope=res.get("scope", scope),
        owner_role=res.get("owner_role"),
        owner_masked=res.get("owner_masked"),
        owner_user_id=str(res.get("owner_user_id"))
        if res.get("owner_user_id")
        else None,
        owner_session_id=str(res.get("owner_session_id"))
        if res.get("owner_session_id")
        else None,
    )


@router.post(
    "/devices/{device_id}/control/scope",
    response_model=ControlLeaseResponse,
    status_code=status.HTTP_200_OK,
)
async def change_device_control_scope(
    device_id: int,
    body: ScopeUpgradeRequest,
    user: dict[str, Any] = Depends(require_operator_user),
    db: AsyncSession = Depends(get_db),
) -> ControlLeaseResponse:
    """Change lease scope on app1."""
    terminal = await _verify_device_access(device_id, user, db)
    org_id = terminal.org_id if user.get("is_superuser") else resolve_org_id(user)

    role_id = int(user.get("role_id", 3))
    is_strictly_superuser = bool(role_id == 1 or user.get("role") == "superuser")
    if body.scope == "console" and not is_strictly_superuser:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Доступ к консоли разрешён только суперадминистраторам",
        )

    status_data = await iot_client.remote_input_status(
        terminal.sn, org_id=org_id, user=user
    )
    lease_info = status_data.get("lease") or {}
    if not lease_info.get("active") or not lease_info.get("lease_id"):
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Нет активной аренды для изменения scope",
        )
    lease_id = str(lease_info["lease_id"])

    res = await iot_client.remote_input_change_scope(
        lease_id=lease_id,
        scope=body.scope,
        org_id=org_id,
        user=user,
    )
    return ControlLeaseResponse(
        lease_id=lease_id,
        expires_at=str(res.get("expires_at", "")),
        keepalive_sec=res.get("keepalive_sec", 15),
        ws_path=f"/api/v1/video/devices/{device_id}/control/ws/{lease_id}",
        scope=res.get("scope", body.scope),
        owner_role=res.get("owner_role"),
        owner_masked=res.get("owner_masked"),
        owner_user_id=str(res.get("owner_user_id"))
        if res.get("owner_user_id")
        else None,
        owner_session_id=str(res.get("owner_session_id"))
        if res.get("owner_session_id")
        else None,
    )


@router.get("/devices/{device_id}/inventory")
async def get_device_inventory(
    device_id: int,
    refresh: int = Query(0, ge=0, le=1),
    user: dict[str, Any] = Depends(require_permission(PERMISSION_VIDEO_VIEW)),
    db: AsyncSession = Depends(get_db),
) -> dict[str, Any]:
    """Fetch terminal hardware/display/camera inventory."""
    terminal = await _verify_device_access(device_id, user, db)
    org_id = terminal.org_id if user.get("is_superuser") else resolve_org_id(user)
    return await iot_client.remote_input_inventory(
        sn=terminal.sn,
        refresh=refresh,
        org_id=org_id,
        user=user,
    )


@router.post(
    "/devices/{device_id}/stream/start",
    response_model=StreamStartResponse,
    status_code=status.HTTP_200_OK,
)
async def start_device_stream(
    device_id: int,
    body: StreamStartRequest,
    user: dict[str, Any] = Depends(require_operator_user),
    db: AsyncSession = Depends(get_db),
) -> StreamStartResponse:
    """Start or switch media stream on terminal (requires stream lease)."""
    terminal = await _verify_device_access(device_id, user, db)
    org_id = terminal.org_id if user.get("is_superuser") else resolve_org_id(user)

    lease_id = body.lease_id
    if not lease_id:
        status_data = await iot_client.remote_input_status(
            terminal.sn, org_id=org_id, user=user
        )
        lease_info = status_data.get("lease") or {}
        if not lease_info.get("active") or not lease_info.get("lease_id"):
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail="Для запуска трансляции требуется активная аренда",
            )
        lease_id = str(lease_info["lease_id"])

    res = await iot_client.remote_input_stream_start(
        lease_id=lease_id,
        mode=body.mode,
        source_id=body.source_id,
        profile=body.profile,
        org_id=org_id,
        user=user,
    )
    return StreamStartResponse(
        stream_instance_id=str(res.get("stream_instance_id", "")),
        result=str(res.get("result", "")),
        state=res.get("state"),
    )


@router.post(
    "/devices/{device_id}/stream/stop",
    response_model=StreamStopResponse,
    status_code=status.HTTP_200_OK,
)
async def stop_device_stream(
    device_id: int,
    body: StreamStopRequest | None = None,
    user: dict[str, Any] = Depends(require_operator_user),
    db: AsyncSession = Depends(get_db),
) -> StreamStopResponse:
    """Stop media stream on terminal."""
    terminal = await _verify_device_access(device_id, user, db)
    org_id = terminal.org_id if user.get("is_superuser") else resolve_org_id(user)

    lease_id = body.lease_id if body else None
    if not lease_id:
        status_data = await iot_client.remote_input_status(
            terminal.sn, org_id=org_id, user=user
        )
        lease_info = status_data.get("lease") or {}
        if not lease_info.get("active") or not lease_info.get("lease_id"):
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail="Для остановки трансляции требуется активная аренда",
            )
        lease_id = str(lease_info["lease_id"])

    res = await iot_client.remote_input_stream_stop(
        lease_id=lease_id,
        org_id=org_id,
        user=user,
    )
    return StreamStopResponse(result=str(res.get("result", "stopped")))


@router.get(
    "/devices/{device_id}/stream/state",
    response_model=StreamStateResponse,
    status_code=status.HTTP_200_OK,
)
async def get_device_stream_state(
    device_id: int,
    user: dict[str, Any] = Depends(require_permission(PERMISSION_VIDEO_VIEW)),
    db: AsyncSession = Depends(get_db),
) -> StreamStateResponse:
    """Get presence stream state and ingress RTP status."""
    terminal = await _verify_device_access(device_id, user, db)
    org_id = terminal.org_id if user.get("is_superuser") else resolve_org_id(user)
    status_data = await iot_client.remote_input_status(
        terminal.sn, org_id=org_id, user=user
    )
    stream_info = (status_data.get("agent") or {}).get("stream")
    ingress_stats = await _get_ingress_status(terminal.sn)
    return StreamStateResponse(
        sn=terminal.sn,
        stream=stream_info,
        ingress=ingress_stats,
    )


@router.post("/devices/{device_id}/control/keepalive")
async def keepalive_device_control_lease(
    device_id: int,
    body: KeepaliveRequest,
    user: dict[str, Any] = Depends(require_permission(PERMISSION_VIDEO_VIEW)),
    db: AsyncSession = Depends(get_db),
) -> dict[str, Any]:
    """Extend active control lease."""
    terminal = await _verify_device_access(device_id, user, db)
    org_id = terminal.org_id if user.get("is_superuser") else resolve_org_id(user)
    return await iot_client.remote_input_keepalive(
        lease_id=body.lease_id,
        org_id=org_id,
        user=user,
    )


@router.delete(
    "/devices/{device_id}/control/lease/{lease_id}",
    status_code=status.HTTP_204_NO_CONTENT,
)
async def release_device_control_lease(
    device_id: int,
    lease_id: str,
    user: dict[str, Any] = Depends(require_permission(PERMISSION_VIDEO_VIEW)),
    db: AsyncSession = Depends(get_db),
) -> None:
    """Release active control lease."""
    terminal = await _verify_device_access(device_id, user, db)
    org_id = terminal.org_id if user.get("is_superuser") else resolve_org_id(user)
    await iot_client.remote_input_release(
        lease_id=lease_id,
        org_id=org_id,
        user=user,
    )


@router.post("/devices/{device_id}/control/events")
async def send_device_control_event(
    device_id: int,
    event: ControlEventRequest,
    user: dict[str, Any] = Depends(require_operator_user),
    db: AsyncSession = Depends(get_db),
) -> Any:
    """REST fallback for sending pointer movement, mouse clicks, or keyboard events."""
    terminal = await _verify_device_access(device_id, user, db)
    org_id = terminal.org_id if user.get("is_superuser") else resolve_org_id(user)
    if event.type == "pointer_move":
        if event.x is None or event.y is None:
            raise HTTPException(
                status_code=422, detail="x and y are required for pointer_move"
            )
        await iot_client.remote_input_move(
            lease_id=event.lease_id,
            x=event.x,
            y=event.y,
            org_id=org_id,
            user=user,
        )
        return Response(status_code=status.HTTP_202_ACCEPTED)
    elif event.type == "mouse_click":
        if event.x is None or event.y is None:
            raise HTTPException(
                status_code=422, detail="x and y are required for mouse_click"
            )
        return await iot_client.remote_input_click(
            lease_id=event.lease_id,
            x=event.x,
            y=event.y,
            client_ref=event.client_ref,
            org_id=org_id,
            user=user,
        )
    elif event.type == "key":
        if event.kind is None or event.vk is None:
            raise HTTPException(
                status_code=422, detail="kind and vk are required for key event"
            )
        return await iot_client.remote_input_key(
            lease_id=event.lease_id,
            kind=event.kind,
            vk=event.vk,
            text=event.text,
            client_ref=event.client_ref,
            org_id=org_id,
            user=user,
        )


# ---------------------------------------------------------------------------
# WebSocket Proxy Endpoint
# ---------------------------------------------------------------------------


@router.websocket("/devices/{device_id}/control/ws/{lease_id}")
async def control_ws_proxy(
    websocket: WebSocket,
    device_id: int,
    lease_id: str,
) -> None:
    """WebSocket bidirectional proxy connecting browser to app1 remote input endpoint."""
    user = await get_ws_user(websocket)
    if not user:
        await websocket.close(code=4401)
        return

    org_id = user.get("org_id")
    role_id = int(user.get("role_id", 3))
    user_perms = user.get("permissions") or []
    is_su = bool(
        user.get("is_superuser", False)
        or user.get("role") in ("superuser", "admin")
        or role_id == 1
    )

    if org_id in (None, 0):
        await websocket.close(code=4403)
        return

    if role_id == 4:
        if PERMISSION_VIDEO_VIEW not in user_perms and "*" not in user_perms:
            await websocket.close(code=4403)
            return
    elif not is_su and role_id not in (1, 2, 3):
        await websocket.close(code=4403)
        return

    async with async_session() as db:
        try:
            terminal = await _verify_device_access(device_id, user, db)
        except HTTPException as exc:
            code = 4404 if exc.status_code == status.HTTP_404_NOT_FOUND else 4403
            await websocket.close(code=code)
            return

    target_org_id = terminal.org_id if is_su else resolve_org_id(user)
    try:
        status_res = await iot_client.remote_input_status(
            sn=terminal.sn,
            org_id=target_org_id,
            user=user,
        )
    except Exception:  # noqa: BLE001
        await websocket.close(code=4403)
        return

    lease_info = status_res.get("lease") or {}
    owner_user_id = str(lease_info.get("owner_user_id") or "")
    current_sub = str(user.get("sub", ""))
    user_id_str = str(user.get("user_id", ""))

    if (
        not lease_info.get("active")
        or str(lease_info.get("lease_id")) != lease_id
        or (not is_su and owner_user_id not in (current_sub, user_id_str))
    ):
        await websocket.close(code=4403)
        return

    if role_id == 4 and lease_info.get("scope") != "view":
        await websocket.close(code=4403)
        return

    await websocket.accept()

    session_id = user.get("session_id")
    upstream_url = iot_client.remote_input_ws_url(lease_id, session_id=session_id)
    raw_headers = iot_client._get_headers(org_id=target_org_id, user=user)
    ws_headers = {k: v for k, v in raw_headers.items() if k.lower() != "content-type"}

    click_count = 0
    click_results: list[dict[str, Any]] = []
    close_reason = "normal"

    try:
        async with websockets.connect(
            upstream_url,
            additional_headers=ws_headers,
            open_timeout=settings.remote_control_ws_connect_timeout_sec,
        ) as upstream_ws:

            async def browser_to_upstream() -> None:
                nonlocal click_count, close_reason
                while True:
                    try:
                        raw_msg = await websocket.receive_text()
                    except WebSocketDisconnect:
                        close_reason = "browser_disconnect"
                        break
                    except Exception:  # noqa: BLE001
                        close_reason = "browser_receive_error"
                        break

                    try:
                        data = json.loads(raw_msg)
                        msg_type = data.get("type") if isinstance(data, dict) else None
                        if msg_type == "pointer_move":
                            validated = WsInboundMove.model_validate(data)
                        elif msg_type == "mouse_click":
                            validated = WsInboundClick.model_validate(data)
                            click_count += 1
                        elif msg_type == "key":
                            validated = WsInboundKey.model_validate(data)
                        elif msg_type == "keepalive":
                            validated = WsInboundKeepalive.model_validate(data)
                        elif msg_type == "release":
                            validated = WsInboundRelease.model_validate(data)
                            close_reason = "client_release"
                        else:
                            await websocket.send_text(
                                json.dumps({"type": "error", "code": "invalid_message"})
                            )
                            continue
                    except Exception:  # noqa: BLE001
                        await websocket.send_text(
                            json.dumps({"type": "error", "code": "invalid_message"})
                        )
                        continue

                    await upstream_ws.send(validated.model_dump_json())
                    if msg_type == "release":
                        break

            async def upstream_to_browser() -> None:
                nonlocal click_results, close_reason
                while True:
                    try:
                        raw_msg = await upstream_ws.recv()
                    except websockets.exceptions.ConnectionClosed as cc:
                        close_reason = f"upstream_closed_{cc.code}"
                        break
                    except Exception:  # noqa: BLE001
                        close_reason = "upstream_receive_error"
                        break

                    if isinstance(raw_msg, bytes):
                        raw_msg = raw_msg.decode("utf-8")

                    with contextlib.suppress(Exception):
                        parsed = json.loads(raw_msg)
                        if isinstance(parsed, dict):
                            if parsed.get("type") == "click_result":
                                click_results.append(
                                    {
                                        "command_id": parsed.get("command_id"),
                                        "result": parsed.get("result"),
                                        "latency_ms": parsed.get("latency_ms"),
                                    }
                                )
                            elif parsed.get("type") == "lease_revoked":
                                close_reason = "lease_revoked"

                    try:
                        await websocket.send_text(raw_msg)
                    except Exception:  # noqa: BLE001
                        close_reason = "browser_send_error"
                        break

            browser_task = asyncio.create_task(browser_to_upstream())
            upstream_task = asyncio.create_task(upstream_to_browser())

            _done, pending = await asyncio.wait(
                [browser_task, upstream_task],
                return_when=asyncio.FIRST_COMPLETED,
            )
            for t in pending:
                t.cancel()
                with contextlib.suppress(asyncio.CancelledError):
                    await t

    except Exception as exc:  # noqa: BLE001
        close_reason = f"connect_or_relay_error: {exc}"
        logger.warning(
            "WebSocket upstream connection error lease=%s device_id=%d: %s",
            lease_id,
            device_id,
            exc,
        )
    finally:
        with contextlib.suppress(Exception):
            await websocket.close()

        # Idempotent best-effort release of the lease on disconnect
        with contextlib.suppress(Exception):
            await iot_client.remote_input_release(
                lease_id=lease_id,
                org_id=target_org_id,
                user=user,
            )

        logger.info(
            "WebSocket session finished lease=%s device_id=%d sn=%s org_id=%s user=%s reason=%s clicks=%d results=%s",
            lease_id,
            device_id,
            terminal.sn,
            target_org_id,
            user.get("sub"),
            close_reason,
            click_count,
            click_results,
        )
