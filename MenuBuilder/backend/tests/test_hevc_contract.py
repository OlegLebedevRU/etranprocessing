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

from app.config import settings
from app.services.video_contract import (
    VideoContractOrchestrator,
    load_video_status,
    select_hevc_profile,
    validate_video,
)

FIXTURES = Path(__file__).resolve().parents[3] / "contracts/hevc-v2/fixtures"


def fixture(name):
    return json.loads((FIXTURES / f"{name}.json").read_text())["payload"]


def test_remembered_measurement_uses_current_check_for_freshness():
    cap = fixture("capabilities")["video_capabilities"]
    now = int(time.time() * 1000)
    cap.update(
        measured_at_ms=now - 86400000, checked_at_ms=now, valid_until_ms=now + 60000
    )
    track, reason = select_hevc_profile(
        cap, fixture("media_capabilities"), cap["source_id"], "low", now
    )
    assert track is not None and reason == "ready"
    _, reason = select_hevc_profile(
        cap, fixture("media_capabilities"), cap["source_id"], "low", now + 60000
    )
    assert reason == "stale_capability"
    cap["measured_at_ms"] = now + 1
    with pytest.raises(HTTPException) as exc:
        validate_video("capabilities", cap)
    assert exc.value.detail["code"] == "invalid_video_contract"


@pytest.mark.parametrize("reason", ["probe_timeout", "encoder_busy", "no_hardware_mft"])
def test_hevc_unavailable_reason_does_not_become_profile_unsupported(reason):
    cap = fixture("capabilities")["video_capabilities"]
    now = int(time.time() * 1000)
    cap.update(checked_at_ms=now, valid_until_ms=now + 60000)
    cap["video"][0].update(
        status="unsupported"
        if reason == "no_hardware_mft"
        else "temporarily_unavailable",
        reason_code=reason,
        profiles=[],
        probe_evidence="none",
    )
    track, actual = select_hevc_profile(
        cap, fixture("media_capabilities"), cap["source_id"], "low", now
    )
    assert track is None
    assert actual == reason


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.fixture
def context(monkeypatch):
    now = int(time.time() * 1000)
    cap = fixture("capabilities")
    cap["video_capabilities"].update(
        checked_at_ms=now - 1000, valid_until_ms=now + 59000
    )
    lease = {
        "active": True,
        "lease_id": "00000000-0000-4000-8000-000000000001",
        "scope": "stream",
        "owner_user_id": "8",
        "owner_session_id": "owned",
        "expires_at": (datetime.now(UTC) + timedelta(seconds=60)).isoformat(),
    }
    status = {"lease": lease, "agent": {"online": True, "stream": {"state": "stopped"}}}
    for key, value in {
        "video_contract_v2_enabled": True,
        "l4desk_session_orchestration_enabled": True,
        "video_proxy_rtp_port": 5004,
        "video_proxy_rtcp_port": 5005,
    }.items():
        monkeypatch.setattr(settings, key, value)
    trace = []

    async def media(action, payload=None):
        trace.append(("media", action, copy.deepcopy(payload)))
        if action == "capabilities":
            return fixture("media_capabilities")
        if action == "start":
            value = fixture("media_response")
            value.update(
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
            value.update(
                stream_instance_id=payload["publisher"]["stream_instance_id"],
                pin="synthetic-pin",
            )
            return value
        return {}

    async def iot(sn, action, **kwargs):
        trace.append(("iot", action, copy.deepcopy(kwargs)))
        if action == "inventory":
            return {
                "negotiated_v2": True,
                "video_capabilities": cap["video_capabilities"],
            }
        if action == "start":
            command = kwargs["payload"]
            value = {
                "version": 2,
                "result": "started",
                "state": "running",
                "resolved_codec": "h265",
                "confirmation": "agent_ready",
                "descriptor": command["descriptor"],
            }
            value.update(
                {
                    key: command[key]
                    for key in (
                        "sn",
                        "lease_id",
                        "stream_instance_id",
                        "command_id",
                        "operation_id",
                        "agent_instance_id",
                    )
                }
            )
            value["actual_descriptor"] = command["descriptor"]
            return value
        return {}

    service = VideoContractOrchestrator(
        SimpleNamespace(remote_video_v2=AsyncMock(side_effect=iot)),
        SimpleNamespace(video_v2=AsyncMock(side_effect=media)),
    )
    args = {
        "sn": cap["sn"],
        "lease_id": lease["lease_id"],
        "policy": "h265",
        "mode": "desktop",
        "source_id": "display-0",
        "profile_id": "low",
        "status": status,
        "org_id": 1,
        "user": {"sub": "8", "session_id": "owned"},
    }
    return service, args, trace, cap


@pytest.mark.anyio
@pytest.mark.parametrize("sn", ["new-desktop-terminal", "new-camera-terminal"])
async def test_hevc_availability_does_not_require_pilot_sn(context, sn):
    service, args, _, _ = context
    result = await service.availability(
        sn, args["source_id"], args["profile_id"], args["org_id"], args["user"]
    )
    assert result["hevc_available"] is True
    assert result["reason_code"] == "ready"
    service.iot.remote_video_v2.assert_awaited_once_with(
        sn,
        "inventory",
        org_id=args["org_id"],
        user=args["user"],
        source_id=None,
        profile_id=None,
        refresh=False,
    )


@pytest.mark.anyio
async def test_desktop_availability_probes_selected_source(context):
    service, args, _, cap = context
    cap["video_capabilities"]["source_id"] = "disp:secondary"
    result = await service.availability(
        args["sn"], "disp:secondary", args["profile_id"], args["org_id"], args["user"]
    )
    assert result["hevc_available"] is True
    service.iot.remote_video_v2.assert_awaited_once_with(
        args["sn"],
        "inventory",
        org_id=args["org_id"],
        user=args["user"],
        source_id="disp:secondary",
        profile_id=args["profile_id"],
        refresh=False,
    )


@pytest.mark.anyio
async def test_desktop_start_reconfirms_context_before_publishing(context):
    service, args, trace, cap = context
    args["source_id"] = "disp:secondary"
    cap["video_capabilities"]["source_id"] = args["source_id"]
    cap["video_capabilities"]["admission_clock"] = "issuer_monotonic_v1"
    result = await service.start(**args)
    assert result["resolved_codec"] == "h265"
    queries = [entry for entry in trace if entry[:2] == ("iot", "inventory")]
    assert (
        len(queries) == 2
        and queries[0][2]["refresh"] is False
        and queries[1][2]["refresh"] is True
    )
    assert queries[0][2]["profile_id"] == args["profile_id"]
    assert trace.index(queries[0]) < next(
        i for i, entry in enumerate(trace) if entry[:2] == ("iot", "start")
    )


@pytest.mark.anyio
async def test_non_desktop_availability_does_not_enable_desktop_time_handshake(context):
    service, args, _, cap = context
    source = cap["video_capabilities"]["source_id"]
    assert not source.startswith("disp:")
    await service.availability(
        args["sn"], source, args["profile_id"], args["org_id"], args["user"], fresh=True
    )
    assert service.iot.remote_video_v2.call_args.kwargs["refresh"] is False


@pytest.mark.anyio
async def test_legacy_desktop_start_does_not_force_another_hardware_probe(context):
    service, args, trace, cap = context
    args["source_id"] = "disp:legacy"
    cap["video_capabilities"]["source_id"] = args["source_id"]
    await service.start(**args)
    queries = [entry for entry in trace if entry[:2] == ("iot", "inventory")]
    assert len(queries) == 1 and queries[0][2]["refresh"] is False


@pytest.mark.anyio
async def test_hevc_orchestration_disabled_still_denies_before_probe(
    context, monkeypatch
):
    service, args, trace, _ = context
    monkeypatch.setattr(settings, "l4desk_session_orchestration_enabled", False)
    result = await service.availability(
        args["sn"], args["source_id"], args["profile_id"], args["org_id"], args["user"]
    )
    assert result == {"hevc_available": False, "reason_code": "disabled_by_policy"}
    assert not trace


@pytest.mark.anyio
async def test_start_reads_publisher_context_without_changing_legacy_status(context):
    service, args, trace, _ = context
    lease = dict(args["status"]["lease"], sn=args["sn"])
    del args["status"]["lease"]["owner_session_id"]
    original = service.iot.remote_video_v2.side_effect

    async def iot(sn, action, **kwargs):
        if action == "lease":
            return lease
        return await original(sn, action, **kwargs)

    service.iot.remote_video_v2.side_effect = iot
    await service.start(**args)
    assert "owner_session_id" not in args["status"]["lease"]
    lease["owner_session_id"] = "other"
    trace.clear()
    with pytest.raises(HTTPException) as denied:
        await service.start(**args)
    assert denied.value.status_code == 403
    assert trace == []


@pytest.mark.anyio
async def test_prepare_before_exact_ready(context):
    service, args, trace, _ = context
    result = await service.start(**args)
    assert result["resolved_codec"] == "h265"
    actions = [(owner, action) for owner, action, _ in trace]
    assert actions.index(("media", "start")) < actions.index(("iot", "start"))
    command = next(
        value["payload"]
        for owner, action, value in trace
        if (owner, action) == ("iot", "start")
    )
    assert command["recovery_policy"] == "stop_on_failure"
    assert command["descriptor"] == result["descriptor"]


@pytest.mark.anyio
async def test_unknown_auto_and_explicit_fail_closed(context):
    service, args, trace, _ = context
    service.iot.remote_video_v2.side_effect = HTTPException(404)
    assert await service.start(**dict(args, policy="auto")) is None
    with pytest.raises(HTTPException) as error:
        await service.start(**args)
    assert error.value.status_code == 422
    assert not any((owner, action) == ("media", "start") for owner, action, _ in trace)


@pytest.mark.anyio
async def test_ack_not_ready_compensates_exact_epoch(context):
    service, args, trace, _ = context
    original = service.iot.remote_video_v2.side_effect

    async def unconfirmed(sn, action, **kwargs):
        if action == "start":
            return {"result": "accepted"}
        return await original(sn, action, **kwargs)

    service.iot.remote_video_v2.side_effect = unconfirmed
    with pytest.raises(HTTPException):
        await service.start(**args)
    start = next(
        value for owner, action, value in trace if (owner, action) == ("media", "start")
    )
    stop = next(
        value for owner, action, value in trace if (owner, action) == ("media", "stop")
    )
    assert stop["stream_instance_id"] == start["publisher"]["stream_instance_id"]
    assert stop["lease_id"] == args["lease_id"]
    assert sum((owner, action) == ("media", "start") for owner, action, _ in trace) == 1


@pytest.mark.anyio
async def test_no_owner_session_bypass(context):
    service, args, trace, _ = context
    args["user"].update(session_id="other", is_superuser=True)
    with pytest.raises(HTTPException) as error:
        await service.start(**args)
    assert error.value.status_code == 403
    assert not trace


def test_stale_and_unverified_capabilities(context):
    _, _, _, cap = context
    capability = cap["video_capabilities"]
    server = fixture("media_capabilities")
    now = capability["valid_until_ms"]
    assert select_hevc_profile(capability, server, "display-0", "low", now)[0] is None
    server["hevc_runtime_verified"] = False
    server["codecs"] = ["h264"]
    server["formats"] = server["formats"][:1]
    assert (
        select_hevc_profile(capability, server, "display-0", "low", now - 1000)[0]
        is None
    )


def test_frozen_vectors_and_schema_bytes():
    assert (FIXTURES.parent / "hevc.schema.json").read_bytes() == (
        Path(__file__).resolve().parents[1] / "app/contracts/hevc.schema.json"
    ).read_bytes()
    for path in FIXTURES.glob("*.json"):
        value = json.loads(path.read_text())
        if value["valid"]:
            validate_video(value["definition"], value["payload"])
        else:
            with pytest.raises(HTTPException):
                validate_video(value["definition"], value["payload"])


def test_camera_requires_explicit_test_policy(context):
    _, _, _, cap = context
    capability = copy.deepcopy(cap["video_capabilities"])
    capability["source_id"] = "test-rtsp-camera"
    capability["video"][0].update(backend="camera", hardware=False)
    server = fixture("media_capabilities")
    now = int(time.time() * 1000)
    args = (capability, server, "test-rtsp-camera", "low", now)
    assert select_hevc_profile(*args)[0] is None
    assert select_hevc_profile(*args, allow_test_camera=True)[0] is not None
    assert capability["video"][0]["hardware"] is False


@pytest.mark.anyio
async def test_full_state_reader_preserves_epoch_and_does_not_hide_denial(context):
    service, args, _, _ = context
    started = await service.start(**args)
    command = next(
        call.kwargs["payload"]
        for call in service.iot.remote_video_v2.call_args_list
        if call.args[1] == "start"
    )
    view = {
        "version": 2,
        "result": "started",
        "state": "running",
        "resolved_codec": "h265",
        "confirmation": "agent_ready",
        "descriptor": started["descriptor"],
        "actual_descriptor": started["descriptor"],
        **{
            key: command[key]
            for key in (
                "sn",
                "lease_id",
                "stream_instance_id",
                "agent_instance_id",
                "operation_id",
                "command_id",
            )
        },
    }
    service.iot.remote_input_status = AsyncMock(return_value=args["status"])
    service.iot.remote_video_v2.side_effect = None
    service.iot.remote_video_v2.return_value = view
    snapshot = await load_video_status(
        service.iot, args["sn"], org_id=1, user=args["user"]
    )
    assert (
        snapshot["agent"]["stream"]["stream_instance_id"]
        == started["stream_instance_id"]
    )
    assert (
        snapshot["agent"]["stream"]["agent_instance_id"] == command["agent_instance_id"]
    )
    service.iot.remote_video_v2.side_effect = HTTPException(403)
    with pytest.raises(HTTPException) as denied:
        await load_video_status(service.iot, args["sn"], org_id=1, user=args["user"])
    assert denied.value.status_code == 403


@pytest.mark.anyio
async def test_legacy_view_state_does_not_require_publisher_reader(monkeypatch):
    monkeypatch.setattr(settings, "video_contract_v2_enabled", True)
    legacy = {"lease": {"lease_id": str(uuid4()), "scope": "view"}, "agent": {}}
    iot = SimpleNamespace(
        remote_input_status=AsyncMock(return_value=legacy),
        remote_video_v2=AsyncMock(side_effect=HTTPException(403)),
    )
    assert await load_video_status(iot, "legacy", org_id=1, user={}) == legacy
    iot.remote_video_v2.assert_not_awaited()


@pytest.mark.anyio
async def test_http_adapter_uses_negotiated_routes_without_legacy_retry(monkeypatch):
    import httpx

    from app.services.iot_client import IotPlatformClient

    monkeypatch.setattr(settings, "remote_control_enabled", True)
    calls = []

    def respond(request):
        calls.append((request.method, request.url.path))
        return httpx.Response(200, json={})

    original = httpx.AsyncClient
    monkeypatch.setattr(
        "app.services.iot_client.httpx.AsyncClient",
        lambda **kwargs: original(transport=httpx.MockTransport(respond), **kwargs),
    )
    client = IotPlatformClient(
        base_url="http://localhost", service_token="synthetic-test-only"
    )
    for action in ("inventory", "lease", "state", "start", "stop"):
        await client.remote_video_v2(
            "test-source", action, lease_id="lease", payload={}, org_id=1
        )
    assert calls == [
        ("GET", "/api/internal/v1/remote-input/devices/test-source/video-capabilities"),
        ("GET", "/api/internal/v1/remote-input/lease/lease/video-lease"),
        ("GET", "/api/internal/v1/remote-input/lease/lease/video-state"),
        ("POST", "/api/internal/v1/remote-input/lease/lease/stream/start"),
        ("POST", "/api/internal/v1/remote-input/lease/lease/video-stop"),
    ]


@pytest.mark.anyio
async def test_http_adapter_preserves_source_and_separate_camera_contract(monkeypatch):
    import httpx

    from app.services.iot_client import IotPlatformClient

    monkeypatch.setattr(settings, "remote_control_enabled", True)
    requests = []

    def respond(request):
        requests.append(request)
        return httpx.Response(200, json={})

    original = httpx.AsyncClient
    monkeypatch.setattr(
        "app.services.iot_client.httpx.AsyncClient",
        lambda **kwargs: original(transport=httpx.MockTransport(respond), **kwargs),
    )
    client = IotPlatformClient(
        base_url="http://localhost", service_token="synthetic-test-only"
    )
    await client.remote_video_v2(
        "test-source", "inventory", source_id="disp:secondary", org_id=1
    )
    assert requests[0].url.params["source_id"] == "disp:secondary"
    assert "X-L4-Camera-Contract" not in requests[0].headers
    await client.remote_video_v2(
        "test-camera", "inventory", camera_contract=True, org_id=1
    )
    assert "source_id" not in requests[1].url.params
    assert "X-L4-Camera-Contract" in requests[1].headers
