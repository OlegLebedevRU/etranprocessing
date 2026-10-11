"""Camera settings use the existing RPC and device relay guard, not a new lease."""

from __future__ import annotations

from typing import Any, Literal
from uuid import UUID

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, ConfigDict, Field, model_validator
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import require_tenant_context
from app.database import get_db
from app.routers.video import _verify_device_access
from app.routers.video_control import require_operator_user
from app.security.permissions import PERMISSION_VIDEO_VIEW, require_permission
from app.services.camera_platform import camera_request, camera_snapshot
from app.services.iot_client import iot_client

router = APIRouter(prefix="/api/cameras", tags=["cameras"])


class CameraCommand(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)
    operation_id: UUID = Field(strict=False)
    action: Literal["refresh", "quality", "day_night", "power"]
    value: str | None = None
    expected_revision: str | None = Field(default=None, pattern=r"^[0-9a-f]{64}$")
    expected_power_on: bool | None = None
    allow_disruptive: bool = False

    @model_validator(mode="after")
    def validate_action(self):
        values = {
            "refresh": (None,),
            "quality": ("medium", "hd"),
            "day_night": ("auto", "day", "night"),
            "power": ("on", "off", "restart"),
        }
        if self.value not in values[self.action]:
            raise ValueError("Unsupported camera action value")
        if self.action in ("quality", "day_night") and self.expected_revision is None:
            raise ValueError("expected_revision is required")
        if self.action == "power" and self.expected_power_on is None:
            raise ValueError("expected_power_on is required")
        if self.action != "refresh" and not self.allow_disruptive:
            raise ValueError("Explicit disruptive consent is required")
        return self

    def rpc(self) -> tuple[int, dict[str, Any]]:
        item: dict[str, Any] = {"camera_id": "cam:ip000001", "timeout_ms": 15000}
        method = {"refresh": 160, "quality": 163, "day_night": 162, "power": 164}[
            self.action
        ]
        if self.action != "refresh":
            item["allow_disruptive"] = self.allow_disruptive
        if self.action in ("quality", "day_night"):
            item["expected_revision"] = self.expected_revision
            item["preset" if self.action == "quality" else "mode"] = self.value
        if self.action == "power":
            item.update(action=self.value, expected_power_on=self.expected_power_on)
        return method, item


async def camera_access(
    device_id: int, user: dict[str, Any], db: AsyncSession, *, write: bool = False
):
    terminal = await _verify_device_access(device_id, user, db, require_active=write)
    data = await camera_request(
        "GET",
        "devices/",
        terminal.org_id,
        user,
        params={"device_id": device_id, "size": 1},
    )
    device = next(
        (item for item in data.get("items", []) if item.get("device_id") == device_id),
        None,
    )
    if not device or not any(
        tag.get("tag") == "camera" and tag.get("value") == "openipc"
        for tag in device.get("device_tags", [])
    ):
        raise HTTPException(404, "Терминал не назначен камерой OpenIPC")
    return terminal


@router.get("/{device_id}")
async def get_camera(
    device_id: int,
    user: dict[str, Any] = Depends(require_permission(PERMISSION_VIDEO_VIEW)),
    db: AsyncSession = Depends(get_db),
):
    terminal = await camera_access(device_id, user, db)
    try:
        gauges = await camera_request(
            "GET",
            "gauges/",
            terminal.org_id,
            user,
            params={"device_id": device_id, "type": "84"},
        )
    except HTTPException as exc:
        if exc.status_code != 404:
            raise
        gauges = {"items": []}
    control = await iot_client.remote_input_status(
        sn=terminal.sn, org_id=terminal.org_id, user=user
    )
    return {
        "device_id": device_id,
        "terminal_online": (control.get("agent") or {}).get("online") is True,
        "snapshot": camera_snapshot(gauges, device_id),
        "lease_active": (
            active
            if isinstance(active := (control.get("lease") or {}).get("active"), bool)
            else None
        ),
    }


@router.post("/{device_id}/commands")
async def command_camera(
    device_id: int,
    body: CameraCommand,
    user: dict[str, Any] = Depends(require_operator_user),
    db: AsyncSession = Depends(get_db),
):
    terminal = await camera_access(device_id, user, db, write=body.action != "refresh")
    if body.action != "refresh":
        control = await iot_client.remote_input_status(
            sn=terminal.sn, org_id=terminal.org_id, user=user
        )
        if (control.get("agent") or {}).get("online") is not True:
            raise HTTPException(
                409,
                {"code": "camera_terminal_offline", "message": "Терминал не в сети"},
            )
        if (control.get("lease") or {}).get("active") is not False:
            raise HTTPException(
                409, {"code": "camera_busy", "message": "Сначала остановите просмотр"}
            )
    method, item = body.rpc()
    return await camera_request(
        "POST",
        "device-tasks/",
        terminal.org_id,
        user,
        body={
            "ext_task_id": str(body.operation_id),
            "device_id": device_id,
            "method_code": method,
            "ttl": 1,
            "payload": {"dt": [item]},
        },
    )


@router.get("/{device_id}/commands/{task_id}")
async def get_camera_command(
    device_id: int,
    task_id: UUID,
    user: dict[str, Any] = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
):
    await require_operator_user(user)
    terminal = await camera_access(device_id, user, db)
    task = await camera_request("GET", f"device-tasks/{task_id}", terminal.org_id, user)
    header = task.get("header") or {}
    if header.get("device_id") != device_id or header.get("method_code") not in (
        160,
        162,
        163,
        164,
    ):
        raise HTTPException(404, "Команда камеры не найдена")
    return task
