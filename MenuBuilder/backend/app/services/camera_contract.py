"""Accepted camera wire validation; errors never include provider/user payloads."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, TypedDict
from uuid import UUID

from fastapi import HTTPException
from jsonschema import Draft202012Validator, FormatChecker
from referencing import Registry, Resource

CAMERA_REVISION = "siplite-camera/1.0.0"
CAMERA_MEDIA_REVISION = "siplite-camera/1.4.0"
CADENCE_REVISION = "camera-cadence/0.3.0-proposed"
MEDIA_REVISION = "l4-media/3.0.0"
CAMERA_MEDIA_HEADERS = {
    "X-L4-Media-Contract": MEDIA_REVISION,
    "X-L4-Camera-Contract": CAMERA_MEDIA_REVISION,
    "X-L4-Camera-Cadence": CADENCE_REVISION,
    "X-L4-Camera-Enrollment": "camera-enrollment/0.5.0-proposed",
    "X-L4-Camera-Admission": "camera-ctl-monotonic/0.2.0-proposed",
}
CAMERA_SESSION_PREFIX = "op-stream-v2-camera14-"
PREVIOUS_CAMERA_REVISION = "siplite-camera/1.3.0"
PREVIOUS_CAMERA_HEADERS = {
    **CAMERA_MEDIA_HEADERS,
    "X-L4-Camera-Contract": PREVIOUS_CAMERA_REVISION,
    "X-L4-Camera-Cadence": "camera-cadence/0.2.0-proposed",
    "X-L4-Camera-Enrollment": "camera-enrollment/0.4.0-proposed",
}
CAMERA_SESSION_REVISIONS = {
    CAMERA_SESSION_PREFIX: CAMERA_MEDIA_REVISION,
    "op-stream-v2-camera13-": PREVIOUS_CAMERA_REVISION,
}


class CameraStateOptions(TypedDict, total=False):
    camera_contract: str
    expected_stream_instance_id: str


def camera_result_state_options(value: dict[str, Any]) -> CameraStateOptions:
    """Context from the validated native START result, never request mode alone."""
    descriptor = value.get("descriptor") or {}
    stream = value.get("stream_instance_id")
    if (
        value.get("version") == 2
        and value.get("contract_version") == 3
        and value.get("camera_contract_revision") == CAMERA_MEDIA_REVISION
        and isinstance(descriptor, dict)
        and descriptor.get("source_type") == "camera"
        and descriptor.get("source_id") == "cam:ip000001"
        and isinstance(stream, str)
        and stream
    ):
        return {
            "camera_contract": CAMERA_MEDIA_REVISION,
            "expected_stream_instance_id": stream,
        }
    return {}


def camera_session_operation_id(stream: str) -> str:
    return CAMERA_SESSION_PREFIX + stream


def camera_session_state_options(session: Any) -> CameraStateOptions:
    """Read the server-owned session marker, retaining its exact provider epoch."""
    from app.models_l4desk import L4DeskRemoteSession

    if not isinstance(session, L4DeskRemoteSession) or session.session_type != "video":
        return {}
    operation = str(session.operation_id or "")
    if not operation.startswith("op-stream-v2-camera"):
        return {}
    stream = session.provider_session_id
    revision = next(
        (
            revision
            for prefix, revision in CAMERA_SESSION_REVISIONS.items()
            if stream and operation == prefix + stream
        ),
        None,
    )
    if revision is None or not stream:
        raise HTTPException(409, detail={"code": "camera_session_context_changed"})
    return {"camera_contract": revision, "expected_stream_instance_id": stream}


def camera_headers(selection: bool | str) -> dict[str, str]:
    if selection is True or selection == CAMERA_MEDIA_REVISION:
        return dict(CAMERA_MEDIA_HEADERS)
    if selection == PREVIOUS_CAMERA_REVISION:
        return dict(PREVIOUS_CAMERA_HEADERS)
    if not selection:
        return {}
    raise HTTPException(422, detail={"code": "camera_contract_not_negotiated"})


CONTRACTS = Path(__file__).parents[1] / "contracts"
RPC_SCHEMA = json.loads(
    (CONTRACTS / "camera-rpc.schema.json").read_text(encoding="utf-8")
)
MEDIA_SCHEMA = json.loads(
    (CONTRACTS / "camera-media.schema.json").read_text(encoding="utf-8")
)
RPC_VALIDATORS = {
    code: Draft202012Validator(
        dict(RPC_SCHEMA, oneOf=[{"$ref": f"#/$defs/rpc{code}"}]),
        format_checker=FormatChecker(),
    )
    for code in range(160, 168)
}
MEDIA_VALIDATORS = {
    key: Draft202012Validator(
        dict(MEDIA_SCHEMA, oneOf=[{"$ref": f"#/$defs/{key}"}]),
        format_checker=FormatChecker(),
    )
    for key in MEDIA_SCHEMA["$defs"]
}


OBSERVATION_SCHEMA = json.loads(
    (CONTRACTS / "camera-v1.4.schema.json").read_text(encoding="utf-8")
)
COMMON_MEDIA_SCHEMA = json.loads(
    (CONTRACTS / "media-v3.schema.json").read_text(encoding="utf-8")
)
OBSERVATION_REGISTRY = Registry().with_resource(
    "urn:leo4:l4-media:3.0.0", Resource.from_contents(COMMON_MEDIA_SCHEMA)
)
OBSERVATION_VALIDATORS = {
    key: Draft202012Validator(
        dict(OBSERVATION_SCHEMA, oneOf=[{"$ref": f"#/$defs/{key}"}]),
        format_checker=FormatChecker(),
        registry=OBSERVATION_REGISTRY,
    )
    for key in OBSERVATION_SCHEMA["$defs"]
}


def extended_camera(value: dict[str, Any]) -> bool:
    descriptor = value.get("descriptor", value)
    return isinstance(descriptor, dict) and (
        "camera_contract_revision" in descriptor
        or descriptor.get("contract_version") == 3
        and descriptor.get("source_type") == "camera"
    )


class MediaContractOptions(TypedDict, total=False):
    camera_contract: bool | str


def media_contract_options(
    descriptor: dict[str, Any], *, camera_contract: str | None = None
) -> MediaContractOptions:
    return (
        {"camera_contract": camera_contract or CAMERA_MEDIA_REVISION}
        if extended_camera(descriptor)
        else {}
    )


def _bounded(value: Any, depth: int = 0) -> bool:
    if depth > 12:
        return False
    if isinstance(value, dict):
        return all(
            isinstance(key, str) and _bounded(item, depth + 1)
            for key, item in value.items()
        )
    if isinstance(value, list):
        return all(_bounded(item, depth + 1) for item in value)
    return value is None or type(value) in (str, int, bool, float)


def validate_camera_rpc(
    code: int, payload: dict[str, Any], operation_id: str
) -> dict[str, Any]:
    """Shape, byte/depth and task identity checks, prior to any upstream dispatch."""
    valid = False
    try:
        valid = (
            code in RPC_VALIDATORS
            and _bounded(payload)
            and len(
                json.dumps(payload, ensure_ascii=False, allow_nan=False).encode("utf-8")
            )
            <= 8192
            and RPC_VALIDATORS[code].is_valid(payload)
        )
        if valid:
            operation_id = str(UUID(operation_id))
            item = payload["dt"][0]
            valid = item.get("operation_id", operation_id) == operation_id
            if code == 165 and item.get("action") == "cancel":
                valid = valid and item["target_operation_id"] != operation_id
            envelope = item.get("sealed_credentials")
            if envelope is not None:
                valid = valid and envelope["operation_id"] == operation_id
    except ValueError, TypeError, KeyError, UnicodeError, OverflowError:
        valid = False
    if not valid:
        raise HTTPException(422, detail={"code": "invalid_camera_command"})
    return payload
