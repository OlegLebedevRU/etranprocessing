import json
import os
from datetime import UTC, datetime, timedelta
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

import httpx
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
from tests.test_hevc_contract import fixture


@pytest.mark.anyio
async def test_actual_desktop_v3_renew_uses_live_provider_utc_after_clock_jumps():
    """Exercise router -> real MEDIA client/HTTP serialization, never camera clock."""
    terminal = Terminal(id=1, device_id=773, sn="hevc-fixture", org_id=1)
    stream = "00000000-0000-4000-8000-000000000002"
    lease = "00000000-0000-4000-8000-000000000001"
    session = L4DeskRemoteSession(
        session_type="video",
        provider_session_id=stream,
        operation_id="op-stream-v2-fixture",
    )
    descriptor = fixture("media_start")["descriptor"]
    descriptor["contract_version"] = 3
    status = {
        "agent": {"stream": {"stream_instance_id": stream, "descriptor": descriptor}}
    }
    captured = []

    def http(request):
        captured.append(
            {
                "method": request.method,
                "path": request.url.path,
                "headers": {
                    key: value
                    for key, value in request.headers.items()
                    if key.startswith("x-l4-")
                },
                "body": json.loads(request.content),
            }
        )
        return httpx.Response(200, json={})

    provider = {"stream_instance_id": stream}
    vectors = []
    async with httpx.AsyncClient(transport=httpx.MockTransport(http)) as transport:
        media = MediaOrchestratorClient(
            base_url="http://localhost", service_token="", client=transport
        )
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
                "app.routers.video_control.load_video_status",
                AsyncMock(return_value=status),
            ),
            patch(
                "app.routers.video_control.iot_client.remote_input_keepalive",
                AsyncMock(return_value=provider),
            ),
            patch(
                "app.routers.video_control.iot_client.remote_video_v2", AsyncMock()
            ) as camera_clock,
            patch("app.routers.video_control.media_orchestrator_client", media),
        ):
            for shift in [3600, -3600]:
                receipt_utc = 1800000002 + shift
                provider["expires_at"] = datetime.fromtimestamp(
                    receipt_utc + 60, UTC
                ).isoformat()
                with patch(
                    "app.services.media_orchestrator_client.time.time",
                    return_value=receipt_utc,
                ):
                    await keepalive_device_control_lease(
                        773,
                        KeepaliveRequest(lease_id=lease),
                        {"org_id": 1},
                        AsyncMock(),
                    )
                request = captured[-1]
                assert request["body"]["lease_expires_at"] == receipt_utc + 60
                assert request["body"]["contract_version"] == 3
                assert request["headers"] == {"x-l4-media-contract": "l4-media/3.0.0"}
                vectors.append(
                    {
                        "name": "utc_forward" if shift > 0 else "utc_backward",
                        "source_type": descriptor["source_type"],
                        "profile_id": descriptor["profile_id"],
                        "initial_utc_seconds": 1800000000,
                        "utc_jump_seconds": shift,
                        "receipt_utc_seconds": receipt_utc,
                        "receipt_monotonic_ms": 7000,
                        "expected_remaining_ms": 60000,
                        "expected_deadline_monotonic_ms": 67000,
                        "request": request,
                    }
                )
            camera_clock.assert_not_awaited()
    # Explicit local evidence export lets MEDIA run these actual BFF requests.
    if output := os.environ.get("MB_DESKTOP_RENEW_VECTOR_OUTPUT"):
        Path(output).write_text(
            json.dumps({"contract": "l4-media/3.0.0", "vectors": vectors}, indent=2)
            + "\n"
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
