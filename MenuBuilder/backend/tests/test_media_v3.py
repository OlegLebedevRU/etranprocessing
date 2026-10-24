"""Native quality roundtrips and explicit media version selection, offline only."""

import copy
import hashlib
import json
import time
from datetime import UTC, datetime
from pathlib import Path

import httpx
import pytest
from fastapi import HTTPException

from app.services.video_contract import select_hevc_profile, validate_video
from tests.test_hevc_contract import context as hevc_context
from tests.test_hevc_contract import fixture


@pytest.fixture
def context(monkeypatch):
    return hevc_context.__wrapped__(monkeypatch)


@pytest.fixture
def anyio_backend():
    return "asyncio"


def test_shared_schema_is_the_coordinator_copy():
    schema = Path(__file__).parents[1] / "app/contracts/media-v3.schema.json"
    assert hashlib.sha256(schema.read_bytes()).hexdigest() == (
        "b86ca6f066fcdecb09cf7145a7fb23d87b77311a53c7ea04f0fb6e8a905b2818"
    )


@pytest.mark.anyio
@pytest.mark.parametrize(
    "size", [(800, 600), (1024, 768), (1280, 1024), (1920, 1080), (3000, 2000)]
)
@pytest.mark.parametrize("profile", ["low", "default"])
async def test_native_start_ready_state_health_roundtrip(context, size, profile):
    service, args, trace, envelope = context
    capability = envelope["video_capabilities"]
    entry = capability["video"][0]
    track = entry["profiles"][0]["track"]
    track.update(
        width=size[0], height=size[1], fps=10 if profile == "low" else 15, level_id=150
    )
    entry["profiles"] = [{"profile_id": profile, "track": track}]
    args["profile_id"] = profile
    original_track = copy.deepcopy(track)
    result = await service.start(**args)
    descriptor = result["descriptor"]
    assert descriptor["contract_version"] == 3
    assert descriptor["profile_id"] == profile
    assert descriptor["tracks"] == [original_track]
    command = next(
        value["payload"]
        for owner, action, value in trace
        if (owner, action) == ("iot", "start")
    )
    assert command["v"] == 2
    assert command["descriptor"] == descriptor
    assert result["media"]["descriptor"] == descriptor
    assert result["media"]["contract_version"] == 3

    args["status"]["agent"]["stream"] = dict(
        result, agent_instance_id=capability["agent_instance_id"]
    )
    original_media = service.media.video_v2.side_effect

    async def media(action, payload=None):
        if action == "health":
            response = copy.deepcopy(result["media"])
            response.pop("pin", None)
            response.update(
                state="active",
                transport={
                    "connected": True,
                    "rtp_packets": 1,
                    "bytes": 100,
                    "last_rtp_at_ms": None,
                    "rtp_fresh": False,
                },
            )
            return response
        return await original_media(action, payload)

    service.media.video_v2.side_effect = media
    replay = await service.start(**args)
    assert replay["descriptor"] == descriptor
    assert replay["result"] == "already_running"


@pytest.mark.anyio
@pytest.mark.parametrize("version", [None, 2, 4])
async def test_unknown_or_mixed_capability_never_falls_back(context, version):
    service, args, trace, envelope = context
    capability = envelope["video_capabilities"]
    if version is None:
        capability.pop("contract_version")
    else:
        capability["contract_version"] = version
    with pytest.raises(HTTPException) as error:
        await service.start(**dict(args, policy="auto"))
    assert error.value.detail["code"] == "invalid_video_contract"
    assert not any(action == "start" for _, action, _ in trace)


@pytest.mark.anyio
@pytest.mark.parametrize(
    "field,value,reason",
    [
        ("source_id", "other", "source_mismatch"),
        ("valid_until_ms", 1, "stale_capability"),
    ],
)
async def test_stale_or_changed_source_is_rejected(context, field, value, reason):
    service, args, trace, envelope = context
    envelope["video_capabilities"][field] = value
    with pytest.raises(HTTPException) as error:
        await service.start(**args)
    assert error.value.detail["code"] == reason
    assert not any(action == "start" for _, action, _ in trace)


def test_common_shape_has_no_camera_quality_limit():
    value = fixture("media_response")["descriptor"]
    for source in ("desktop", "usb-camera", "camera"):
        for profile in ("low", "default"):
            value.update(source_type=source, profile_id=profile)
            value["tracks"][0].update(width=3000, height=2000, fps=60, level_id=186)
            assert validate_video("descriptor", value) is value


def test_media_format_is_checked_without_codec_profile_relabel():
    capability = fixture("capabilities")["video_capabilities"]
    server = fixture("media_capabilities")
    now = int(time.time() * 1000)
    capability.update(checked_at_ms=now, valid_until_ms=now + 60000)
    server["formats"] = [fmt for fmt in server["formats"] if fmt["codec"] == "h264"]
    # Schema requires advertised codecs and formats to agree.
    with pytest.raises(HTTPException):
        select_hevc_profile(capability, server, capability["source_id"], "low", now)


@pytest.mark.anyio
@pytest.mark.parametrize("support", [False, None])
async def test_browser_cannot_decode_selected_codec(context, support):
    service, args, trace, _ = context
    now = int(time.time() * 1000)
    with pytest.raises(HTTPException) as error:
        await service.start(
            **args,
            browser_codecs={
                "h264": True,
                "h265": support,
                "checked_at_ms": now,
                "valid_until_ms": now + 60000,
            },
        )
    assert error.value.detail["code"] == "browser_codec_unsupported"
    assert not any(action == "start" for _, action, _ in trace)


@pytest.mark.anyio
async def test_http_media_selection_and_camera_isolation():
    from app.services.media_orchestrator_client import MediaOrchestratorClient

    requests = []

    def respond(request):
        requests.append(request)
        return httpx.Response(200, json={})

    async with httpx.AsyncClient(transport=httpx.MockTransport(respond)) as http:
        media = MediaOrchestratorClient(
            base_url="http://localhost", service_token="", client=http
        )
        identity = {
            "contract_version": 3,
            "session_id": "test",
            "lease_id": "lease",
            "stream_instance_id": "stream",
        }
        for action in ("capabilities", "start", "health", "renew", "stop"):
            await media.video_v2(action, identity)
        await media.video_v2("capabilities", camera_contract=True)
    for request in requests[:-1]:
        assert request.headers["X-L4-Media-Contract"] == "l4-media/3.0.0"
        assert "X-L4-Camera-Contract" not in request.headers
        if request.method == "POST":
            assert json.loads(request.content)["contract_version"] == 3
    assert "X-L4-Media-Contract" not in requests[-1].headers
    assert "X-L4-Camera-Contract" in requests[-1].headers


@pytest.mark.anyio
@pytest.mark.parametrize(
    "policy", ["auto", "h264", "h265", "auto-browser-h264", "auto-server-h264"]
)
async def test_codec_selection_preserves_declared_native_track(context, policy):
    service, args, _, envelope = context
    entry = envelope["video_capabilities"]["video"][0]
    h264 = copy.deepcopy(entry)
    h264.update(codec="h264", backend="openh264", hardware=False)
    track = fixture("media_response")["descriptor"]["tracks"][0]
    for key in (
        "profile_space",
        "profile_id",
        "tier_flag",
        "level_id",
        "sprop_max_don_diff",
    ):
        track.pop(key)
    track.update(
        codec="h264",
        codec_profile="baseline",
        packetization="rfc6184",
        profile_level_id="42e01f",
        packetization_mode=1,
        width=3000,
        height=2000,
    )
    h264["profiles"][0]["track"] = track
    envelope["video_capabilities"]["video"].append(h264)
    if policy == "auto":
        entry.update(
            status="unsupported",
            reason_code="no_hardware_mft",
            profiles=[],
            probe_evidence="none",
        )
    if policy == "auto-browser-h264":
        now = int(time.time() * 1000)
        args["browser_codecs"] = {
            "h264": True,
            "h265": False,
            "checked_at_ms": now,
            "valid_until_ms": now + 60000,
        }
    if policy == "auto-server-h264":
        original = service.media.video_v2.side_effect

        async def media(action, payload=None):
            if action == "capabilities":
                server = fixture("media_capabilities")
                server.update(codecs=["h264"], hevc_runtime_verified=False)
                server["formats"] = [
                    fmt for fmt in server["formats"] if fmt["codec"] == "h264"
                ]
                return server
            return await original(action, payload)

        service.media.video_v2.side_effect = media
    selected_policy = "auto" if policy.startswith("auto-") else policy
    result = await service.start(**dict(args, policy=selected_policy))
    expected = "h265" if policy == "h265" else "h264"
    assert result["resolved_codec"] == expected
    assert result["descriptor"]["tracks"][0] == (
        entry["profiles"][0]["track"] if expected == "h265" else track
    )
    assert result["descriptor"]["contract_version"] == 3


@pytest.mark.anyio
@pytest.mark.parametrize("mutation", ["version", "size", "codec"])
async def test_state_rejects_mixed_version_and_changed_actual_descriptor(
    context, mutation
):
    from app.services.video_contract import validate_iot_state

    service, args, trace, _ = context
    result = await service.start(**args)
    command = next(
        value["payload"]
        for owner, action, value in trace
        if (owner, action) == ("iot", "start")
    )
    state = {
        key: command[key]
        for key in (
            "sn",
            "lease_id",
            "agent_instance_id",
            "operation_id",
            "command_id",
            "stream_instance_id",
        )
    }
    state.update(
        version=2,
        contract_version=3,
        result="started",
        state="running",
        resolved_codec="h265",
        descriptor=result["descriptor"],
        actual_descriptor=copy.deepcopy(result["descriptor"]),
    )
    if mutation == "version":
        state["actual_descriptor"]["contract_version"] = 2
    elif mutation == "size":
        state["actual_descriptor"]["tracks"][0]["width"] = 800
    else:
        state["resolved_codec"] = "h264"
    with pytest.raises(HTTPException):
        validate_iot_state(state)


@pytest.mark.anyio
@pytest.mark.parametrize("lease_seconds", [12, 60])
async def test_cold_start_budget_before_and_after_prepare_keeps_lease_deadline(
    context, monkeypatch, lease_seconds
):
    from app.services import video_contract

    service, args, trace, envelope = context
    clock = [int(time.time())]
    deadline = clock[0] + lease_seconds
    args["status"]["lease"]["expires_at"] = datetime.fromtimestamp(
        deadline, UTC
    ).isoformat()
    unchanged_lease = copy.deepcopy(args["status"]["lease"])
    envelope["video_capabilities"].update(
        checked_at_ms=clock[0] * 1000, valid_until_ms=(clock[0] + 60) * 1000
    )
    monkeypatch.setattr(video_contract.time, "time", lambda: clock[0])
    initial = []
    original_validate = video_contract.validate_video

    def validate(definition, value):
        if definition == "start":
            initial.append(copy.deepcopy(value))
        return original_validate(definition, value)

    monkeypatch.setattr(video_contract, "validate_video", validate)
    original_media = service.media.video_v2.side_effect

    async def media(action, payload=None):
        if action == "start":
            clock[0] += 5
        return await original_media(action, payload)

    service.media.video_v2.side_effect = media
    await service.start(**args)
    command = next(
        value["payload"]
        for owner, action, value in trace
        if (owner, action) == ("iot", "start")
    )
    assert initial[0]["expires_at_ms"] - initial[0]["issued_at_ms"] == min(
        35000, lease_seconds * 1000
    )
    assert command["expires_at_ms"] - command["issued_at_ms"] == min(
        35000, (lease_seconds - 5) * 1000
    )
    assert command["expires_at_ms"] <= command["lease_expires_at_ms"] == deadline * 1000
    assert args["status"]["lease"] == unchanged_lease
    assert sum(action == "start" and owner == "iot" for owner, action, _ in trace) == 1
