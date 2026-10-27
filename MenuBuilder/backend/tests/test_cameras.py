from datetime import UTC, datetime
from types import SimpleNamespace
from unittest.mock import AsyncMock
from uuid import uuid4

import pytest
from fastapi import HTTPException
from httpx import ASGITransport, AsyncClient
from pydantic import ValidationError

from app.auth import get_current_user
from app.database import get_db
from app.main import app
from app.routers import cameras
from app.services.camera_platform import camera_main_app_online, camera_snapshot


def test_native_patch_uses_accepted_schema_and_larger_deadline():
    command = cameras.CameraCommand(
        operation_id=uuid4(),
        action="patch",
        expected_revision="a" * 64,
        allow_disruptive=True,
        patch={
            "video0": {"codec": "h265", "size": "2592x1944"},
            "image": {"mirror": True},
        },
    )
    code, item = command.rpc()
    assert code == 161 and item["timeout_ms"] == 30000
    assert item["patch"] == command.patch
    with pytest.raises(HTTPException) as error:
        cameras.CameraCommand(
            operation_id=uuid4(),
            action="patch",
            expected_revision="a" * 64,
            allow_disruptive=True,
            patch={"credentials": {"password": "private-input"}},
        )
    assert error.value.detail == {"code": "invalid_camera_command"}


def test_discovery_cancel_and_binding_preserve_separate_identities():
    target = uuid4()
    command = cameras.CameraCommand(
        operation_id=uuid4(),
        action="cancel_discovery",
        target_operation_id=target,
        allow_disruptive=True,
    )
    code, item = command.rpc()
    assert code == 165 and item["action"] == "cancel"
    assert item["operation_id"] != item["target_operation_id"] == str(target)
    bind = cameras.CameraCommand(
        operation_id=uuid4(),
        action="bind",
        expected_revision="a" * 64,
        candidate_mac="02:00:00:00:00:01",
        discovery_generation=2,
        allow_disruptive=True,
    )
    assert bind.rpc()[0] == 166
    with pytest.raises(HTTPException):
        cameras.CameraCommand(
            operation_id=uuid4(),
            action="bind",
            expected_revision="a" * 64,
            allow_disruptive=True,
        )


@pytest.mark.anyio
async def test_discovery_rest_opt_in_and_task_identity_are_explicit(
    context, monkeypatch
):
    _, upstream, _ = context
    monkeypatch.setattr(cameras, "require_camera_extension", AsyncMock(return_value={}))
    command = cameras.CameraCommand(
        operation_id=uuid4(), action="discover", allow_disruptive=True
    )
    await cameras.command_camera(4619, command, {"org_id": 1}, AsyncMock())
    task = upstream.await_args.kwargs["body"]
    assert task["camera_contract_revision"] == "siplite-camera/1.0.0"
    assert task["method_code"] == 165
    assert task["payload"]["dt"][0]["operation_id"] == str(command.operation_id)


@pytest.mark.anyio
@pytest.mark.parametrize("verified,generation", [(False, 2), (True, 1)])
async def test_binding_rejects_unverified_port_or_stale_discovery(
    context, monkeypatch, verified, generation
):
    _, upstream, _ = context
    gate = AsyncMock(
        return_value={
            "revision": "a" * 64,
            "discovery": {
                "generation": generation,
                "candidates": [{"mac": "02:00:00:00:00:01", "port_verified": verified}],
            },
        }
    )
    monkeypatch.setattr(cameras, "require_camera_extension", gate)
    command = cameras.CameraCommand(
        operation_id=uuid4(),
        action="bind",
        expected_revision="a" * 64,
        candidate_mac="02:00:00:00:00:01",
        discovery_generation=2,
        allow_disruptive=True,
    )
    with pytest.raises(HTTPException) as error:
        await cameras.command_camera(4619, command, {"org_id": 1}, AsyncMock())
    assert error.value.detail == {"code": "camera_candidate_changed"}
    assert all(call.args[0] == "GET" for call in upstream.await_args_list)


@pytest.mark.anyio
async def test_extended_writer_requires_fresh_advertised_method(context):
    _, upstream, _ = context
    camera = {
        "camera_id": "cam:ip000001",
        "camera_contract_revision": "siplite-camera/1.0.0",
        "capabilities": {"methods": [160, 161]},
        "revision": "a" * 64,
    }
    snapshot = {"observed_at": datetime.now(UTC).isoformat(), "cameras": [camera]}
    upstream.return_value = {
        "items": [
            {"device_id": 4619, "type": 84, "gauges": {"300": [{"450": snapshot}]}}
        ]
    }
    assert await cameras.require_camera_extension(4619, 1, {}, 161) == camera
    snapshot["observed_at"] = "2000-01-01T00:00:00Z"
    with pytest.raises(HTTPException) as error:
        await cameras.require_camera_extension(4619, 1, {}, 161)
    assert error.value.status_code == 409


@pytest.mark.anyio
async def test_patch_revision_conflict_stops_before_task_creation(context, monkeypatch):
    _, upstream, _ = context
    gate = AsyncMock(return_value={"revision": "b" * 64})
    monkeypatch.setattr(cameras, "require_camera_extension", gate)
    body = cameras.CameraCommand(
        operation_id=uuid4(),
        action="patch",
        expected_revision="a" * 64,
        allow_disruptive=True,
        patch={"video0": {"codec": "h265"}},
    )
    with pytest.raises(HTTPException) as error:
        await cameras.command_camera(4619, body, {"org_id": 1}, AsyncMock())
    assert error.value.status_code == 409
    assert all(call.args[0] == "GET" for call in upstream.await_args_list)


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.fixture
def context(monkeypatch):
    terminal = SimpleNamespace(org_id=1, sn="fixture")
    access = AsyncMock(return_value=terminal)
    upstream = AsyncMock(
        return_value={
            "items": [
                {
                    "device_id": 4619,
                    "device_tags": [{"tag": "camera", "value": "openipc"}],
                }
            ]
        }
    )
    control = AsyncMock(
        return_value={"agent": {"online": True}, "lease": {"active": False}}
    )
    monkeypatch.setattr(cameras, "_verify_device_access", access)
    monkeypatch.setattr(cameras, "camera_request", upstream)
    monkeypatch.setattr(cameras.iot_client, "remote_input_status", control)
    return access, upstream, control


@pytest.mark.anyio
async def test_membership_and_tenant_denial_prevent_dispatch(context):
    access, upstream, control = context
    user = {"role_id": 3, "org_id": 1}
    access.side_effect = HTTPException(403, "wrong tenant")
    with pytest.raises(HTTPException) as error:
        await cameras.camera_access(4619, user, AsyncMock())
    assert error.value.status_code == 403
    upstream.assert_not_awaited()
    access.side_effect = None
    upstream.return_value = {
        "items": [
            {"device_id": 4619, "device_tags": [{"tag": "sys", "value": "ESP32"}]}
        ]
    }
    with pytest.raises(HTTPException) as error:
        await cameras.camera_access(4619, user, AsyncMock())
    assert error.value.status_code == 404
    control.assert_not_awaited()


@pytest.mark.anyio
@pytest.mark.parametrize("active", [True, None])
async def test_busy_or_unknown_lease_never_submits(context, active):
    access, upstream, control = context
    control.return_value = {"agent": {"online": True}, "lease": {"active": active}}
    request = cameras.CameraCommand(
        operation_id=uuid4(),
        action="power",
        value="restart",
        expected_power_on=True,
        allow_disruptive=True,
    )
    with pytest.raises(HTTPException) as error:
        await cameras.command_camera(4619, request, {"org_id": 1}, AsyncMock())
    assert error.value.status_code == 409
    assert len(upstream.await_args_list) == 1  # membership read only
    assert access.await_args.kwargs["require_active"] is True


@pytest.mark.anyio
async def test_unknown_lease_is_not_reported_as_idle(context):
    _, _, control = context
    control.return_value = {"agent": {"online": True}, "lease": {}}
    result = await cameras.get_camera(4619, {"org_id": 1}, AsyncMock())
    assert result["lease_active"] is None


@pytest.mark.anyio
async def test_idle_maps_only_fixed_camera_rpc(context):
    _, upstream, _ = context
    request = cameras.CameraCommand(
        operation_id=uuid4(),
        action="quality",
        value="hd",
        expected_revision="a" * 64,
        allow_disruptive=True,
    )
    await cameras.command_camera(4619, request, {"org_id": 1}, AsyncMock())
    call = upstream.await_args
    assert call.args[:3] == ("POST", "device-tasks/", 1)
    assert call.kwargs["body"] == {
        "ext_task_id": str(request.operation_id),
        "device_id": 4619,
        "method_code": 163,
        "ttl": 1,
        "payload": {
            "dt": [
                {
                    "camera_id": "cam:ip000001",
                    "timeout_ms": 15000,
                    "allow_disruptive": True,
                    "expected_revision": "a" * 64,
                    "preset": "hd",
                }
            ]
        },
    }


@pytest.mark.anyio
async def test_task_device_or_method_mismatch_is_hidden(context):
    _, upstream, _ = context
    upstream.side_effect = [
        {
            "items": [
                {
                    "device_id": 4619,
                    "device_tags": [{"tag": "camera", "value": "openipc"}],
                }
            ]
        },
        {"header": {"device_id": 4623, "method_code": 164}},
    ]
    with pytest.raises(HTTPException) as error:
        await cameras.get_camera_command(
            4619, uuid4(), {"role_id": 3, "org_id": 1}, AsyncMock()
        )
    assert error.value.status_code == 404


@pytest.mark.anyio
async def test_viewer_cannot_submit_even_refresh(context):
    _, upstream, _ = context
    app.dependency_overrides[get_current_user] = lambda: {
        "role_id": 4,
        "role": "viewer",
        "org_id": 1,
    }
    app.dependency_overrides[get_db] = lambda: AsyncMock()
    try:
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            response = await client.post(
                "/api/cameras/4619/commands",
                json={"operation_id": str(uuid4()), "action": "refresh"},
            )
        assert response.status_code == 403
        upstream.assert_not_awaited()
    finally:
        app.dependency_overrides.clear()


@pytest.mark.parametrize(
    "item",
    [
        {"action": "power", "value": "restart"},
        {
            "action": "power",
            "value": "off",
            "expected_power_on": "true",
            "allow_disruptive": True,
        },
        {
            "action": "quality",
            "value": "unknown",
            "expected_revision": "a" * 64,
            "allow_disruptive": True,
        },
        {"action": "refresh", "url": "http://localhost"},
    ],
)
def test_invalid_commands_are_rejected(item):
    with pytest.raises(ValidationError):
        cameras.CameraCommand.model_validate({"operation_id": str(uuid4()), **item})


@pytest.mark.anyio
async def test_offline_terminal_never_queues_settings(context):
    _, upstream, control = context
    control.return_value = {"agent": {"online": False}, "lease": {"active": False}}
    request = cameras.CameraCommand(
        operation_id=uuid4(),
        action="power",
        value="on",
        expected_power_on=False,
        allow_disruptive=True,
    )
    with pytest.raises(HTTPException) as error:
        await cameras.command_camera(4619, request, {"org_id": 1}, AsyncMock())
    assert error.value.status_code == 409
    assert len(upstream.await_args_list) == 1


def test_gauge_snapshot_is_device_scoped():
    snapshot = {"snapshot_id": "fixture", "cameras": []}
    assert (
        camera_snapshot(
            {
                "items": [
                    {
                        "device_id": 4619,
                        "type": "84",
                        "gauges": {"300": [{"450": snapshot}]},
                    }
                ]
            },
            4619,
        )
        == snapshot
    )
    assert (
        camera_snapshot(
            {
                "items": [
                    {
                        "device_id": 4623,
                        "type": "84",
                        "gauges": {"300": [{"450": snapshot}]},
                    }
                ]
            },
            4619,
        )
        is None
    )


@pytest.mark.parametrize(
    "extra",
    [
        {"action": "apply"},
        {"action": "search"},
        {"pairs": [[10, 11]]},
        {"method_code": 168},
        {"camera_mac": "01:00:00:00:00:01"},
        {"action": "start"},
        {"boot_id": str(uuid4())},
    ],
)
def test_ircut_ui_cannot_scan_assign_pins_or_start_without_inspection(extra):
    with pytest.raises(ValidationError):
        cameras.CameraIrcutCommand.model_validate(
            {
                "operation_id": str(uuid4()),
                "action": "inspect",
                "camera_mac": "02:00:00:00:00:01",
                **extra,
            }
        )


@pytest.mark.anyio
@pytest.mark.parametrize("active", [True, None])
async def test_ircut_does_not_interrupt_existing_or_unknown_lease(context, active):
    _, upstream, control = context
    control.return_value = {"lease": {"active": active}}
    command = cameras.CameraIrcutCommand(
        operation_id=uuid4(),
        action="start",
        camera_mac="02:00:00:00:00:01",
        session_id=uuid4(),
        boot_id=uuid4(),
        expected_revision="a" * 64,
        allow_disruptive=True,
    )
    with pytest.raises(HTTPException) as error:
        await cameras.diagnose_camera_ircut(4619, command, {"org_id": 1}, AsyncMock())
    assert error.value.status_code == 409
    assert not any(call.args[0] == "POST" for call in upstream.await_args_list)


@pytest.mark.anyio
async def test_ircut_uses_its_own_revision_and_only_configured_diagnostic(
    context, monkeypatch
):
    _, upstream, _ = context
    monkeypatch.setattr(cameras, "camera_main_app_online", AsyncMock(return_value=True))
    session = uuid4()
    command = cameras.CameraIrcutCommand(
        operation_id=uuid4(),
        action="start",
        camera_mac="02:00:00:00:00:01",
        session_id=session,
        boot_id=uuid4(),
        expected_revision="a" * 64,
        allow_disruptive=True,
    )
    await cameras.diagnose_camera_ircut(4619, command, {"org_id": 1}, AsyncMock())
    payload = upstream.await_args.kwargs["body"]
    assert payload["method_code"] == 169
    assert "camera_contract_revision" not in payload
    item = payload["payload"]["dt"][0]
    assert item["session_id"] == str(session)
    assert item["ircut_revision"] == "siplite-ircut/1.0.0"
    assert "pairs" not in item and item["budget_ms"] == 300000


@pytest.mark.anyio
async def test_ircut_cleanup_does_not_depend_on_start_lease_check(context):
    _, upstream, control = context
    for action in ("status", "cancel", "recover"):
        await cameras.diagnose_camera_ircut(
            4619,
            cameras.CameraIrcutCommand(
                operation_id=uuid4(),
                action=action,
                camera_mac="02:00:00:00:00:01",
                session_id=uuid4(),
            ),
            {"org_id": 1},
            AsyncMock(),
        )
        assert upstream.await_args.kwargs["body"]["method_code"] == 169
    control.assert_not_awaited()


@pytest.mark.anyio
@pytest.mark.parametrize(
    "revision,online,expected",
    [
        ("siplite-camera/1.4.0", True, True),
        ("siplite-camera/1.4.0", False, False),
        ("siplite-camera/1.0.0", True, None),
    ],
)
async def test_main_app_presence_checks_the_requested_media_revision(
    monkeypatch, revision, online, expected
):
    inventory = AsyncMock(
        return_value={"camera_contract_revision": revision, "main_app_online": online}
    )
    monkeypatch.setattr(cameras.iot_client, "remote_video_v2", inventory)
    assert await camera_main_app_online("fixture", 1, {"org_id": 1}) is expected
    assert inventory.await_args.kwargs["camera_contract"] is True
