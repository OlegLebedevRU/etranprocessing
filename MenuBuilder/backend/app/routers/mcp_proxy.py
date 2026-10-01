"""Authenticated public gateway for the MenuBuilder L4mcp service."""

from __future__ import annotations

import json
from typing import Annotated, Any

import httpx
from fastapi import APIRouter, Depends, HTTPException, Path, Query, Request
from fastapi.responses import Response
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import get_current_user, resolve_org_id
from app.config import settings
from app.database import get_db
from app.routers.video import _verify_device_access
from app.schemas.mcp_events import UserEventFilters, UserEventPage
from app.services.iot_client import iot_client

router = APIRouter()


@router.get("/mcp/events/{device_id}", response_model=UserEventPage)
async def user_event_history(
    device_id: Annotated[int, Path(gt=0)],
    filters: Annotated[UserEventFilters, Query()],
    request: Request,
    user: dict[str, Any] = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
) -> UserEventPage:
    """Read one terminal's tenant history, including while the terminal is offline."""
    _require_mcp_user(request, user)
    org_id = resolve_org_id(user)
    terminal = await _verify_device_access(device_id, user, db)
    # Existing access helpers allow superusers across tenants; this reader does not.
    if terminal.org_id != org_id:
        raise HTTPException(status_code=403, detail="Terminal is outside active tenant")
    try:
        device = await iot_client.get_console_device(device_id, org_id)
    except (httpx.HTTPError, ValueError, TypeError, AttributeError) as exc:
        raise HTTPException(
            status_code=503, detail="IoT device lookup unavailable"
        ) from exc
    if (
        not isinstance(device, dict)
        or device.get("device_id") != device_id
        or not terminal.sn
        or device.get("sn") != terminal.sn
    ):
        raise HTTPException(status_code=409, detail="IoT terminal binding mismatch")
    return await iot_client.search_user_events(device_id, org_id, filters)


@router.get("/mcp/console/preflight/{device_id}")
async def console_preflight(
    device_id: int,
    request: Request,
    user: dict[str, Any] = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
) -> dict[str, Any]:
    """Fail closed before opening a billed console lease or dispatching RPC."""
    _require_mcp_user(request, user)
    terminal = await _verify_device_access(device_id, user, db)
    try:
        device = await iot_client.get_console_device(device_id, terminal.org_id)
    except httpx.HTTPError as exc:
        raise HTTPException(
            status_code=503, detail="IoT device status unavailable"
        ) from exc
    if device is None or device.get("sn") != terminal.sn:
        return {
            "ready": False,
            "reason": "iot_device_missing_or_mismatch",
            "device_id": device_id,
        }
    tags = device.get("device_tags") or []
    sys_tag = next(
        (
            str(tag.get("value") or "").strip().lower()
            for tag in tags
            if str(tag.get("tag") or "").strip().lower() == "sys"
        ),
        "",
    )
    connection = device.get("connection") or {}
    online = (
        connection.get("last_checked_result") is True
        and connection.get("is_blocked") is not True
    )
    svc_online = online and connection.get("svc_connect") is True
    reason = (
        "sys_tag_missing"
        if not sys_tag
        else "unsupported_sys"
        if sys_tag != "windows"
        else "terminal_offline"
        if not online
        else "l4con_offline"
        if not svc_online
        else None
    )
    return {
        "ready": reason is None,
        "reason": reason,
        "device_id": device_id,
        "sn": terminal.sn,
        "org_id": terminal.org_id,
        "sys": sys_tag or None,
        "online": online,
        "svc_online": svc_online,
    }


def _require_mcp_user(request: Request, user: dict[str, Any]) -> str:
    bearer = request.headers.get("authorization", "")
    if (
        not bearer.lower().startswith("bearer ")
        or user.get("token_type") != "api_token"
    ):
        raise HTTPException(status_code=401, detail="L4mcp API token required")
    if int(user.get("role_id") or 0) not in (1, 3, 5):
        raise HTTPException(status_code=403, detail="L4mcp access denied")
    return bearer


@router.get("/mcp/identity")
async def mcp_identity(
    request: Request,
    user: dict[str, Any] = Depends(get_current_user),
) -> dict[str, Any]:
    """Resolve current user and tenant from a revocable API token."""
    _require_mcp_user(request, user)
    return {
        "user_id": user["user_id"],
        "username": user["username"],
        "org_id": user["org_id"],
        "role_id": user["role_id"],
        "role": user["role"],
        "session_id": user["session_id"],
        "jti": user["jti"],
    }


@router.api_route("/mcp/proxy", methods=["POST", "GET", "DELETE"])
async def mcp_proxy(
    request: Request,
    user: dict[str, Any] = Depends(get_current_user),
) -> Response:
    bearer = _require_mcp_user(request, user)
    if not settings.l4mcp_url:
        raise HTTPException(status_code=503, detail="L4mcp service is unavailable")
    headers = {
        "Authorization": bearer,
        "Accept": request.headers.get("accept", "application/json, text/event-stream"),
    }
    for key in ("mcp-session-id", "mcp-protocol-version", "content-type"):
        if value := request.headers.get(key):
            headers[key] = value
    try:
        async with httpx.AsyncClient(timeout=35.0) as client:
            upstream = await client.request(
                request.method,
                f"{settings.l4mcp_url.rstrip('/')}/mcp",
                content=await request.body() if request.method == "POST" else None,
                headers=headers,
            )
    except httpx.RequestError as exc:
        raise HTTPException(
            status_code=502, detail="L4mcp service is unavailable"
        ) from exc
    response_headers = {}
    if session_id := upstream.headers.get("mcp-session-id"):
        response_headers["mcp-session-id"] = session_id
    return Response(
        content=upstream.content,
        status_code=upstream.status_code,
        media_type=upstream.headers.get("content-type", "application/json"),
        headers=response_headers,
    )


@router.get("/mcp/tools")
async def mcp_tools_list(
    request: Request,
    user: dict[str, Any] = Depends(get_current_user),
) -> dict[str, Any]:
    """Retain the old convenience endpoint with the new bearer authorization."""
    bearer = _require_mcp_user(request, user)
    if not settings.l4mcp_url:
        raise HTTPException(status_code=503, detail="L4mcp service is unavailable")
    try:
        async with httpx.AsyncClient(timeout=10.0) as client:
            response = await client.post(
                f"{settings.l4mcp_url.rstrip('/')}/mcp",
                json={"jsonrpc": "2.0", "id": 1, "method": "tools/list", "params": {}},
                headers={
                    "Authorization": bearer,
                    "Accept": "application/json, text/event-stream",
                },
            )
    except httpx.RequestError as exc:
        raise HTTPException(
            status_code=502, detail="L4mcp service is unavailable"
        ) from exc
    if response.status_code >= 400:
        raise HTTPException(status_code=502, detail="L4mcp tool list unavailable")
    if "text/event-stream" in response.headers.get("content-type", ""):
        for line in response.text.splitlines():
            if line.startswith("data: "):
                data = json.loads(line[6:])
                if "result" in data:
                    return data["result"]
        return {"tools": []}
    return response.json().get("result", {"tools": []})
