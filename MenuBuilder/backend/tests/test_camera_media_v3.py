# ruff: noqa: F811
import copy
import hashlib
from pathlib import Path

import httpx
import pytest
from fastapi import HTTPException
from test_camera_admission import admission, anyio_backend, ready_ticket  # noqa: F401
from test_camera_video import context  # noqa: F401

from app.config import settings
from app.services.camera_admission import unseal
from app.services.camera_contract import (
    CAMERA_MEDIA_HEADERS,
    CAMERA_MEDIA_REVISION,
    camera_headers,
)


@pytest.mark.anyio
@pytest.mark.parametrize("profile", ["low", "default"])
async def test_native_auto_common_descriptor_before_lease(admission, profile):
    service, args, camera, selection, calls, _, trace = admission
    camera["config"]["video0"].update(fps=0, size="", bitrate=0)
    selection.profile_id = args["profile_id"] = profile
    configured = copy.deepcopy(camera["config"])
    prepared = await service.prepare(args["terminal"], args["user"], selection)
    ticket = unseal(prepared["ticket"], args["terminal"], args["user"], "prepared")
    descriptor = ticket["descriptor"]
    assert set(descriptor) == {
        "contract_version",
        "descriptor_revision",
        "source_type",
        "source_id",
        "geometry_generation",
        "profile_id",
        "tracks",
    }
    assert descriptor["contract_version"] == 3 and descriptor["profile_id"] == profile
    assert descriptor["geometry_generation"] == 7 and descriptor["tracks"] == [
        camera["actual_track"]
    ]
    ready = await service.readiness(args["terminal"], args["user"], prepared["ticket"])
    assert not any(action in ("lease", "start") for _, action, _ in trace)
    result = await service.native.start(**args, admission_ticket=ready["ticket"])
    assert result["descriptor"] == descriptor and result["actual_descriptor"] is None
    assert (
        result["contract_version"] == 3
        and result["camera_contract_revision"] == CAMERA_MEDIA_REVISION
    )
    assert camera["config"] == configured
    assert calls == ["prepare", "admission", "readiness"]


@pytest.mark.anyio
@pytest.mark.parametrize("where", ["observation", "projected"])
@pytest.mark.parametrize(
    "field",
    [
        "camera_contract_revisions",
        "camera_cadence_revisions",
        "camera_enrollment_revisions",
        "camera_admission_revisions",
        "media_contract_versions",
        "quality_options",
    ],
)
async def test_partial_bundle_never_prepares(admission, where, field):
    service, args, _, selection, calls, _, _ = admission
    original = service.native.iot.remote_video_v2.side_effect

    async def incomplete(sn, action, **kwargs):
        value = await original(sn, action, **kwargs)
        if action == "inventory":
            value = copy.deepcopy(value)
            del (
                value["camera_observation"]["capabilities"]
                if where == "observation"
                else value["camera_capabilities"]
            )[field]
        return value

    service.native.iot.remote_video_v2.side_effect = incomplete
    with pytest.raises(HTTPException):
        await service.prepare(args["terminal"], args["user"], selection)
    assert not calls


@pytest.mark.anyio
async def test_profile_selection_is_immutable_in_ready_ticket(admission):
    service, args, *_ = admission
    ready = await ready_ticket(admission)
    with pytest.raises(HTTPException, match="camera_preparation_selection_changed"):
        await service.native.start(
            **{**args, "profile_id": "low"}, admission_ticket=ready["ticket"]
        )


@pytest.mark.anyio
async def test_unsupported_quality_policy_never_prepares(admission):
    service, args, _, selection, calls, _, _ = admission
    selection.profile_id = "low"
    original = service.native.iot.remote_video_v2.side_effect

    async def unsupported(sn, action, **kwargs):
        value = await original(sn, action, **kwargs)
        if action == "inventory":
            value = copy.deepcopy(value)
            value["camera_capabilities"]["quality_options"] = [
                {"profile_id": "default"}
            ]
            value["camera_observation"]["capabilities"]["quality_options"] = [
                {"profile_id": "default"}
            ]
        return value

    service.native.iot.remote_video_v2.side_effect = unsupported
    with pytest.raises(HTTPException, match="camera_quality_policy_unsupported"):
        await service.prepare(args["terminal"], args["user"], selection)
    assert not calls


@pytest.mark.anyio
@pytest.mark.parametrize("action", ["capabilities", "start", "health", "renew", "stop"])
async def test_media_uses_only_common_header(action):
    from app.services.media_orchestrator_client import MediaOrchestratorClient

    seen = []

    def handle(request):
        seen.append(request)
        return httpx.Response(200, json={"ok": True})

    async with httpx.AsyncClient(transport=httpx.MockTransport(handle)) as transport:
        client = MediaOrchestratorClient(
            base_url="http://localhost", service_token="", client=transport
        )
        await client.video_v2(
            action,
            {"session_id": "test", "contract_version": 3},
            camera_contract=CAMERA_MEDIA_REVISION,
        )
    assert seen[0].headers["X-L4-Media-Contract"] == "l4-media/3.0.0"
    assert not any(key.lower().startswith("x-l4-camera") for key in seen[0].headers)


@pytest.mark.anyio
@pytest.mark.parametrize("action", ["inventory", "lease", "state", "start", "stop"])
async def test_iot_exact_five_header_bundle(action, monkeypatch):
    from app.services.iot_client import IotPlatformClient

    monkeypatch.setattr(settings, "remote_control_enabled", True)
    seen = []

    def handle(request):
        seen.append(request)
        return httpx.Response(200, json={"ok": True})

    original = httpx.AsyncClient
    monkeypatch.setattr(
        httpx,
        "AsyncClient",
        lambda **kwargs: original(transport=httpx.MockTransport(handle), **kwargs),
    )
    client = IotPlatformClient(base_url="http://localhost", service_token="")
    await client.remote_video_v2(
        "test",
        action,
        lease_id="isolated",
        camera_contract=CAMERA_MEDIA_REVISION,
        camera_readiness="test-nonce" if action == "start" else None,
    )
    assert len(seen) == 1
    assert all(seen[0].headers[k] == v for k, v in CAMERA_MEDIA_HEADERS.items())
    if action == "start":
        assert seen[0].headers["X-L4-Camera-Readiness"] == "test-nonce"


@pytest.mark.parametrize("revision", ["siplite-camera/1.1.0", "siplite-camera/1.3.0"])
def test_unknown_or_old_adapter_revision_is_not_downgraded(revision):
    with pytest.raises(HTTPException):
        camera_headers(revision)


def test_schema_copies_are_exact():
    root = Path(__file__).resolve().parents[3]
    assert (
        hashlib.sha256(
            (root / "contracts/l4-media-v3/media.schema.json").read_bytes()
        ).hexdigest()
        == "b86ca6f066fcdecb09cf7145a7fb23d87b77311a53c7ea04f0fb6e8a905b2818"
    )
    assert (root / "contracts/siplite-camera-v1.2/camera.schema.json").read_bytes() == (
        root / "MenuBuilder/backend/app/contracts/camera-v1.2.schema.json"
    ).read_bytes()


@pytest.mark.anyio
async def test_confirmed_h265_incompatibility_selects_observed_h264_before_start(
    admission,
):
    service, args, camera, selection, _, _, trace = admission
    track = camera["actual_track"]
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
        codec_profile="high",
        packetization="rfc6184",
        profile_level_id="640033",
        packetization_mode=1,
    )
    args["browser"] = args["browser"].model_copy(update={"h265": False})
    selection.browser_codecs = args["browser"]
    ready = await ready_ticket(admission)
    result = await service.native.start(**args, admission_ticket=ready["ticket"])
    assert result["resolved_codec"] == "h264"
    assert result["codec_reason"] == "camera_chain_h265_unsupported"
    assert [action for owner, action, _ in trace if owner == "iot"].count("start") == 1
    assert all(action not in ("patch", "rpc", "switch_codec") for _, action, _ in trace)


@pytest.mark.anyio
async def test_producer_quality_subset_cannot_be_overridden_by_projection(admission):
    service, args, _, selection, calls, _, _ = admission
    selection.profile_id = "low"
    original = service.native.iot.remote_video_v2.side_effect

    async def producer_default_only(sn, action, **kwargs):
        value = await original(sn, action, **kwargs)
        if action == "inventory":
            value = copy.deepcopy(value)
            value["camera_observation"]["capabilities"]["quality_options"] = [
                {"profile_id": "default"}
            ]
        return value

    service.native.iot.remote_video_v2.side_effect = producer_default_only
    with pytest.raises(HTTPException, match="camera_quality_policy_unsupported"):
        await service.prepare(args["terminal"], args["user"], selection)
    assert not calls


@pytest.mark.anyio
async def test_start_rejects_changed_captured_clock_and_scopes_compensation(admission):
    from uuid import uuid4

    service, args, _, _, _, _, trace = admission
    ready = await ready_ticket(admission)
    original = service.native.iot.remote_video_v2.side_effect

    async def wrong_clock(sn, action, **kwargs):
        value = await original(sn, action, **kwargs)
        if action == "start":
            value["media_clock_id"] = str(uuid4())
        return value

    service.native.iot.remote_video_v2.side_effect = wrong_clock
    with pytest.raises(HTTPException, match="camera_start_unconfirmed"):
        await service.native.start(**args, admission_ticket=ready["ticket"])
    start = next(
        body for owner, action, body in trace if owner == "media" and action == "start"
    )
    stop = next(
        body for owner, action, body in trace if owner == "media" and action == "stop"
    )
    assert stop["stream_instance_id"] == start["publisher"]["stream_instance_id"]


@pytest.mark.anyio
async def test_unknown_media_camera_selector_is_rejected_before_http():
    from app.services.media_orchestrator_client import MediaOrchestratorClient

    seen = []

    def handle(request):
        seen.append(request)
        return httpx.Response(200, json={})

    async with httpx.AsyncClient(transport=httpx.MockTransport(handle)) as transport:
        client = MediaOrchestratorClient(
            base_url="http://localhost", service_token="", client=transport
        )
        with pytest.raises(HTTPException):
            await client.video_v2(
                "capabilities", camera_contract="siplite-camera/unknown"
            )
    assert not seen
