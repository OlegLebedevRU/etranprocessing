"""Old CTL v1 stays usable without enabling or fabricating a HEVC contract."""

import json
import time
from types import SimpleNamespace
from unittest.mock import AsyncMock
from uuid import uuid4

import pytest
from fastapi import HTTPException

from app.config import settings
from tests.test_hevc_contract import context as hevc_context


@pytest.fixture
def context(monkeypatch):
    result = hevc_context.__wrapped__(monkeypatch)
    now = int(time.time() * 1000)
    result[1]["browser_codecs"] = {
        "h264": True,
        "h265": True,
        "checked_at_ms": now,
        "valid_until_ms": now + 60000,
    }
    return result


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.anyio
@pytest.mark.parametrize("protocols", [None, [], ["pointer_move", "stream_start"]])
@pytest.mark.parametrize("policy", ["auto", "h264"])
async def test_old_presence_uses_v1_without_new_transport_or_probe(
    context, monkeypatch, protocols, policy
):
    service, args, trace, _ = context
    args["status"]["agent"]["capabilities"] = protocols
    for name in ("video_proxy_rtp_port", "video_proxy_rtcp_port"):
        monkeypatch.setattr(settings, name, 0)
    args["browser_codecs"].update(
        h264=None, h265=None, checked_at_ms=1, valid_until_ms=2
    )
    assert await service.start(**dict(args, policy=policy)) is None
    assert trace == []  # Return to caller's route-before-START adapter only.


@pytest.mark.anyio
async def test_explicit_hevc_on_legacy_never_sends_start(context):
    service, args, trace, _ = context
    args["status"]["agent"]["capabilities"] = []
    with pytest.raises(HTTPException) as error:
        await service.start(**args)
    assert error.value.detail["code"] == "legacy_h264_only"
    assert trace == []


@pytest.mark.anyio
async def test_presence_before_protocol_field_remains_legacy(context):
    service, args, trace, _ = context
    args["status"]["agent"].pop("capabilities")
    assert await service.start(**dict(args, policy="auto")) is None
    assert trace == []


@pytest.mark.anyio
@pytest.mark.parametrize("policy", ["h264", "auto"])
async def test_disabled_hevc_policy_does_not_disable_selected_h264(
    context, monkeypatch, policy
):
    service, args, trace, _ = context
    monkeypatch.setattr(settings, "video_contract_v2_enabled", False)
    args["browser_codecs"]["h265"] = False
    assert await service.start(**dict(args, policy=policy)) is None
    assert trace == []


@pytest.mark.anyio
async def test_h264_adapter_does_not_bypass_changed_native_epoch(context):
    service, args, trace, _ = context
    args["status"]["agent"]["agent_instance_id"] = str(uuid4())
    with pytest.raises(HTTPException) as error:
        await service.start(**dict(args, policy="h264"))
    assert error.value.detail["code"] == "capability_mismatch"
    assert not any(action == "start" for _, action, _ in trace)


@pytest.mark.anyio
@pytest.mark.parametrize("change", [{"online": False}, {"stale": True}])
async def test_old_agent_still_requires_fresh_online_presence(context, change):
    service, args, trace, _ = context
    args["status"]["agent"].update(capabilities=[], **change)
    with pytest.raises(HTTPException) as error:
        await service.start(**dict(args, policy="h264"))
    assert error.value.detail["code"] == "offline"
    assert trace == []


@pytest.mark.anyio
@pytest.mark.parametrize(
    "field,value", [("owner_session_id", "other"), ("scope", "view")]
)
async def test_old_path_cannot_bypass_lease_authorization(context, field, value):
    service, args, trace, _ = context
    args["status"]["agent"]["capabilities"] = []
    args["status"]["lease"][field] = value
    with pytest.raises(HTTPException) as error:
        await service.start(**dict(args, policy="auto"))
    assert error.value.status_code == 403
    assert trace == []


@pytest.mark.anyio
@pytest.mark.parametrize("policy", ["auto", "h264", "h265"])
async def test_false_negotiation_does_not_mean_old_agent(context, policy):
    service, args, trace, _ = context
    original = service.iot.remote_video_v2.side_effect

    async def iot(sn, action, **kwargs):
        if action == "inventory":
            return {
                "contract_version": 3,
                "negotiated_v2": False,
                "video_capabilities": None,
            }
        return await original(sn, action, **kwargs)

    service.iot.remote_video_v2.side_effect = iot
    if policy == "h264":
        assert await service.start(**dict(args, policy=policy)) is None
    else:
        with pytest.raises(HTTPException) as error:
            await service.start(**dict(args, policy=policy))
        assert error.value.detail["code"] == "negotiation_unavailable"
    assert not any(action == "start" for _, action, _ in trace)


@pytest.mark.anyio
async def test_missing_ports_do_not_hide_codec_but_block_descriptor_start(
    context, monkeypatch
):
    service, args, trace, _ = context
    monkeypatch.setattr(settings, "video_proxy_rtp_port", 0)
    available = await service.availability(
        args["sn"], args["source_id"], args["profile_id"], args["org_id"], args["user"]
    )
    assert available["hevc_available"] is True
    trace.clear()
    with pytest.raises(HTTPException) as error:
        await service.start(**args)
    assert error.value.status_code == 503
    assert error.value.detail["code"] == "transport_not_configured"
    assert not any(action == "start" for _, action, _ in trace)


@pytest.mark.anyio
async def test_cold_probe_does_not_expire_browser_observation_mid_request(
    context, monkeypatch
):
    service, args, _, _ = context
    original = service.availability
    original_now = time.time()

    async def availability(*values, **kwargs):
        result = await original(*values, **kwargs)
        # The capability read may take most of the browser report's lifetime.
        monkeypatch.setattr(
            "app.services.video_contract.time.time", lambda: original_now + 61
        )
        return result

    # Use a longer pre-existing lease, never extend it in the implementation.
    args["status"]["lease"]["expires_at"] = "2099-01-01T00:00:00+00:00"
    service.availability = availability
    result = await service.start(**args)
    assert result["resolved_codec"] == "h265"


@pytest.mark.anyio
@pytest.mark.parametrize("failure", [None, 409, 504])
async def test_legacy_route_before_start_and_cleanup(context, monkeypatch, failure):
    from app.routers import video_control as router

    _, args, _, _ = context
    args["status"]["agent"]["capabilities"] = []
    terminal = SimpleNamespace(id=1, device_id=773, sn=args["sn"], org_id=1)
    monkeypatch.setattr(
        router, "_verify_device_access", AsyncMock(return_value=terminal)
    )
    monkeypatch.setattr(
        router, "load_video_status", AsyncMock(return_value=args["status"])
    )
    repo = SimpleNamespace(
        get_active_session_by_terminal_id=AsyncMock(return_value=None),
        ensure_l4desk_terminal=AsyncMock(),
        create_remote_session=AsyncMock(return_value=SimpleNamespace()),
    )
    monkeypatch.setattr(router, "L4DeskRepository", lambda db: repo)
    trace = []

    async def prepare(*values, **kwargs):
        trace.append("route")
        return SimpleNamespace(
            model_dump=lambda: {"mountpoint_id": 773, "janus_ws": "/janus-ws"}
        )

    async def start(**kwargs):
        trace.append("start")
        assert kwargs["mode"] == "desktop" and kwargs["profile"] == "low"
        if failure:
            raise HTTPException(
                failure,
                detail={
                    "code": "terminal_timeout" if failure == 504 else "unsupported"
                },
            )
        return {
            "stream_instance_id": str(uuid4()),
            "result": "started",
            "state": "running",
        }

    monkeypatch.setattr(router, "create_video_session", prepare)
    monkeypatch.setattr(router.iot_client, "remote_input_stream_start", start)
    cleanup = AsyncMock()
    monkeypatch.setattr(router, "_stop_active_media_session", cleanup)
    body = router.StreamStartRequest(
        lease_id=args["lease_id"],
        mode="desktop",
        source_id="disp:old",
        profile="low",
        codec_policy="auto",
        browser_codecs=args["browser_codecs"],
    )
    user = dict(args["user"], org_id=1)
    if failure:
        with pytest.raises(HTTPException) as error:
            await router.start_device_stream(773, body, user, AsyncMock())
        assert error.value.status_code == failure
        cleanup.assert_awaited_once_with(args["sn"], args["lease_id"], None)
    else:
        response = await router.start_device_stream(773, body, user, AsyncMock())
        result = json.loads(response.body)
        assert result["resolved_codec"] == "h264"
        assert result["media"]["stream_instance_id"] == result["stream_instance_id"]
        assert "descriptor" not in result
        cleanup.assert_not_awaited()
    assert trace == ["route", "start"]


@pytest.mark.anyio
async def test_legacy_http_payload_has_no_v3_header_or_descriptor(monkeypatch):
    import httpx

    from app.services.iot_client import IotPlatformClient

    monkeypatch.setattr(settings, "remote_control_enabled", True)
    calls = []

    def respond(request):
        calls.append(request)
        assert "X-L4-Media-Contract" not in request.headers
        assert "X-L4-Camera-Contract" not in request.headers
        assert json.loads(request.content) == {
            "mode": "desktop",
            "source_id": "disp:old",
            "profile": "low",
        }
        return httpx.Response(200, json={"result": "started"})

    original = httpx.AsyncClient
    monkeypatch.setattr(
        "app.services.iot_client.httpx.AsyncClient",
        lambda **kwargs: original(transport=httpx.MockTransport(respond), **kwargs),
    )
    client = IotPlatformClient(base_url="http://localhost", service_token="")
    await client.remote_input_stream_start(
        "lease", "desktop", "disp:old", "low", org_id=1
    )
    assert len(calls) == 1
