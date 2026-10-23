"""Camera settings use the existing RPC and device relay guard, not a new lease."""

from __future__ import annotations

from datetime import UTC, datetime
from typing import Any, Literal
from uuid import UUID

from etranprocessing_db.camera_credentials import (
    CameraCredentialProfile,
    CameraCredentialReceipt,
)
from fastapi import APIRouter, Depends, HTTPException, Request
from pydantic import BaseModel, ConfigDict, Field, model_validator
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import require_tenant_context
from app.database import get_db
from app.routers.camera_request_route import CameraRequestRoute
from app.routers.video import _verify_device_access
from app.routers.video_control import require_operator_user
from app.security.permissions import PERMISSION_VIDEO_VIEW, require_permission
from app.services.camera_contract import CAMERA_REVISION, validate_camera_rpc
from app.services.camera_credential_recipient import (
    RecipientUnavailable,
    fetch_credential_recipient,
)
from app.services.camera_credential_vault import CameraCredentialVault, VaultError
from app.services.camera_credential_vault_config import CameraCredentialVaultSettings
from app.services.camera_credential_workflow import (
    dispatch_credentials,
    parse_credential_change,
    reconcile_credentials,
    scope_for,
)
from app.services.camera_platform import (
    camera_main_app_online,
    camera_request,
    camera_snapshot,
)
from app.services.iot_client import iot_client

router = APIRouter(
    prefix="/api/cameras", tags=["cameras"], route_class=CameraRequestRoute
)


class CameraCommand(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)
    operation_id: UUID = Field(strict=False)
    action: Literal[
        "refresh",
        "quality",
        "day_night",
        "power",
        "patch",
        "discover",
        "cancel_discovery",
        "bind",
        "unbind",
        "clear_credentials",
    ]
    value: str | None = None
    expected_revision: str | None = Field(default=None, pattern=r"^[0-9a-f]{64}$")
    expected_power_on: bool | None = None
    allow_disruptive: bool = False
    patch: dict[str, Any] | None = None
    target_operation_id: UUID | None = Field(default=None, strict=False)
    candidate_mac: str | None = Field(
        default=None, pattern=r"^([0-9a-f]{2}:){5}[0-9a-f]{2}$"
    )
    discovery_generation: int | None = Field(default=None, ge=0, le=2147483647)

    @model_validator(mode="after")
    def validate_action(self):
        values = {
            "refresh": (None,),
            "quality": ("medium", "hd"),
            "day_night": ("auto", "day", "night"),
            "power": ("on", "off", "restart"),
            "patch": (None,),
            "discover": (None,),
            "cancel_discovery": (None,),
            "bind": (None,),
            "unbind": (None,),
            "clear_credentials": (None,),
        }
        if self.value not in values[self.action]:
            raise ValueError("Unsupported camera action value")
        if (
            self.action
            in ("quality", "day_night", "patch", "bind", "unbind", "clear_credentials")
            and self.expected_revision is None
        ):
            raise ValueError("expected_revision is required")
        if self.action == "power" and self.expected_power_on is None:
            raise ValueError("expected_power_on is required")
        if self.action != "refresh" and not self.allow_disruptive:
            raise ValueError("Explicit disruptive consent is required")
        if self.action in (
            "patch",
            "discover",
            "cancel_discovery",
            "bind",
            "unbind",
            "clear_credentials",
        ):
            method, item = self.rpc()
            validate_camera_rpc(method, {"dt": [item]}, str(self.operation_id))
        if self.action != "patch" and self.patch is not None:
            raise ValueError("patch is only supported for the patch action")
        if self.action != "cancel_discovery" and self.target_operation_id is not None:
            raise ValueError(
                "target_operation_id is only supported for discovery cancellation"
            )
        if self.action != "bind" and (
            self.candidate_mac is not None or self.discovery_generation is not None
        ):
            raise ValueError("Candidate fields are only supported for binding")
        return self

    def rpc(self) -> tuple[int, dict[str, Any]]:
        if self.action in (
            "discover",
            "cancel_discovery",
            "bind",
            "unbind",
            "clear_credentials",
        ):
            item: dict[str, Any] = {
                "camera_id": "cam:ip000001",
                "timeout_ms": 30000,
                "operation_id": str(self.operation_id),
            }
            if self.action in ("discover", "cancel_discovery"):
                item["action"] = "scan" if self.action == "discover" else "cancel"
                if self.target_operation_id is not None:
                    item["target_operation_id"] = str(self.target_operation_id)
                return 165, item
            item.update(action=self.action, expected_revision=self.expected_revision)
            if self.action == "bind":
                item.update(
                    candidate_mac=self.candidate_mac,
                    discovery_generation=self.discovery_generation,
                )
            return 166, item
        item: dict[str, Any] = {"camera_id": "cam:ip000001", "timeout_ms": 15000}
        method = {
            "refresh": 160,
            "quality": 163,
            "day_night": 162,
            "power": 164,
            "patch": 161,
        }[self.action]
        if self.action != "refresh":
            item["allow_disruptive"] = self.allow_disruptive
        if self.action in ("quality", "day_night"):
            item["expected_revision"] = self.expected_revision
            item["preset" if self.action == "quality" else "mode"] = self.value
        if self.action == "power":
            item.update(action=self.value, expected_power_on=self.expected_power_on)
        if self.action == "patch":
            item.update(
                timeout_ms=30000,
                expected_revision=self.expected_revision,
                patch=self.patch,
            )
        return method, item


async def require_camera_extension(
    device_id: int, org_id: int, user: dict[str, Any], method: int
) -> dict[str, Any]:
    gauges = await camera_request(
        "GET", "gauges/", org_id, user, params={"device_id": device_id, "type": "84"}
    )
    snapshot = camera_snapshot(gauges, device_id) or {}
    camera = next(
        (
            item
            for item in snapshot.get("cameras", [])
            if isinstance(item, dict) and item.get("camera_id") == "cam:ip000001"
        ),
        {},
    )
    try:
        observed = datetime.fromisoformat(snapshot.get("observed_at", ""))
        age = (datetime.now(UTC) - observed).total_seconds()
        limit = snapshot.get("freshness_max_age_sec", 330)
        fresh = type(limit) in (int, float) and 1 <= limit <= 330 and -5 <= age <= limit
    except ValueError, TypeError:
        fresh = False
    if (
        not fresh
        or camera.get("stale")
        and method in (161, 162, 163)
        or camera.get("camera_contract_revision") != CAMERA_REVISION
        or method not in (camera.get("capabilities") or {}).get("methods", [])
    ):
        raise HTTPException(
            409,
            detail={
                "code": "camera_capability_unavailable",
                "message": "Обновите состояние камеры; новая версия управления ещё не подтверждена",
            },
        )
    return camera


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
    main_online = await camera_main_app_online(terminal.sn, terminal.org_id, user)
    return {
        "device_id": device_id,
        "terminal_online": main_online
        if main_online is not None
        else (control.get("agent") or {}).get("online") is True,
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
    if body.action == "clear_credentials":
        # Clearing must also update the encrypted server profile after RES/gauge84.
        raise HTTPException(409, detail={"code": "use_camera_credentials_endpoint"})
    terminal = await camera_access(device_id, user, db, write=body.action != "refresh")
    if body.action != "refresh":
        control = await iot_client.remote_input_status(
            sn=terminal.sn, org_id=terminal.org_id, user=user
        )
        if (control.get("agent") or {}).get(
            "online"
        ) is not True and await camera_main_app_online(
            terminal.sn, terminal.org_id, user
        ) is not True:
            raise HTTPException(
                409,
                {"code": "camera_terminal_offline", "message": "Терминал не в сети"},
            )
        if (control.get("lease") or {}).get("active") is not False:
            raise HTTPException(
                409, {"code": "camera_busy", "message": "Сначала остановите просмотр"}
            )
    method, item = body.rpc()
    if body.action in (
        "patch",
        "discover",
        "cancel_discovery",
        "bind",
        "unbind",
        "clear_credentials",
    ):
        camera = await require_camera_extension(
            device_id, terminal.org_id, user, method
        )
        if (
            body.expected_revision is not None
            and camera.get("revision") != body.expected_revision
        ):
            raise HTTPException(409, detail={"code": "camera_revision_changed"})
        if body.action == "bind":
            discovery = camera.get("discovery") or {}
            if (
                discovery.get("physical_port") != 2
                or discovery.get("generation") != body.discovery_generation
                or not any(
                    candidate.get("mac") == body.candidate_mac
                    and candidate.get("port_verified") is True
                    for candidate in discovery.get("candidates", [])
                )
            ):
                raise HTTPException(409, detail={"code": "camera_candidate_changed"})
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
            **(
                {"camera_contract_revision": CAMERA_REVISION}
                if body.action
                in (
                    "patch",
                    "discover",
                    "cancel_discovery",
                    "bind",
                    "unbind",
                    "clear_credentials",
                )
                else {}
            ),
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
        161,
        162,
        163,
        164,
        165,
        166,
    ):
        raise HTTPException(404, "Команда камеры не найдена")
    if header.get("method_code") == 166:
        await reconcile_credentials(
            db, terminal=terminal, user=user, operation_id=task_id, task=task
        )
    return task


@router.get("/{device_id}/credentials")
async def credential_metadata(
    device_id: int,
    user: dict[str, Any] = Depends(require_operator_user),
    db: AsyncSession = Depends(get_db),
):
    terminal = await camera_access(device_id, user, db)
    try:
        metadata = await CameraCredentialVault().safe_metadata(
            db, scope=scope_for(terminal)
        )
    except VaultError as error:
        if error.status_code != 404:
            raise HTTPException(
                error.status_code, detail={"code": error.code}
            ) from None
        metadata = {
            "has_active": False,
            "version": 0,
            "active_operation_id": None,
            "pending_operation_id": None,
        }
    last = await db.scalar(
        select(CameraCredentialReceipt)
        .join(CameraCredentialProfile)
        .where(
            CameraCredentialProfile.terminal_id == terminal.id,
            CameraCredentialProfile.tenant_id == terminal.org_id,
            CameraCredentialProfile.device_id == device_id,
            CameraCredentialProfile.sn == terminal.sn,
            CameraCredentialProfile.camera_id == "cam:ip000001",
            CameraCredentialReceipt.version == metadata["version"],
        )
    )
    metadata["last_operation_id"] = (
        str(last.operation_id)
        if last is not None
        and last.kind == "replace"
        and last.state in ("active", "pending", "failed")
        else None
    )
    metadata["last_state"] = last.state if last is not None else None
    metadata["latest_operation_id"] = (
        str(last.operation_id) if last is not None else None
    )
    metadata["latest_state"] = last.state if last is not None else None
    # Read-only metadata helper locks identity/profile; release before provider I/O.
    await db.commit()
    try:
        CameraCredentialVaultSettings().key()
        vault_ready = True
    except ValueError:
        vault_ready = False
    try:
        await fetch_credential_recipient(
            tenant_id=terminal.org_id, device_id=device_id, sn=terminal.sn
        )
        recipient_ready = True
    except RecipientUnavailable:
        recipient_ready = False
    return {**metadata, "vault_ready": vault_ready, "recipient_ready": recipient_ready}


@router.post("/{device_id}/credentials")
async def change_credentials(
    device_id: int,
    request: Request,
    user: dict[str, Any] = Depends(require_operator_user),
    db: AsyncSession = Depends(get_db),
):
    terminal = await camera_access(device_id, user, db, write=True)
    control = await iot_client.remote_input_status(
        terminal.sn, org_id=terminal.org_id, user=user
    )
    if (control.get("lease") or {}).get("active") is not False:
        raise HTTPException(
            409,
            detail={"code": "camera_busy", "message": "Сначала остановите просмотр"},
        )
    if await camera_main_app_online(terminal.sn, terminal.org_id, user) is not True:
        raise HTTPException(409, detail={"code": "camera_terminal_offline"})
    camera = await require_camera_extension(device_id, terminal.org_id, user, 166)
    if (
        request.headers.get("content-type", "").split(";", 1)[0].lower()
        != "application/json"
    ):
        raise HTTPException(415, detail={"code": "invalid_camera_credentials"})
    raw = bytearray()
    async for chunk in request.stream():
        if len(raw) + len(chunk) > 4096:
            raise HTTPException(413, detail={"code": "invalid_camera_credentials"})
        raw.extend(chunk)
    try:
        change = parse_credential_change(raw)
    finally:
        raw[:] = b"\0" * len(raw)
        raw.clear()
    return await dispatch_credentials(
        db, terminal=terminal, user=user, change=change, camera=camera
    )
