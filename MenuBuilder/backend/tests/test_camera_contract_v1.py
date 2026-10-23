import copy
import hashlib
import json
from pathlib import Path

import httpx
import pytest
from fastapi import HTTPException

from app.services.camera_contract import (
    CONTRACTS,
    MEDIA_VALIDATORS,
    extended_camera,
    validate_camera_rpc,
)
from app.services.media_orchestrator_client import MediaOrchestratorClient
from app.services.video_contract import validate_video

PACKAGE = Path(__file__).resolve().parents[3] / "contracts/siplite-camera-v1"
VECTORS = [
    json.loads(path.read_text(encoding="utf-8"))
    for path in sorted((PACKAGE / "fixtures").glob("*.json"))
]


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.parametrize(
    "vector",
    [item for item in VECTORS if item["target"] == "rpc"],
    ids=lambda item: item["name"],
)
def test_accepted_rpc_vectors(vector):
    payload = copy.deepcopy(vector["payload"])
    item = payload.get("dt", [{}])[0]
    operation = item.get("operation_id", "00000000-0000-4000-8000-000000000011")
    if vector["valid"]:
        assert (
            validate_camera_rpc(int(vector["definition"][3:]), payload, operation)
            == payload
        )
    else:
        with pytest.raises(HTTPException) as error:
            validate_camera_rpc(int(vector["definition"][3:]), payload, operation)
        assert error.value.status_code == 422


@pytest.mark.parametrize(
    "vector",
    [item for item in VECTORS if item["target"] == "media"],
    ids=lambda item: item["name"],
)
def test_accepted_media_vectors(vector):
    assert (
        MEDIA_VALIDATORS[vector["definition"]].is_valid(vector["payload"])
        is vector["valid"]
    )


def test_runtime_mirrors_match_the_accepted_package():
    for runtime, canonical in [
        ("camera-rpc.schema.json", "camera-rpc.schema.json"),
        ("camera-media.schema.json", "hevc.schema.json"),
    ]:
        assert (
            hashlib.sha256((CONTRACTS / runtime).read_bytes()).digest()
            == hashlib.sha256((PACKAGE / canonical).read_bytes()).digest()
        )


def test_discovery_identity_is_not_the_cancel_target():
    vector = next(item for item in VECTORS if item["name"] == "discovery_cancel")
    payload = vector["payload"]
    own = payload["dt"][0]["operation_id"]
    assert validate_camera_rpc(165, payload, own) == payload
    with pytest.raises(HTTPException):
        validate_camera_rpc(165, payload, payload["dt"][0]["target_operation_id"])
    same_target = copy.deepcopy(payload)
    same_target["dt"][0]["target_operation_id"] = own
    with pytest.raises(HTTPException):
        validate_camera_rpc(165, same_target, own)


@pytest.mark.parametrize(
    "payload",
    [
        {"dt": [{"username": "private-input", "password": "private-input"}]},
        {"dt": [{"timeout_ms": True}]},
        {"dt": [{"timeout_ms": float("nan")}]},
        {"dt": [{}], "unexpected": "x" * 8192},
        {"dt": [{}], "unexpected": "\ud800"},
    ],
)
def test_bad_input_is_rejected_without_echoing_it(payload):
    with pytest.raises(HTTPException) as error:
        validate_camera_rpc(160, payload, "00000000-0000-4000-8000-000000000011")
    assert error.value.detail == {"code": "invalid_camera_command"}


def test_media_revision_detection_never_infers_camera_from_source_name():
    assert not extended_camera({"source_type": "camera"})
    assert extended_camera({"camera_contract_revision": "unknown"})
    assert extended_camera({"descriptor": {"camera_contract_revision": "unknown"}})


def test_video_reader_accepts_concrete_camera_and_rejects_unknown_revision():
    payload = copy.deepcopy(
        next(item for item in VECTORS if item["name"] == "native_hevc_level51")[
            "payload"
        ]
    )
    assert validate_video("media_start", payload) == payload
    payload["descriptor"]["camera_contract_revision"] = "unknown"
    with pytest.raises(HTTPException) as error:
        validate_video("media_start", payload)
    assert error.value.status_code == 502


@pytest.mark.anyio
@pytest.mark.parametrize("action", ["capabilities", "start", "health", "renew", "stop"])
@pytest.mark.parametrize("negotiated", [False, True])
async def test_negotiation_is_explicit_on_every_media_operation(action, negotiated):
    seen = []

    def handle(request):
        seen.append(request)
        return httpx.Response(200, json={"ok": True})

    async with httpx.AsyncClient(transport=httpx.MockTransport(handle)) as transport:
        client = MediaOrchestratorClient(
            base_url="http://fixture.invalid", service_token="", client=transport
        )
        assert await client.video_v2(
            action,
            {"session_id": "isolated", "contract_version": 2},
            camera_contract=negotiated,
        ) == {"ok": True}
    assert seen[0].headers.get("X-L4-Camera-Contract") == (
        "siplite-camera/1.0.0" if negotiated else None
    )
