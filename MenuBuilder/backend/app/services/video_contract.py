"""Unified media 3.0.0 validation and pre-START negotiation.

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
from app.services.camera_contract import (
    extended_camera,
    media_contract_options,
)
from app.services.camera_video import CameraVideoState
from app.services.iot_client import IotPlatformClient
from app.services.media_orchestrator_client import (
    MediaOrchestratorClient,
    MediaOrchestratorError,
    media_lease_deadline,
)

logger = logging.getLogger(__name__)
CodecPolicy = Literal["auto", "h264", "h265"]


def verify_browser_codec(
    browser_codecs: dict[str, Any] | None,
    codec: str,
    *,
    observed_at_ms: int | None = None,
    legacy: bool = False,
) -> None:
    if browser_codecs is None:
        return
    from app.services.camera_video import BrowserCodecs

    try:
        browser = BrowserCodecs.model_validate(browser_codecs)
    except ModelValidationError as exc:
        raise HTTPException(
            422, detail={"code": "browser_capabilities_invalid"}
        ) from exc
    # A browser report is a negotiation hint, not a lease or authorization token.
    # Legacy H264 still negotiates SDP when the capability API is unavailable.
    if legacy:
        if browser.h264 is False:
            raise HTTPException(422, detail={"code": "browser_codec_unsupported"})
        return
    now_ms = observed_at_ms if observed_at_ms is not None else int(time.time() * 1000)
    if not (
        browser.checked_at_ms - 5000 <= now_ms < browser.valid_until_ms
        and 0 < browser.valid_until_ms - browser.checked_at_ms <= 60000
        and getattr(browser, codec) is True
    ):
        raise HTTPException(422, detail={"code": "browser_codec_unsupported"})


SCHEMA = json.loads(
    (Path(__file__).parent.parent / "contracts/media-v3.schema.json").read_text(
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
    contract_version: Literal[3]
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
        model = (
            CameraVideoState if "camera_contract_revision" in value else IotVideoState
        )
        result = model.model_validate(value).model_dump(mode="json")
    except ModelValidationError as exc:
        raise HTTPException(502, detail={"code": "invalid_video_contract"}) from exc
    validate_video("descriptor", result["descriptor"])
    if (
        "camera_contract_revision" in result
        and result["descriptor"]["source_type"] != "camera"
    ):
        raise HTTPException(502, detail={"code": "invalid_video_contract"})
    if result["actual_descriptor"] is not None:
        validate_video("descriptor", result["actual_descriptor"])
        if result["actual_descriptor"] != result["descriptor"]:
            raise HTTPException(409, detail={"code": "descriptor_mismatch"})
    if result["resolved_codec"] != result["descriptor"]["tracks"][0]["codec"]:
        raise HTTPException(409, detail={"code": "descriptor_mismatch"})
    if result.get("error") is not None:
        validate_video("error_detail", result["error"])
    return result


async def load_video_status(
    iot: IotPlatformClient,
    sn: str,
    *,
    org_id: int,
    user: dict[str, Any],
    camera_contract: str | None = None,
    expected_stream_instance_id: str | None = None,
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
    state_options: dict[str, Any] = {}
    if camera_contract:
        state_options["camera_contract"] = camera_contract
    try:
        raw = await iot.remote_video_v2(
            sn,
            "state",
            lease_id=str(lease["lease_id"]),
            org_id=org_id,
            user=user,
            **state_options,
        )
    except HTTPException as exc:
        if exc.status_code == 404:
            return status  # Existing v1 lease has no v2 record.
        raise
    stream = validate_iot_state(raw)
    if stream["sn"] != sn or stream["lease_id"] != str(lease["lease_id"]):
        raise HTTPException(409, detail={"code": "lease_mismatch"})
    if expected_stream_instance_id and (
        stream["stream_instance_id"] != expected_stream_instance_id
    ):
        raise HTTPException(409, detail={"code": "stream_mismatch"})
    if camera_contract and stream.get("camera_contract_revision") != camera_contract:
        raise HTTPException(409, detail={"code": "camera_contract_not_negotiated"})
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
        if (
            definition == "capabilities"
            and "measured_at_ms" in value
            and value["measured_at_ms"] > value["checked_at_ms"]
        ):
            raise ValueError("measurement cannot follow applicability check")
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
        "contract_version": 3,
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
    if extended_camera(descriptor):
        return dict(
            media_identity(
                sn, lease_id, str(stream.get("stream_instance_id") or ""), descriptor
            ),
            reason="user_stop",
        )
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
    codec: str = "h265",
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
        or (codec == "h265" and not server["hevc_runtime_verified"])
        or codec not in server["codecs"]
    ):
        return None, "server_hevc_unavailable"
    unavailable_reason = None
    for entry in capability["video"]:
        if entry["codec"] == codec and entry["status"] != "ready":
            unavailable_reason = entry["reason_code"] or "capability_unavailable"
        if (
            entry["codec"] == codec
            and (
                (entry["backend"] == "media_foundation" and entry["hardware"])
                or (codec == "h264" and entry["backend"] in ("openh264", "ffmpeg"))
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
                    track = profile["track"]
                    if any(
                        all(
                            track[key] == fmt[key]
                            for key in (
                                "codec",
                                "codec_profile",
                                "packetization",
                                "clock_rate",
                            )
                        )
                        for fmt in server["formats"]
                    ):
                        return copy.deepcopy(track), "ready"
                    return None, "server_format_unsupported"
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
        *,
        fresh: bool = False,
        codec: str = "h265",
    ) -> dict[str, Any]:
        reason = "disabled_by_policy"
        if (
            not settings.video_contract_v2_enabled
            or not settings.l4desk_session_orchestration_enabled
        ):
            return {"hevc_available": False, "reason_code": reason}
        try:
            inventory = await self.iot.remote_video_v2(
                sn,
                "inventory",
                org_id=org_id,
                user=user,
                source_id=source_id if source_id.startswith("disp:") else None,
                profile_id=profile_id if source_id.startswith("disp:") else None,
                refresh=fresh and source_id.startswith("disp:"),
            )
            if (
                set(inventory)
                != {"contract_version", "negotiated_v2", "video_capabilities"}
                or inventory["contract_version"] != 3
                or not isinstance(inventory["negotiated_v2"], bool)
            ):
                return {"hevc_available": False, "reason_code": "capability_mismatch"}
            if not inventory["negotiated_v2"]:
                return {
                    "hevc_available": False,
                    "reason_code": "negotiation_unavailable"
                    if inventory["video_capabilities"] is None
                    else "capability_mismatch",
                }
            capability = inventory["video_capabilities"]
            server = await self.media.video_v2("capabilities")
            track, reason = select_hevc_profile(
                capability,
                server,
                source_id,
                profile_id,
                int(time.time() * 1000),
                allow_test_camera=sn in settings.video_test_camera_sns,
                codec=codec,
            )
            return {
                "hevc_available": track is not None,
                "reason_code": reason,
                "capability": capability,
                "track": track,
            }
        except HTTPException as exc:
            code = exc.detail.get("code") if isinstance(exc.detail, dict) else None
            return {
                "hevc_available": False,
                "reason_code": code or "capability_unavailable",
            }
        except MediaOrchestratorError:
            return {"hevc_available": False, "reason_code": "server_unavailable"}

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
        browser_codecs: dict[str, Any] | None = None,
        quality_writer_key: str | None = None,
    ) -> dict[str, Any] | None:
        observed_at_ms = int(time.time() * 1000)
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
                    "media_health",
                    await self.media.video_v2(
                        "health", identity, **media_contract_options(existing)
                    ),
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
        agent = status.get("agent") or {}
        if not agent.get("online") or agent.get("stale"):
            raise HTTPException(409, detail={"code": "offline"})
        protocols = agent.get("capabilities")
        if protocols is not None and (
            not isinstance(protocols, list)
            or any(not isinstance(item, str) for item in protocols)
        ):
            raise HTTPException(502, detail={"code": "invalid_video_contract"})
        # Presence is authoritative about advertised protocols. Absence of the
        # opt-in contract is normal for agents predating HEVC, not a v3 error.
        if "video_contract_v2" not in (protocols or []):
            if policy == "h265":
                raise HTTPException(422, detail={"code": "legacy_h264_only"})
            verify_browser_codec(browser_codecs, "h264", legacy=True)
            return None
        requested_codec = (
            "h264"
            if policy == "h264"
            or (
                policy == "auto"
                and browser_codecs is not None
                and browser_codecs.get("h265") is not True
            )
            else "h265"
        )
        # Explicit H264 selection can use CTL v1 without enabling HEVC policy.
        # Route preparation and app1 still authorize/serialize the legacy START.
        if requested_codec == "h264" and (
            not settings.video_contract_v2_enabled
            or not settings.l4desk_session_orchestration_enabled
        ):
            verify_browser_codec(browser_codecs, "h264", legacy=True)
            return None
        available = await self.availability(
            sn,
            source_id,
            profile_id,
            org_id,
            user,
            codec=requested_codec,
        )
        if (
            requested_codec == "h264"
            and available["reason_code"] == "negotiation_unavailable"
        ):
            # This is a deliberate H264 request, not an inference that a false
            # negotiated_v2 proves an old agent. Never use it for explicit HEVC.
            verify_browser_codec(browser_codecs, "h264", legacy=True)
            return None
        if policy == "auto" and available["reason_code"] in (
            "no_hardware_mft",
            "profile_unsupported",
            "os_api_unavailable",
            "server_hevc_unavailable",
        ):
            requested_codec = "h264"
            available = await self.availability(
                sn, source_id, profile_id, org_id, user, codec="h264"
            )
        # Native 1.13.54 advertises only HEVC through v3. Its existing H264
        # adapter remains usable. Prefer a real v3 H264 track when advertised;
        # never mask a stale/mismatched capability or a declared encoder failure.
        if (
            agent.get("agent_instance_id")
            and available.get("capability")
            and available["capability"]["agent_instance_id"]
            != agent["agent_instance_id"]
        ):
            raise HTTPException(409, detail={"code": "capability_mismatch"})
        if (
            requested_codec == "h264"
            and available["reason_code"] == "profile_unsupported"
            and available.get("capability")
            and not any(
                entry["codec"] == "h264" for entry in available["capability"]["video"]
            )
        ):
            verify_browser_codec(browser_codecs, "h264", legacy=True)
            return None
        if (
            source_id.startswith("disp:")
            and available.get("hevc_available")
            and (available.get("capability") or {}).get("admission_clock")
            == "issuer_monotonic_v1"
        ):
            available = await self.availability(
                sn,
                source_id,
                profile_id,
                org_id,
                user,
                fresh=True,
                codec=available["track"]["codec"],
            )
        if not available["hevc_available"]:
            raise HTTPException(
                422,
                detail={
                    "code": available["reason_code"],
                    "message": "Не удалось согласовать видеопоток с терминалом",
                },
            )
        resolved_codec = available["track"]["codec"]
        verify_browser_codec(
            browser_codecs, resolved_codec, observed_at_ms=observed_at_ms
        )
        if not (
            0 < settings.video_proxy_rtp_port <= 65535
            and 0 < settings.video_proxy_rtcp_port <= 65535
        ):
            raise HTTPException(503, detail={"code": "transport_not_configured"})
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
                "contract_version": 3,
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
                "contract_version": 3,
                "session_id": identity["session_id"],
                "operation_id": operation_id,
                "publisher": {"sn": sn, "stream_instance_id": stream_id},
                "lease_id": lease_id,
                "lease_expires_at": deadline,
                "descriptor": descriptor,
            },
        )
        now = int(time.time() * 1000)
        start_budget_ms = 35000 if mode == "desktop" else 10000
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
                "expires_at_ms": min(now + start_budget_ms, deadline * 1000),
                "lease_expires_at_ms": deadline * 1000,
                "codec_policy": policy,
                "resolved_codec": resolved_codec,
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
                "media_response",
                await self.media.video_v2(
                    "start", request, **media_contract_options(descriptor)
                ),
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
            # Media preparation must not consume the producer START budget.
            now = int(time.time() * 1000)
            if deadline * 1000 <= now:
                raise HTTPException(410, detail={"code": "expired"})
            command["issued_at_ms"] = now
            command["expires_at_ms"] = min(now + start_budget_ms, deadline * 1000)
            producer_attempted = True
            writer_options: dict[str, Any] = (
                {"quality_writer_key": quality_writer_key}
                if quality_writer_key is not None
                else {}
            )
            ready = validate_iot_state(
                await self.iot.remote_video_v2(
                    sn,
                    "start",
                    payload=command,
                    lease_id=lease_id,
                    org_id=org_id,
                    user=user,
                    **writer_options,
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
                or ready["resolved_codec"] != resolved_codec
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
            return self._result(stream_id, resolved_codec, descriptor, media, "started")
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
                    "stop",
                    dict(identity, reason="partial_start_failed"),
                    **media_contract_options(descriptor),
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
