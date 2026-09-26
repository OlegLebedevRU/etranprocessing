import asyncio
import contextlib
import json
import logging
import uuid
from datetime import UTC, datetime
from typing import Any, Literal
from urllib.parse import urlsplit

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
from sqlalchemy import func
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import (
    decode_token,
    get_current_user,
    require_tenant_context,
    resolve_org_id,
)
from app.config import settings
from app.database import async_session, get_db
from app.models_l4desk import L4DeskRemoteSession
from app.repositories.l4desk_repository import L4DeskRepository
from app.routers.video import (
    _destroy_janus_mountpoint,
    _get_ingress_status,
    _mountpoint_pins,
    _verify_device_access,
    clear_mountpoint_pin,
    set_mountpoint_stream_instance,
)
from app.security.permissions import (
    ALL_PERMISSIONS,
    PERMISSION_VIDEO_VIEW,
    require_permission,
)
from app.services.iot_client import iot_client
from app.services.media_orchestrator_client import media_orchestrator_client
from app.services.remote_session_policy import get_remote_session_policy

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
    generation: int | None = None

    model_config = ConfigDict(extra="ignore")


class StreamStartRequest(BaseModel):
    mode: Literal["desktop", "usb-camera"]
    source_id: str
    profile: str = "default"
    lease_id: str | None = None


class StreamStopRequest(BaseModel):
    lease_id: str | None = None
    stream_instance_id: str | None = None
    destroy_mountpoint: bool = False

    model_config = ConfigDict(extra="ignore")


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
    type: Literal["pointer_move", "mouse_click", "key", "key_event", "shortcut_action"]
    x: int | None = Field(default=None, ge=0, le=65535)
    y: int | None = Field(default=None, ge=0, le=65535)
    button: Literal["left", "right"] = "left"
    action: Literal["f12", "alt_f4", "win_d"] | None = None
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
    button: Literal["left", "right"] = "left"
    client_ref: str | None = Field(default=None, max_length=64)

    model_config = ConfigDict(extra="forbid")


class WsInboundShortcut(BaseModel):
    type: Literal["shortcut_action"]
    action: Literal["f12", "alt_f4", "win_d"]
    client_ref: str | None = Field(default=None, max_length=64)

    model_config = ConfigDict(extra="forbid")


class WsInboundKey(BaseModel):
    type: Literal["key", "key_event"]
    kind: Literal["down", "up", "press"]
    vk: int = Field(ge=0, le=255)
    text: str | None = Field(default=None, max_length=32)
    client_ref: str | None = Field(default=None, max_length=64)

    model_config = ConfigDict(extra="forbid")


class WsInboundKeepalive(BaseModel):
    type: Literal["keepalive"]
    generation: int | None = None
    lease_id: str | None = None
    request_id: str | None = None

    model_config = ConfigDict(extra="ignore")


class WsInboundRelease(BaseModel):
    type: Literal["release"]
    generation: int | None = None
    lease_id: str | None = None
    reason: str | None = None
    request_id: str | None = None

    model_config = ConfigDict(extra="ignore")


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
    if not is_su and role_id not in (1, 2, 3, 5):
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
        or role_id in (1, 2, 3, 5)
        or user.get("role") in ("superuser", "admin", "user")
    )

    if scope == "console":
        if not is_strictly_superuser and role_id != 5:
            user_perms = user.get("permissions") or []
            if "console" not in user_perms and "*" not in user_perms:
                raise HTTPException(
                    status_code=status.HTTP_403_FORBIDDEN,
                    detail="Доступ к консоли разрешён только суперадминистраторам и пользователям L4Desk",
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

    policy = get_remote_session_policy(user)
    session_type_for_scope = "console" if scope == "console" else "video"
    decision = await policy.evaluate_session_request(
        tenant_id=org_id,
        terminal=terminal,
        session_type=session_type_for_scope,
        user=user,
        db=db,
    )
    if not decision.allowed:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail={
                "code": decision.error_code or "policy_denied",
                "message": decision.reason or "Сессия отклонена политикой",
            },
        )

    repo = L4DeskRepository(db)
    active_sess = await repo.get_active_session_by_terminal_id(device_id)
    if (
        isinstance(active_sess, L4DeskRemoteSession)
        and active_sess.session_type != session_type_for_scope
    ):
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail={
                "code": "session_busy",
                "message": (
                    f"Терминал {terminal.sn} занят активной сессией "
                    f"({active_sess.session_type}). Автоматическое переключение запрещено."
                ),
            },
        )

    if scope == "input":
        try:
            status_data = await iot_client.remote_input_status(
                terminal.sn, org_id=org_id, user=user
            )
            stream_info = (status_data.get("agent") or {}).get("stream") or {}
            if stream_info.get("state") == "running":
                if stream_info.get("mode") == "usb-camera":
                    raise HTTPException(
                        status_code=status.HTTP_403_FORBIDDEN,
                        detail="Ввод запрещён в режиме трансляции камеры (требуется рабочий стол)",
                    )
                if stream_info.get("profile") and stream_info.get("profile") not in (
                    "low",
                    "480p",
                ):
                    raise HTTPException(
                        status_code=status.HTTP_400_BAD_REQUEST,
                        detail="Удалённое управление разрешено только в режиме качества 480p",
                    )
        except HTTPException:
            raise
        except Exception as exc:  # noqa: BLE001
            logger.warning("Could not pre-check stream status for input lease: %s", exc)

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
    if scope == "console":
        await repo.ensure_l4desk_terminal(
            terminal_id=terminal.id,
            tenant_id=terminal.org_id,
            sn=terminal.sn,
            correlation_id=f"lease-console-{lease_id}",
        )
        user_db_id = None
        with contextlib.suppress(Exception):
            user_db_id = (
                int(user["id"])
                if "id" in user
                else int(user["user_id"])
                if "user_id" in user
                else None
            )
        if not active_sess:
            await repo.create_remote_session(
                tenant_id=terminal.org_id,
                terminal_id=terminal.id,
                operation_id=f"op-console-{lease_id}",
                correlation_id=f"corr-console-{lease_id}",
                session_type="console",
                requested_by_user_id=user_db_id,
                provider_session_id=str(res.get("owner_session_id") or lease_id),
                state="active",
                active_at=datetime.now(UTC),
            )
            await db.flush()

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
    if body.scope == "input":
        stream_info = (status_data.get("agent") or {}).get("stream") or {}
        if stream_info.get("state") == "running":
            if stream_info.get("mode") == "usb-camera":
                raise HTTPException(
                    status_code=status.HTTP_403_FORBIDDEN,
                    detail="Ввод запрещён в режиме трансляции камеры (требуется рабочий стол)",
                )
            if stream_info.get("profile") and stream_info.get("profile") not in (
                "low",
                "480p",
            ):
                raise HTTPException(
                    status_code=status.HTTP_400_BAD_REQUEST,
                    detail="Удалённое управление разрешено только в режиме качества 480p",
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
    try:
        inv = await iot_client.remote_input_inventory(
            sn=terminal.sn,
            refresh=refresh,
            org_id=org_id,
            user=user,
        )
    except HTTPException as exc:
        if exc.status_code == status.HTTP_409_CONFLICT and refresh:
            inv = await iot_client.remote_input_inventory(
                sn=terminal.sn,
                refresh=0,
                org_id=org_id,
                user=user,
            )
        else:
            raise

    displays = list(inv.get("displays") or [])
    cameras = list(inv.get("cameras") or [])

    if not displays:
        with contextlib.suppress(Exception):
            status_data = await iot_client.remote_input_status(
                terminal.sn, org_id=org_id, user=user
            )
            agent_data = status_data.get("agent") or {}
            if agent_data.get("desktop_available") or agent_data.get("screen"):
                screen = agent_data.get("screen") or {}
                w = screen.get("virtual_width") or 1920
                h = screen.get("virtual_height") or 1080
                x = screen.get("virtual_x") or 0
                y = screen.get("virtual_y") or 0
                displays.append(
                    {
                        "id": "0",
                        "desktop_id": "0",
                        "name": "Основной экран",
                        "resolution": f"{w}x{h}",
                        "width": w,
                        "height": h,
                        "x": x,
                        "y": y,
                        "is_primary": True,
                        "primary": True,
                        "policy": "input",
                    }
                )

    for d in displays:
        if "id" not in d and "desktop_id" in d:
            d["id"] = str(d["desktop_id"])
        if "desktop_id" not in d and "id" in d:
            d["desktop_id"] = str(d["id"])
        if "is_primary" not in d and "primary" in d:
            d["is_primary"] = bool(d["primary"])
        if "primary" not in d and "is_primary" in d:
            d["primary"] = bool(d["is_primary"])
        if "resolution" not in d and d.get("width") and d.get("height"):
            d["resolution"] = f"{d['width']}x{d['height']}"

    for c in cameras:
        if "id" not in c and "camera_id" in c:
            c["id"] = str(c["camera_id"])
        if "camera_id" not in c and "id" in c:
            c["camera_id"] = str(c["id"])

    return {"displays": displays, "cameras": cameras}


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

    repo = L4DeskRepository(db)
    active_sess = await repo.get_active_session_by_terminal_id(device_id)
    if (
        isinstance(active_sess, L4DeskRemoteSession)
        and active_sess.session_type == "console"
    ):
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail={
                "code": "session_busy",
                "message": (
                    f"Терминал {terminal.sn} занят активной сессией консоли. "
                    f"Автоматическое переключение на видео запрещено."
                ),
            },
        )

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

    source_id = body.source_id
    if body.mode == "desktop" and source_id in ("0", "disp", "desktop", ""):
        with contextlib.suppress(Exception):
            inv = await iot_client.remote_input_inventory(
                sn=terminal.sn,
                refresh=0,
                org_id=org_id,
                user=user,
            )
            displays = list(inv.get("displays") or [])
            if displays:
                primary = next(
                    (d for d in displays if d.get("is_primary") or d.get("primary")),
                    displays[0],
                )
                resolved_id = str(primary.get("id") or primary.get("desktop_id") or "")
                if resolved_id:
                    source_id = resolved_id
    elif body.mode == "usb-camera" and source_id in ("0", "cam", "camera", ""):
        with contextlib.suppress(Exception):
            inv = await iot_client.remote_input_inventory(
                sn=terminal.sn,
                refresh=0,
                org_id=org_id,
                user=user,
            )
            cameras = list(inv.get("cameras") or [])
            if cameras:
                cam = cameras[0]
                resolved_id = str(cam.get("id") or cam.get("camera_id") or "")
                if resolved_id:
                    source_id = resolved_id

    try:
        res = await iot_client.remote_input_stream_start(
            lease_id=lease_id,
            mode=body.mode,
            source_id=source_id,
            profile=body.profile,
            org_id=org_id,
            user=user,
        )
        stream_inst_id = str(res.get("stream_instance_id", ""))
        if stream_inst_id:
            set_mountpoint_stream_instance(device_id, stream_inst_id)
            await repo.ensure_l4desk_terminal(
                terminal_id=terminal.id,
                tenant_id=terminal.org_id,
                sn=terminal.sn,
                correlation_id=f"stream-{stream_inst_id}",
            )
            user_db_id = None
            with contextlib.suppress(Exception):
                user_db_id = (
                    int(user["id"])
                    if "id" in user
                    else int(user["user_id"])
                    if "user_id" in user
                    else None
                )
            if not active_sess:
                await repo.create_remote_session(
                    tenant_id=terminal.org_id,
                    terminal_id=terminal.id,
                    operation_id=f"op-stream-{stream_inst_id}",
                    correlation_id=f"corr-stream-{stream_inst_id}",
                    session_type="video",
                    requested_by_user_id=user_db_id,
                    provider_session_id=stream_inst_id,
                    state="active",
                    active_at=datetime.now(UTC),
                )
                await db.flush()
        return StreamStartResponse(
            stream_instance_id=stream_inst_id,
            result=str(res.get("result", "")),
            state=res.get("state"),
        )
    except HTTPException as exc:
        err_detail = (
            str(exc.detail)
            if isinstance(exc.detail, str)
            else str(exc.detail.get("code", ""))
            if isinstance(exc.detail, dict)
            else ""
        )
        if exc.status_code == status.HTTP_409_CONFLICT and "unsupported" in err_detail:
            logger.warning(
                "Terminal %s stream_start not supported by agent (%s), proceeding in legacy streaming mode",
                terminal.sn,
                err_detail,
            )
            fallback_inst_id = str(uuid.uuid4())
            set_mountpoint_stream_instance(device_id, fallback_inst_id)
            return StreamStartResponse(
                stream_instance_id=fallback_inst_id,
                result="started",
                state="running",
            )
        raise


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

    try:
        res = await iot_client.remote_input_stream_stop(
            lease_id=lease_id,
            org_id=org_id,
            user=user,
        )
        clear_mountpoint_pin(device_id, lease_id=lease_id)
        if body and body.destroy_mountpoint:
            cached = _mountpoint_pins.get(device_id)
            if not cached or cached.get("lease_id") == lease_id:
                await _destroy_janus_mountpoint(device_id)

        repo = L4DeskRepository(db)
        active_sess = await repo.get_active_session_by_terminal_id(device_id)
        if (
            isinstance(active_sess, L4DeskRemoteSession)
            and active_sess.session_type == "video"
        ):
            active_sess.state = "closed"
            active_sess.closed_at = func.now()
            active_sess.reason = "stream_stopped"
            await db.flush()
            if active_sess.provider_session_id:
                with contextlib.suppress(Exception):
                    await media_orchestrator_client.stop_session(
                        session_id=active_sess.provider_session_id,
                        reason="stream_stopped",
                    )
        return StreamStopResponse(result=str(res.get("result", "stopped")))
    except HTTPException as exc:
        err_detail = (
            str(exc.detail).lower()
            if isinstance(exc.detail, str)
            else str(exc.detail.get("code", "")).lower()
            if isinstance(exc.detail, dict)
            else ""
        )
        # Idempotent stop check: only known stopped states are no-op
        # Conflict on owner/session/tenant/epoch must NOT be swallowed!
        is_known_stopped = any(
            c in err_detail
            for c in (
                "already_stopped",
                "no_active_stream",
                "not_running",
                "already",
                "no active",
            )
        ) and not any(
            c in err_detail
            for c in ("owner", "tenant", "epoch", "session", "instance", "mode")
        )
        if (exc.status_code == status.HTTP_409_CONFLICT and is_known_stopped) or (
            exc.status_code == status.HTTP_504_GATEWAY_TIMEOUT
            and any(
                c in err_detail
                for c in ("unsupported", "terminal_timeout", "already", "no active")
            )
        ):
            clear_mountpoint_pin(device_id, lease_id=lease_id)
            if body and body.destroy_mountpoint:
                cached = _mountpoint_pins.get(device_id)
                if not cached or cached.get("lease_id") == lease_id:
                    await _destroy_janus_mountpoint(device_id)
            repo = L4DeskRepository(db)
            active_sess = await repo.get_active_session_by_terminal_id(device_id)
            if (
                isinstance(active_sess, L4DeskRemoteSession)
                and active_sess.session_type == "video"
            ):
                active_sess.state = "closed"
                active_sess.closed_at = func.now()
                active_sess.reason = "stream_stopped"
                await db.flush()
            return StreamStopResponse(result="stopped")
        raise


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
    lease_info = status_data.get("lease") or {}
    stream_info = (status_data.get("agent") or {}).get("stream")
    ingress_stats = await _get_ingress_status(terminal.sn)

    # Stopped state of current epoch takes priority over stale running cache
    if (
        not lease_info.get("active")
        and stream_info
        and stream_info.get("state") == "running"
    ):
        stream_info = {
            **stream_info,
            "state": "stopped",
            "reason": "lease_expired",
        }

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
    try:
        return await iot_client.remote_input_keepalive(
            lease_id=body.lease_id,
            generation=body.generation,
            org_id=org_id,
            user=user,
        )
    except HTTPException as exc:
        if exc.status_code == status.HTTP_404_NOT_FOUND:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail={
                    "code": "lease_not_found",
                    "detail": "Аренда не найдена или срок её действия истёк",
                    "lease_id": body.lease_id,
                    "generation": body.generation,
                },
            ) from exc
        raise


@router.delete(
    "/devices/{device_id}/control/lease/{lease_id}",
    status_code=status.HTTP_204_NO_CONTENT,
)
async def release_device_control_lease(
    device_id: int,
    lease_id: str,
    destroy_mountpoint: bool = Query(False),
    user: dict[str, Any] = Depends(require_permission(PERMISSION_VIDEO_VIEW)),
    db: AsyncSession = Depends(get_db),
) -> None:
    """Release active control lease."""
    terminal = await _verify_device_access(device_id, user, db)
    org_id = terminal.org_id if user.get("is_superuser") else resolve_org_id(user)
    try:
        await iot_client.remote_input_release(
            lease_id=lease_id,
            org_id=org_id,
            user=user,
        )
    finally:
        clear_mountpoint_pin(device_id, lease_id=lease_id)
        if destroy_mountpoint:
            cached = _mountpoint_pins.get(device_id)
            if not cached or cached.get("lease_id") == lease_id:
                await _destroy_janus_mountpoint(device_id)
        repo = L4DeskRepository(db)
        active_sess = await repo.get_active_session_by_terminal_id(device_id)
        if isinstance(active_sess, L4DeskRemoteSession):
            active_sess.state = "closed"
            active_sess.closed_at = func.now()
            active_sess.reason = "lease_released"
            await db.flush()


@router.post("/devices/{device_id}/control/events")
async def send_device_control_event(
    device_id: int,
    event: ControlEventRequest,
    user: dict[str, Any] = Depends(require_operator_user),
    db: AsyncSession = Depends(get_db),
) -> Any:
    """REST fallback for sending pointer movement, mouse clicks, keyboard events, or shortcut actions."""
    terminal = await _verify_device_access(device_id, user, db)
    org_id = terminal.org_id if user.get("is_superuser") else resolve_org_id(user)

    status_data = await iot_client.remote_input_status(
        terminal.sn, org_id=org_id, user=user
    )
    lease_info = status_data.get("lease") or {}
    agent_info = status_data.get("agent") or {}
    stream_info = agent_info.get("stream") or {}

    if lease_info.get("lease_id") or iot_client.base_url:
        if (
            not lease_info.get("active")
            or str(lease_info.get("lease_id")) != event.lease_id
        ):
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail="Недействительная или неактивная аренда управления",
            )
        if lease_info.get("scope", "input") != "input":
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail="Ввод не разрешён: требуется аренда со scope input",
            )
        if stream_info.get("mode") == "usb-camera":
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail="Ввод запрещён в режиме трансляции камеры",
            )
        if stream_info.get("profile") and stream_info.get("profile") not in (
            "low",
            "480p",
        ):
            raise HTTPException(
                status_code=status.HTTP_400_BAD_REQUEST,
                detail="Удалённое управление разрешено только в режиме качества 480p",
            )

    desktop_id = lease_info.get("selected_desktop_id") or lease_info.get("desktop_id")
    stream_inst_id = str(lease_info.get("stream_instance_id") or "") or None

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
            button=event.button,
            client_ref=event.client_ref,
            org_id=org_id,
            user=user,
        )
    elif event.type in ("key", "key_event"):
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
            desktop_id=desktop_id,
            stream_instance_id=stream_inst_id,
            org_id=org_id,
            user=user,
        )
    elif event.type == "shortcut_action":
        if event.action is None:
            raise HTTPException(
                status_code=422, detail="action is required for shortcut_action"
            )
        return await iot_client.remote_input_shortcut(
            lease_id=event.lease_id,
            action=event.action,
            client_ref=event.client_ref,
            desktop_id=desktop_id,
            stream_instance_id=stream_inst_id,
            org_id=org_id,
            user=user,
        )


# ---------------------------------------------------------------------------
# WebSocket Proxy Endpoint
# ---------------------------------------------------------------------------


@router.websocket("/devices/{device_id}/watch/ws")
async def watch_device_status_ws(websocket: WebSocket, device_id: int) -> None:
    """Relay app1 read-only invalidations without exposing its service key."""
    # Browser credentials must remain in a cookie; never accept a URL token.
    if "token" in websocket.query_params:
        await websocket.close(code=4401)
        return
    origin = websocket.headers.get("origin")
    if origin:
        origin_url = urlsplit(origin)
        forwarded_proto = websocket.headers.get("x-forwarded-proto")
        expected_proto = (
            forwarded_proto
            if forwarded_proto in {"http", "https"}
            else "https"
            if websocket.url.scheme == "wss"
            else "http"
        )
        if (
            origin_url.scheme != expected_proto
            or origin_url.netloc != websocket.headers.get("host")
            or origin_url.path
            or origin_url.query
            or origin_url.fragment
        ):
            await websocket.close(code=4403)
            return
    user = await get_ws_user(websocket)
    if not user:
        await websocket.close(code=4401)
        return
    permissions = user.get("permissions") or []
    if PERMISSION_VIDEO_VIEW not in permissions and "*" not in permissions:
        await websocket.close(code=4403)
        return

    async with async_session() as db:
        try:
            terminal = await _verify_device_access(device_id, user, db)
        except HTTPException as exc:
            await websocket.close(
                code=4404 if exc.status_code == status.HTTP_404_NOT_FOUND else 4403
            )
            return

    org_id = terminal.org_id if user.get("is_superuser") else resolve_org_id(user)
    if not isinstance(org_id, int) or org_id <= 0 or not iot_client.service_token:
        await websocket.close(code=4403)
        return
    base = iot_client.base_url or "http://app1:8000"
    if base.startswith("https://"):
        ws_base = "wss://" + base[8:]
    elif base.startswith("http://"):
        ws_base = "ws://" + base[7:]
    else:
        ws_base = f"ws://{base}"
    upstream_url = f"{ws_base}/api/internal/v1/remote-input/ws/watch/{terminal.sn}"
    headers = {
        "X-Internal-Service-Key": iot_client.service_token,
        "X-Org-Id": str(org_id),
    }

    closed_by_bff = False
    try:
        async with websockets.connect(
            upstream_url,
            additional_headers=headers,
            open_timeout=settings.remote_control_ws_connect_timeout_sec,
        ) as upstream_ws:
            first = json.loads(await asyncio.wait_for(upstream_ws.recv(), timeout=5))
            if (
                not isinstance(first, dict)
                or first.get("type") != "snapshot"
                or not isinstance(first.get("data"), dict)
                or first["data"].get("sn") != terminal.sn
            ):
                await websocket.close(code=1011)
                closed_by_bff = True
                return
            await websocket.accept()
            # The browser always obtains the sanitized, authoritative REST view.
            await websocket.send_json({"type": "invalidate"})

            async def browser_to_upstream() -> None:
                nonlocal closed_by_bff
                while True:
                    try:
                        await websocket.receive_text()
                    except WebSocketDisconnect:
                        return
                    # The feed is read-only, including at the BFF boundary.
                    await websocket.close(code=4403)
                    closed_by_bff = True
                    return

            async def upstream_to_browser() -> None:
                while True:
                    raw = await upstream_ws.recv()
                    event = json.loads(raw)
                    if isinstance(event, dict) and event.get("type") == "invalidate":
                        await websocket.send_json({"type": "invalidate"})

            tasks = [
                asyncio.create_task(browser_to_upstream()),
                asyncio.create_task(upstream_to_browser()),
            ]
            try:
                await asyncio.wait(
                    tasks, timeout=60, return_when=asyncio.FIRST_COMPLETED
                )
            finally:
                for task in tasks:
                    task.cancel()
                for task in tasks:
                    with contextlib.suppress(asyncio.CancelledError):
                        await task
    except TimeoutError, OSError, ValueError, websockets.exceptions.WebSocketException:
        logger.warning("Video watch upstream unavailable for device_id=%d", device_id)
    finally:
        if not closed_by_bff:
            with contextlib.suppress(Exception):
                await websocket.close()


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
    elif not is_su and role_id not in (1, 2, 3, 5):
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

    agent_info = status_res.get("agent") or {}
    stream_info = agent_info.get("stream") or {}
    is_camera = stream_info.get("mode") == "usb-camera"
    is_non_480p = bool(
        stream_info.get("profile") and stream_info.get("profile") not in ("low", "480p")
    )
    is_input_scope = lease_info.get("scope", "input") == "input"

    await websocket.accept()

    session_id = user.get("session_id")
    upstream_url = iot_client.remote_input_ws_url(lease_id, session_id=session_id)
    raw_headers = iot_client._get_headers(org_id=target_org_id, user=user)
    ws_headers = {k: v for k, v in raw_headers.items() if k.lower() != "content-type"}

    click_count = 0
    click_results: list[dict[str, Any]] = []
    close_reason = "normal"
    upstream_close_code: int | None = None

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
                        elif msg_type in ("key", "key_event"):
                            validated = WsInboundKey.model_validate(data)
                        elif msg_type == "shortcut_action":
                            validated = WsInboundShortcut.model_validate(data)
                            click_count += 1
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

                    if msg_type in (
                        "pointer_move",
                        "mouse_click",
                        "key",
                        "key_event",
                        "shortcut_action",
                    ):
                        client_ref = getattr(validated, "client_ref", None)
                        if not is_input_scope:
                            await websocket.send_text(
                                json.dumps(
                                    {
                                        "type": "error"
                                        if msg_type == "pointer_move"
                                        else "click_result",
                                        "result": "nack",
                                        "code": "action_blocked_policy",
                                        "message": "Ввод не разрешён: требуется аренда со scope input",
                                        "client_ref": client_ref,
                                    }
                                )
                            )
                            continue
                        if is_camera:
                            await websocket.send_text(
                                json.dumps(
                                    {
                                        "type": "error"
                                        if msg_type == "pointer_move"
                                        else "click_result",
                                        "result": "nack",
                                        "code": "action_blocked_policy",
                                        "message": "Ввод запрещён в режиме трансляции камеры",
                                        "client_ref": client_ref,
                                    }
                                )
                            )
                            continue
                        if is_non_480p:
                            await websocket.send_text(
                                json.dumps(
                                    {
                                        "type": "error"
                                        if msg_type == "pointer_move"
                                        else "click_result",
                                        "result": "nack",
                                        "code": "action_blocked_policy",
                                        "message": "Удалённое управление разрешено только в режиме качества 480p",
                                        "client_ref": client_ref,
                                    }
                                )
                            )
                            continue

                    payload = validated.model_dump(exclude_none=True)
                    if msg_type in ("key", "key_event"):
                        payload["type"] = "key_event"
                    if msg_type in (
                        "pointer_move",
                        "mouse_click",
                        "key",
                        "key_event",
                        "shortcut_action",
                    ):
                        desktop_id = lease_info.get(
                            "selected_desktop_id"
                        ) or lease_info.get("desktop_id")
                        if desktop_id:
                            payload.setdefault("desktop_id", desktop_id)
                        if lease_info.get("stream_instance_id"):
                            payload.setdefault(
                                "stream_instance_id",
                                str(lease_info["stream_instance_id"]),
                            )
                    await upstream_ws.send(json.dumps(payload))
                    if msg_type == "keepalive":
                        logger.info(
                            "WS ctl browser->upstream type=keepalive lease_id=%s generation=%s request_id=%s",
                            lease_id,
                            getattr(validated, "generation", None),
                            getattr(validated, "request_id", None),
                        )
                    elif msg_type == "release":
                        logger.info(
                            "WS ctl browser->upstream type=release lease_id=%s generation=%s reason=%s request_id=%s",
                            lease_id,
                            getattr(validated, "generation", None),
                            getattr(validated, "reason", None),
                            getattr(validated, "request_id", None),
                        )
                        break

            async def upstream_to_browser() -> None:
                nonlocal click_results, close_reason, upstream_close_code
                while True:
                    try:
                        raw_msg = await upstream_ws.recv()
                    except websockets.exceptions.ConnectionClosed as cc:
                        close_reason = f"upstream_closed_{cc.code}"
                        upstream_close_code = cc.code
                        break
                    except Exception:  # noqa: BLE001
                        close_reason = "upstream_receive_error"
                        break

                    if isinstance(raw_msg, bytes):
                        raw_msg = raw_msg.decode("utf-8")

                    with contextlib.suppress(Exception):
                        parsed = json.loads(raw_msg)
                        if isinstance(parsed, dict):
                            out_type = parsed.get("type")
                            if out_type in ("click_result", "action_result"):
                                parsed_lease = parsed.get("lease_id")
                                if parsed_lease and str(parsed_lease) != lease_id:
                                    logger.warning(
                                        "Dropping late %s event for stale lease=%s (current lease=%s)",
                                        out_type,
                                        parsed_lease,
                                        lease_id,
                                    )
                                    continue
                                click_results.append(
                                    {
                                        "command_id": parsed.get("command_id"),
                                        "result": parsed.get("result"),
                                        "code": parsed.get("code"),
                                        "latency_ms": parsed.get("latency_ms"),
                                    }
                                )
                            elif out_type == "lease_revoked":
                                close_reason = "lease_revoked"
                                logger.info(
                                    "Lease revoked upstream lease=%s device_id=%d reason=%s",
                                    lease_id,
                                    device_id,
                                    parsed.get("reason"),
                                )
                            elif out_type == "stream_state":
                                parsed_lease = parsed.get("lease_id")
                                if parsed_lease and str(parsed_lease) != lease_id:
                                    logger.warning(
                                        "Dropping late stream_state event for stale lease=%s (current lease=%s)",
                                        parsed_lease,
                                        lease_id,
                                    )
                                    continue
                                if "lease_id" not in parsed:
                                    parsed["lease_id"] = lease_id
                                if (
                                    "stream_instance_id" not in parsed
                                    and lease_info.get("stream_instance_id")
                                ):
                                    parsed["stream_instance_id"] = str(
                                        lease_info["stream_instance_id"]
                                    )
                                raw_msg = json.dumps(parsed)
                                logger.info(
                                    "Stream state event received upstream lease=%s device_id=%d state=%s stream_instance_id=%s",
                                    lease_id,
                                    device_id,
                                    parsed.get("state"),
                                    parsed.get("stream_instance_id"),
                                )

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

        # Idempotent best-effort release of the lease on disconnect (safe bounded fallback)
        if close_reason != "lease_revoked":
            with contextlib.suppress(Exception):
                await iot_client.remote_input_release(
                    lease_id=lease_id,
                    org_id=target_org_id,
                    user=user,
                )
        clear_mountpoint_pin(device_id, lease_id=lease_id)

        logger.info(
            "WebSocket session finished lease=%s device_id=%d sn=%s org_id=%s user=%s close_reason=%s upstream_code=%s clicks=%d results=%s",
            lease_id,
            device_id,
            terminal.sn,
            target_org_id,
            user.get("sub"),
            close_reason,
            upstream_close_code,
            click_count,
            click_results,
        )
