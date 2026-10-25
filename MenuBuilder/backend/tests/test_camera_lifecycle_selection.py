"""Real BFF routes and HTTP adapters must preserve stored camera selection."""

# ruff: noqa: F811

import copy
import json
from types import SimpleNamespace
from unittest.mock import AsyncMock
from uuid import uuid4

import httpx
import pytest
from fastapi import FastAPI, HTTPException
from pydantic import ValidationError
from test_camera_admission import admission, anyio_backend, ready_ticket  # noqa: F401
from test_camera_video import context  # noqa: F401

from app.config import settings
from app.models_l4desk import L4DeskRemoteSession
from app.routers import remote_sessions, video, video_control
from app.services import camera_video
from app.services.camera_contract import CAMERA_MEDIA_HEADERS, CAMERA_MEDIA_REVISION
from app.services.iot_client import IotPlatformClient
from app.services.media_orchestrator_client import MediaOrchestratorClient
from tests.test_hevc_contract import fixture


@pytest.fixture
async def lifecycle(admission, monkeypatch):
    service, args, *_ = admission
    ready = await ready_ticket(admission)
    started = await service.native.start(**args, admission_ticket=ready["ticket"])
    started["media"]["pin"] = "fixture-pin"
    state = {
        k: copy.deepcopy(v)
        for k, v in started.items()
        if k not in ("media", "codec_reason")
    }
    stream = state["stream_instance_id"]
    lease = await service.native.iot.remote_video_v2(
        args["terminal"].sn, "lease", camera_contract=CAMERA_MEDIA_REVISION
    )
    lease = dict(lease, stream_instance_id=stream)
    terminal = args["terminal"]
    terminal.id = 1
    session = L4DeskRemoteSession(
        id=11,
        tenant_id=1,
        terminal_id=1,
        session_type="video",
        state="active",
        provider_session_id=stream,
        operation_id=f"op-stream-v2-camera13-{stream}",
    )
    # This is the native CTLv1 presence token, NOT a desktop USB classification.
    status = {
        "lease": lease,
        "agent": {
            "online": True,
            "stream": {
                "mode": "usb-camera",
                "source_id": "cam:ip000001",
                "state": "running",
                "stream_instance_id": stream,
            },
        },
    }
    seen = []
    protocol = SimpleNamespace(record="camera")
    health = fixture("media_health")
    health.update(
        {
            key: started["media"][key]
            for key in (
                "session_id",
                "stream_instance_id",
                "descriptor",
                "publisher",
                "lease_id",
                "operation_id",
            )
        }
    )
    health["state"] = "active"

    def respond(request):
        path = request.url.path
        body = json.loads(request.content) if request.content else None
        seen.append(
            {
                "path": path,
                "method": request.method,
                "headers": dict(request.headers),
                "body": body,
            }
        )
        if path.endswith("/status"):
            return httpx.Response(200, json=status)
        if path.endswith(("/video-state", "/video-lease", "/video-stop")):
            if protocol.record == "desktop":
                assert not any(k.startswith("x-l4-camera-") for k in request.headers)
                return httpx.Response(404, json={"detail": "not_found"})
            if any(
                request.headers.get(k) != v for k, v in CAMERA_MEDIA_HEADERS.items()
            ):
                return httpx.Response(
                    422, json={"detail": "camera3_incomplete_revision_bundle"}
                )
            return httpx.Response(
                200,
                json=state
                if path.endswith("/video-state")
                else lease
                if path.endswith("/video-lease")
                else {"result": "stopped"},
            )
        if path.endswith("/keepalive"):
            return httpx.Response(
                200,
                json={"stream_instance_id": stream, "expires_at": lease["expires_at"]},
            )
        if path.startswith("/api/v2/media/"):
            assert request.headers.get("X-L4-Media-Contract") == "l4-media/3.0.0"
            assert not any(k.startswith("x-l4-camera-") for k in request.headers)
            return httpx.Response(200, json=health if request.method == "GET" else {})
        raise AssertionError(path)

    original_client = httpx.AsyncClient

    def client(*args, **kwargs):
        return original_client(
            *args, **dict(kwargs, transport=httpx.MockTransport(respond))
        )

    monkeypatch.setattr(httpx, "AsyncClient", client)
    monkeypatch.setattr(settings, "remote_control_enabled", True)
    iot = IotPlatformClient(base_url="http://iot", service_token="")
    media = MediaOrchestratorClient(base_url="http://media", service_token="")
    repository = SimpleNamespace(
        get_active_session_by_terminal_id=AsyncMock(return_value=session)
    )
    for module in (video_control, video):
        monkeypatch.setattr(module, "iot_client", iot)
        monkeypatch.setattr(module, "media_orchestrator_client", media)
        monkeypatch.setattr(
            module, "_verify_device_access", AsyncMock(return_value=terminal)
        )
    monkeypatch.setattr(video_control, "L4DeskRepository", lambda _: repository)
    monkeypatch.setattr(video, "L4DeskRepository", lambda _: repository)
    monkeypatch.setattr(video_control, "checkpoint_due", lambda *_: False)
    monkeypatch.setattr(video_control, "_close_local_session", AsyncMock())
    monkeypatch.setattr(video_control, "clear_mountpoint_pin", lambda *_, **__: None)
    monkeypatch.setattr(
        video_control, "_get_ingress_status", AsyncMock(return_value={})
    )
    return SimpleNamespace(
        args=args,
        user={**args["user"], "org_id": 1},
        session=session,
        state=state,
        status=status,
        seen=seen,
        protocol=protocol,
        repository=repository,
        started=started,
    )


@pytest.mark.anyio
@pytest.mark.parametrize("action", ["state", "stop", "stop_without_lease", "keepalive"])
async def test_native_usb_presence_uses_stored_camera_bundle(lifecycle, action):
    value = lifecycle
    if action == "state":
        await video_control.get_device_stream_state(4623, 3, value.user, AsyncMock())
    elif action.startswith("stop"):
        body = (
            video_control.StreamStopRequest(lease_id=value.args["lease_id"])
            if action == "stop"
            else None
        )
        await video_control.stop_device_stream(4623, body, value.user, AsyncMock())
    else:
        await video_control.keepalive_device_control_lease(
            4623,
            video_control.KeepaliveRequest(lease_id=value.args["lease_id"]),
            value.user,
            AsyncMock(),
        )
    state_calls = [r for r in value.seen if r["path"].endswith("/video-state")]
    assert state_calls
    for request in state_calls:
        assert all(
            request["headers"].get(k.lower()) == v
            for k, v in CAMERA_MEDIA_HEADERS.items()
        )
    if action.startswith("stop"):
        assert any(r["path"].endswith("/video-stop") for r in value.seen)
    if action == "keepalive":
        assert any(r["path"].endswith("/keepalive") for r in value.seen)
        assert any(r["path"].endswith("/sessions/renew") for r in value.seen)


@pytest.mark.anyio
async def test_mountpoint_reader_uses_the_same_stored_context(lifecycle):
    await video.create_video_session(4623, 3, lifecycle.user, AsyncMock())
    assert any(r["path"].endswith("/video-state") for r in lifecycle.seen)
    assert any(
        r["method"] == "GET" and r["path"].startswith("/api/v2/media/")
        for r in lifecycle.seen
    )


@pytest.mark.anyio
@pytest.mark.parametrize("action", ["state", "stop", "keepalive"])
async def test_old_stored_context_cannot_mutate_a_new_stream(lifecycle, action):
    lifecycle.state["stream_instance_id"] = str(uuid4())
    with pytest.raises(HTTPException, match="stream_mismatch"):
        if action == "state":
            await video_control.get_device_stream_state(
                4623, 3, lifecycle.user, AsyncMock()
            )
        elif action == "stop":
            await video_control.stop_device_stream(
                4623,
                video_control.StreamStopRequest(lease_id=lifecycle.args["lease_id"]),
                lifecycle.user,
                AsyncMock(),
            )
        else:
            await video_control.keepalive_device_control_lease(
                4623,
                video_control.KeepaliveRequest(lease_id=lifecycle.args["lease_id"]),
                lifecycle.user,
                AsyncMock(),
            )
    assert not any(
        r["path"].endswith(("/video-stop", "/sessions/stop", "/sessions/renew"))
        for r in lifecycle.seen
    )


@pytest.mark.anyio
@pytest.mark.parametrize("hint", ["usb-camera", "camera", "descriptor"])
async def test_desktop_usb_or_presence_hint_does_not_select_camera(lifecycle, hint):
    lifecycle.protocol.record = "desktop"
    lifecycle.session.operation_id = (
        "op-stream-v2-" + lifecycle.session.provider_session_id
    )
    lifecycle.status["agent"]["stream"]["mode"] = hint
    if hint == "descriptor":
        lifecycle.status["agent"]["stream"]["descriptor"] = {
            "source_type": "camera",
            "contract_version": 3,
        }
    await video_control.get_device_stream_state(4623, 3, lifecycle.user, AsyncMock())
    assert any(r["path"].endswith("/video-state") for r in lifecycle.seen)


@pytest.mark.anyio
@pytest.mark.parametrize("fail_persistence", [False, True])
async def test_marker_and_unpersisted_start_compensation_use_validated_result(
    lifecycle, monkeypatch, fail_persistence
):
    lifecycle.repository.get_active_session_by_terminal_id.return_value = None
    lifecycle.repository.ensure_l4desk_terminal = AsyncMock(
        side_effect=RuntimeError("persistence_failed") if fail_persistence else None
    )
    lifecycle.repository.create_remote_session = AsyncMock(
        return_value=lifecycle.session
    )
    monkeypatch.setattr(
        video_control, "_reconcile_stale_video_session", AsyncMock(return_value=None)
    )
    monkeypatch.setattr(
        camera_video.NativeCameraVideo,
        "start",
        AsyncMock(return_value=lifecycle.started),
    )
    body = video_control.StreamStartRequest(
        mode="camera",
        source_id="cam:ip000001",
        profile="default",
        lease_id=lifecycle.args["lease_id"],
        codec_policy="auto",
        browser_codecs=lifecycle.args["browser"].model_dump(),
        camera_admission_ticket="opaque-test",
    )
    if fail_persistence:
        with pytest.raises(RuntimeError, match="persistence_failed"):
            await video_control.start_device_stream(
                4623, body, lifecycle.user, AsyncMock()
            )
        assert any(r["path"].endswith("/video-stop") for r in lifecycle.seen)
        assert any(r["path"].endswith("/sessions/stop") for r in lifecycle.seen)
    else:
        await video_control.start_device_stream(4623, body, lifecycle.user, AsyncMock())
        assert (
            lifecycle.repository.create_remote_session.await_args.kwargs["operation_id"]
            == "op-stream-v2-camera13-" + lifecycle.state["stream_instance_id"]
        )


@pytest.mark.anyio
async def test_stop_captures_old_context_before_db_row_changes(lifecycle):
    old_stream = lifecycle.session.provider_session_id
    new_stream = str(uuid4())
    newer = L4DeskRemoteSession(
        id=lifecycle.session.id,
        session_type="video",
        provider_session_id=new_stream,
        operation_id="op-stream-v2-camera13-" + new_stream,
    )
    lifecycle.repository.get_active_session_by_terminal_id.side_effect = [
        lifecycle.session,
        newer,
    ]
    await video_control.stop_device_stream(
        4623,
        video_control.StreamStopRequest(lease_id=lifecycle.args["lease_id"]),
        lifecycle.user,
        AsyncMock(),
    )
    assert (
        video_control._close_local_session.await_args.kwargs[
            "expected_provider_session_id"
        ]
        == old_stream
    )
    stops = [
        r
        for r in lifecycle.seen
        if r["path"].endswith(("/video-stop", "/sessions/stop"))
    ]
    assert len(stops) == 2
    assert all(r["body"]["stream_instance_id"] == old_stream for r in stops)


@pytest.mark.anyio
async def test_corrupt_stored_marker_rejects_before_forwarding(lifecycle):
    lifecycle.session.provider_session_id = str(uuid4())
    with pytest.raises(HTTPException, match="camera_session_context_changed"):
        await video_control.get_device_stream_state(
            4623, 3, lifecycle.user, AsyncMock()
        )
    assert lifecycle.seen == []


def test_generic_operation_namespace_cannot_forge_a_camera_session():
    with pytest.raises(ValidationError, match="server-owned"):
        remote_sessions.SessionStartRequest(
            device_id=4623,
            session_type="video",
            operation_id="op-stream-v2-camera13-" + str(uuid4()),
        )


def test_generic_desktop_operation_ids_remain_accepted():
    for operation in [None, "caller-operation", "op-stream-v2-" + str(uuid4())]:
        value = remote_sessions.SessionStartRequest(
            device_id=773, session_type="video", operation_id=operation
        )
        assert value.operation_id == operation


@pytest.mark.anyio
async def test_reserved_namespace_is_rejected_by_actual_route_before_provider_io(
    monkeypatch,
):
    application = FastAPI()
    application.include_router(remote_sessions.router)
    application.dependency_overrides[remote_sessions.require_tenant_context] = lambda: {
        "org_id": 1
    }
    application.dependency_overrides[remote_sessions.get_db] = lambda: AsyncMock()
    provider = AsyncMock()
    monkeypatch.setattr(
        remote_sessions,
        "RemoteSessionUseCase",
        lambda _: SimpleNamespace(start_session=provider),
    )
    async with httpx.AsyncClient(
        transport=httpx.ASGITransport(app=application), base_url="http://test"
    ) as client:
        response = await client.post(
            "/remote-sessions/start",
            json={
                "device_id": 4623,
                "session_type": "video",
                "operation_id": "op-stream-v2-camera13-" + str(uuid4()),
            },
        )
    assert response.status_code == 422
    assert response.json()["detail"][0]["loc"] == ["body", "operation_id"]
    provider.assert_not_awaited()
