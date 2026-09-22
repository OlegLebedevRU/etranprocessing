import asyncio
import json
from unittest.mock import AsyncMock, patch

import pytest
from fastapi import HTTPException
from httpx import ASGITransport, AsyncClient
from starlette.testclient import TestClient

from app.auth import create_access_token
from app.database import get_db
from app.main import app
from app.models import Terminal
from app.routers.video import (
    _mountpoint_pins,
)
from app.services.iot_client import iot_client


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


@pytest.mark.anyio
async def test_stream_stop_conflict_not_swallowed(mock_db_session, operator_headers):
    """Unknown or owner/epoch conflict (409) must NOT be swallowed as a successful stop."""
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
                "remote_input_stream_stop",
                side_effect=HTTPException(
                    status_code=409,
                    detail={
                        "code": "owner_conflict",
                        "detail": "Stream owned by another session",
                    },
                ),
            ),
        ):
            resp = await ac.post(
                "/api/v1/video/devices/1/stream/stop",
                json={"lease_id": "lease-abc"},
                headers=operator_headers,
            )
            assert resp.status_code == 409
            data = resp.json()
            assert "owner_conflict" in str(data)


@pytest.mark.anyio
async def test_stream_stop_does_not_clear_new_lease_pin(
    mock_db_session, operator_headers
):
    """Stopping stream for an old lease must NOT clear the PIN/mountpoint of a new lease."""
    _mountpoint_pins[1] = {
        "pin": "pin-for-new-lease",
        "lease_id": "new-lease-id",
        "stream_instance_id": "inst-new",
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
                "remote_input_stream_stop",
                return_value={"result": "stopped"},
            ),
        ):
            resp = await ac.post(
                "/api/v1/video/devices/1/stream/stop",
                json={"lease_id": "old-lease-id"},
                headers=operator_headers,
            )
            assert resp.status_code == 200
            # PIN of new lease must still be intact!
            assert 1 in _mountpoint_pins
            assert _mountpoint_pins[1]["lease_id"] == "new-lease-id"
            assert _mountpoint_pins[1]["pin"] == "pin-for-new-lease"


@pytest.mark.anyio
async def test_keepalive_404_normalized_error_contract(
    mock_db_session, operator_headers
):
    """Keepalive 404 from upstream must return a normalized error contract (lease_not_found), not generic 404."""
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
                "remote_input_keepalive",
                side_effect=HTTPException(
                    status_code=404,
                    detail="Not Found",
                ),
            ),
        ):
            resp = await ac.post(
                "/api/v1/video/devices/1/control/keepalive",
                json={"lease_id": "released-lease-773", "generation": 4},
                headers=operator_headers,
            )
            assert resp.status_code == 404
            data = resp.json()
            assert isinstance(data.get("detail"), dict)
            assert data["detail"].get("code") == "lease_not_found"
            assert data["detail"].get("lease_id") == "released-lease-773"
            assert data["detail"].get("generation") == 4


def test_ws_inbound_keepalive_and_release_with_generation(
    operator_token, mock_db_session
):
    """WS inbound keepalive and release schemas must accept generation and reason without validation error."""
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
    mock_release = AsyncMock(return_value=None)

    with (
        patch(
            "app.routers.video_control.async_session",
            return_value=FakeSessionContext(mock_db_session),
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
            cookies={"accessToken": operator_token},
        ) as ws,
    ):
        hello = ws.receive_json()
        assert hello.get("type") == "hello"

        # Send keepalive with generation and lease_id
        ws.send_json({"type": "keepalive", "lease_id": "test-lease", "generation": 2})

        # Send release with generation and reason
        ws.send_json(
            {
                "type": "release",
                "lease_id": "test-lease",
                "generation": 2,
                "reason": "client_release",
            }
        )

    sent = fake_connect.ws.sent
    # Should have sent 2 messages (keepalive and release), neither should have failed with invalid_message
    assert len(sent) == 2
    assert sent[0]["type"] == "keepalive"
    assert sent[0].get("generation") == 2
    assert sent[1]["type"] == "release"
    assert sent[1].get("reason") == "client_release"


@pytest.mark.anyio
async def test_duplicate_delete_idempotent(mock_db_session, operator_headers):
    """Multiple DELETE calls on the same lease must return 204 idempotently."""
    mock_delete = AsyncMock(return_value=None)
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as ac:
        with (
            patch(
                "app.routers.video_control.async_session",
                return_value=FakeSessionContext(mock_db_session),
            ),
            patch.object(iot_client, "remote_input_release", new=mock_delete),
        ):
            resp1 = await ac.delete(
                "/api/v1/video/devices/1/control/lease/lease-1",
                headers=operator_headers,
            )
            assert resp1.status_code == 204

            resp2 = await ac.delete(
                "/api/v1/video/devices/1/control/lease/lease-1",
                headers=operator_headers,
            )
            assert resp2.status_code == 204


@pytest.mark.anyio
async def test_stream_stop_idempotent_known_stopped(mock_db_session, operator_headers):
    """Known stopped states (already_stopped / no active stream / terminal_timeout 504) must return 200 stopped."""
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as ac:
        # Case 1: 409 already_stopped
        with (
            patch(
                "app.routers.video_control.async_session",
                return_value=FakeSessionContext(mock_db_session),
            ),
            patch.object(
                iot_client,
                "remote_input_stream_stop",
                side_effect=HTTPException(
                    status_code=409,
                    detail={
                        "code": "already_stopped",
                        "detail": "Stream already stopped",
                    },
                ),
            ),
        ):
            resp = await ac.post(
                "/api/v1/video/devices/1/stream/stop",
                json={"lease_id": "lease-abc"},
                headers=operator_headers,
            )
            assert resp.status_code == 200
            assert resp.json()["result"] == "stopped"

        # Case 2: 504 terminal_timeout
        with (
            patch(
                "app.routers.video_control.async_session",
                return_value=FakeSessionContext(mock_db_session),
            ),
            patch.object(
                iot_client,
                "remote_input_stream_stop",
                side_effect=HTTPException(
                    status_code=504,
                    detail={
                        "code": "terminal_timeout",
                        "detail": "Agent command timed out",
                    },
                ),
            ),
        ):
            resp = await ac.post(
                "/api/v1/video/devices/1/stream/stop",
                json={"lease_id": "lease-abc"},
                headers=operator_headers,
            )
            assert resp.status_code == 200
            assert resp.json()["result"] == "stopped"


@pytest.mark.anyio
async def test_stream_stop_epoch_and_tenant_conflict_raises(
    mock_db_session, operator_headers
):
    """True conflicts (epoch_conflict, tenant_mismatch) must raise 409 and NOT be swallowed."""
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as ac:
        for err_code in ("epoch_conflict", "tenant_conflict", "mode_conflict"):
            with (
                patch(
                    "app.routers.video_control.async_session",
                    return_value=FakeSessionContext(mock_db_session),
                ),
                patch.object(
                    iot_client,
                    "remote_input_stream_stop",
                    side_effect=HTTPException(
                        status_code=409,
                        detail={"code": err_code, "detail": f"Conflict: {err_code}"},
                    ),
                ),
            ):
                resp = await ac.post(
                    "/api/v1/video/devices/1/stream/stop",
                    json={"lease_id": "lease-abc"},
                    headers=operator_headers,
                )
                assert resp.status_code == 409


@pytest.mark.anyio
async def test_get_device_stream_state_stopped_priority(
    mock_db_session, operator_headers
):
    """When lease is inactive, stale agent cached running stream must be reported as stopped."""
    fake_status = {
        "sn": "sn0001",
        "agent": {
            "online": True,
            "stream": {
                "state": "running",
                "mode": "desktop",
                "stream_instance_id": "old-inst-123",
            },
        },
        "lease": {
            "active": False,
            "lease_id": None,
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
        ):
            resp = await ac.get(
                "/api/v1/video/devices/1/stream/state",
                headers=operator_headers,
            )
            assert resp.status_code == 200
            data = resp.json()
            assert data["stream"]["state"] == "stopped"
            assert data["stream"]["reason"] == "lease_expired"


def test_ws_proxy_drops_late_event_for_old_lease(operator_token, mock_db_session):
    """Late stream_state event for an old lease must be dropped and NOT forwarded to browser."""
    client = TestClient(app)
    fake_status = {
        "sn": "sn0001",
        "agent": {"online": True, "desktop_available": True},
        "lease": {
            "active": True,
            "lease_id": "current-lease",
            "owner_user_id": "operator1",
            "stream_instance_id": "current-inst",
            "expires_at": "2026-09-09T10:05:00Z",
        },
    }

    class FakeUpstreamWs:
        def __init__(self):
            self.incoming = asyncio.Queue()
            self.sent = []
            # First send late event from old lease
            self.incoming.put_nowait(
                json.dumps(
                    {
                        "type": "stream_state",
                        "lease_id": "old-stale-lease",
                        "state": "stopped",
                        "reason": "lease_expired",
                    }
                )
            )
            # Then send valid event for current lease
            self.incoming.put_nowait(
                json.dumps(
                    {
                        "type": "stream_state",
                        "lease_id": "current-lease",
                        "state": "running",
                    }
                )
            )

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
    mock_release = AsyncMock(return_value=None)

    with (
        patch(
            "app.routers.video_control.async_session",
            return_value=FakeSessionContext(mock_db_session),
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
            "/api/v1/video/devices/1/control/ws/current-lease",
            cookies={"accessToken": operator_token},
        ) as ws,
    ):
        # The browser should ONLY receive the second event (for current lease), not the old one!
        msg = ws.receive_json()
        assert msg["type"] == "stream_state"
        assert msg["lease_id"] == "current-lease"
        assert msg["state"] == "running"
        assert msg.get("stream_instance_id") == "current-inst"

        # Close cleanly
        ws.send_json({"type": "release"})


@pytest.mark.anyio
async def test_string_org_id_boundary_cast(mock_db_session):
    """org_id claim in JWT as a string must be properly cast at the auth boundary."""
    token_with_str_org = create_access_token(
        {
            "sub": "operator1",
            "userId": 101,
            "org": "1",
            "org_id": "1",  # String org_id
            "role": "user",
            "roleId": 3,
            "token_type": "tenant",
        }
    )
    headers = {"Authorization": f"Bearer {token_with_str_org}"}

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
                "remote_input_keepalive",
                return_value={"result": "ok", "expires_at": "2026-09-11T12:00:00Z"},
            ),
        ):
            resp = await ac.post(
                "/api/v1/video/devices/1/control/keepalive",
                json={"lease_id": "lease-abc"},
                headers=headers,
            )
            assert resp.status_code == 200
            assert resp.json()["result"] == "ok"
