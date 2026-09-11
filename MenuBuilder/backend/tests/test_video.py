from __future__ import annotations

import time
from typing import Any
from unittest.mock import AsyncMock, patch

import pytest
from httpx import ASGITransport, AsyncClient

from app.auth import create_access_token
from app.database import get_db
from app.main import app
from app.models import Terminal
from app.routers.video import (
    _ensure_janus_mountpoint,
    _get_ingress_status,
    _mountpoint_pins,
    clear_mountpoint_pin,
    get_device_ports,
    get_or_create_mountpoint_pin,
    set_mountpoint_stream_instance,
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
def mock_db_session():
    mock_db = AsyncMock()
    terminal_1 = Terminal(device_id=1, sn="sn0001", org_id=1)

    async def mock_scalar(stmt, *args, **kwargs):
        return terminal_1

    mock_db.scalar = mock_scalar
    return mock_db


@pytest.fixture
def operator_token():
    return create_access_token(
        {
            "sub": "op_user",
            "userId": 3,
            "org": "1",
            "org_id": 1,
            "role": "user",
            "roleId": 3,
            "role_id": 3,
            "token_type": "tenant",
            "session_id": "sess-op-3",
        }
    )


@pytest.mark.anyio
async def test_route_before_start_session_creation(mock_db_session, operator_token):
    """Route-before-Start: session creation succeeds before stream_start when stream_instance_id is None."""
    app.dependency_overrides[get_db] = lambda: mock_db_session

    # Active stream lease exists, but stream is NOT yet started (stream_instance_id is None)
    op_status = {
        "sn": "sn0001",
        "agent": {"online": True, "desktop_available": True},
        "lease": {
            "active": True,
            "lease_id": "lease-route-1",
            "scope": "stream",
            "owner_user_id": "op_user",
            "stream_instance_id": None,
        },
    }

    mock_ingress_route = AsyncMock()
    mock_janus_mp = AsyncMock()

    with (
        patch.object(
            iot_client, "remote_input_status", new=AsyncMock(return_value=op_status)
        ),
        patch("app.routers.video._ensure_ingress_route", new=mock_ingress_route),
        patch("app.routers.video._ensure_janus_mountpoint", new=mock_janus_mp),
    ):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.post(
                "/api/v1/video/devices/1/session",
                headers={"Authorization": f"Bearer {operator_token}"},
            )
            assert resp.status_code == 200
            data = resp.json()

            assert data["mountpoint_id"] == 1
            assert data["sn"] == "sn0001"
            assert data["pin"] is not None
            assert len(data["pin"]) > 0

            # Verify ingress route was configured with device ports
            rtp_port, rtcp_port = get_device_ports(1)
            mock_ingress_route.assert_called_once_with("sn0001", rtp_port, rtcp_port)

            # Verify Janus mountpoint was configured with the same pin
            mock_janus_mp.assert_called_once_with(
                1, 1, rtp_port, rtcp_port, pin=data["pin"]
            )


@pytest.mark.anyio
async def test_pin_stability_on_repeated_session_creation(
    mock_db_session, operator_token
):
    """PIN stability: repeated create_video_session calls within the same lease return the identical PIN."""
    app.dependency_overrides[get_db] = lambda: mock_db_session

    op_status = {
        "sn": "sn0001",
        "agent": {"online": True},
        "lease": {
            "active": True,
            "lease_id": "lease-stable-pin",
            "scope": "stream",
            "owner_user_id": "op_user",
            "stream_instance_id": None,
        },
    }

    with (
        patch.object(
            iot_client, "remote_input_status", new=AsyncMock(return_value=op_status)
        ),
        patch("app.routers.video._ensure_ingress_route", new=AsyncMock()),
        patch("app.routers.video._ensure_janus_mountpoint", new=AsyncMock()),
    ):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            # First call (before stream_start)
            r1 = await client.post(
                "/api/v1/video/devices/1/session",
                headers={"Authorization": f"Bearer {operator_token}"},
            )
            assert r1.status_code == 200
            pin1 = r1.json()["pin"]

            # Second call (still before stream_start, e.g. reconnect or retry)
            r2 = await client.post(
                "/api/v1/video/devices/1/session",
                headers={"Authorization": f"Bearer {operator_token}"},
            )
            assert r2.status_code == 200
            pin2 = r2.json()["pin"]
            assert pin1 == pin2

            # Now stream is started and terminal reports stream_instance_id
            op_status["lease"]["stream_instance_id"] = "inst-after-start-99"
            set_mountpoint_stream_instance(1, "inst-after-start-99")

            # Third call (after stream_start)
            r3 = await client.post(
                "/api/v1/video/devices/1/session",
                headers={"Authorization": f"Bearer {operator_token}"},
            )
            assert r3.status_code == 200
            pin3 = r3.json()["pin"]
            assert pin1 == pin3


@pytest.mark.anyio
async def test_janus_mountpoint_idempotent_reuse():
    """_ensure_janus_mountpoint reuses existing mountpoint when PIN matches without destroying it."""
    # Pre-populate pin
    pin = get_or_create_mountpoint_pin(1, lease_id="lease-test")

    # Simulate Janus responses:
    # 1. create session -> 200, id=100
    # 2. attach plugin -> 200, id=200
    # 3. create mountpoint -> 200, but plugin returns error 456 ("already exists")
    # 4. session destroy -> 200
    responses = [
        # session create
        AsyncMock(status_code=200, json=lambda: {"janus": "success", "data": {"id": 100}}),
        # attach plugin
        AsyncMock(status_code=200, json=lambda: {"janus": "success", "data": {"id": 200}}),
        # create mountpoint -> first time succeeds
        AsyncMock(
            status_code=200,
            json=lambda: {
                "janus": "success",
                "plugindata": {"data": {"streaming": "created", "id": 1}},
            },
        ),
        # destroy temporary session
        AsyncMock(status_code=200, json=lambda: {"janus": "success"}),
    ]

    mock_client = AsyncMock()
    mock_client.post = AsyncMock(side_effect=responses)
    mock_client.__aenter__.return_value = mock_client
    mock_client.__aexit__.return_value = None

    with patch("httpx.AsyncClient", return_value=mock_client):
        await _ensure_janus_mountpoint(1, 1, 50000, 50001, pin=pin)

    # First call created mountpoint, so janus_pin is recorded
    assert _mountpoint_pins[1]["janus_pin"] == pin

    # Second call: Janus reports "already exists"
    second_responses = [
        AsyncMock(status_code=200, json=lambda: {"janus": "success", "data": {"id": 101}}),
        AsyncMock(status_code=200, json=lambda: {"janus": "success", "data": {"id": 201}}),
        AsyncMock(
            status_code=200,
            json=lambda: {
                "janus": "success",
                "plugindata": {
                    "data": {
                        "error_code": 456,
                        "error": "Mountpoint already exists",
                    }
                },
            },
        ),
        AsyncMock(status_code=200, json=lambda: {"janus": "success"}),
    ]

    mock_client2 = AsyncMock()
    mock_client2.post = AsyncMock(side_effect=second_responses)
    mock_client2.__aenter__.return_value = mock_client2
    mock_client2.__aexit__.return_value = None

    with patch("httpx.AsyncClient", return_value=mock_client2):
        await _ensure_janus_mountpoint(1, 1, 50000, 50001, pin=pin)

    # Check that NO 'destroy' request was sent for mountpoint 1 because pin matched!
    for call in mock_client2.post.call_args_list:
        body = call.kwargs.get("json", {}).get("body", {})
        assert body.get("request") != "destroy"


@pytest.mark.anyio
async def test_ingress_status_telemetry():
    """_get_ingress_status correctly incorporates last_rtp_at / last_activity in status."""
    now = time.time()
    mock_stats_resp = AsyncMock(
        status_code=200,
        json=lambda: {
            "status": "ok",
            "sessions": [
                {
                    "sn": "sn0001",
                    "state": "streaming",
                    "rtp_packets": 150,
                    "bytes": 204800,
                    "idle_sec": 1,
                    "last_rtp_at": now - 0.5,
                    "last_activity": now - 0.5,
                }
            ],
        },
    )

    mock_client = AsyncMock()
    mock_client.get = AsyncMock(return_value=mock_stats_resp)
    mock_client.__aenter__.return_value = mock_client
    mock_client.__aexit__.return_value = None

    with patch("httpx.AsyncClient", return_value=mock_client):
        stats = await _get_ingress_status("sn0001")

    assert stats["streaming"] is True
    assert stats["rtp_packets"] == 150
    assert stats["bytes"] == 204800
    assert stats["idle_sec"] == 1.0
    assert stats["sn"] == "sn0001"
    assert stats["last_rtp_at"] == now - 0.5
    assert stats["last_activity"] == now - 0.5


@pytest.mark.anyio
async def test_session_cleanup_on_stop_and_release(mock_db_session, operator_token):
    """stop_device_stream and release_device_control_lease clear the cached PIN."""
    app.dependency_overrides[get_db] = lambda: mock_db_session

    # Initialize pin in cache
    pin = get_or_create_mountpoint_pin(1, lease_id="lease-cleanup-1")
    assert 1 in _mountpoint_pins

    headers = {"Authorization": f"Bearer {operator_token}"}

    with (
        patch.object(
            iot_client,
            "remote_input_stream_stop",
            new=AsyncMock(return_value={"result": "stopped"}),
        ),
        patch("app.routers.video_control._destroy_janus_mountpoint", new=AsyncMock()) as mock_destroy,
    ):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.post(
                "/api/v1/video/devices/1/stream/stop",
                json={"lease_id": "lease-cleanup-1", "destroy_mountpoint": True},
                headers=headers,
            )
            assert resp.status_code == 200
            assert resp.json()["result"] == "stopped"

            # Cache was cleared and destroy was called
            assert 1 not in _mountpoint_pins
            mock_destroy.assert_called_once_with(1)
