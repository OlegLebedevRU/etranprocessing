import asyncio
import json
from unittest.mock import AsyncMock, patch

import pytest
from httpx import ASGITransport, AsyncClient
from starlette.testclient import TestClient
from starlette.websockets import WebSocketDisconnect

from app.auth import create_access_token
from app.config import settings
from app.database import get_db
from app.main import app
from app.models import Terminal
from app.services.iot_client import iot_client


@pytest.fixture(autouse=True)
def reset_dependency_overrides():
    app.dependency_overrides.clear()
    yield
    app.dependency_overrides.clear()


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.fixture
def org1_operator_token():
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
def org1_operator_headers(org1_operator_token):
    return {"Authorization": f"Bearer {org1_operator_token}"}


@pytest.fixture
def org2_operator_token():
    return create_access_token(
        {
            "sub": "operator2",
            "userId": 102,
            "org": "2",
            "org_id": 2,
            "role": "user",
            "roleId": 3,
            "role_id": 3,
            "token_type": "tenant",
        }
    )


@pytest.fixture
def org2_operator_headers(org2_operator_token):
    return {"Authorization": f"Bearer {org2_operator_token}"}


@pytest.fixture
def org1_viewer_token():
    return create_access_token(
        {
            "sub": "viewer1",
            "userId": 103,
            "org": "1",
            "org_id": 1,
            "role": "viewer",
            "roleId": 4,
            "role_id": 4,
            "token_type": "tenant",
        }
    )


@pytest.fixture
def org1_viewer_headers(org1_viewer_token):
    return {"Authorization": f"Bearer {org1_viewer_token}"}


@pytest.fixture
def mock_db_session():
    mock_db = AsyncMock()

    terminal_1 = Terminal(
        device_id=1,
        sn="sn0001",
        org_id=1,
    )
    terminal_2 = Terminal(
        device_id=2,
        sn="sn0002",
        org_id=2,
    )

    async def mock_scalar(stmt, *args, **kwargs):
        stmt_str = str(stmt)
        if "device_id = :device_id_1" in stmt_str or "terminals.device_id" in stmt_str:
            params = stmt.compile().params
            dev_id = params.get("device_id_1") or params.get("device_id")
            if dev_id == 1:
                return terminal_1
            if dev_id == 2:
                return terminal_2
        return None

    mock_db.scalar.side_effect = mock_scalar
    return mock_db


@pytest.mark.anyio
async def test_control_status_viewer_allowed(mock_db_session, org1_viewer_headers):
    app.dependency_overrides[get_db] = lambda: mock_db_session

    fake_status = {
        "sn": "sn0001",
        "agent": {
            "online": True,
            "desktop_available": True,
            "screen": {"virtual_width": 1920, "virtual_height": 1080},
            "last_seen_at": "2026-09-09T10:00:00Z",
            "stale": False,
        },
        "lease": {
            "active": False,
            "lease_id": None,
            "owner_user_id": None,
            "expires_at": None,
        },
    }

    with patch.object(
        iot_client, "remote_input_status", new=AsyncMock(return_value=fake_status)
    ):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.get(
                "/api/v1/video/devices/1/control/status",
                headers=org1_viewer_headers,
            )
            assert resp.status_code == 200
            data = resp.json()
            assert data["agent"]["online"] is True
            assert data["lease"]["active"] is False
            assert data["lease"]["mine"] is False


@pytest.mark.anyio
async def test_control_lease_viewer_forbidden(mock_db_session, org1_viewer_headers):
    app.dependency_overrides[get_db] = lambda: mock_db_session

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        resp = await client.post(
            "/api/v1/video/devices/1/control/lease",
            headers=org1_viewer_headers,
        )
        assert resp.status_code == 403
        assert "Управление доступно только операторам" in resp.json().get("detail", "")


@pytest.mark.anyio
async def test_control_events_viewer_forbidden(mock_db_session, org1_viewer_headers):
    app.dependency_overrides[get_db] = lambda: mock_db_session

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        resp = await client.post(
            "/api/v1/video/devices/1/control/events",
            json={
                "lease_id": "00000000-0000-0000-0000-000000000000",
                "type": "pointer_move",
                "x": 100,
                "y": 200,
            },
            headers=org1_viewer_headers,
        )
        assert resp.status_code == 403


@pytest.mark.anyio
async def test_control_cross_tenant_forbidden(mock_db_session, org1_operator_headers):
    app.dependency_overrides[get_db] = lambda: mock_db_session

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # Operator from org 1 trying to access terminal 2 (org 2)
        resp = await client.get(
            "/api/v1/video/devices/2/control/status",
            headers=org1_operator_headers,
        )
        assert resp.status_code == 403


@pytest.mark.anyio
async def test_control_acquire_lease_happy_path(mock_db_session, org1_operator_headers):
    app.dependency_overrides[get_db] = lambda: mock_db_session

    fake_lease = {
        "lease_id": "52857e4e-2895-46c0-b2be-5f80b27feea7",
        "sn": "sn0001",
        "device_id": 1,
        "org_id": 1,
        "owner_user_id": "operator1",
        "created_at": "2026-09-09T10:00:00Z",
        "expires_at": "2026-09-09T10:01:00Z",
        "keepalive_sec": 15,
        "ws_path": "/api/internal/v1/remote-input/ws/lease/52857e4e-2895-46c0-b2be-5f80b27feea7",
    }

    with patch.object(
        iot_client, "remote_input_acquire_lease", new=AsyncMock(return_value=fake_lease)
    ):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.post(
                "/api/v1/video/devices/1/control/lease",
                headers=org1_operator_headers,
            )
            assert resp.status_code == 201
            data = resp.json()
            assert data["lease_id"] == "52857e4e-2895-46c0-b2be-5f80b27feea7"
            assert (
                data["ws_path"]
                == "/api/v1/video/devices/1/control/ws/52857e4e-2895-46c0-b2be-5f80b27feea7"
            )


@pytest.mark.anyio
async def test_control_acquire_lease_busy_conflict(
    mock_db_session, org1_operator_headers
):
    app.dependency_overrides[get_db] = lambda: mock_db_session

    from fastapi import HTTPException

    async def raise_busy(*args, **kwargs):
        raise HTTPException(
            status_code=409,
            detail={
                "detail": "lease busy",
                "owner_user_id": "other_operator",
                "expires_at": "2026-09-09T10:05:00Z",
            },
        )

    with patch.object(iot_client, "remote_input_acquire_lease", new=raise_busy):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.post(
                "/api/v1/video/devices/1/control/lease",
                headers=org1_operator_headers,
            )
            assert resp.status_code == 409
            detail = resp.json().get("detail")
            assert isinstance(detail, dict)
            assert detail.get("owner_user_id") == "other_operator"


@pytest.mark.anyio
async def test_control_events_move_and_click(mock_db_session, org1_operator_headers):
    app.dependency_overrides[get_db] = lambda: mock_db_session

    mock_move = AsyncMock(return_value=None)
    mock_click = AsyncMock(
        return_value={
            "command_id": "8bb3eb62-8176-46b5-93fa-1a293be46b52",
            "client_ref": "ref-123",
            "result": "injected",
            "latency_ms": 42,
        }
    )

    with (
        patch.object(iot_client, "remote_input_move", new=mock_move),
        patch.object(iot_client, "remote_input_click", new=mock_click),
    ):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            # Move
            resp_move = await client.post(
                "/api/v1/video/devices/1/control/events",
                json={
                    "lease_id": "52857e4e-2895-46c0-b2be-5f80b27feea7",
                    "type": "pointer_move",
                    "x": 32768,
                    "y": 16384,
                },
                headers=org1_operator_headers,
            )
            assert resp_move.status_code == 202
            mock_move.assert_awaited_once()

            # Click
            resp_click = await client.post(
                "/api/v1/video/devices/1/control/events",
                json={
                    "lease_id": "52857e4e-2895-46c0-b2be-5f80b27feea7",
                    "type": "mouse_click",
                    "x": 32768,
                    "y": 16384,
                    "button": "left",
                    "client_ref": "ref-123",
                },
                headers=org1_operator_headers,
            )
            assert resp_click.status_code == 200
            data = resp_click.json()
            assert data["result"] == "injected"
            assert data["latency_ms"] == 42


@pytest.mark.anyio
async def test_internal_service_key_never_leaked(
    mock_db_session, org1_operator_headers
):
    app.dependency_overrides[get_db] = lambda: mock_db_session

    secret_key = "super_secret_internal_key_test_12345"
    with patch.object(settings, "internal_service_key", secret_key):
        fake_status = {
            "sn": "sn0001",
            "agent": {"online": True, "desktop_available": True},
            "lease": {"active": False},
        }
        with patch.object(
            iot_client, "remote_input_status", new=AsyncMock(return_value=fake_status)
        ):
            async with AsyncClient(
                transport=ASGITransport(app=app), base_url="http://test"
            ) as client:
                resp = await client.get(
                    "/api/v1/video/devices/1/control/status",
                    headers=org1_operator_headers,
                )
                assert resp.status_code == 200
                assert secret_key not in resp.text


def test_ws_unauthenticated_closes_4401():
    client = TestClient(app)
    with (
        pytest.raises(WebSocketDisconnect),
        client.websocket_connect("/api/v1/video/devices/1/control/ws/test-lease"),
    ):
        pass


def test_ws_foreign_lease_closes_4403(org1_operator_token, mock_db_session):
    client = TestClient(app)

    fake_status = {
        "sn": "sn0001",
        "agent": {"online": True, "desktop_available": True},
        "lease": {
            "active": True,
            "lease_id": "test-lease",
            "owner_user_id": "another_user",  # Not operator1
            "expires_at": "2026-09-09T10:05:00Z",
        },
    }

    class FakeSessionContext:
        async def __aenter__(self):
            return mock_db_session

        async def __aexit__(self, *args):
            pass

    with (
        patch(
            "app.routers.video_control.async_session", return_value=FakeSessionContext()
        ),
        patch.object(
            iot_client, "remote_input_status", new=AsyncMock(return_value=fake_status)
        ),
        pytest.raises(WebSocketDisconnect),
        client.websocket_connect(
            "/api/v1/video/devices/1/control/ws/test-lease",
            cookies={"accessToken": org1_operator_token},
        ),
    ):
        pass


def test_ws_relay_happy_path_and_release(org1_operator_token, mock_db_session):
    client = TestClient(app)

    fake_status = {
        "sn": "sn0001",
        "agent": {"online": True, "desktop_available": True},
        "lease": {
            "active": True,
            "lease_id": "test-lease",
            "owner_user_id": "operator1",
            "expires_at": "2026-09-09T10:05:00Z",
        },
    }

    class FakeSessionContext:
        async def __aenter__(self):
            return mock_db_session

        async def __aexit__(self, *args):
            pass

    mock_release = AsyncMock(return_value=None)

    class FakeUpstreamWs:
        def __init__(self):
            self.sent = []
            self.incoming = asyncio.Queue()
            self.incoming.put_nowait(json.dumps({"type": "hello", "v": 1}))

        async def send(self, msg):
            self.sent.append(msg)
            data = json.loads(msg)
            if data.get("type") == "mouse_click":
                await self.incoming.put(
                    json.dumps(
                        {
                            "type": "click_result",
                            "command_id": "cmd-1",
                            "client_ref": data.get("client_ref"),
                            "result": "injected",
                            "latency_ms": 15,
                        }
                    )
                )

        async def recv(self):
            return await self.incoming.get()

    class FakeUpstreamConnect:
        def __init__(self, *args, **kwargs):
            self.ws = FakeUpstreamWs()

        async def __aenter__(self):
            return self.ws

        async def __aexit__(self, *args):
            pass

    fake_connect_instance = FakeUpstreamConnect()

    with (
        patch(
            "app.routers.video_control.async_session", return_value=FakeSessionContext()
        ),
        patch.object(
            iot_client, "remote_input_status", new=AsyncMock(return_value=fake_status)
        ),
        patch.object(iot_client, "remote_input_release", new=mock_release),
        patch(
            "app.routers.video_control.websockets.connect",
            return_value=fake_connect_instance,
        ),
        client.websocket_connect(
            "/api/v1/video/devices/1/control/ws/test-lease",
            cookies={"accessToken": org1_operator_token},
        ) as ws,
    ):
        # Browser receives hello from fake upstream
        hello = ws.receive_json()
        assert hello.get("type") == "hello"

        # Browser sends invalid message -> receives error invalid_message, not forwarded
        ws.send_json({"type": "hack_the_terminal"})
        err = ws.receive_json()
        assert err.get("type") == "error"
        assert err.get("code") == "invalid_message"
        assert len(fake_connect_instance.ws.sent) == 0

        # Browser sends valid move -> forwarded
        ws.send_json({"type": "pointer_move", "x": 100, "y": 200})

        # Browser sends valid click -> forwarded
        ws.send_json(
            {
                "type": "mouse_click",
                "x": 100,
                "y": 200,
                "button": "left",
                "client_ref": "ref1",
            }
        )

        # Browser receives click_result from fake upstream
        click_res = ws.receive_json()
        assert click_res.get("type") == "click_result"
        assert click_res.get("result") == "injected"

        # Send release
        ws.send_json({"type": "release"})

    # Upon disconnect, remote_input_release should have been called
    mock_release.assert_awaited_once()
