"""HEVC contract 2.0.0 validation and pre-START negotiation.

This layer does not acquire leases or authorize users. Its caller must verify
the device/tenant and the live publisher lease before preparing any media route.
"""

from __future__ import annotations

import contextlib
import copy
import json
import logging
import time
from pathlib import Path
from typing import Any, Literal
from uuid import UUID, uuid4, uuid5

from fastapi import HTTPException
from jsonschema import Draft202012Validator, FormatChecker
from jsonschema.exceptions import ValidationError
from pydantic import BaseModel, ConfigDict
from pydantic import ValidationError as ModelValidationError

from app.config import settings
from app.services.iot_client import IotPlatformClient
from app.services.media_orchestrator_client import (
    MediaOrchestratorClient,
    MediaOrchestratorError,
    media_lease_deadline,
)

logger = logging.getLogger(__name__)
CodecPolicy = Literal["auto", "h264", "h265"]
SCHEMA = json.loads(
    (Path(__file__).parent.parent / "contracts/hevc.schema.json").read_text(
        encoding="utf-8"
    )
)
VALIDATORS = {
    key: Draft202012Validator(
        dict(SCHEMA, oneOf=[{"$ref": f"#/$defs/{key}"}]), format_checker=FormatChecker()
    )
    for key in SCHEMA["$defs"]
}


class IotVideoState(BaseModel):
    """Authorized HTTP projection, distinct from the frozen MQTT READY event."""

    model_config = ConfigDict(extra="forbid")
    version: Literal[2]
    sn: str
    lease_id: UUID
    agent_instance_id: UUID
    operation_id: UUID
    command_id: UUID
    stream_instance_id: UUID
    confirmation: Literal["agent_ready"] | None = None
    result: str
    state: Literal["starting", "running", "stopping", "stopped", "failed"]
    resolved_codec: Literal["h264", "h265"]
    descriptor: dict[str, Any]
    actual_descriptor: dict[str, Any] | None = None
    error: dict[str, Any] | None = None
    reason: str | None = None


def validate_iot_state(value: dict[str, Any]) -> dict[str, Any]:
    try:
        result = IotVideoState.model_validate(value).model_dump(mode="json")
    except ModelValidationError as exc:
        raise HTTPException(502, detail={"code": "invalid_video_contract"}) from exc
    validate_video("descriptor", result["descriptor"])
    if result["actual_descriptor"] is not None:
        validate_video("descriptor", result["actual_descriptor"])
    if result["error"] is not None:
        validate_video("error_detail", result["error"])
    return result


async def load_video_status(
    iot: IotPlatformClient, sn: str, *, org_id: int, user: dict[str, Any]
) -> dict[str, Any]:
    """Enrich an authorized lease snapshot via IoT's full video-state reader."""
    status = await iot.remote_input_status(sn, org_id=org_id, user=user)
    lease = status.get("lease") or {}
    if (
        not settings.video_contract_v2_enabled
        or not lease.get("lease_id")
        or lease.get("scope") == "view"
    ):
        return status
    try:
        raw = await iot.remote_video_v2(
            sn, "state", lease_id=str(lease["lease_id"]), org_id=org_id, user=user
        )
    except HTTPException as exc:
        if exc.status_code == 404:
            return status  # Existing v1 lease has no v2 record.
        raise
    stream = validate_iot_state(raw)
    if stream["sn"] != sn or stream["lease_id"] != str(lease["lease_id"]):
        raise HTTPException(409, detail={"code": "lease_mismatch"})
    descriptor = stream["descriptor"]
    stream.update(
        mode=descriptor["source_type"],
        source_id=descriptor["source_id"],
        profile=descriptor["profile_id"],
    )
    return dict(status, agent=dict(status.get("agent") or {}, stream=stream))


def validate_video(definition: str, value: dict[str, Any]) -> dict[str, Any]:
    try:
        if len(json.dumps(value, ensure_ascii=False).encode()) > 32768:
            raise ValueError("oversize")
        VALIDATORS[definition].validate(value)
    except (ValidationError, ValueError, TypeError) as exc:
        # Provider payloads may contain PINs: never return/log the validation instance.
        raise HTTPException(
            502,
            detail={
                "code": "invalid_video_contract",
                "message": "Некорректный ответ видеосервиса",
            },
        ) from exc
    return value


def media_identity(
    sn: str, lease_id: str, stream_id: str, descriptor: dict[str, Any]
) -> dict[str, Any]:
    return {
        "contract_version": 2,
        "sn": sn,
        "lease_id": lease_id,
        "session_id": _media_session_id(stream_id),
        "stream_instance_id": stream_id,
        "descriptor_revision": descriptor["descriptor_revision"],
    }


def _media_session_id(stream_id: str) -> str:
    try:
        return str(uuid5(UUID(stream_id), "media"))
    except ValueError as exc:
        raise HTTPException(502, detail={"code": "invalid_video_contract"}) from exc


def verify_media_response(
    media: dict[str, Any], identity: dict[str, Any], descriptor: dict[str, Any]
) -> None:
    if (
        any(
            media.get(key) != identity[key]
            for key in ("session_id", "lease_id", "stream_instance_id")
        )
        or media.get("publisher", {}).get("sn") != identity["sn"]
        or media.get("publisher", {}).get("stream_instance_id")
        != identity["stream_instance_id"]
        or media.get("descriptor") != descriptor
    ):
        raise HTTPException(409, detail={"code": "descriptor_mismatch"})


def stream_descriptor(status: dict[str, Any]) -> dict[str, Any] | None:
    stream = (status.get("agent") or {}).get("stream") or {}
    descriptor = stream.get("actual_descriptor") or stream.get("descriptor")
    if not isinstance(descriptor, dict):
        return None
    return validate_video("descriptor", descriptor)


def make_stop_command(
    sn: str, lease_id: str, status: dict[str, Any]
) -> dict[str, Any] | None:
    descriptor = stream_descriptor(status)
    if descriptor is None:
        return None
    agent = status.get("agent") or {}
    stream = agent.get("stream") or {}
    now = int(time.time() * 1000)
    return validate_video(
        "stop",
        {
            "v": 2,
            "type": "stream_stop",
            "sn": sn,
            "lease_id": lease_id,
            "stream_instance_id": str(stream.get("stream_instance_id") or ""),
            "agent_instance_id": str(
                stream.get("agent_instance_id") or agent.get("agent_instance_id") or ""
            ),
            "command_id": str(uuid4()),
            "issued_at_ms": now,
            "expires_at_ms": now + 5000,
            "descriptor_revision": descriptor["descriptor_revision"],
        },
    )


def verify_publisher_lease(
    status: dict[str, Any], lease_id: str, user: dict[str, Any]
) -> int:
    lease = status.get("lease") or {}
    owners = {str(user[key]) for key in ("sub", "user_id", "id") if user.get(key)}
    if not lease.get("active") or str(lease.get("lease_id") or "") != lease_id:
        raise HTTPException(409, detail={"code": "lease_invalid"})
    if str(lease.get("owner_user_id") or "") not in owners or str(
        lease.get("owner_session_id") or ""
    ) != str(user.get("session_id") or ""):
        raise HTTPException(403, detail={"code": "lease_owner_mismatch"})
    if not user.get("session_id"):
        raise HTTPException(403, detail={"code": "lease_owner_mismatch"})
    if lease.get("scope") not in ("stream", "input"):
        raise HTTPException(403, detail={"code": "stream_scope_required"})
    try:
        return media_lease_deadline(lease.get("expires_at"))
    except MediaOrchestratorError as exc:
        raise HTTPException(410, detail={"code": "expired"}) from exc


def select_hevc_profile(
    capability: dict[str, Any] | None,
    server: dict[str, Any] | None,
    source_id: str,
    profile_id: str,
    now_ms: int,
    agent_id: str | None = None,
    allow_test_camera: bool = False,
) -> tuple[dict[str, Any] | None, str]:
    if not capability:
        return None, "capability_unavailable"
    validate_video("capabilities", capability)
    if agent_id and capability["agent_instance_id"] != agent_id:
        return None, "capability_mismatch"
    if capability["source_id"] != source_id:
        return None, "source_mismatch"
    checked, until = capability["checked_at_ms"], capability["valid_until_ms"]
    if not checked <= now_ms < until or not 0 < until - checked <= 60000:
        return None, "stale_capability"
    if not server:
        return None, "server_unavailable"
    validate_video("media_capabilities", server)
    if (
        not server["dependencies_ready"]
        or not server["hevc_runtime_verified"]
        or "h265" not in server["codecs"]
    ):
        return None, "server_hevc_unavailable"
    unavailable_reason = None
    for entry in capability["video"]:
        if entry["codec"] == "h265" and entry["status"] != "ready":
            unavailable_reason = entry["reason_code"] or "capability_unavailable"
        if (
            entry["codec"] == "h265"
            and (
                (entry["backend"] == "media_foundation" and entry["hardware"])
                or (
                    allow_test_camera
                    and entry["backend"] == "camera"
                    and source_id.startswith("test-")
                )
            )
            and entry["status"] == "ready"
            and entry["probe_evidence"] == "encoded_sequence"
        ):
            for profile in entry["profiles"]:
                if profile["profile_id"] == profile_id:
                    return copy.deepcopy(profile["track"]), "ready"
    return None, unavailable_reason or "profile_unsupported"


class VideoContractOrchestrator:
    def __init__(self, iot: IotPlatformClient, media: MediaOrchestratorClient) -> None:
        self.iot = iot
        self.media = media

    async def availability(
        self,
        sn: str,
        source_id: str,
        profile_id: str,
        org_id: int,
        user: dict[str, Any],
    ) -> dict[str, Any]:
        reason = "disabled_by_policy"
        if (
            not settings.video_contract_v2_enabled
            or not settings.l4desk_session_orchestration_enabled
        ):
            return {"hevc_available": False, "reason_code": reason}
        if not (
            0 < settings.video_proxy_rtp_port <= 65535
            and 0 < settings.video_proxy_rtcp_port <= 65535
        ):
            return {"hevc_available": False, "reason_code": "transport_not_configured"}
        try:
            inventory = await self.iot.remote_video_v2(
                sn, "inventory", org_id=org_id, user=user
            )
            if (
                set(inventory) != {"negotiated_v2", "video_capabilities"}
                or inventory["negotiated_v2"] is not True
            ):
                return {"hevc_available": False, "reason_code": "capability_mismatch"}
            capability = inventory["video_capabilities"]
            server = await self.media.video_v2("capabilities")
            track, reason = select_hevc_profile(
                capability,
                server,
                source_id,
                profile_id,
                int(time.time() * 1000),
                allow_test_camera=sn in settings.video_test_camera_sns,
            )
            return {
                "hevc_available": track is not None,
                "reason_code": reason,
                "capability": capability,
                "track": track,
            }
        except HTTPException, MediaOrchestratorError:
            return {"hevc_available": False, "reason_code": "capability_unavailable"}

    async def start(
        self,
        *,
        sn: str,
        lease_id: str,
        policy: CodecPolicy,
        mode: str,
        source_id: str,
        profile_id: str,
        status: dict[str, Any],
        org_id: int,
        user: dict[str, Any],
    ) -> dict[str, Any] | None:
        if not (status.get("lease") or {}).get("owner_session_id"):
            lease = await self.iot.remote_video_v2(
                sn, "lease", lease_id=lease_id, org_id=org_id, user=user
            )
            if lease.get("sn") != sn or str(lease.get("lease_id")) != lease_id:
                raise HTTPException(409, detail={"code": "lease_mismatch"})
            status = dict(status, lease=lease)
        deadline = verify_publisher_lease(status, lease_id, user)
        stream = (status.get("agent") or {}).get("stream") or {}
        if stream.get("state") in ("running", "starting", "restarting"):
            existing = stream_descriptor(status)
            if existing:
                actual = existing["tracks"][0]["codec"]
                if (
                    existing["source_id"] != source_id
                    or existing["profile_id"] != profile_id
                    or existing["source_type"] != mode
                    or policy not in ("auto", actual)
                ):
                    raise HTTPException(409, detail={"code": "conflict"})
                stream_id = str(stream.get("stream_instance_id") or "")
                identity = media_identity(sn, lease_id, stream_id, existing)
                media = validate_video(
                    "media_health", await self.media.video_v2("health", identity)
                )
                verify_media_response(media, identity, existing)
                if (
                    media["state"] not in ("starting", "active")
                    or media["descriptor"] != existing
                ):
                    raise HTTPException(409, detail={"code": "producer_stopped"})
                result = self._result(
                    stream_id, actual, existing, media, "already_running"
                )
                result["state"] = stream["state"]
                return result
            if any(
                stream.get(key) != expected
                for key, expected in (
                    ("source_id", source_id),
                    ("profile", profile_id),
                    ("mode", mode),
                )
            ):
                raise HTTPException(409, detail={"code": "conflict"})
            if policy == "h265":
                raise HTTPException(409, detail={"code": "conflict"})
            return None
        if policy == "h264":
            return None
        available = await self.availability(sn, source_id, profile_id, org_id, user)
        if not available["hevc_available"]:
            if policy == "h265":
                raise HTTPException(
                    422,
                    detail={
                        "code": available["reason_code"],
                        "message": "H.265 недоступен для выбранного источника",
                    },
                )
            return None
        capability = available["capability"]
        agent = status.get("agent") or {}
        if (
            not agent.get("online")
            or agent.get("stale")
            or (
                agent.get("agent_instance_id")
                and agent["agent_instance_id"] != capability["agent_instance_id"]
            )
        ):
            raise HTTPException(409, detail={"code": "capability_mismatch"})
        stream_id = str(uuid4())
        operation_id = str(uuid5(UUID(stream_id), "prepare"))
        descriptor = validate_video(
            "descriptor",
            {
                "descriptor_revision": str(uuid4()),
                "source_type": mode,
                "source_id": source_id,
                "geometry_generation": capability["binding"]["geometry_generation"],
                "profile_id": profile_id,
                "tracks": [available["track"]],
            },
        )
        identity = media_identity(sn, lease_id, stream_id, descriptor)
        request = validate_video(
            "media_start",
            {
                "contract_version": 2,
                "session_id": identity["session_id"],
                "operation_id": operation_id,
                "publisher": {"sn": sn, "stream_instance_id": stream_id},
                "lease_id": lease_id,
                "lease_expires_at": deadline,
                "descriptor": descriptor,
            },
        )
        now = int(time.time() * 1000)
        command = validate_video(
            "start",
            {
                "v": 2,
                "type": "stream_start",
                "command_id": str(uuid4()),
                "sn": sn,
                "lease_id": lease_id,
                "stream_instance_id": stream_id,
                "operation_id": operation_id,
                "agent_instance_id": capability["agent_instance_id"],
                "capability_revision": capability["capability_revision"],
                "issued_at_ms": now,
                "expires_at_ms": min(now + 10000, deadline * 1000),
                "lease_expires_at_ms": deadline * 1000,
                "codec_policy": policy,
                "resolved_codec": "h265",
                "descriptor": descriptor,
                "delivery": {
                    "host": "localhost",
                    "rtp_port": settings.video_proxy_rtp_port,
                    "rtcp_port": settings.video_proxy_rtcp_port,
                },
                "recovery_policy": "stop_on_failure",
            },
        )
        producer_attempted = False
        try:
            media = validate_video(
                "media_response", await self.media.video_v2("start", request)
            )
            verify_media_response(media, identity, descriptor)
            if (
                media["operation_id"] != operation_id
                or media["session_id"] != identity["session_id"]
                or media["stream_instance_id"] != stream_id
                or media["descriptor"] != descriptor
                or media["lease_id"] != lease_id
                or media["publisher"]["sn"] != sn
                or not media.get("pin")
                or media["state"] not in ("starting", "active")
            ):
                raise HTTPException(409, detail={"code": "descriptor_mismatch"})
            producer_attempted = True
            ready = validate_iot_state(
                await self.iot.remote_video_v2(
                    sn,
                    "start",
                    payload=command,
                    lease_id=lease_id,
                    org_id=org_id,
                    user=user,
                )
            )
            if ready.get("state") == "failed":
                if any(
                    ready[key] != command[key]
                    for key in (
                        "sn",
                        "lease_id",
                        "stream_instance_id",
                        "agent_instance_id",
                    )
                ):
                    raise HTTPException(409, detail={"code": "descriptor_mismatch"})
                raise HTTPException(
                    502, detail={"code": ready["reason"], "error": ready["error"]}
                )
            if (
                ready["state"] != "running"
                or ready["confirmation"] != "agent_ready"
                or ready["resolved_codec"] != "h265"
                or ready["descriptor"] != descriptor
                or any(
                    ready[key] != command[key]
                    for key in (
                        "sn",
                        "command_id",
                        "operation_id",
                        "lease_id",
                        "stream_instance_id",
                        "agent_instance_id",
                    )
                )
                or ready["actual_descriptor"] != descriptor
            ):
                raise HTTPException(409, detail={"code": "descriptor_mismatch"})
            return self._result(stream_id, "h265", descriptor, media, "started")
        except HTTPException, MediaOrchestratorError:
            if producer_attempted:
                stop = {
                    key: command[key]
                    for key in (
                        "v",
                        "sn",
                        "lease_id",
                        "stream_instance_id",
                        "agent_instance_id",
                    )
                }
                stop.update(
                    type="stream_stop",
                    command_id=str(uuid4()),
                    issued_at_ms=int(time.time() * 1000),
                    expires_at_ms=min(int(time.time() * 1000) + 5000, deadline * 1000),
                    descriptor_revision=descriptor["descriptor_revision"],
                )
                with contextlib.suppress(HTTPException):
                    await self.iot.remote_video_v2(
                        sn,
                        "stop",
                        payload=stop,
                        lease_id=lease_id,
                        org_id=org_id,
                        user=user,
                    )
            with contextlib.suppress(MediaOrchestratorError):
                await self.media.video_v2(
                    "stop", dict(identity, reason="partial_start_failed")
                )
            raise

    @staticmethod
    def _result(
        stream_id: str,
        codec: str,
        descriptor: dict[str, Any],
        media: dict[str, Any],
        result: str,
    ) -> dict[str, Any]:
        return {
            "stream_instance_id": stream_id,
            "result": result,
            "state": "running",
            "resolved_codec": codec,
            "descriptor": descriptor,
            "media": media,
        }
