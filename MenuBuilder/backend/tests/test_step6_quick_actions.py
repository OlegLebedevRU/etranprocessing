from __future__ import annotations

import asyncio
import json
from unittest.mock import AsyncMock, patch

import pytest
from fastapi import HTTPException
from httpx import ASGITransport, AsyncClient
from starlette.testclient import TestClient

from app.auth import create_access_token
from app.config import settings
from app.database import get_db
from app.main import app
from app.models import Terminal
from app.routers.video import _mountpoint_pins
from app.services.iot_client import iot_client
from app.services.media_orchestrator_client import media_orchestrator_client


@pytest.fixture(autouse=True)
def reset_dependency_overrides():
    app.dependency_overrides.clear()
    _mountpoint_pins.clear()
    yield
    app.dependency_overrides.clear()
    _mountpoint_pins.clear()


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.fixture
def operator_token():
    return create_access_token(
        {
            "sub": "operator1",
            "userId": 101,
            "org": "1",
            "org_id": 1,
            "role": "user",
            "roleId": 3,
            "role_id": 3,
            "token_type": "tenant",
        }
    )


@pytest.fixture
def operator_headers(operator_token):
    return {"Authorization": f"Bearer {operator_token}"}


@pytest.fixture
def mock_db_session():
    mock_db = AsyncMock()
    terminal_1 = Terminal(
        device_id=1,
        sn="sn0001",
        org_id=1,
    )

    async def mock_scalar(stmt, *args, **kwargs):
        stmt_str = str(stmt)
        if "device_id = :device_id_1" in stmt_str or "terminals.device_id" in stmt_str:
            return terminal_1
        return None

    mock_db.scalar = AsyncMock(side_effect=mock_scalar)
    app.dependency_overrides[get_db] = lambda: mock_db
    return mock_db


class FakeSessionContext:
    def __init__(self, session):
        self.session = session

    async def __aenter__(self):
        return self.session

    async def __aexit__(self, *args):
        pass


# ---------------------------------------------------------------------------
# 1. Config & Timeout Verification
# ---------------------------------------------------------------------------


def test_config_separated_timeouts():
    """iot_rpc_stream_start_timeout_seconds and iot_rpc_inventory_timeout_seconds are properly defined."""
    assert settings.iot_rpc_stream_start_timeout_seconds == 20.0
    assert settings.iot_rpc_inventory_timeout_seconds == 10.0
    assert settings.remote_control_click_timeout_sec == 7.0


# ---------------------------------------------------------------------------
# 2. REST Fallback: Click, Shortcut, and Validation
# ---------------------------------------------------------------------------


@pytest.mark.anyio
async def test_rest_click_left_default_and_right(mock_db_session, operator_headers):
    """mouse_click supports button='left' by default and button='right'."""
    fake_status = {
        "sn": "sn0001",
        "agent": {
            "online": True,
            "desktop_available": True,
            "stream": {"mode": "desktop", "profile": "low"},
        },
        "lease": {"active": True, "lease_id": "lease-1", "scope": "input"},
    }

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as ac:
        with (
            patch(
                "app.routers.video_control.async_session",
                return_value=FakeSessionContext(mock_db_session),
            ),
            patch.object(
                iot_client,
                "remote_input_status",
                new=AsyncMock(return_value=fake_status),
            ),
            patch.object(
                iot_client,
                "remote_input_click",
                new=AsyncMock(return_value={"result": "injected"}),
            ) as mock_click,
        ):
            # 1. Default button -> "left"
            resp_default = await ac.post(
                "/api/v1/video/devices/1/control/events",
                json={
                    "lease_id": "lease-1",
                    "type": "mouse_click",
                    "x": 100,
                    "y": 200,
                    "client_ref": "c-1",
                },
                headers=operator_headers,
            )
            assert resp_default.status_code == 200
            assert resp_default.json() == {"result": "injected"}
            assert mock_click.call_args.kwargs["lease_id"] == "lease-1"
            assert mock_click.call_args.kwargs["x"] == 100
            assert mock_click.call_args.kwargs["y"] == 200
            assert mock_click.call_args.kwargs["button"] == "left"
            assert mock_click.call_args.kwargs["client_ref"] == "c-1"
            assert mock_click.call_args.kwargs["org_id"] == 1

            # 2. Explicit button -> "right"
            resp_right = await ac.post(
                "/api/v1/video/devices/1/control/events",
                json={
                    "lease_id": "lease-1",
                    "type": "mouse_click",
                    "x": 32768,
                    "y": 32768,
                    "button": "right",
                    "client_ref": "c-2",
                },
                headers=operator_headers,
            )
            assert resp_right.status_code == 200
            assert mock_click.call_args.kwargs["lease_id"] == "lease-1"
            assert mock_click.call_args.kwargs["x"] == 32768
            assert mock_click.call_args.kwargs["y"] == 32768
            assert mock_click.call_args.kwargs["button"] == "right"
            assert mock_click.call_args.kwargs["client_ref"] == "c-2"


@pytest.mark.anyio
async def test_rest_click_invalid_inputs(mock_db_session, operator_headers):
    """Middle button and out-of-bounds coordinates must be rejected with 422."""
    fake_status = {
        "sn": "sn0001",
        "agent": {
            "online": True,
            "desktop_available": True,
            "stream": {"mode": "desktop", "profile": "low"},
        },
        "lease": {"active": True, "lease_id": "lease-1", "scope": "input"},
    }

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as ac:
        with (
            patch(
                "app.routers.video_control.async_session",
                return_value=FakeSessionContext(mock_db_session),
            ),
            patch.object(
                iot_client,
                "remote_input_status",
                new=AsyncMock(return_value=fake_status),
            ),
        ):
            # middle button forbidden
            r_middle = await ac.post(
                "/api/v1/video/devices/1/control/events",
                json={
                    "lease_id": "lease-1",
                    "type": "mouse_click",
                    "x": 100,
                    "y": 200,
                    "button": "middle",
                },
                headers=operator_headers,
            )
            assert r_middle.status_code == 422

            # out of bounds x > 65535
            r_oob = await ac.post(
                "/api/v1/video/devices/1/control/events",
                json={
                    "lease_id": "lease-1",
                    "type": "mouse_click",
                    "x": 70000,
                    "y": 200,
                },
                headers=operator_headers,
            )
            assert r_oob.status_code == 422

            # negative coordinate
            r_neg = await ac.post(
                "/api/v1/video/devices/1/control/events",
                json={"lease_id": "lease-1", "type": "mouse_click", "x": -5, "y": 200},
                headers=operator_headers,
            )
            assert r_neg.status_code == 422


@pytest.mark.anyio
async def test_rest_shortcut_action_valid_and_invalid(
    mock_db_session, operator_headers
):
    """shortcut_action supports f12, alt_f4, win_d, rejects unknown actions with 422."""
    fake_status = {
        "sn": "sn0001",
        "agent": {
            "online": True,
            "desktop_available": True,
            "stream": {"mode": "desktop", "profile": "low"},
        },
        "lease": {
            "active": True,
            "lease_id": "lease-1",
            "scope": "input",
            "desktop_id": "disp:0",
            "stream_instance_id": "inst-42",
        },
    }

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as ac:
        with (
            patch(
                "app.routers.video_control.async_session",
                return_value=FakeSessionContext(mock_db_session),
            ),
            patch.object(
                iot_client,
                "remote_input_status",
                new=AsyncMock(return_value=fake_status),
            ),
            patch.object(
                iot_client,
                "remote_input_shortcut",
                new=AsyncMock(return_value={"result": "injected"}),
            ) as mock_shortcut,
        ):
            for action in ("f12", "alt_f4", "win_d"):
                r = await ac.post(
                    "/api/v1/video/devices/1/control/events",
                    json={
                        "lease_id": "lease-1",
                        "type": "shortcut_action",
                        "action": action,
                        "client_ref": f"ref-{action}",
                    },
                    headers=operator_headers,
                )
                assert r.status_code == 200
                assert r.json() == {"result": "injected"}
            assert mock_shortcut.call_args.kwargs["lease_id"] == "lease-1"
            assert mock_shortcut.call_args.kwargs["action"] == action
            assert mock_shortcut.call_args.kwargs["client_ref"] == f"ref-{action}"
            assert mock_shortcut.call_args.kwargs["desktop_id"] == "disp:0"
            assert mock_shortcut.call_args.kwargs["stream_instance_id"] == "inst-42"
            assert mock_shortcut.call_args.kwargs["org_id"] == 1

            # Invalid action
            r_invalid = await ac.post(
                "/api/v1/video/devices/1/control/events",
                json={
                    "lease_id": "lease-1",
                    "type": "shortcut_action",
                    "action": "ctrl_alt_del",
                },
                headers=operator_headers,
            )
            assert r_invalid.status_code == 422


# ---------------------------------------------------------------------------
# 3. Policy Guardrails: Scope, Camera Mode, and 480p Quality Requirement
# ---------------------------------------------------------------------------


@pytest.mark.anyio
async def test_rest_input_denied_when_camera_mode(mock_db_session, operator_headers):
    """Input is denied with 403 when stream is in usb-camera mode."""
    camera_status = {
        "sn": "sn0001",
        "agent": {
            "online": True,
            "desktop_available": True,
            "stream": {"mode": "usb-camera", "profile": "low"},
        },
        "lease": {"active": True, "lease_id": "lease-1", "scope": "input"},
    }

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as ac:
        with (
            patch(
                "app.routers.video_control.async_session",
                return_value=FakeSessionContext(mock_db_session),
            ),
            patch.object(
                iot_client,
                "remote_input_status",
                new=AsyncMock(return_value=camera_status),
            ),
        ):
            resp = await ac.post(
                "/api/v1/video/devices/1/control/events",
                json={"lease_id": "lease-1", "type": "mouse_click", "x": 100, "y": 100},
                headers=operator_headers,
            )
            assert resp.status_code == 403
            assert "камеры" in resp.json()["detail"]


@pytest.mark.anyio
async def test_rest_input_denied_when_not_480p(mock_db_session, operator_headers):
    """Input is denied with 400 when stream profile is not 480p (low)."""
    hd_status = {
        "sn": "sn0001",
        "agent": {
            "online": True,
            "desktop_available": True,
            "stream": {"mode": "desktop", "profile": "default"},
        },
        "lease": {"active": True, "lease_id": "lease-1", "scope": "input"},
    }

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as ac:
        with (
            patch(
                "app.routers.video_control.async_session",
                return_value=FakeSessionContext(mock_db_session),
            ),
            patch.object(
                iot_client, "remote_input_status", new=AsyncMock(return_value=hd_status)
            ),
        ):
            resp = await ac.post(
                "/api/v1/video/devices/1/control/events",
                json={"lease_id": "lease-1", "type": "mouse_click", "x": 100, "y": 100},
                headers=operator_headers,
            )
            assert resp.status_code == 400
            assert "480p" in resp.json()["detail"]


@pytest.mark.anyio
async def test_rest_input_denied_when_scope_not_input(
    mock_db_session, operator_headers
):
    """Input is denied with 403 when lease scope is view (view-only)."""
    view_status = {
        "sn": "sn0001",
        "agent": {
            "online": True,
            "desktop_available": True,
            "stream": {"mode": "desktop", "profile": "low"},
        },
        "lease": {"active": True, "lease_id": "lease-1", "scope": "view"},
    }

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as ac:
        with (
            patch(
                "app.routers.video_control.async_session",
                return_value=FakeSessionContext(mock_db_session),
            ),
            patch.object(
                iot_client,
                "remote_input_status",
                new=AsyncMock(return_value=view_status),
            ),
        ):
            resp = await ac.post(
                "/api/v1/video/devices/1/control/events",
                json={"lease_id": "lease-1", "type": "mouse_click", "x": 100, "y": 100},
                headers=operator_headers,
            )
            assert resp.status_code == 403
            assert "scope input" in resp.json()["detail"]


@pytest.mark.anyio
async def test_acquire_lease_input_enforces_480p_policy(
    mock_db_session, operator_headers
):
    """Acquiring input lease fails with 400 when stream is running in 720p (default) quality."""
    running_hd_status = {
        "sn": "sn0001",
        "agent": {
            "online": True,
            "desktop_available": True,
            "stream": {"state": "running", "mode": "desktop", "profile": "default"},
        },
        "lease": {"active": False},
    }

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as ac:
        with (
            patch(
                "app.routers.video_control.async_session",
                return_value=FakeSessionContext(mock_db_session),
            ),
            patch.object(
                iot_client,
                "remote_input_status",
                new=AsyncMock(return_value=running_hd_status),
            ),
        ):
            resp = await ac.post(
                "/api/v1/video/devices/1/control/lease",
                json={"scope": "input"},
                headers=operator_headers,
            )
            assert resp.status_code == 400
            assert "480p" in resp.json()["detail"]


# ---------------------------------------------------------------------------
# 4. Honest Upstream Timeouts & No-Retry Behavior
# ---------------------------------------------------------------------------


@pytest.mark.anyio
async def test_stream_start_504_terminal_timeout_not_swallowed(
    mock_db_session, operator_headers
):
    """HTTP 504 terminal_timeout on stream_start must raise 504 and NOT be swallowed by legacy fallback."""
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as ac:
        with (
            patch(
                "app.routers.video_control.async_session",
                return_value=FakeSessionContext(mock_db_session),
            ),
            patch.object(
                media_orchestrator_client,
                "start_session",
                new=AsyncMock(
                    return_value={
                        "status": "success",
                        "mountpoint_id": 1,
                        "session_id": "sess-test-1",
                        "state": "active",
                    }
                ),
            ),
            patch.object(
                iot_client,
                "remote_input_stream_start",
                side_effect=HTTPException(
                    status_code=504,
                    detail={
                        "code": "terminal_timeout",
                        "message": "Истекло время ожидания ответа от терминала при запуске видеопотока",
                    },
                ),
            ),
        ):
            resp = await ac.post(
                "/api/v1/video/devices/1/stream/start",
                json={"mode": "desktop", "source_id": "0", "lease_id": "l-1"},
                headers=operator_headers,
            )
            assert resp.status_code == 504
            assert resp.json()["detail"]["code"] == "terminal_timeout"


# ---------------------------------------------------------------------------
# 5. WebSocket Proxy: Click, Shortcut, NACK, Stale Event Filtering
# ---------------------------------------------------------------------------


def test_ws_proxy_click_right_and_shortcut_forwarding(operator_token, mock_db_session):
    """WS proxy forwards mouse_click(button='right') and shortcut_action to upstream ws."""
    client = TestClient(app)
    fake_status = {
        "sn": "sn0001",
        "agent": {
            "online": True,
            "desktop_available": True,
            "stream": {"mode": "desktop", "profile": "low"},
        },
        "lease": {
            "active": True,
            "lease_id": "ws-lease-1",
            "owner_user_id": "operator1",
            "scope": "input",
            "desktop_id": "disp:0",
            "stream_instance_id": "inst-100",
        },
    }

    class FakeUpstreamWs:
        def __init__(self):
            self.incoming = asyncio.Queue()
            self.sent = []

        async def send(self, msg):
            self.sent.append(json.loads(msg))

        async def recv(self):
            return await self.incoming.get()

    fake_ws = FakeUpstreamWs()

    class FakeConnect:
        async def __aenter__(self):
            return fake_ws

        async def __aexit__(self, *args):
            pass

    with (
        patch(
            "app.routers.video_control.async_session",
            return_value=FakeSessionContext(mock_db_session),
        ),
        patch.object(
            iot_client, "remote_input_status", new=AsyncMock(return_value=fake_status)
        ),
        patch.object(iot_client, "remote_input_release", new=AsyncMock()),
        patch(
            "app.routers.video_control.websockets.connect", return_value=FakeConnect()
        ),
        client.websocket_connect(
            "/api/v1/video/devices/1/control/ws/ws-lease-1",
            cookies={"accessToken": operator_token},
        ) as ws,
    ):
        # 1. Send right click
        ws.send_json(
            {
                "type": "mouse_click",
                "x": 32768,
                "y": 32768,
                "button": "right",
                "client_ref": "ref-right-1",
            }
        )

        # 2. Send shortcut action
        ws.send_json(
            {
                "type": "shortcut_action",
                "action": "f12",
                "client_ref": "ref-f12-1",
            }
        )

        # Close
        ws.send_json({"type": "release"})

    # Verify messages received by upstream
    click_msg = next((m for m in fake_ws.sent if m.get("type") == "mouse_click"), None)
    assert click_msg is not None
    assert click_msg["button"] == "right"
    assert click_msg["x"] == 32768
    assert click_msg["y"] == 32768
    assert click_msg["client_ref"] == "ref-right-1"
    assert click_msg["desktop_id"] == "disp:0"
    assert click_msg["stream_instance_id"] == "inst-100"

    shortcut_msg = next(
        (m for m in fake_ws.sent if m.get("type") == "shortcut_action"), None
    )
    assert shortcut_msg is not None
    assert shortcut_msg["action"] == "f12"
    assert shortcut_msg["client_ref"] == "ref-f12-1"
    assert shortcut_msg["desktop_id"] == "disp:0"
    assert shortcut_msg["stream_instance_id"] == "inst-100"


def test_ws_proxy_drops_late_action_result_for_stale_lease(
    operator_token, mock_db_session
):
    """Late action_result / click_result from a stale lease must NOT reach browser."""
    client = TestClient(app)
    fake_status = {
        "sn": "sn0001",
        "agent": {
            "online": True,
            "desktop_available": True,
            "stream": {"mode": "desktop", "profile": "low"},
        },
        "lease": {
            "active": True,
            "lease_id": "current-lease",
            "owner_user_id": "operator1",
            "scope": "input",
        },
    }

    class FakeUpstreamWs:
        def __init__(self):
            self.incoming = asyncio.Queue()
            self.sent = []
            # 1. Stale lease event
            self.incoming.put_nowait(
                json.dumps(
                    {
                        "type": "action_result",
                        "lease_id": "stale-old-lease",
                        "command_id": "cmd-old",
                        "result": "injected",
                    }
                )
            )
            # 2. Current lease event
            self.incoming.put_nowait(
                json.dumps(
                    {
                        "type": "action_result",
                        "lease_id": "current-lease",
                        "command_id": "cmd-cur",
                        "result": "injected",
                    }
                )
            )

        async def send(self, msg):
            self.sent.append(json.loads(msg))

        async def recv(self):
            return await self.incoming.get()

    fake_ws = FakeUpstreamWs()

    class FakeConnect:
        async def __aenter__(self):
            return fake_ws

        async def __aexit__(self, *args):
            pass

    with (
        patch(
            "app.routers.video_control.async_session",
            return_value=FakeSessionContext(mock_db_session),
        ),
        patch.object(
            iot_client, "remote_input_status", new=AsyncMock(return_value=fake_status)
        ),
        patch.object(iot_client, "remote_input_release", new=AsyncMock()),
        patch(
            "app.routers.video_control.websockets.connect", return_value=FakeConnect()
        ),
        client.websocket_connect(
            "/api/v1/video/devices/1/control/ws/current-lease",
            cookies={"accessToken": operator_token},
        ) as ws,
    ):
        msg = ws.receive_json()
        assert msg["type"] == "action_result"
        assert msg["command_id"] == "cmd-cur"
        assert msg["lease_id"] == "current-lease"

        ws.send_json({"type": "release"})
