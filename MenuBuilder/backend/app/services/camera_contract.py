"""Accepted camera wire validation; errors never include provider/user payloads."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, TypedDict
from uuid import UUID

from fastapi import HTTPException
from jsonschema import Draft202012Validator, FormatChecker

CAMERA_REVISION = "siplite-camera/1.0.0"
CAMERA11_REVISION = "siplite-camera/1.1.0"
CADENCE_REVISION = "camera-cadence/0.1.0-proposed"
CAMERA11_HEADERS = {
    "X-L4-Camera-Contract": CAMERA11_REVISION,
    "X-L4-Camera-Cadence": CADENCE_REVISION,
    "X-L4-Camera-Enrollment": "camera-enrollment/0.2.0-proposed",
    "X-L4-Camera-Admission": "camera-ctl-monotonic/0.2.0-proposed",
}


def camera_headers(selection: bool | str) -> dict[str, str]:
    if selection == CAMERA11_REVISION:
        return dict(CAMERA11_HEADERS)
    if selection is True or selection == CAMERA_REVISION:
        return {"X-L4-Camera-Contract": CAMERA_REVISION}
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


MEDIA11_SCHEMA = json.loads(
    (CONTRACTS / "camera-media-v1.1.schema.json").read_text(encoding="utf-8")
)
MEDIA11_VALIDATORS = {
    key: Draft202012Validator(
        dict(MEDIA11_SCHEMA, oneOf=[{"$ref": f"#/$defs/{key}"}]),
        format_checker=FormatChecker(),
    )
    for key in MEDIA11_SCHEMA["$defs"]
}


def camera11_selected(value: dict[str, Any]) -> bool:
    descriptor = value.get("descriptor", value)
    return (
        isinstance(descriptor, dict)
        and descriptor.get("camera_contract_revision") == CAMERA11_REVISION
        or CAMERA11_REVISION in value.get("camera_contract_revisions", [])
    )


def extended_camera(value: dict[str, Any]) -> bool:
    descriptor = value.get("descriptor", value)
    return isinstance(descriptor, dict) and "camera_contract_revision" in descriptor


class MediaContractOptions(TypedDict, total=False):
    camera_contract: bool | str


def media_contract_options(descriptor: dict[str, Any]) -> MediaContractOptions:
    return (
        {
            "camera_contract": CAMERA11_REVISION
            if camera11_selected(descriptor)
            else True
        }
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
