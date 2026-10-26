"""Candidate-only contract checks; no routes, MQTT, camera or browser START."""

from uuid import uuid4

import pytest
from pydantic import ValidationError

from app.services.camera_quality_candidate import CameraQualityCandidate


def request(target):
    return {
        "quality_revision": "l4-quality/0.1.0-proposed",
        "lease_id": str(uuid4()),
        "stream_instance_id": str(uuid4()),
        "writer_key": str(uuid4()),
        "target": target,
    }


@pytest.mark.parametrize(
    "target",
    [
        {"target_bitrate_kbps": 8000},
        {"target_bitrate_kbps": 99},
        {"target_bitrate_kbps": 0, "target_fps": 0, "size": ""},
        {"size": "2048x1536"},
        {"size": "2304x1296"},
        {"preset_id": "5MP", "target_bitrate_kbps": 6000},
        {"target_fps": 29.97},
        {"codec_policy": "h265", "rate_control": "avbr", "gop_seconds": 1.5},
    ],
)
def test_candidate_does_not_inherit_desktop_policy_limits(target):
    body = request(target)
    parsed = CameraQualityCandidate(body)
    assert parsed.root == body
    assert parsed.provider_payload() == {
        key: value for key, value in body.items() if key != "lease_id"
    }


@pytest.mark.parametrize(
    "target",
    [
        {},
        {"target_bitrate_kbps": -1},
        {"target_bitrate_kbps": True},
        {"target_bitrate_kbps": "8000"},
        {"target_bitrate_kbps": 2**53},
        {"target_fps": None},
        {"size": "0x1080"},
        {"unknown_control": 1},
    ],
)
def test_candidate_rejects_invalid_fields_without_widening_current_routes(target):
    with pytest.raises(ValidationError):
        CameraQualityCandidate(request(target))


@pytest.mark.parametrize(
    "field", ["quality_revision", "writer_key", "stream_instance_id", "lease_id"]
)
def test_revision_and_existing_identity_proofs_are_required(field):
    body = request({"target_bitrate_kbps": 8000})
    del body[field]
    with pytest.raises(ValidationError):
        CameraQualityCandidate(body)


def test_unknown_revision_does_not_implicitly_select_camera():
    body = request({"target_bitrate_kbps": 8000})
    body["quality_revision"] = "l4-quality/unknown"
    with pytest.raises(ValidationError):
        CameraQualityCandidate(body)


@pytest.mark.parametrize("fps", [float("nan"), float("inf"), float("-inf")])
def test_non_json_numbers_are_rejected(fps):
    with pytest.raises(ValidationError):
        CameraQualityCandidate(request({"target_fps": fps}))


def test_lease_uuid_normalizes_like_existing_desktop_dto():
    body = request({"target_bitrate_kbps": 8000})
    body["lease_id"] = body["lease_id"].upper()
    assert CameraQualityCandidate(body).root["lease_id"] == body["lease_id"].lower()
