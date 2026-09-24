from __future__ import annotations

import pathlib
from unittest.mock import AsyncMock, patch

import pytest
from httpx import ASGITransport, AsyncClient

from app.auth import create_access_token
from app.database import get_db
from app.main import app
from app.models import Terminal
from app.routers.video import (
    _mountpoint_pins,
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
async def test_create_video_session_uses_unified_use_case(
    mock_db_session, operator_token
):
    """POST /api/v1/video/devices/{id}/session delegates to RemoteSessionUseCase."""
    app.dependency_overrides[get_db] = lambda: mock_db_session

    mock_result = AsyncMock()
    mock_result.mountpoint_id = 1
    mock_result.sn = "sn0001"
    mock_result.janus_ws = "/janus-ws"
    mock_result.ttl_sec = 600
    mock_result.pin = "test-pin-abc"

    with patch(
        "app.services.remote_session_use_case.RemoteSessionUseCase.start_session",
        return_value=mock_result,
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
            assert data["pin"] == "test-pin-abc"


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

    with patch.object(
        iot_client, "remote_input_status", new=AsyncMock(return_value=op_status)
    ):
        pin1 = get_or_create_mountpoint_pin(1, lease_id="lease-stable-pin")
        pin2 = get_or_create_mountpoint_pin(1, lease_id="lease-stable-pin")
        assert pin1 == pin2

        set_mountpoint_stream_instance(1, "inst-after-start-99")
        pin3 = get_or_create_mountpoint_pin(1, lease_id="lease-stable-pin")
        assert pin1 == pin3


@pytest.mark.anyio
async def test_session_cleanup_clears_cached_pin(mock_db_session, operator_token):
    """stop_device_stream clears the cached PIN."""
    app.dependency_overrides[get_db] = lambda: mock_db_session

    get_or_create_mountpoint_pin(1, lease_id="lease-cleanup-1")
    assert 1 in _mountpoint_pins

    headers = {"Authorization": f"Bearer {operator_token}"}

    with patch.object(
        iot_client,
        "remote_input_stream_stop",
        new=AsyncMock(return_value={"result": "stopped"}),
    ):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.post(
                "/api/v1/video/devices/1/stream/stop",
                json={"lease_id": "lease-cleanup-1"},
                headers=headers,
            )
            assert resp.status_code == 200
            assert resp.json()["result"] == "stopped"
            assert 1 not in _mountpoint_pins


@pytest.mark.anyio
async def test_get_video_session_status_uses_lifecycle_health(
    mock_db_session, operator_token
):
    """GET /api/v1/video/devices/{id}/session/status delegates to lifecycle health API."""
    app.dependency_overrides[get_db] = lambda: mock_db_session

    mock_status = AsyncMock()
    mock_status.streaming = True
    mock_status.rtp_packets = 150
    mock_status.bytes = 204800
    mock_status.idle_sec = 1.0
    mock_status.sn = "sn0001"
    mock_status.transport_connected = True
    mock_status.fresh_rtp = True
    mock_status.media_state = "live"

    with patch(
        "app.services.remote_session_use_case.RemoteSessionUseCase.get_session_status",
        return_value=mock_status,
    ):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.get(
                "/api/v1/video/devices/1/session/status",
                headers={"Authorization": f"Bearer {operator_token}"},
            )
            assert resp.status_code == 200
            data = resp.json()
            assert data["streaming"] is True
            assert data["rtp_packets"] == 150
            assert data["sn"] == "sn0001"


@pytest.mark.anyio
async def test_stop_video_session_endpoint(mock_db_session, operator_token):
    """DELETE /api/v1/video/devices/{id}/session delegates to stop_session."""
    app.dependency_overrides[get_db] = lambda: mock_db_session

    with patch(
        "app.services.remote_session_use_case.RemoteSessionUseCase.stop_session",
        new=AsyncMock(return_value={"status": "success", "state": "closed"}),
    ) as mock_stop:
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.delete(
                "/api/v1/video/devices/1/session",
                headers={"Authorization": f"Bearer {operator_token}"},
            )
            assert resp.status_code == 200
            assert resp.json()["status"] == "success"
            assert resp.json()["state"] == "closed"
            mock_stop.assert_called_once()


# ---------------------------------------------------------------------------
# Regression guards: prove no direct ingress/Janus calls exist in production
# ---------------------------------------------------------------------------


def test_no_direct_ingress_route_calls_in_production():
    """Verify no direct PUT /routes/ calls exist in production video routers."""
    video_py = pathlib.Path("app/routers/video.py").read_text()
    assert "_ensure_ingress_route" not in video_py


def test_no_direct_janus_calls_in_production():
    """Verify no direct Janus Admin API calls exist in production video routers."""
    video_py = pathlib.Path("app/routers/video.py").read_text()
    assert "_ensure_janus_mountpoint" not in video_py
    assert "_destroy_janus_mountpoint" not in video_py


def test_no_lifecycle_to_direct_fallback():
    """Verify no fallback from lifecycle API to direct setup exists."""
    video_py = pathlib.Path("app/routers/video.py").read_text()
    assert "falling back" not in video_py.lower()
    assert "_ensure_ingress" not in video_py
    assert "_ensure_janus" not in video_py


def test_no_feature_flag_gate():
    """Verify the orchestration feature flag has been removed."""
    video_py = pathlib.Path("app/routers/video.py").read_text()
    assert "l4desk_session_orchestration_enabled" not in video_py
