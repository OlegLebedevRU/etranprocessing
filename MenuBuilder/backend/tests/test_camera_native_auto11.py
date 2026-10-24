import copy
import hashlib
from pathlib import Path

import httpx
import pytest
from fastapi import HTTPException
from test_camera_admission import admission, anyio_backend  # noqa: F401
from test_camera_video import context  # noqa: F401

from app.config import settings
from app.services.camera_admission import unseal
from app.services.camera_contract import (
    CADENCE_REVISION,
    CAMERA11_HEADERS,
    CAMERA11_REVISION,
)


@pytest.fixture
def native11(admission, monkeypatch):  # noqa: F811
    service, _args, camera, selection, _calls, request, _trace = admission
    monkeypatch.setattr(settings, "camera_native_auto_enabled", True)
    camera["config"]["video0"].update(fps=0, size="")
    caps = {
        "camera_contract_revisions": [CAMERA11_REVISION],
        "camera_cadence_revisions": [CADENCE_REVISION],
        "camera_enrollment_revisions": [CAMERA11_HEADERS["X-L4-Camera-Enrollment"]],
        "camera_admission_revisions": [CAMERA11_HEADERS["X-L4-Camera-Admission"]],
    }
    observation = {
        "camera_contract_revision": CAMERA11_REVISION,
        "cadence_revision": CADENCE_REVISION,
        "source_id": selection.source_id,
        "geometry_generation": 17,
        "actual_track": copy.deepcopy(camera["actual_track"]),
        "capabilities": copy.deepcopy(caps),
    }
    iot_original = service.native.iot.remote_video_v2.side_effect

    async def iot(sn, action, **kwargs):
        assert kwargs["camera_contract"] == CAMERA11_REVISION
        value = await iot_original(sn, action, **{**kwargs, "camera_contract": True})
        if action in ("inventory", "start"):
            value["camera_contract_revision"] = CAMERA11_REVISION
        if action == "inventory":
            value.update(camera_observation=observation, camera_capabilities=caps)
        return value

    service.native.iot.remote_video_v2.side_effect = iot
    media_original = service.media.video_v2.side_effect

    async def media(action, payload=None, **kwargs):
        assert kwargs["camera_contract"] == CAMERA11_REVISION
        value = await media_original(action, payload, camera_contract=True)
        if action == "capabilities":
            value = {**value, **caps}
        return value

    service.media.video_v2.side_effect = media
    request_original = request.side_effect

    async def selected(method, path, org, user, **kwargs):
        from app.services.camera_admission import RUNTIME_HEADERS

        assert kwargs["headers"] == CAMERA11_HEADERS
        value = await request_original(
            method, path, org, user, **{**kwargs, "headers": RUNTIME_HEADERS}
        )
        if path.endswith("readiness"):
            value["binding"]["path_generation"] = "port2/1/17"
        return value

    request.side_effect = selected
    return admission, observation, caps


@pytest.mark.anyio
async def test_native_auto_full_bundle_before_lease_and_device_geometry(native11):
    bundle, observation, _ = native11
    service, args, _, selection, _calls, _, trace = bundle
    prepared = await service.prepare(args["terminal"], args["user"], selection)
    value = unseal(prepared["ticket"], args["terminal"], args["user"], "prepared")
    assert value["descriptor"]["geometry_generation"] == 17
    assert value["descriptor"]["tracks"] == [observation["actual_track"]]
    assert value["descriptor"]["cadence_revision"] == CADENCE_REVISION
    ready = await service.readiness(args["terminal"], args["user"], prepared["ticket"])
    assert not any(action in ("lease", "start") for _, action, _ in trace)
    result = await service.native.start(**args, admission_ticket=ready["ticket"])
    assert result["descriptor"] == value["descriptor"]
    assert result["camera_contract_revision"] == CAMERA11_REVISION


@pytest.mark.anyio
@pytest.mark.parametrize("peer", ["device", "iot"])
@pytest.mark.parametrize(
    "field",
    [
        "camera_contract_revisions",
        "camera_cadence_revisions",
        "camera_enrollment_revisions",
        "camera_admission_revisions",
    ],
)
async def test_partial_bundle_never_prepares(native11, peer, field):
    bundle, observation, caps = native11
    service, args, _, selection, calls, _, _ = bundle
    del (observation["capabilities"] if peer == "device" else caps)[field]
    with pytest.raises(HTTPException):
        await service.prepare(args["terminal"], args["user"], selection)
    assert not calls


@pytest.mark.anyio
@pytest.mark.parametrize(
    "field,value", [("geometry_generation", 18), ("cadence_revision", "other")]
)
async def test_observation_invalidation_cannot_reuse_prepared_ticket(
    native11, field, value
):
    bundle, observation, _ = native11
    service, args, _, selection, _calls, _, trace = bundle
    prepared = await service.prepare(args["terminal"], args["user"], selection)
    observation[field] = value
    with pytest.raises(HTTPException):
        await service.readiness(args["terminal"], args["user"], prepared["ticket"])
    assert _calls == ["prepare"]
    assert not any(action in ("lease", "start") for _, action, _ in trace)


@pytest.mark.anyio
@pytest.mark.parametrize("action", ["capabilities", "start", "health", "renew", "stop"])
async def test_media11_transmits_exact_bundle_on_every_operation(action):
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
            {"session_id": "isolated", "contract_version": 2},
            camera_contract=CAMERA11_REVISION,
        )
    assert all(seen[0].headers[key] == value for key, value in CAMERA11_HEADERS.items())
    assert "X-L4-Camera-Readiness" not in seen[0].headers


def test_camera11_schema_runtime_copies_are_exact():
    root = Path(__file__).resolve().parents[3]
    expected = hashlib.sha256(
        (root / "contracts/siplite-camera-v1.1/hevc.schema.json").read_bytes()
    ).digest()
    for name in [
        "MenuBuilder/backend/app/contracts/camera-media-v1.1.schema.json",
        "l4media/ingress/spec/camera-v1.1/hevc.schema.json",
    ]:
        assert hashlib.sha256((root / name).read_bytes()).digest() == expected


@pytest.mark.anyio
@pytest.mark.parametrize("action", ["inventory", "lease", "state", "start", "stop"])
async def test_iot11_transmits_bundle_without_readiness_downgrade(action, monkeypatch):
    from app.services.iot_client import IotPlatformClient

    monkeypatch.setattr(settings, "remote_control_enabled", True)
    seen = []

    def handle(request):
        seen.append(request)
        return httpx.Response(200, json={"ok": True})

    original_client = httpx.AsyncClient
    monkeypatch.setattr(
        httpx,
        "AsyncClient",
        lambda **kwargs: original_client(
            transport=httpx.MockTransport(handle), **kwargs
        ),
    )
    client = IotPlatformClient(base_url="http://localhost", service_token="")
    await client.remote_video_v2(
        "fixture",
        action,
        lease_id="isolated",
        camera_contract=CAMERA11_REVISION,
        camera_readiness="test-nonce" if action == "start" else None,
    )
    assert len(seen) == 1
    assert all(seen[0].headers[key] == value for key, value in CAMERA11_HEADERS.items())
    if action == "start":
        assert seen[0].headers["X-L4-Camera-Readiness"] == "test-nonce"


@pytest.mark.anyio
async def test_media_missing_cadence_never_prepares(native11):
    bundle, _, _ = native11
    service, args, _, selection, calls, _, _ = bundle
    original = service.media.video_v2.side_effect

    async def incomplete(action, *args, **kwargs):
        value = await original(action, *args, **kwargs)
        value.pop("camera_cadence_revisions", None)
        return value

    service.media.video_v2.side_effect = incomplete
    with pytest.raises(HTTPException):
        await service.prepare(args["terminal"], args["user"], selection)
    assert not calls
