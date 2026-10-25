from datetime import UTC, datetime, timedelta
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch
from uuid import uuid4

import pytest
from fastapi import HTTPException

from app.config import settings
from app.models import Terminal
from app.models_l4desk import L4DeskRemoteSession
from app.routers.video_control import KeepaliveRequest, keepalive_device_control_lease
from app.services.camera_media_clock import camera_media_deadline


@pytest.fixture
def anyio_backend():
    return "asyncio"


def authority():
    return {
        "active": True,
        "sn": "test-sn",
        "lease_id": "test-lease",
        "scope": "stream",
        "owner_user_id": "8",
        "owner_session_id": "owned",
        "expires_at": "invalid-live-UTC-must-not-be-read",
        "media_lease_expires_at": 1000000007,
        "media_remaining_ms": 7000,
        "media_clock_id": str(uuid4()),
    }


def verify(value, elapsed=1):
    return camera_media_deadline(
        value,
        sn="test-sn",
        lease_id="test-lease",
        user={"sub": "8", "session_id": "owned"},
        roundtrip_ms=elapsed,
    )


@pytest.mark.parametrize("wall", [0, 1800000000, 9000000000])
def test_camera_expiry_export_does_not_compare_wall_clock(wall, monkeypatch):
    monkeypatch.setattr("time.time", lambda: wall)
    assert verify(authority()) == 1000000007


@pytest.mark.parametrize(
    "field,value",
    [
        ("media_clock_id", None),
        ("media_clock_id", str(uuid4()).upper()),
        ("media_remaining_ms", 0),
        ("media_remaining_ms", 60001),
        ("media_remaining_ms", True),
        ("media_lease_expires_at", None),
        ("media_lease_expires_at", 1000000007.0),
        ("active", False),
        ("sn", "other"),
        ("scope", "input"),
        ("owner_session_id", "other"),
        ("owner_user_id", "other"),
    ],
)
def test_missing_stale_or_foreign_clock_authority_never_renews(field, value):
    body = authority()
    body[field] = value
    with pytest.raises(HTTPException):
        verify(body)


def test_http_roundtrip_consumes_remaining_budget_at_equality():
    assert verify(authority(), 6999) == 1000000007
    with pytest.raises(HTTPException) as expired:
        verify(authority(), 7000)
    assert expired.value.status_code == 410


@pytest.mark.anyio
async def test_camera_keepalive_uses_fixed_export_after_forward_backward_utc_jumps(
    monkeypatch,
):
    monkeypatch.setattr(settings, "video_contract_v2_enabled", True)
    terminal = Terminal(id=1, device_id=1, sn="test-sn", org_id=1)
    stream = str(uuid4())
    session = L4DeskRemoteSession(session_type="video", provider_session_id=stream)
    track = {
        "track_id": "video-main",
        "kind": "video",
        "direction": "producer_to_viewers",
        "bit_depth": 8,
        "chroma": "420",
        "clock_rate": 90000,
        "payload_type": 97,
        "width": 2592,
        "height": 1944,
        "fps": 20,
        "feedback": {"pli": False, "fir": False},
        "max_b_frames": 0,
        "keyframe_interval_ms": 1000,
        "repeat_parameter_sets": True,
        "codec": "h265",
        "codec_profile": "main",
        "packetization": "rfc7798",
        "profile_space": 0,
        "profile_id": 1,
        "tier_flag": 0,
        "level_id": 153,
        "sprop_max_don_diff": 0,
    }
    descriptor = {
        "contract_version": 3,
        "descriptor_revision": str(uuid4()),
        "source_type": "camera",
        "source_id": "cam:ip000001",
        "geometry_generation": 7,
        "profile_id": "low",
        "tracks": [track],
    }
    status = {
        "agent": {"stream": {"stream_instance_id": stream, "descriptor": descriptor}}
    }
    generic = {"stream_instance_id": stream, "expires_at": "invalid"}
    export = authority()
    status["agent"]["stream"]["media_clock_id"] = export["media_clock_id"]
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
            AsyncMock(return_value=generic),
        ),
        patch(
            "app.routers.video_control.iot_client.remote_video_v2",
            AsyncMock(return_value=export),
        ) as camera_lease,
        patch(
            "app.routers.video_control.media_orchestrator_client.video_v2",
            AsyncMock(return_value={}),
        ) as renew,
    ):
        observed = []
        for shift, expiry, remaining in [
            (86400, 1000000007, 7000),
            (-86400, 1000000015, 15000),
            (31536000, 1000000015, 15000),
        ]:
            generic["expires_at"] = (
                datetime.now(UTC) + timedelta(seconds=shift)
            ).isoformat()
            export.update(media_lease_expires_at=expiry, media_remaining_ms=remaining)
            await keepalive_device_control_lease(
                1,
                KeepaliveRequest(lease_id="test-lease"),
                {"sub": "8", "session_id": "owned", "is_superuser": True},
                AsyncMock(),
            )
            observed.append(renew.await_args.args[1]["lease_expires_at"])
            assert renew.await_args.args[1]["contract_version"] == 3
            assert (
                camera_lease.await_args.kwargs["camera_contract"]
                == "siplite-camera/1.2.0"
            )
        assert observed == [1000000007, 1000000015, 1000000015]
        # Lost/retired anchor is terminal even when generic UTC keepalive looks valid.
        export.pop("media_clock_id")
        count = sum(call.args[0] == "renew" for call in renew.await_args_list)
        with pytest.raises(HTTPException):
            await keepalive_device_control_lease(
                1,
                KeepaliveRequest(lease_id="test-lease"),
                {"sub": "8", "session_id": "owned", "is_superuser": True},
                AsyncMock(),
            )
        assert sum(call.args[0] == "renew" for call in renew.await_args_list) == count
        assert renew.await_args.args[0] == "stop"
        assert renew.await_args.args[1]["stream_instance_id"] == stream


def test_fixed_clock_id_cannot_change_during_an_attempt():
    value = authority()
    with pytest.raises(HTTPException, match="camera_media_clock_changed"):
        camera_media_deadline(
            value,
            sn="test-sn",
            lease_id="test-lease",
            user={"sub": "8", "session_id": "owned"},
            roundtrip_ms=1,
            expected_clock_id=str(uuid4()),
        )
