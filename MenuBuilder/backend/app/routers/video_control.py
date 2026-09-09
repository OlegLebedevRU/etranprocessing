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
from app.routers.video import _verify_device_access
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


class ControlStatusLease(BaseModel):
    active: bool = False
    mine: bool = False
    owner_user_id: str | None = None
    expires_at: str | None = None


class ControlStatusResponse(BaseModel):
    agent: ControlStatusAgent
    lease: ControlStatusLease


class ControlLeaseResponse(BaseModel):
    lease_id: str
    expires_at: str
    keepalive_sec: int
    ws_path: str


class KeepaliveRequest(BaseModel):
    lease_id: str


class ControlEventRequest(BaseModel):
    lease_id: str
    type: Literal["pointer_move", "mouse_click"]
    x: int = Field(ge=0, le=65535)
    y: int = Field(ge=0, le=65535)
    button: Literal["left"] = "left"
    client_ref: str | None = Field(default=None, max_length=64)

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


class WsInboundKeepalive(BaseModel):
    type: Literal["keepalive"]

    model_config = ConfigDict(extra="forbid")


class WsInboundRelease(BaseModel):
    type: Literal["release"]

    model_config = ConfigDict(extra="forbid")


# ---------------------------------------------------------------------------
# Dependencies & Helpers
# ---------------------------------------------------------------------------


async def require_remote_control_user(
    user: dict[str, Any] = Depends(require_tenant_context),
) -> dict[str, Any]:
    """Dependency: require active tenant context and operator role (1: superuser, 2: admin, 3: user)."""
    role_id = user.get("role_id")
    if role_id not in (1, 2, 3):
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Управление доступно только операторам",
        )
    return user


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

    return {
        "sub": str(payload.get("sub") or username or user_id),
        "user_id": user_id,
        "username": username,
        "org_id": org_id,
        "role_id": role_id,
        "role": role,
        "is_superuser": is_su,
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
    user: dict[str, Any] = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
) -> ControlStatusResponse:
    """Fetch terminal remote control status (agent presence & active lease). Accessible to roles 1-4."""
    terminal = await _verify_device_access(device_id, user, db)
    org_id = resolve_org_id(user)
    raw = await iot_client.remote_input_status(
        sn=terminal.sn,
        org_id=org_id,
        user=user,
    )
    agent_data = raw.get("agent") or {}
    lease_data = raw.get("lease") or {}
    owner_user_id = lease_data.get("owner_user_id")
    current_sub = str(user.get("sub", ""))
    mine = bool(
        lease_data.get("active") and owner_user_id and str(owner_user_id) == current_sub
    )

    return ControlStatusResponse(
        agent=ControlStatusAgent(
            online=bool(agent_data.get("online", False)),
            desktop_available=bool(agent_data.get("desktop_available", False)),
            screen=agent_data.get("screen"),
            last_seen_at=agent_data.get("last_seen_at"),
            stale=bool(agent_data.get("stale", False)),
        ),
        lease=ControlStatusLease(
            active=bool(lease_data.get("active", False)),
            mine=mine,
            owner_user_id=str(owner_user_id) if owner_user_id else None,
            expires_at=lease_data.get("expires_at"),
        ),
    )


@router.post(
    "/devices/{device_id}/control/lease",
    response_model=ControlLeaseResponse,
    status_code=status.HTTP_201_CREATED,
)
async def acquire_device_control_lease(
    device_id: int,
    user: dict[str, Any] = Depends(require_remote_control_user),
    db: AsyncSession = Depends(get_db),
) -> ControlLeaseResponse:
    """Acquire exclusive remote control lease for terminal. Accessible to roles 1-3."""
    terminal = await _verify_device_access(device_id, user, db)
    org_id = resolve_org_id(user)
    res = await iot_client.remote_input_acquire_lease(
        sn=terminal.sn,
        org_id=org_id,
        user=user,
    )
    lease_id = res["lease_id"]
    return ControlLeaseResponse(
        lease_id=lease_id,
        expires_at=res["expires_at"],
        keepalive_sec=res.get("keepalive_sec", 15),
        ws_path=f"/api/v1/video/devices/{device_id}/control/ws/{lease_id}",
    )


@router.post("/devices/{device_id}/control/keepalive")
async def keepalive_device_control_lease(
    device_id: int,
    body: KeepaliveRequest,
    user: dict[str, Any] = Depends(require_remote_control_user),
    db: AsyncSession = Depends(get_db),
) -> dict[str, Any]:
    """Extend active control lease."""
    await _verify_device_access(device_id, user, db)
    org_id = resolve_org_id(user)
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
    user: dict[str, Any] = Depends(require_remote_control_user),
    db: AsyncSession = Depends(get_db),
) -> None:
    """Release active control lease."""
    await _verify_device_access(device_id, user, db)
    org_id = resolve_org_id(user)
    await iot_client.remote_input_release(
        lease_id=lease_id,
        org_id=org_id,
        user=user,
    )


@router.post("/devices/{device_id}/control/events")
async def send_device_control_event(
    device_id: int,
    event: ControlEventRequest,
    user: dict[str, Any] = Depends(require_remote_control_user),
    db: AsyncSession = Depends(get_db),
) -> Any:
    """REST fallback for sending pointer movement or mouse clicks."""
    await _verify_device_access(device_id, user, db)
    org_id = resolve_org_id(user)
    if event.type == "pointer_move":
        await iot_client.remote_input_move(
            lease_id=event.lease_id,
            x=event.x,
            y=event.y,
            org_id=org_id,
            user=user,
        )
        return Response(status_code=status.HTTP_202_ACCEPTED)
    elif event.type == "mouse_click":
        return await iot_client.remote_input_click(
            lease_id=event.lease_id,
            x=event.x,
            y=event.y,
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
    role_id = user.get("role_id")
    if org_id in (None, 0) or role_id not in (1, 2, 3):
        await websocket.close(code=4403)
        return

    async with async_session() as db:
        try:
            terminal = await _verify_device_access(device_id, user, db)
        except HTTPException as exc:
            code = 4404 if exc.status_code == status.HTTP_404_NOT_FOUND else 4403
            await websocket.close(code=code)
            return

    try:
        status_res = await iot_client.remote_input_status(
            sn=terminal.sn,
            org_id=resolve_org_id(user),
            user=user,
        )
    except Exception:  # noqa: BLE001
        await websocket.close(code=4403)
        return

    lease_info = status_res.get("lease") or {}
    owner_user_id = lease_info.get("owner_user_id")
    if (
        not lease_info.get("active")
        or lease_info.get("lease_id") != lease_id
        or str(owner_user_id) != str(user.get("sub", ""))
    ):
        await websocket.close(code=4403)
        return

    await websocket.accept()

    upstream_url = iot_client.remote_input_ws_url(lease_id)
    raw_headers = iot_client._get_headers(org_id=resolve_org_id(user), user=user)
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
                org_id=resolve_org_id(user),
                user=user,
            )

        logger.info(
            "WebSocket session finished lease=%s device_id=%d sn=%s org_id=%s user=%s reason=%s clicks=%d results=%s",
            lease_id,
            device_id,
            terminal.sn,
            org_id,
            user.get("sub"),
            close_reason,
            click_count,
            click_results,
        )
