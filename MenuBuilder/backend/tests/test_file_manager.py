from unittest.mock import AsyncMock, patch
from uuid import uuid4

import pytest
from fastapi import HTTPException

from app.routers import file_manager as fm


@pytest.fixture
def anyio_backend():
    return "asyncio"


def test_view_is_bound_to_authenticated_session_not_user_payload():
    user = {"sub": "fixture", "role": "user", "session_id": "authenticated-session"}
    first, second = fm.actor(user, 7, uuid4()), fm.actor(user, 7, uuid4())
    assert first["X-Org-Id"] == "7"
    assert first["X-User-Id"] == "fixture"
    assert first["X-Session-Id"] != second["X-Session-Id"]
    assert first["X-Session-Id"].startswith("authenticated-session:")


@pytest.mark.anyio
async def test_viewer_cannot_acquire_file_control():
    with pytest.raises(HTTPException) as error:
        await fm.fm_user({"role": "viewer", "role_id": 4})
    assert error.value.status_code == 403


@pytest.mark.anyio
async def test_readiness_requires_mqtt_and_pb_heartbeat():
    with (
        patch.object(fm, "upstream", AsyncMock(return_value={"available": True, "state": "ready"})),
        patch.object(fm.iot_client, "get_console_device", AsyncMock(return_value={"connection": {"svc_connect": True, "is_svc_available": False}})),
    ):
        result = await fm.readiness_for(10, {"X-Org-Id": "7"})
    assert not result["available"]
    assert result["state"] == "mqtt_unavailable"


@pytest.mark.anyio
async def test_start_registers_pb_before_mqtt_signal_and_stops_on_failure():
    terminal = type("Terminal", (), {"org_id": 7})()
    calls = []
    async def upstream(target, path, headers, body=None):
        calls.append((target, path, body))
        if path.endswith("/sessions") and target == "iot":
            return {"lease_id": "fixture-lease"}
        if target == "pb":
            raise HTTPException(503, "PB unavailable")
        return {}
    with (
        patch.object(fm, "_verify_device_access", AsyncMock(return_value=terminal)),
        patch.object(fm, "readiness_for", AsyncMock(return_value={"available": True})),
        patch.object(fm, "upstream", upstream),
    ):
        with pytest.raises(HTTPException):
            await fm.start(10, uuid4(), {"sub": "fixture", "role": "user", "session_id": "fixture"}, AsyncMock())
    assert [target for target, _, _ in calls] == ["iot", "pb", "iot"]
    assert calls[-1][2] == {"action": "stop"}
    assert not any(body == {"action": "start"} for _, _, body in calls)
