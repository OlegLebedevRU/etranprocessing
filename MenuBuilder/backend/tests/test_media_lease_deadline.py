from datetime import UTC, datetime, timedelta
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

import pytest
from fastapi import HTTPException

from app.models import Terminal
from app.models_l4desk import L4DeskRemoteSession
from app.routers.video_control import KeepaliveRequest, keepalive_device_control_lease
from app.services.media_orchestrator_client import (
    MediaOrchestratorClient,
    MediaOrchestratorError,
    media_lease_deadline,
)


@pytest.mark.parametrize(
    "value", [None, "bad", "2020-01-01T00:00:00Z", "2099-01-01T00:00:00"]
)
def test_missing_invalid_expired_or_naive_deadline_fails_closed(value):
    with pytest.raises(MediaOrchestratorError) as error:
        media_lease_deadline(value)
    assert error.value.status_code == 409


@pytest.mark.anyio
async def test_media_client_sends_provider_deadline_and_epoch():
    client = MediaOrchestratorClient(base_url="http://localhost", service_token="")
    deadline = int((datetime.now(UTC) + timedelta(seconds=60)).timestamp())
    with patch.object(client, "_send_request", AsyncMock(return_value={})) as send:
        await client.start_session(
            "lease", "operation", "sn", 1, lease_expires_at=deadline
        )
        assert send.await_args.kwargs["json_data"]["lease_expires_at"] == deadline
        await client.renew_session(
            "sn",
            lease_id="lease",
            stream_instance_id="stream",
            lease_expires_at=deadline,
        )
        assert send.await_args.kwargs["json_data"] == {
            "sn": "sn",
            "lease_id": "lease",
            "stream_instance_id": "stream",
            "lease_expires_at": deadline,
        }


@pytest.mark.anyio
async def test_keepalive_only_renews_matching_video_epoch_with_provider_deadline():
    terminal = Terminal(id=1, device_id=1, sn="sn", org_id=1)
    session = L4DeskRemoteSession(session_type="video", provider_session_id="stream")
    expiry = (datetime.now(UTC) + timedelta(seconds=60)).isoformat()
    provider_result = {"stream_instance_id": "stream", "expires_at": expiry}
    with (
        patch(
            "app.routers.video_control._verify_device_access",
            AsyncMock(return_value=terminal),
        ),
        patch(
            "app.routers.video_control.L4DeskRepository",
            return_value=SimpleNamespace(
                get_active_session_by_terminal_id=AsyncMock(return_value=session)
            ),
        ),
        patch("app.routers.video_control.checkpoint_due", return_value=False),
        patch(
            "app.routers.video_control.iot_client.remote_input_keepalive",
            AsyncMock(return_value=provider_result),
        ),
        patch(
            "app.routers.video_control.media_orchestrator_client.renew_session",
            AsyncMock(),
        ) as renew,
    ):
        await keepalive_device_control_lease(
            1, KeepaliveRequest(lease_id="lease"), {"org_id": 1}, AsyncMock()
        )
        renew.assert_awaited_once_with(
            sn="sn",
            lease_id="lease",
            stream_instance_id="stream",
            lease_expires_at=media_lease_deadline(expiry),
        )
        renew.reset_mock()
        provider_result["stream_instance_id"] = "different-stream"
        await keepalive_device_control_lease(
            1, KeepaliveRequest(lease_id="lease"), {"org_id": 1}, AsyncMock()
        )
        renew.assert_not_awaited()
        provider_result["stream_instance_id"] = "stream"
        provider_result.pop("expires_at")
        with pytest.raises(HTTPException) as invalid_deadline:
            await keepalive_device_control_lease(
                1, KeepaliveRequest(lease_id="lease"), {"org_id": 1}, AsyncMock()
            )
        assert invalid_deadline.value.status_code == 409
        renew.assert_not_awaited()
