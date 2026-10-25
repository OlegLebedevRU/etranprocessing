"""Actual camera BFF requests must survive the frozen downstream CTLv1 schema."""

# ruff: noqa: F811

import asyncio
import copy
import hashlib
import json
import os
from pathlib import Path
from uuid import UUID, uuid4, uuid5

import pytest
from fastapi import HTTPException
from jsonschema import Draft202012Validator
from test_camera_admission import admission, anyio_backend, ready_ticket  # noqa: F401
from test_camera_video import context  # noqa: F401

from app.services.camera_admission import descriptor_sha256, seal, unseal
from app.services.video_contract import media_identity

FIXTURES = Path(__file__).parent / "fixtures/camera-ctl-v0.2"


def frozen_start(request, ready):
    """IoT copies these common request IDs into its camera CTL adapter unchanged."""
    message = json.loads((FIXTURES / "start.json").read_text())
    message.update(
        sn=request["publisher"]["sn"],
        command_id=request["operation_id"],
        lease_id=request["lease_id"],
        stream_instance_id=request["publisher"]["stream_instance_id"],
        source_id=request["descriptor"]["source_id"],
        readiness_nonce=ready["nonce"],
    )
    message["binding"].update(
        descriptor_sha256=descriptor_sha256(request["descriptor"]),
        source_id=request["descriptor"]["source_id"],
        codec=request["descriptor"]["tracks"][0]["codec"],
    )
    return message


@pytest.mark.anyio
async def test_actual_bff_start_ids_validate_under_frozen_camera_ctl02(admission):
    service, args, *_ = admission
    ticket = await ready_ticket(admission)
    ready = unseal(ticket["ticket"], args["terminal"], args["user"], "ready")
    await service.native.start(**args, admission_ticket=ticket["ticket"])
    request = next(
        copy.deepcopy(call.args[1])
        for call in service.media.video_v2.await_args_list
        if call.args[0] == "start"
    )
    raw = (FIXTURES / "camera-ctl.schema.json").read_bytes()
    assert hashlib.sha256(raw).hexdigest() == (
        "e75e1c7f49f0d0fc68f6720a3c47766ccb4caad83d721b5d1b8aabe3d3b58a0d"
    )
    schema = json.loads(raw)
    Draft202012Validator({**schema, "$ref": "#/$defs/start"}).validate(
        frozen_start(request, ready)
    )
    if output := os.environ.get("MB_CAMERA_UUID4_START_OUTPUT"):
        Path(output).write_text(
            json.dumps(
                {"request": request, "ctl_mapping": frozen_start(request, ready)},
                indent=2,
            )
            + "\n"
        )


@pytest.mark.anyio
async def test_identity_is_allocated_in_prepare_and_preserved_by_readiness(admission):
    service, args, _, selection, *_ = admission
    prepared = await service.prepare(args["terminal"], args["user"], selection)
    before = unseal(prepared["ticket"], args["terminal"], args["user"], "prepared")
    ready = await service.readiness(args["terminal"], args["user"], prepared["ticket"])
    after = unseal(ready["ticket"], args["terminal"], args["user"], "ready")
    assert before["attempt_ids"] == after["attempt_ids"]
    assert len(set(before["attempt_ids"].values())) == 2
    assert all(UUID(value).version == 4 for value in before["attempt_ids"].values())
    second = await service.prepare(args["terminal"], args["user"], selection)
    different = unseal(second["ticket"], args["terminal"], args["user"], "prepared")
    assert set(different["attempt_ids"].values()).isdisjoint(
        before["attempt_ids"].values()
    )


@pytest.mark.anyio
async def test_concurrent_duplicate_start_retains_one_identity_and_media_mapping(
    admission,
):
    service, args, *_ = admission
    ready = await ready_ticket(admission)
    results = await asyncio.gather(
        *[
            service.native.start(**args, admission_ticket=ready["ticket"])
            for _ in range(2)
        ]
    )
    requests = [
        call.args[1]
        for call in service.media.video_v2.await_args_list
        if call.args[0] == "start"
    ]
    assert len(requests) == 2 and requests[0] == requests[1]
    assert results[0]["stream_instance_id"] == results[1]["stream_instance_id"]
    request = requests[0]
    stream = request["publisher"]["stream_instance_id"]
    identity = media_identity(
        args["terminal"].sn, args["lease_id"], stream, request["descriptor"]
    )
    assert (
        request["session_id"]
        == identity["session_id"]
        == str(uuid5(UUID(stream), "media"))
    )
    assert UUID(request["session_id"]).version == 5  # Common MEDIA identity, never CTL.


@pytest.mark.anyio
@pytest.mark.parametrize(
    "mutation",
    ["missing", "stream_v5", "operation_v5", "collision", "extra", "uppercase"],
)
async def test_old_or_malformed_ticket_identity_rejects_before_provider_io(
    admission, mutation
):
    service, args, *_ = admission
    ready = await ready_ticket(admission)
    value = unseal(ready["ticket"], args["terminal"], args["user"], "ready")
    ids = value["attempt_ids"]
    if mutation == "missing":
        value.pop("attempt_ids")
    elif mutation in ("stream_v5", "operation_v5"):
        ids["stream_instance_id" if mutation == "stream_v5" else "operation_id"] = str(
            uuid5(uuid4(), "old")
        )
    elif mutation == "collision":
        ids["operation_id"] = ids["stream_instance_id"]
    elif mutation == "extra":
        ids["another_id"] = str(uuid4())
    else:
        ids["operation_id"] = "AAAAAAAA-AAAA-4AAA-8AAA-AAAAAAAAAAAA"
    service.native.iot.remote_video_v2.reset_mock()
    service.media.video_v2.reset_mock()
    with pytest.raises(HTTPException, match="camera_preparation_identity_invalid"):
        await service.native.start(**args, admission_ticket=seal(value))
    service.native.iot.remote_video_v2.assert_not_awaited()
    service.media.video_v2.assert_not_awaited()


@pytest.mark.anyio
async def test_wrong_lease_cannot_create_another_identity_or_media_route(admission):
    service, args, *_ = admission
    ready = await ready_ticket(admission)
    value = unseal(ready["ticket"], args["terminal"], args["user"], "ready")
    with pytest.raises(HTTPException):
        await service.native.start(
            **{**args, "lease_id": str(uuid4())}, admission_ticket=ready["ticket"]
        )
    assert not any(
        call.args[0] in ("start", "stop")
        for call in service.media.video_v2.await_args_list
    )
    assert (
        unseal(ready["ticket"], args["terminal"], args["user"], "ready")["attempt_ids"]
        == value["attempt_ids"]
    )


@pytest.mark.anyio
@pytest.mark.parametrize("changed", ["stream_instance_id", "operation_id"])
async def test_running_record_from_a_different_attempt_is_not_reused(
    admission, changed
):
    service, args, *_ = admission
    ready = await ready_ticket(admission)
    result = await service.native.start(**args, admission_ticket=ready["ticket"])
    previous = {
        key: copy.deepcopy(value)
        for key, value in result.items()
        if key not in ("media", "codec_reason")
    }
    previous[changed] = str(uuid4())
    if changed == "operation_id":
        previous["command_id"] = previous[changed]
    original = service.native.iot.remote_video_v2.side_effect

    async def current(sn, action, **kwargs):
        return previous if action == "state" else await original(sn, action, **kwargs)

    service.native.iot.remote_video_v2.side_effect = current
    before = len(service.media.video_v2.await_args_list)
    with pytest.raises(HTTPException, match="conflict"):
        await service.native.start(**args, admission_ticket=ready["ticket"])
    assert len(service.media.video_v2.await_args_list) == before
