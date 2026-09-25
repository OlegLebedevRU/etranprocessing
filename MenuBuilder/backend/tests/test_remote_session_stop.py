from unittest.mock import AsyncMock

import pytest

from app.services.iot_event_feed_client import IotEventFeedTimeoutError
from app.services.media_orchestrator_client import MediaOrchestratorError
from app.services.remote_session_stop import (
    RemoteSessionStopPending,
    confirm_remote_session_stop,
)


def stop_args(iot: AsyncMock, media: AsyncMock) -> dict:
    media.get_session_health.return_value = {
        "session_id": "iot-123",
        "sn": "SN-7",
        "state": "active",
    }
    return {
        "session_id": "iot-123",
        "session_type": "video",
        "tenant_id": 7,
        "sn": "SN-7",
        "operation_id": "stop-mb-42",
        "reason": "user_closed",
        "correlation_id": "corr-42",
        "iot_adapter": iot,
        "media_orchestrator": media,
    }


@pytest.mark.anyio
@pytest.mark.parametrize(
    "incorrect_field,incorrect_value",
    [("session_id", "iot-other"), ("tenant_id", 8), ("sn", "SN-other")],
)
async def test_stop_rejects_mismatched_iot_identity(incorrect_field, incorrect_value):
    iot = AsyncMock()
    media = AsyncMock()
    iot.stop_remote_session.return_value = {
        "session_id": "iot-123",
        "tenant_id": 7,
        "sn": "SN-7",
        "status": "closed",
        incorrect_field: incorrect_value,
    }
    media.stop_session.return_value = {
        "status": "success",
        "session_id": "iot-123",
        "state": "stopped",
    }

    with pytest.raises(RemoteSessionStopPending):
        await confirm_remote_session_stop(**stop_args(iot, media))
    media.stop_session.assert_not_awaited()


@pytest.mark.anyio
async def test_media_response_for_other_session_cannot_confirm_stop():
    iot = AsyncMock()
    media = AsyncMock()
    iot.stop_remote_session.return_value = {
        "session_id": "iot-123",
        "tenant_id": 7,
        "sn": "SN-7",
        "status": "closed",
    }
    media.stop_session.return_value = {
        "status": "success",
        "session_id": "iot-other",
        "state": "stopped",
    }
    with pytest.raises(RemoteSessionStopPending):
        await confirm_remote_session_stop(**stop_args(iot, media))


@pytest.mark.anyio
async def test_late_old_stop_does_not_call_media_for_new_session():
    iot = AsyncMock()
    media = AsyncMock()
    iot.stop_remote_session.return_value = {
        "session_id": "iot-123",
        "tenant_id": 7,
        "sn": "SN-7",
        "status": "closed",
    }
    args = stop_args(iot, media)
    media.get_session_health.return_value = {
        "session_id": "iot-new",
        "sn": "SN-7",
        "state": "active",
    }

    with pytest.raises(RemoteSessionStopPending):
        await confirm_remote_session_stop(**args)
    media.stop_session.assert_not_awaited()


@pytest.mark.anyio
async def test_timeout_uses_exact_id_readback_before_accepting_close():
    iot = AsyncMock()
    media = AsyncMock()
    iot.stop_remote_session.side_effect = IotEventFeedTimeoutError("lost response")
    iot.get_remote_session.return_value = {
        "session_id": "iot-123",
        "tenant_id": 7,
        "sn": "SN-7",
        "status": "closed",
    }
    media.stop_session.return_value = {
        "status": "success",
        "session_id": "iot-123",
        "state": "stopped",
    }

    await confirm_remote_session_stop(**stop_args(iot, media))
    iot.get_remote_session.assert_awaited_once_with("iot-123")


@pytest.mark.anyio
async def test_media_failure_remains_pending_and_retry_reuses_exact_ids():
    iot = AsyncMock()
    media = AsyncMock()
    iot.stop_remote_session.return_value = {
        "session_id": "iot-123",
        "tenant_id": 7,
        "sn": "SN-7",
        "status": "closed",
    }
    media.stop_session.side_effect = MediaOrchestratorError("unavailable")
    args = stop_args(iot, media)

    with pytest.raises(RemoteSessionStopPending):
        await confirm_remote_session_stop(**args)

    media.stop_session.side_effect = None
    media.stop_session.return_value = {
        "status": "success",
        "session_id": "iot-123",
        "state": "stopped",
    }
    await confirm_remote_session_stop(**args)

    assert iot.stop_remote_session.await_count == 2
    assert all(
        call.kwargs["session_id"] == "iot-123"
        and call.kwargs["operation_id"] == "stop-mb-42"
        for call in iot.stop_remote_session.await_args_list
    )
    assert all(
        call.kwargs["session_id"] == "iot-123"
        for call in media.stop_session.await_args_list
    )
