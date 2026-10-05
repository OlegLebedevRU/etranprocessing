"""Tenant BFF. Control/metadata only; file bodies never enter this router."""

import logging
import math
from datetime import UTC, datetime
from typing import Any, Literal
from uuid import UUID

import httpx
from fastapi import APIRouter, Depends, Header, HTTPException, Response
from pydantic import BaseModel, ConfigDict, Field
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.database import get_db
from app.routers.video import _verify_device_access
from app.security.permissions import require_permission
from app.services.iot_client import iot_client

logger = logging.getLogger(__name__)


def drain_seconds(expires_at: str) -> int:
    # IoT keeps the files slot until the last authorised deadline + 5 seconds.
    deadline = datetime.fromisoformat(expires_at)
    return max(0, math.ceil((deadline - datetime.now(UTC)).total_seconds() + 5))


def no_store(response: Response) -> None:
    response.headers["Cache-Control"] = "no-store"


router = APIRouter(
    prefix="/api/file-manager/v1",
    tags=["file-manager"],
    dependencies=[Depends(no_store)],
)


class OperationBody(BaseModel):
    model_config = ConfigDict(extra="forbid")
    id: UUID
    lease_id: UUID
    kind: Literal["upload", "download"]
    path: str = Field(max_length=1024)


class SignalBody(BaseModel):
    model_config = ConfigDict(extra="forbid")
    action: Literal["renew", "stop", "cancel"]
    operation_id: UUID | None = None


class ManifestBody(BaseModel):
    model_config = ConfigDict(extra="forbid")
    size_bytes: int = Field(ge=0, le=64 * 1024 * 1024)
    sha256: str = Field(pattern=r"^[0-9a-f]{64}$")


class NavigationBody(BaseModel):
    model_config = ConfigDict(extra="forbid")
    path: str = Field(min_length=3, max_length=1024)
    offset: int = Field(default=0, ge=0, le=1_000_000)


async def fm_user(
    user: dict[str, Any] = Depends(require_permission("files:read")),
) -> dict[str, Any]:
    # Viewer cannot acquire an exclusive control lease or send MQTT commands.
    if user.get("role_id") == 4 or user.get("role") == "viewer":
        raise HTTPException(403, "File-manager control permission required")
    return user


def actor(user: dict[str, Any], tenant: int, view: UUID) -> dict[str, str]:
    headers = iot_client._get_headers(tenant, user=user)
    session = headers.get("X-Session-Id", "")
    if not session or len(session) > 80:
        raise HTTPException(403, "Authenticated browser session required")
    headers["X-Session-Id"] = f"{session}:{view}"
    return headers


async def upstream(
    target: Literal["pb", "iot"],
    path: str,
    headers: dict[str, str],
    body: dict | None = None,
) -> dict:
    if target == "pb":
        base, key = settings.file_manager_pb_url, settings.file_manager_service_key
        auth_headers = {**headers, "X-File-Manager-Service-Key": key}
        auth_headers.pop("X-Internal-Service-Key", None)
        prefix = "/api/internal/file-manager/v1"
    else:
        base, key = iot_client.base_url, iot_client.service_token
        auth_headers = headers
        prefix = "/api/internal/v1/file-manager"
    if not base or not key:
        raise HTTPException(503, detail={"code": "fm_unconfigured"})
    try:
        async with httpx.AsyncClient(timeout=8, follow_redirects=False) as client:
            response = await client.request(
                "POST" if body is not None else "GET",
                f"{base.rstrip('/')}{prefix}{path}",
                headers=auth_headers,
                json=body,
            )
        if response.status_code not in (200, 201):
            detail = response.json().get("detail", {"code": "fm_upstream_failed"})
            if (
                isinstance(detail, dict)
                and detail.get("code") == "lease_taken"
                and detail.get("scope") == "files"
            ):
                detail = {
                    **detail,
                    "retry_after_sec": drain_seconds(detail["expires_at"]),
                }
            raise HTTPException(
                response.status_code
                if response.status_code in (401, 403, 404, 409, 422, 503)
                else 502,
                detail=detail,
            )
        return response.json()
    except (httpx.RequestError, ValueError, TypeError, KeyError) as exc:
        logger.warning(
            "FM upstream unavailable target=%s path=%s error=%s",
            target,
            path,
            type(exc).__name__,
        )
        raise HTTPException(503, detail={"code": "fm_control_unavailable"}) from exc


async def readiness_for(device_id: int, headers: dict[str, str]) -> dict:
    result = await upstream("pb", f"/devices/{device_id}/readiness", headers)
    try:
        device = await iot_client.get_console_device(
            device_id, int(headers["X-Org-Id"])
        )
        connection = (device or {}).get("connection") or {}
        mqtt = bool(
            connection.get("svc_connect") is True
            and connection.get("is_svc_available") is True
        )
    except httpx.HTTPError, HTTPException:
        mqtt = False
    result["mqtt_available"] = mqtt
    result["available"] = result.get("available") is True and mqtt
    if not mqtt and result.get("state") == "ready":
        result["state"] = "mqtt_unavailable"
    return result


@router.get("/devices/{device_id}/readiness")
async def readiness(
    device_id: int,
    response: Response,
    x_fm_view_id: UUID = Header(),
    user: dict = Depends(fm_user),
    db: AsyncSession = Depends(get_db),
):
    terminal = await _verify_device_access(device_id, user, db)
    response.headers["Cache-Control"] = "no-store"
    return await readiness_for(device_id, actor(user, terminal.org_id, x_fm_view_id))


@router.post("/devices/{device_id}/sessions")
async def start(
    device_id: int,
    x_fm_view_id: UUID = Header(),
    user: dict = Depends(fm_user),
    db: AsyncSession = Depends(get_db),
):
    terminal = await _verify_device_access(device_id, user, db, require_active=True)
    headers = actor(user, terminal.org_id, x_fm_view_id)
    if not (await readiness_for(device_id, headers))["available"]:
        raise HTTPException(409, detail={"code": "fm_agent_unavailable"})
    lease = await upstream(
        "iot", f"/devices/{device_id}/sessions", headers, {"ttl_sec": 60}
    )
    identifier = lease["lease_id"]
    try:
        await upstream(
            "pb", f"/devices/{device_id}/sessions", headers, {"lease_id": identifier}
        )
        await upstream(
            "iot", f"/sessions/{identifier}/signals", headers, {"action": "start"}
        )
    except Exception as failure:
        try:
            await upstream(
                "iot", f"/sessions/{identifier}/signals", headers, {"action": "stop"}
            )
        except Exception as exc:  # noqa: BLE001 - cleanup must preserve the original failure.
            logger.warning(
                "FM start cleanup failed; lease will expire: %s",
                type(exc).__name__,
                extra={"lease_id": identifier},
            )
        if isinstance(failure, HTTPException):
            raise HTTPException(
                failure.status_code,
                detail={
                    "code": "fm_start_failed",
                    "retry_after_sec": drain_seconds(lease["expires_at"]),
                },
            ) from failure
        raise
    return lease


@router.get("/devices/{device_id}/operations/{identifier}")
async def status(
    device_id: int,
    identifier: UUID,
    response: Response,
    x_fm_view_id: UUID = Header(),
    user: dict = Depends(fm_user),
    db: AsyncSession = Depends(get_db),
):
    terminal = await _verify_device_access(device_id, user, db)
    headers = actor(user, terminal.org_id, x_fm_view_id)
    result = await upstream("pb", f"/operations/{identifier}", headers)
    owned = await upstream("iot", f"/sessions/{result['lease_id']}", headers)
    if owned.get("device_id") != device_id:
        raise HTTPException(404, "Operation not found")
    response.headers["Cache-Control"] = "no-store"
    return result


@router.post("/devices/{device_id}/operations")
async def create(
    device_id: int,
    body: OperationBody,
    x_fm_view_id: UUID = Header(),
    user: dict = Depends(fm_user),
    db: AsyncSession = Depends(get_db),
):
    terminal = await _verify_device_access(device_id, user, db, require_active=True)
    headers = actor(user, terminal.org_id, x_fm_view_id)
    result = await upstream(
        "pb", f"/devices/{device_id}/operations", headers, body.model_dump(mode="json")
    )
    if body.kind != "upload":
        await upstream(
            "iot",
            f"/sessions/{body.lease_id}/signals",
            headers,
            {
                "action": "transfer",
                "operation_id": str(body.id),
            },
        )
    return result


@router.post("/devices/{device_id}/sessions/{identifier}/signals")
async def signal(
    device_id: int,
    identifier: UUID,
    body: SignalBody,
    x_fm_view_id: UUID = Header(),
    user: dict = Depends(fm_user),
    db: AsyncSession = Depends(get_db),
):
    terminal = await _verify_device_access(
        device_id, user, db, require_active=body.action == "renew"
    )
    headers = actor(user, terminal.org_id, x_fm_view_id)
    closing_query = "?closing=true" if body.action in ("stop", "cancel") else ""
    lease = await upstream("iot", f"/sessions/{identifier}{closing_query}", headers)
    if lease.get("device_id") != device_id:
        raise HTTPException(404, "Session not found")
    if body.action == "cancel" and body.operation_id is None:
        raise HTTPException(422, "Operation required")
    if body.action == "cancel" and not lease.get("closed"):
        try:
            await upstream("pb", f"/operations/{body.operation_id}/abort", headers, {})
        except Exception:
            # Abort metadata failure must not keep the terminal worker authorised.
            try:
                await upstream(
                    "iot",
                    f"/sessions/{identifier}/signals",
                    headers,
                    {"action": "stop"},
                )
            except Exception as cleanup_error:  # noqa: BLE001 - preserve the abort failure.
                logger.warning(
                    "FM close after abort failure: %s", type(cleanup_error).__name__
                )
            raise
    result = await upstream(
        "iot",
        f"/sessions/{identifier}/signals",
        headers,
        body.model_dump(mode="json", exclude_none=True),
    )
    if body.action in ("stop", "cancel"):
        result["retry_after_sec"] = (
            0 if result.get("stopped") is True else drain_seconds(result["expires_at"])
        )
    return result


@router.post("/devices/{device_id}/operations/{identifier}/manifest")
async def manifest(
    device_id: int,
    identifier: UUID,
    body: ManifestBody,
    x_fm_view_id: UUID = Header(),
    user: dict = Depends(fm_user),
    db: AsyncSession = Depends(get_db),
):
    terminal = await _verify_device_access(device_id, user, db, require_active=True)
    return await upstream(
        "pb",
        f"/operations/{identifier}/manifest",
        actor(user, terminal.org_id, x_fm_view_id),
        body.model_dump(),
    )


@router.post("/devices/{device_id}/operations/{identifier}/source-complete")
async def source_complete(
    device_id: int,
    identifier: UUID,
    x_fm_view_id: UUID = Header(),
    user: dict = Depends(fm_user),
    db: AsyncSession = Depends(get_db),
):
    terminal = await _verify_device_access(device_id, user, db, require_active=True)
    return await upstream(
        "pb",
        f"/operations/{identifier}/source-complete",
        actor(user, terminal.org_id, x_fm_view_id),
        {},
    )


@router.get("/devices/{device_id}/operations/{identifier}/download")
async def download(
    device_id: int,
    identifier: UUID,
    x_fm_view_id: UUID = Header(),
    user: dict = Depends(fm_user),
    db: AsyncSession = Depends(get_db),
):
    terminal = await _verify_device_access(device_id, user, db, require_active=True)
    return await upstream(
        "pb",
        f"/operations/{identifier}/download",
        actor(user, terminal.org_id, x_fm_view_id),
    )


@router.post("/devices/{device_id}/operations/{identifier}/received")
async def received(
    device_id: int,
    identifier: UUID,
    body: ManifestBody,
    x_fm_view_id: UUID = Header(),
    user: dict = Depends(fm_user),
    db: AsyncSession = Depends(get_db),
):
    terminal = await _verify_device_access(device_id, user, db, require_active=True)
    return await upstream(
        "pb",
        f"/operations/{identifier}/received",
        actor(user, terminal.org_id, x_fm_view_id),
        body.model_dump(),
    )


@router.post("/devices/{device_id}/sessions/{identifier}/navigation")
async def navigation(
    device_id: int,
    identifier: UUID,
    body: NavigationBody,
    x_fm_view_id: UUID = Header(),
    user: dict = Depends(fm_user),
    db: AsyncSession = Depends(get_db),
):
    terminal = await _verify_device_access(device_id, user, db, require_active=True)
    headers = actor(user, terminal.org_id, x_fm_view_id)
    lease = await upstream("iot", f"/sessions/{identifier}", headers)
    if lease.get("device_id") != device_id:
        raise HTTPException(404, "Session not found")
    return await upstream(
        "iot", f"/sessions/{identifier}/navigation", headers, body.model_dump()
    )
