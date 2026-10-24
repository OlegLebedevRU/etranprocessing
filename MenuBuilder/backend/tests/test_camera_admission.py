import hashlib
import json
import time
from types import SimpleNamespace
from unittest.mock import AsyncMock
from uuid import uuid4

import pytest
from cryptography.fernet import Fernet
from fastapi import HTTPException
from test_camera_video import context  # noqa: F401

from app.config import settings
from app.services.camera_admission import RUNTIME_HEADERS, CameraAdmission, seal, unseal


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.fixture
def admission(context, monkeypatch):  # noqa: F811
    native, args, camera, _, trace = context
    monkeypatch.setattr(settings, "camera_monotonic_runtime_enabled", True)
    monkeypatch.setattr(
        settings, "camera_admission_ticket_key", Fernet.generate_key().decode()
    )
    camera["discovery"].update(
        state="ready",
        physical_port=2,
        candidates=[
            {
                "mac": "02:00:00:00:00:01",
                "port_verified": True,
                "auth": "verified",
            }
        ],
    )
    camera["config"] = {"video0": {"fps": 20, "size": "1920x1080"}}
    selection = SimpleNamespace(
        source_id="cam:ip000001",
        physical_camera_mac="02:00:00:00:00:01",
        discovery_generation=1,
        codec_policy="auto",
        browser_codecs=args["browser"],
    )
    service = CameraAdmission(native.iot, native.media)
    configured = {}
    calls = []
    boot, epoch = str(uuid4()), str(uuid4())

    async def request(method, path, org_id, user, *, body=None, headers=None):
        assert headers == RUNTIME_HEADERS
        assert org_id == args["terminal"].org_id and user == args["user"]
        calls.append(path.rsplit("/", 1)[-1])
        if path.endswith("/prepare"):
            assert set(body) == {"descriptor", "physical_camera_mac"}
            configured.update(body)
            return {"configured": True}
        assert body is None
        if path.endswith("/readiness"):
            desc = configured["descriptor"]
            return {
                "type": "camera_readiness_query",
                "camera_admission_revision": RUNTIME_HEADERS["X-L4-Camera-Admission"],
                "sn": args["terminal"].sn,
                "issuer_epoch": epoch,
                "readiness_nonce": str(uuid4()),
                "binding": {
                    "boot_id": boot,
                    "physical_camera_mac": selection.physical_camera_mac,
                    "path_generation": "port2/1/1",
                    "source_id": selection.source_id,
                    "codec": desc["tracks"][0]["codec"],
                    "descriptor_sha256": hashlib.sha256(
                        json.dumps(desc, sort_keys=True, separators=(",", ":")).encode()
                    ).hexdigest(),
                },
            }
        return {"enrolled": True, "boot": boot, "epoch": epoch}

    mock = AsyncMock(side_effect=request)
    monkeypatch.setattr("app.services.camera_admission.camera_request", mock)
    return service, args, camera, selection, calls, mock, trace


async def ready_ticket(admission):
    service, args, _, selection, *_ = admission
    prepared = await service.prepare(args["terminal"], args["user"], selection)
    assert await service.enrollment(
        args["terminal"], args["user"], prepared["ticket"]
    ) == {"enrolled": True}
    return await service.readiness(args["terminal"], args["user"], prepared["ticket"])


@pytest.mark.anyio
async def test_prepare_enrollment_readiness_before_lease_media_start(admission):
    service, args, _, _, calls, _, trace = admission
    ready = await ready_ticket(admission)
    assert calls == ["prepare", "admission", "admission", "readiness"]
    assert not any(action in ("lease", "start") for _, action, _ in trace)
    result = await service.native.start(**args, admission_ticket=ready["ticket"])
    start = next(
        kwargs
        for owner, action, kwargs in trace
        if owner == "iot" and action == "start"
    )
    assert start["camera_readiness"]
    assert result["actual_descriptor"] is None
    assert [action for _, action, _ in trace].count(
        "start"
    ) == 2  # one media + one producer


@pytest.mark.anyio
async def test_disabled_has_no_io(admission, monkeypatch):
    service, args, _, selection, calls, _, trace = admission
    monkeypatch.setattr(settings, "camera_monotonic_runtime_enabled", False)
    with pytest.raises(HTTPException, match="camera_runtime_disabled"):
        await service.prepare(args["terminal"], args["user"], selection)
    assert not calls and not trace


@pytest.mark.anyio
@pytest.mark.parametrize(
    "change", ["port", "generation", "mac", "auth", "verified", "auto"]
)
async def test_unverified_or_native_auto_never_prepares(admission, change):
    service, args, camera, selection, calls, *_ = admission
    if change == "port":
        camera["discovery"]["physical_port"] = 1
    if change == "generation":
        camera["discovery"]["generation"] = 2
    if change == "mac":
        selection.physical_camera_mac = "02:00:00:00:00:02"
    if change == "auth":
        camera["discovery"]["candidates"][0]["auth"] = "required"
    if change == "verified":
        camera["discovery"]["candidates"][0]["port_verified"] = False
    if change == "auto":
        camera["config"]["video0"]["fps"] = 0
    with pytest.raises(HTTPException):
        await service.prepare(args["terminal"], args["user"], selection)
    assert not calls


@pytest.mark.anyio
async def test_ticket_cannot_change_owner_stage_or_deadline(admission):
    _, args, *_ = admission
    ready = await ready_ticket(admission)
    value = unseal(ready["ticket"], args["terminal"], args["user"], "ready")
    with pytest.raises(HTTPException):
        unseal(
            ready["ticket"],
            args["terminal"],
            {**args["user"], "session_id": "other"},
            "ready",
        )
    with pytest.raises(HTTPException):
        unseal(ready["ticket"], args["terminal"], args["user"], "prepared")
    with pytest.raises(HTTPException):
        unseal(
            seal({**value, "deadline": time.time() - 1}),
            args["terminal"],
            args["user"],
            "ready",
        )


@pytest.mark.anyio
async def test_changed_selection_before_start_never_acquires_media(admission):
    service, args, camera, _, _, _, trace = admission
    ready = await ready_ticket(admission)
    camera["discovery"]["generation"] += 1
    with pytest.raises(HTTPException):
        await service.native.start(**args, admission_ticket=ready["ticket"])
    assert not any(action in ("lease", "start") for _, action, _ in trace)


@pytest.mark.anyio
@pytest.mark.parametrize("stage", ["prepare", "readiness"])
async def test_ambiguous_mutation_never_replayed(admission, stage):
    service, args, _, selection, _calls, mock, _ = admission
    prepared = None
    if stage == "readiness":
        prepared = await service.prepare(args["terminal"], args["user"], selection)
    original = mock.side_effect

    async def fail(method, path, *rest, **kwargs):
        if path.endswith("/" + stage):
            raise HTTPException(504, "unconfirmed")
        return await original(method, path, *rest, **kwargs)

    mock.side_effect = fail
    with pytest.raises(HTTPException):
        if stage == "prepare":
            await service.prepare(args["terminal"], args["user"], selection)
        else:
            await service.readiness(args["terminal"], args["user"], prepared["ticket"])
    assert sum(c.args[1].endswith("/" + stage) for c in mock.call_args_list) == 1


@pytest.mark.anyio
async def test_missing_readiness_cannot_downgrade(admission):
    service, args, *_ = admission
    with pytest.raises(HTTPException):
        await service.native.start(**args)
    service.native.iot.remote_video_v2.assert_not_called()


@pytest.mark.anyio
@pytest.mark.parametrize(
    "field",
    ["camera_admission_revision", "sn", "issuer_epoch", "readiness_nonce", "binding"],
)
async def test_partial_or_wrong_readiness_never_authorizes_start(admission, field):
    service, args, _, selection, _, mock, trace = admission
    prepared = await service.prepare(args["terminal"], args["user"], selection)
    original = mock.side_effect

    async def malformed(method, path, *rest, **kwargs):
        result = await original(method, path, *rest, **kwargs)
        if path.endswith("/readiness"):
            result.pop(field)
        return result

    mock.side_effect = malformed
    with pytest.raises(HTTPException):
        await service.readiness(args["terminal"], args["user"], prepared["ticket"])
    assert not any(action in ("lease", "start") for _, action, _ in trace)


@pytest.mark.anyio
async def test_ambiguous_start_stops_exact_attempt_and_never_replays(admission):
    service, args, _, _, _, _, trace = admission
    ready = await ready_ticket(admission)
    original = service.native.iot.remote_video_v2.side_effect

    async def lost(sn, action, **kwargs):
        result = await original(sn, action, **kwargs)
        if action == "start":
            raise HTTPException(502, "response lost")
        return result

    service.native.iot.remote_video_v2.side_effect = lost
    with pytest.raises(HTTPException):
        await service.native.start(**args, admission_ticket=ready["ticket"])
    starts = [kw for owner, action, kw in trace if owner == "iot" and action == "start"]
    stops = [kw for owner, action, kw in trace if owner == "iot" and action == "stop"]
    assert len(starts) == len(stops) == 1
    assert (
        starts[0]["payload"]["publisher"]["stream_instance_id"]
        == stops[0]["payload"]["stream_instance_id"]
    )


@pytest.mark.anyio
async def test_duplicate_admission_has_identical_media_and_producer_identity(admission):
    service, args, _, _, _, _, trace = admission
    ready = await ready_ticket(admission)
    await service.native.start(**args, admission_ticket=ready["ticket"])
    await service.native.start(**args, admission_ticket=ready["ticket"])
    starts = [
        kw["payload"]
        for owner, action, kw in trace
        if owner == "iot" and action == "start"
    ]
    assert starts[0] == starts[1]
