import copy
import json
import time
from datetime import UTC, datetime, timedelta
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import AsyncMock
from uuid import uuid4

import pytest
from fastapi import HTTPException

from app.services.camera_video import (
    BrowserCodecs,
    NativeCameraVideo,
    resolve_camera_codec,
    validate_browser,
)
from app.services.video_contract import make_stop_command, validate_iot_state

PACKAGE = Path(__file__).resolve().parents[3] / "contracts/siplite-camera-v1/fixtures"
BASE = Path(__file__).resolve().parents[3] / "contracts/hevc-v2/fixtures"


def fixture(name, base=PACKAGE):
    return json.loads((base / f"{name}.json").read_text())["payload"]


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.fixture
def context(monkeypatch):
    from app.config import settings

    monkeypatch.setattr(settings, "video_contract_v2_enabled", True)
    monkeypatch.setattr(settings, "l4desk_session_orchestration_enabled", True)
    from cryptography.fernet import Fernet

    monkeypatch.setattr(settings, "camera_monotonic_runtime_enabled", True)
    monkeypatch.setattr(settings, "camera_native_auto_enabled", True)
    monkeypatch.setattr(
        settings, "camera_admission_ticket_key", Fernet.generate_key().decode()
    )
    sample = fixture("current_hevc_level41")
    camera = {
        "camera_id": "cam:ip000001",
        "revision": "a" * 64,
        "camera_contract_revision": "siplite-camera/1.0.0",
        "actual_track": sample["descriptor"]["tracks"][0],
        "discovery": {"generation": 1},
        "capabilities": {
            "fields": [
                {"path": "video0.codec", "write": True, "values": ["h264", "h265"]}
            ]
        },
    }
    server = {
        "contract_version": 3,
        "media_contract_versions": [3],
        "server_revision": "test-media3",
        "dependencies_ready": True,
        "hevc_runtime_verified": True,
        "codecs": ["h264", "h265"],
        "formats": [
            {
                "codec": "h265",
                "codec_profile": "main",
                "packetization": "rfc7798",
                "clock_rate": 90000,
            },
            {
                "codec": "h264",
                "codec_profile": "high",
                "packetization": "rfc6184",
                "clock_rate": 90000,
            },
        ],
        "max_video_tracks": 1,
        "audio_supported": False,
        "reverse_rtcp": True,
    }
    camera["geometry_generation"] = 7
    camera["discovery"].update(
        state="ready",
        physical_port=2,
        candidates=[
            {"mac": "02:00:00:00:00:01", "port_verified": True, "auth": "verified"}
        ],
    )
    caps = {
        "camera_contract_revisions": ["siplite-camera/1.2.0"],
        "camera_cadence_revisions": ["camera-cadence/0.1.0-proposed"],
        "camera_enrollment_revisions": ["camera-enrollment/0.3.0-proposed"],
        "camera_admission_revisions": ["camera-ctl-monotonic/0.2.0-proposed"],
        "media_contract_versions": [3],
        "quality_options": [{"profile_id": "low"}, {"profile_id": "default"}],
    }
    lease_id = str(uuid4())
    lease = {
        "sn": "fixture",
        "lease_id": lease_id,
        "scope": "stream",
        "active": True,
        "owner_user_id": "8",
        "owner_session_id": "owned",
        "expires_at": (datetime.now(UTC) + timedelta(seconds=90)).isoformat(),
        "media_lease_expires_at": int(time.time()) + 7,
        "media_remaining_ms": 7000,
        "media_clock_id": str(uuid4()),
    }
    trace = []

    async def iot(sn, action, **kwargs):
        trace.append(("iot", action, kwargs))
        assert kwargs["camera_contract"] == "siplite-camera/1.2.0"
        if action == "lease":
            return lease
        if action == "state":
            raise HTTPException(404)
        if action == "inventory":
            return {
                "camera_contract_revision": "siplite-camera/1.2.0",
                "media_clock_id": lease["media_clock_id"],
                "contract_version": 3,
                "media_contract_revision": "l4-media/3.0.0",
                "camera_capabilities": caps,
                "camera_observation": {
                    "camera_contract_revision": "siplite-camera/1.2.0",
                    "media_contract_revision": "l4-media/3.0.0",
                    "cadence_revision": "camera-cadence/0.1.0-proposed",
                    "source_id": camera["camera_id"],
                    "geometry_generation": camera["geometry_generation"],
                    "actual_track": camera["actual_track"],
                    "capabilities": caps,
                },
                "main_app_online": True,
                "fresh": True,
                "camera": camera,
                "producer_adapter": {"ready": True},
            }
        if action == "start":
            value = kwargs["payload"]
            return {
                "camera_contract_revision": "siplite-camera/1.2.0",
                "media_clock_id": lease["media_clock_id"],
                "contract_version": 3,
                "media_contract_revision": "l4-media/3.0.0",
                "cadence_revision": "camera-cadence/0.1.0-proposed",
                "version": 2,
                "sn": sn,
                "lease_id": value["lease_id"],
                "operation_id": value["operation_id"],
                "command_id": value["operation_id"],
                "stream_instance_id": value["publisher"]["stream_instance_id"],
                "descriptor": value["descriptor"],
                "resolved_codec": value["descriptor"]["tracks"][0]["codec"],
                "result": "started",
                "state": "running",
                "confirmation": "command_ack",
                "actual_descriptor": None,
                "reason": None,
            }
        return {}

    async def media(action, payload=None, **kwargs):
        trace.append(("media", action, payload))
        assert not kwargs.get("camera_contract")
        if action == "capabilities":
            return server
        if action == "start":
            response = fixture("media_response", BASE)
            response.update(
                {
                    key: payload[key]
                    for key in (
                        "session_id",
                        "operation_id",
                        "publisher",
                        "lease_id",
                        "descriptor",
                    )
                }
            )
            response.update(
                contract_version=3,
                stream_instance_id=payload["publisher"]["stream_instance_id"],
            )
            return response
        return {}

    service = NativeCameraVideo(
        SimpleNamespace(remote_video_v2=AsyncMock(side_effect=iot)),
        SimpleNamespace(video_v2=AsyncMock(side_effect=media)),
    )
    now = int(time.time() * 1000)
    browser = BrowserCodecs(
        h264=True, h265=True, checked_at_ms=now, valid_until_ms=now + 60000
    )
    args = {
        "terminal": SimpleNamespace(sn="fixture", device_id=4623, org_id=1),
        "lease_id": lease_id,
        "policy": "auto",
        "source_id": "cam:ip000001",
        "profile_id": "default",
        "browser": browser,
        "user": {"sub": "8", "session_id": "owned"},
    }
    return service, args, camera, server, trace


def ready_start_args(args, camera):
    from app.services.camera_admission import scope, seal

    descriptor = {
        "contract_version": 3,
        "descriptor_revision": str(uuid4()),
        "source_type": "camera",
        "source_id": args["source_id"],
        "geometry_generation": camera["geometry_generation"],
        "profile_id": args["profile_id"],
        "tracks": [copy.deepcopy(camera["actual_track"])],
    }
    return {
        **args,
        "admission_ticket": seal(
            {
                "scope": scope(args["terminal"], args["user"]),
                "stage": "ready",
                "deadline": time.time() + 5,
                "descriptor": descriptor,
                "mac": "02:00:00:00:00:01",
                "generation": 1,
                "codec_reason": "test",
                "policy": args["policy"],
                "nonce": str(uuid4()),
            }
        ),
    }


def test_auto_prefers_hevc_but_never_treats_unknown_as_incompatibility(context):
    _, args, camera, server, _ = context
    assert resolve_camera_codec("auto", camera, server, args["browser"])[0] == "h265"
    unknown = args["browser"].model_copy(update={"h265": None})
    with pytest.raises(HTTPException) as error:
        resolve_camera_codec("auto", camera, server, unknown)
    assert error.value.detail["code"] == "camera_capability_chain_unknown"
    unsupported = args["browser"].model_copy(update={"h265": False})
    assert resolve_camera_codec("auto", camera, server, unsupported)[0] == "h264"
    with pytest.raises(HTTPException):
        resolve_camera_codec("h265", camera, server, unsupported)


def test_explicit_h264_does_not_require_known_hevc_browser_support(context):
    _, args, camera, server, _ = context
    browser = args["browser"].model_copy(update={"h265": None})
    assert resolve_camera_codec("h264", camera, server, browser) == (
        "h264",
        "explicit_h264",
    )


def test_expired_browser_observation_is_not_reused(context):
    _, args, _, _, _ = context
    with pytest.raises(HTTPException):
        validate_browser(args["browser"], args["browser"].valid_until_ms)


@pytest.mark.anyio
async def test_native_order_and_ack_is_not_ready(context):
    service, args, camera, _, trace = context
    result = await service.start(**ready_start_args(args, camera))
    assert (
        result["resolved_codec"] == "h265" and result["confirmation"] == "command_ack"
    )
    assert result["actual_descriptor"] is None
    actions = [(entry[0], entry[1]) for entry in trace]
    assert actions.index(("media", "start")) < actions.index(("iot", "start"))
    assert result["descriptor"]["tracks"][0]["level_id"] == 123
    stop = make_stop_command("fixture", args["lease_id"], {"agent": {"stream": result}})
    assert stop["stream_instance_id"] == result["stream_instance_id"]
    assert stop["contract_version"] == 3 and "agent_instance_id" not in stop


@pytest.mark.anyio
async def test_unconfirmed_producer_compensates_only_the_created_epoch(context):
    service, args, camera, _, trace = context
    original = service.iot.remote_video_v2.side_effect

    async def wrong(sn, action, **kwargs):
        value = await original(sn, action, **kwargs)
        if action == "start":
            value = copy.deepcopy(value)
            value["stream_instance_id"] = str(uuid4())
        return value

    service.iot.remote_video_v2.side_effect = wrong
    with pytest.raises(HTTPException):
        await service.start(**ready_start_args(args, camera))
    request = next(item[2] for item in trace if item[:2] == ("media", "start"))
    stop = next(item[2] for item in trace if item[:2] == ("media", "stop"))
    assert stop["stream_instance_id"] == request["publisher"]["stream_instance_id"]


def test_camera_state_cannot_claim_desktop_ready(context):
    sample = fixture("current_hevc_level41")
    value = {
        "camera_contract_revision": "siplite-camera/1.0.0",
        "version": 2,
        "sn": "fixture",
        "lease_id": str(uuid4()),
        "operation_id": str(uuid4()),
        "command_id": str(uuid4()),
        "stream_instance_id": str(uuid4()),
        "descriptor": sample["descriptor"],
        "resolved_codec": "h265",
        "state": "running",
        "result": "started",
        "confirmation": "agent_ready",
        "actual_descriptor": sample["descriptor"],
    }
    with pytest.raises(HTTPException):
        validate_iot_state(value)
