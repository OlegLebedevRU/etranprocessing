"""Offline candidate parser, deliberately not registered as a live handler."""

from __future__ import annotations

import hashlib
import json
from functools import lru_cache
from pathlib import Path
from typing import Any
from uuid import UUID

from jsonschema import Draft202012Validator, FormatChecker
from pydantic import RootModel, model_validator

SCHEMA_SHA256 = "4c80fcc16f5e74ab4ebd954e0598e162d23e3ce5bead29a49c81fc6dca66e4c0"


@lru_cache(maxsize=1)
def request_validator() -> Draft202012Validator:
    raw = (
        Path(__file__).parents[1] / "contracts/runtime-quality-proposal.schema.json"
    ).read_bytes()
    if hashlib.sha256(raw).hexdigest() != SCHEMA_SHA256:
        raise ValueError("camera_quality_schema_pin_mismatch")
    schema = json.loads(raw)
    return Draft202012Validator(
        dict(schema, oneOf=[{"$ref": "#/$defs/QualityRequest"}]),
        format_checker=FormatChecker(),
    )


class CameraQualityCandidate(RootModel[dict[str, Any]]):
    """Existing BFF lease_id wrapper around the canonical candidate request.

    Not added to the current route union until the source adapter and complete
    negotiated camera extension are ready. Parsing grants no writer authority.
    """

    @model_validator(mode="before")
    @classmethod
    def validate_body(cls, value: Any) -> dict[str, Any]:
        if not isinstance(value, dict):
            # Pydantic converts ValueError to a validation failure; TypeError escapes.
            raise ValueError("camera_quality_request_invalid")  # noqa: TRY004
        try:
            json.dumps(value, allow_nan=False)
        except ValueError, TypeError:
            raise ValueError("camera_quality_request_invalid") from None
        parsed = dict(value)
        lease_id = parsed.pop("lease_id", None)
        try:
            if not isinstance(lease_id, str):
                raise TypeError("camera_quality_lease_invalid")
            lease_id = str(UUID(lease_id))
        except ValueError, AttributeError, TypeError:
            raise ValueError("camera_quality_lease_invalid") from None
        if not request_validator().is_valid(parsed):
            # Never copy writer credentials or user-provided content to errors.
            raise ValueError("camera_quality_request_invalid")
        return dict(parsed, lease_id=lease_id)

    def provider_payload(self) -> dict[str, Any]:
        return {key: value for key, value in self.root.items() if key != "lease_id"}
