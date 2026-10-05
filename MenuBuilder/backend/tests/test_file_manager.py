from datetime import UTC, datetime, timedelta
from unittest.mock import AsyncMock, patch
from uuid import uuid4

import pytest
from fastapi import HTTPException
from sqlalchemy.dialects import postgresql

from app.routers import file_manager as fm
from app.routers.video import _verify_device_access


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


def test_drain_wait_includes_guard_and_never_becomes_negative():
    now = datetime.now(UTC)
    assert 64 <= fm.drain_seconds((now + timedelta(seconds=60)).isoformat()) <= 65
    assert fm.drain_seconds((now - timedelta(seconds=6)).isoformat()) == 0


@pytest.mark.anyio
async def test_admission_lock_allows_pb_foreign_key_insert_but_blocks_disable():
    # Reproduce the production cycle: BFF keeps its guard while awaiting PB,
    # whose fm_operations insert takes KEY SHARE on the same terminal row.
    db = AsyncMock()
    db.scalar.return_value = type("Terminal", (), {"org_id": 7, "is_active": False})()
    with pytest.raises(HTTPException) as error:
        await _verify_device_access(
            10, {"org_id": 7, "is_superuser": False}, db, require_active=True
        )
    assert error.value.status_code == 403
    statement = str(db.scalar.call_args.args[0].compile(dialect=postgresql.dialect()))
    assert "FOR NO KEY UPDATE OF terminals" in statement
    assert "FOR UPDATE OF terminals" not in statement


@pytest.mark.anyio
async def test_viewer_cannot_acquire_file_control():
    with pytest.raises(HTTPException) as error:
        await fm.fm_user({"role": "viewer", "role_id": 4})
    assert error.value.status_code == 403


@pytest.mark.anyio
async def test_readiness_requires_mqtt_and_pb_heartbeat():
    with (
        patch.object(
            fm,
            "upstream",
            AsyncMock(return_value={"available": True, "state": "ready"}),
        ),
        patch.object(
            fm.iot_client,
            "get_console_device",
            AsyncMock(
                return_value={
                    "connection": {"svc_connect": True, "is_svc_available": False}
                }
            ),
        ),
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
            return {
                "lease_id": "fixture-lease",
                "expires_at": (datetime.now(UTC) + timedelta(seconds=60)).isoformat(),
            }
        if target == "pb":
            raise HTTPException(503, "PB unavailable")
        return {}

    with (
        patch.object(fm, "_verify_device_access", AsyncMock(return_value=terminal)),
        patch.object(fm, "readiness_for", AsyncMock(return_value={"available": True})),
        patch.object(fm, "upstream", upstream),
        pytest.raises(HTTPException) as error,
    ):
        await fm.start(
            10,
            uuid4(),
            {"sub": "fixture", "role": "user", "session_id": "fixture"},
            AsyncMock(),
        )
    assert error.value.detail["code"] == "fm_start_failed"
    assert 64 <= error.value.detail["retry_after_sec"] <= 65
    assert [target for target, _, _ in calls] == ["iot", "pb", "iot"]
    assert calls[-1][2] == {"action": "stop"}
    assert not any(body == {"action": "start"} for _, _, body in calls)


def test_v1_listing_and_unconfirmed_close_are_rejected():
    from pydantic import ValidationError
    with pytest.raises(ValidationError):
        fm.OperationBody(id=uuid4(), lease_id=uuid4(), kind="list", path="C:\\")
    with pytest.raises(ValidationError):
        fm.SignalBody(action="stop", confirmed_close=False)
