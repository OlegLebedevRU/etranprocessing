import asyncio
import json
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

import pytest
from fastapi import HTTPException
from httpx import ASGITransport, AsyncClient
from starlette.testclient import TestClient
from starlette.websockets import WebSocketDisconnect

from app.auth import create_access_token
from app.config import settings
from app.database import get_db
from app.main import app
from app.models import Terminal
from app.services.iot_client import iot_client


@pytest.mark.anyio
async def test_owner_lease_looks_up_runtime_terminal_id() -> None:
    from app.routers.video_control import (
        LeaseAcquireRequest,
        acquire_device_control_lease,
    )

    terminal = Terminal(id=3718, device_id=1000003, sn="test-sn", org_id=3)
    user = {
        "role": "l4desk_owner",
        "role_id": 5,
        "org_id": 3,
        "sub": "owner",
        "session_id": "jwt-session",
    }
    db = AsyncMock()
    policy = SimpleNamespace(
        evaluate_session_request=AsyncMock(return_value=SimpleNamespace(allowed=True))
    )
    lease = {
        "lease_id": "lease-1",
        "expires_at": "2026-09-26T17:00:00Z",
        "scope": "stream",
    }
    with (
        patch(
            "app.routers.video_control._verify_device_access",
            new=AsyncMock(return_value=terminal),
        ),
        patch(
            "app.routers.video_control.get_remote_session_policy",
            return_value=policy,
        ),
        patch(
            "app.routers.video_control.L4DeskRepository.get_active_session_by_terminal_id",
            new=AsyncMock(return_value=None),
        ) as lookup,
        patch.object(
            iot_client,
            "remote_input_acquire_lease",
            new=AsyncMock(return_value=lease),
        ) as upstream,
    ):
        response = await acquire_device_control_lease(
            1000003,
            LeaseAcquireRequest(scope="stream", session_id="jwt-session"),
            user,
            db,
        )
        with pytest.raises(HTTPException) as mismatch:
            await acquire_device_control_lease(
                1000003,
                LeaseAcquireRequest(scope="stream", session_id="forged-session"),
                user,
                db,
            )
    assert response.lease_id == "lease-1"
    assert mismatch.value.status_code == 400
    lookup.assert_awaited_once_with(3718)
    assert upstream.await_args.kwargs["user"]["role"] == "l4desk_owner"
    assert upstream.await_args.kwargs["user"]["session_id"] == "jwt-session"
    upstream.assert_awaited_once()


@pytest.mark.anyio
async def test_owner_can_change_lease_scope_to_console() -> None:
    from app.routers.video_control import (
        ScopeUpgradeRequest,
        change_device_control_scope,
    )

    terminal = Terminal(id=3718, device_id=1000003, sn="test-sn", org_id=3)
    user = {"role": "l4desk_owner", "role_id": 5, "org_id": 3}
    lease = {
        "lease_id": "lease-1",
        "active": True,
    }
    changed = {"expires_at": "2026-09-26T17:00:00Z", "scope": "console"}
    with (
        patch(
            "app.routers.video_control._verify_device_access",
            new=AsyncMock(return_value=terminal),
        ),
        patch.object(
            iot_client,
            "remote_input_status",
            new=AsyncMock(return_value={"lease": lease}),
        ),
        patch.object(
            iot_client,
            "remote_input_change_scope",
            new=AsyncMock(return_value=changed),
        ) as upstream,
    ):
        response = await change_device_control_scope(
            1000003, ScopeUpgradeRequest(scope="console"), user, AsyncMock()
        )

    assert response.scope == "console"
    upstream.assert_awaited_once()
    assert upstream.await_args.kwargs["org_id"] == 3


@pytest.mark.anyio
async def test_repeated_console_leases_use_unique_provider_session_ids() -> None:
    from app.routers.video_control import (
        LeaseAcquireRequest,
        acquire_device_control_lease,
    )

    terminal = Terminal(id=3718, device_id=1000003, sn="test-sn", org_id=3)
    user = {
        "role": "l4desk_owner",
        "role_id": 5,
        "org_id": 3,
        "session_id": "same-browser-session",
    }
    policy = SimpleNamespace(
        evaluate_session_request=AsyncMock(return_value=SimpleNamespace(allowed=True))
    )
    repo = SimpleNamespace(
        get_active_session_by_terminal_id=AsyncMock(return_value=None),
        ensure_l4desk_terminal=AsyncMock(),
        create_remote_session=AsyncMock(),
    )
    leases = [
        {
            "lease_id": f"lease-{number}",
            "owner_session_id": "same-browser-session",
            "expires_at": "2026-09-26T17:00:00Z",
            "scope": "console",
        }
        for number in (1, 2)
    ]
    with (
        patch(
            "app.routers.video_control._verify_device_access",
            new=AsyncMock(return_value=terminal),
        ),
        patch(
            "app.routers.video_control.get_remote_session_policy", return_value=policy
        ),
        patch("app.routers.video_control.L4DeskRepository", return_value=repo),
        patch.object(
            iot_client,
            "remote_input_acquire_lease",
            new=AsyncMock(side_effect=leases),
        ),
    ):
        db = AsyncMock()
        for _ in leases:
            await acquire_device_control_lease(
                1000003, LeaseAcquireRequest(scope="console"), user, db
            )

    provider_ids = [
        call.kwargs["provider_session_id"]
        for call in repo.create_remote_session.await_args_list
    ]
    assert provider_ids == ["lease-1", "lease-2"]
    assert db.commit.await_count == 2


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
            "permissions": ["video:view"],
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
    mock_key = AsyncMock(
        return_value={
            "command_id": "9cc3eb62-8176-46b5-93fa-1a293be46b53",
            "result": "injected",
        }
    )

    with (
        patch.object(iot_client, "remote_input_move", new=mock_move),
        patch.object(iot_client, "remote_input_click", new=mock_click),
        patch.object(iot_client, "remote_input_key", new=mock_key),
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

            # Key (legacy "key")
            resp_key = await client.post(
                "/api/v1/video/devices/1/control/events",
                json={
                    "lease_id": "52857e4e-2895-46c0-b2be-5f80b27feea7",
                    "type": "key",
                    "kind": "press",
                    "vk": 13,
                    "text": "\r",
                    "client_ref": "ref-key-1",
                },
                headers=org1_operator_headers,
            )
            assert resp_key.status_code == 200
            assert mock_key.await_count == 1

            # Key ("key_event")
            resp_key_event = await client.post(
                "/api/v1/video/devices/1/control/events",
                json={
                    "lease_id": "52857e4e-2895-46c0-b2be-5f80b27feea7",
                    "type": "key_event",
                    "kind": "down",
                    "vk": 65,
                },
                headers=org1_operator_headers,
            )
            assert resp_key_event.status_code == 200
            assert mock_key.await_count == 2


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


def test_ws_proxy_key_normalization_and_enrichment(
    org1_operator_token, mock_db_session
):
    client = TestClient(app)

    fake_status = {
        "sn": "sn0001",
        "agent": {"online": True, "desktop_available": True},
        "lease": {
            "active": True,
            "lease_id": "test-lease",
            "owner_user_id": "operator1",
            "selected_desktop_id": "disp:3f8a12bc",
            "stream_instance_id": "a1b2c3d4-e5f6-7a8b-9c0d-1e2f3a4b5c6d",
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
            self.sent.append(json.loads(msg))

        async def recv(self):
            return await self.incoming.get()

    class FakeUpstreamConnect:
        def __init__(self, *args, **kwargs):
            self.ws = FakeUpstreamWs()

        async def __aenter__(self):
            return self.ws

        async def __aexit__(self, *args):
            pass

    fake_connect = FakeUpstreamConnect()

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
            return_value=fake_connect,
        ),
        client.websocket_connect(
            "/api/v1/video/devices/1/control/ws/test-lease",
            cookies={"accessToken": org1_operator_token},
        ) as ws,
    ):
        hello = ws.receive_json()
        assert hello.get("type") == "hello"

        # 1. Send "key" -> normalized to "key_event" + enriched
        ws.send_json({"type": "key", "kind": "press", "vk": 13, "text": "\r"})

        # 2. Send "key_event" -> stays "key_event" + enriched
        ws.send_json({"type": "key_event", "kind": "down", "vk": 65})

        # 3. Send "pointer_move" -> enriched with desktop_id & stream_instance_id
        ws.send_json({"type": "pointer_move", "x": 100, "y": 200})

        # 4. Send "mouse_click" -> enriched with desktop_id & stream_instance_id
        ws.send_json(
            {
                "type": "mouse_click",
                "x": 300,
                "y": 400,
                "button": "left",
                "client_ref": "c-test",
            }
        )

        # 5. Send keepalive -> NOT enriched with desktop_id / stream_instance_id
        ws.send_json({"type": "keepalive"})

        # Send release to finish cleanly
        ws.send_json({"type": "release"})

    sent = fake_connect.ws.sent
    assert len(sent) == 6

    # Verify key -> key_event
    msg_key = sent[0]
    assert msg_key["type"] == "key_event"
    assert msg_key["kind"] == "press"
    assert msg_key["vk"] == 13
    assert msg_key["text"] == "\r"
    assert msg_key["desktop_id"] == "disp:3f8a12bc"
    assert msg_key["stream_instance_id"] == "a1b2c3d4-e5f6-7a8b-9c0d-1e2f3a4b5c6d"

    # Verify key_event
    msg_key_event = sent[1]
    assert msg_key_event["type"] == "key_event"
    assert msg_key_event["kind"] == "down"
    assert msg_key_event["vk"] == 65
    assert msg_key_event["desktop_id"] == "disp:3f8a12bc"
    assert msg_key_event["stream_instance_id"] == "a1b2c3d4-e5f6-7a8b-9c0d-1e2f3a4b5c6d"

    # Verify pointer_move enrichment
    msg_move = sent[2]
    assert msg_move["type"] == "pointer_move"
    assert msg_move["x"] == 100
    assert msg_move["y"] == 200
    assert msg_move["desktop_id"] == "disp:3f8a12bc"
    assert msg_move["stream_instance_id"] == "a1b2c3d4-e5f6-7a8b-9c0d-1e2f3a4b5c6d"

    # Verify mouse_click enrichment
    msg_click = sent[3]
    assert msg_click["type"] == "mouse_click"
    assert msg_click["x"] == 300
    assert msg_click["y"] == 400
    assert msg_click["button"] == "left"
    assert msg_click["client_ref"] == "c-test"
    assert msg_click["desktop_id"] == "disp:3f8a12bc"
    assert msg_click["stream_instance_id"] == "a1b2c3d4-e5f6-7a8b-9c0d-1e2f3a4b5c6d"

    # Verify keepalive has no desktop_id / stream_instance_id
    msg_keepalive = sent[4]
    assert msg_keepalive == {"type": "keepalive"}


def test_ws_proxy_stream_state_and_lease_revoked_forwarding(
    org1_operator_token, mock_db_session
):
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
            self.sent.append(json.loads(msg))

        async def recv(self):
            return await self.incoming.get()

    class FakeUpstreamConnect:
        def __init__(self, *args, **kwargs):
            self.ws = FakeUpstreamWs()

        async def __aenter__(self):
            return self.ws

        async def __aexit__(self, *args):
            pass

    fake_connect = FakeUpstreamConnect()

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
            return_value=fake_connect,
        ),
        client.websocket_connect(
            "/api/v1/video/devices/1/control/ws/test-lease",
            cookies={"accessToken": org1_operator_token},
        ) as ws,
    ):
        hello = ws.receive_json()
        assert hello.get("type") == "hello"

        # Upstream sends stream_state
        fake_connect.ws.incoming.put_nowait(
            json.dumps(
                {
                    "type": "stream_state",
                    "state": "running",
                    "stream_instance_id": "inst-456",
                    "reason": "Process started successfully",
                }
            )
        )
        msg_stream_state = ws.receive_json()
        assert msg_stream_state["type"] == "stream_state"
        assert msg_stream_state["state"] == "running"
        assert msg_stream_state["stream_instance_id"] == "inst-456"

        # Upstream sends lease_revoked
        fake_connect.ws.incoming.put_nowait(
            json.dumps(
                {
                    "type": "lease_revoked",
                    "reason": "lease_expired",
                }
            )
        )
        msg_revoked = ws.receive_json()
        assert msg_revoked["type"] == "lease_revoked"
        assert msg_revoked["reason"] == "lease_expired"

        ws.send_json({"type": "release"})
