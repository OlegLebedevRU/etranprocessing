"""Common media-v3 orchestration with the separate main_app camera adapter."""

from __future__ import annotations

import asyncio
import contextlib
import time
from typing import Any, Literal, NoReturn
from uuid import UUID, uuid5

from fastapi import HTTPException
from pydantic import UUID4, BaseModel, ConfigDict

from app.services.camera_contract import CAMERA_MEDIA_REVISION, MEDIA_REVISION
from app.services.camera_media_clock import camera_media_deadline


class BrowserCodecs(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)
    h264: bool | None
    h265: bool | None
    checked_at_ms: int
    valid_until_ms: int


class CameraVideoState(BaseModel):
    model_config = ConfigDict(extra="forbid")
    version: Literal[2]
    contract_version: Literal[3]
    media_contract_revision: Literal["l4-media/3.0.0"]
    camera_contract_revision: Literal["siplite-camera/1.3.0"]
    cadence_revision: Literal["camera-cadence/0.2.0-proposed"]
    media_clock_id: UUID4
    sn: str
    lease_id: UUID
    operation_id: UUID
    command_id: UUID
    stream_instance_id: UUID
    descriptor: dict[str, Any]
    resolved_codec: Literal["h264", "h265"]
    result: str
    state: Literal["starting", "running", "stopping", "stopped", "failed"]
    confirmation: Literal["command_ack"] | None = None
    actual_descriptor: None = None
    reason: str | None = None


def refuse(code: str, status: int = 409) -> NoReturn:
    raise HTTPException(status, detail={"code": code})


def validate_browser(browser: BrowserCodecs | None, now_ms: int) -> BrowserCodecs:
    if browser is None or not (
        browser.checked_at_ms - 5000 <= now_ms < browser.valid_until_ms
        and 0 < browser.valid_until_ms - browser.checked_at_ms <= 60000
    ):
        refuse("camera_browser_capabilities_stale")
    return browser


def server_accepts(
    server: dict, track: dict, revision: str = CAMERA_MEDIA_REVISION
) -> bool:
    from app.services.video_contract import VALIDATORS

    return (
        revision == CAMERA_MEDIA_REVISION
        and VALIDATORS["media_capabilities"].is_valid(server)
        and VALIDATORS["track"].is_valid(track)
        and server["dependencies_ready"] is True
        and track["codec"] in server["codecs"]
        and (track["codec"] != "h265" or server["hevc_runtime_verified"] is True)
        and any(
            all(
                track[k] == fmt[k]
                for k in ("codec", "codec_profile", "packetization", "clock_rate")
            )
            for fmt in server["formats"]
        )
    )


def resolve_camera_codec(
    policy: str,
    camera: dict,
    server: dict,
    browser: BrowserCodecs,
    revision: str = CAMERA_MEDIA_REVISION,
) -> tuple[str, str]:
    from app.services.video_contract import VALIDATORS

    if policy not in ("auto", "h264", "h265"):
        refuse("camera_codec_policy_invalid", 422)
    track = camera.get("actual_track")
    if not isinstance(track, dict) or not VALIDATORS["track"].is_valid(track):
        refuse("camera_actual_track_unknown")
    if (
        revision != CAMERA_MEDIA_REVISION
        or not VALIDATORS["media_capabilities"].is_valid(server)
        or server.get("dependencies_ready") is not True
    ):
        refuse("camera_server_unavailable")
    supported = {track["codec"]}
    for field in camera.get("capabilities", {}).get("fields", []):
        if field.get("path") == "video0.codec" and field.get("write") is True:
            supported.update(field.get("values", []))

    def available(codec: str) -> str:
        browser_support = getattr(browser, codec)
        if browser_support is None:
            return "unknown"
        if (
            codec not in supported
            or browser_support is False
            or codec not in server["codecs"]
        ):
            return "unsupported"
        if track["codec"] == codec and not server_accepts(server, track, revision):
            return "unsupported"
        return "ready"

    if policy in ("auto", "h265"):
        hevc = available("h265")
        if hevc == "ready":
            return "h265", "camera_chain_h265_ready"
        if hevc == "unknown":
            refuse("camera_capability_chain_unknown")
        if policy == "h265":
            refuse("camera_h265_unsupported", 422)
    if available("h264") != "ready":
        refuse("camera_h264_unavailable", 422)
    return (
        "h264",
        "explicit_h264" if policy == "h264" else "camera_chain_h265_unsupported",
    )


class NativeCameraVideo:
    def __init__(self, iot, media):
        self.iot, self.media = iot, media

    async def inventory(self, sn, org_id, user, revision=CAMERA_MEDIA_REVISION):
        if revision != CAMERA_MEDIA_REVISION:
            refuse("camera_contract_not_negotiated")
        value = await self.iot.remote_video_v2(
            sn, "inventory", org_id=org_id, user=user, camera_contract=revision
        )
        if (
            value.get("camera_contract_revision") != revision
            or value.get("contract_version") != 3
            or value.get("media_contract_revision") != MEDIA_REVISION
        ):
            refuse("camera_contract_not_negotiated")
        return value

    async def start(
        self,
        *,
        terminal,
        lease_id,
        policy,
        source_id,
        profile_id,
        browser,
        user,
        admission_ticket=None,
    ):
        from app.config import settings
        from app.services.camera_admission import CameraAdmission, unseal
        from app.services.video_contract import (
            media_identity,
            validate_iot_state,
            validate_video,
            verify_media_response,
        )

        if (
            not settings.video_contract_v2_enabled
            or not settings.l4desk_session_orchestration_enabled
        ):
            refuse("camera_control_disabled")
        if (
            not settings.camera_monotonic_runtime_enabled
            or not settings.camera_native_auto_enabled
        ):
            refuse("camera_runtime_disabled", 503)
        if source_id != "cam:ip000001" or profile_id not in ("low", "default"):
            refuse("camera_source_mismatch", 422)
        validate_browser(browser, int(time.time() * 1000))
        admission = unseal(admission_ticket, terminal, user, "ready")
        descriptor = validate_video("descriptor", admission["descriptor"])
        if (
            descriptor["source_id"] != source_id
            or descriptor["profile_id"] != profile_id
            or admission["policy"] != policy
        ):
            refuse("camera_preparation_selection_changed")
        await CameraAdmission(self.iot, self.media).check(admission, terminal, user)
        lease_query_at = time.monotonic_ns()
        authority = await self.iot.remote_video_v2(
            terminal.sn,
            "lease",
            lease_id=lease_id,
            org_id=terminal.org_id,
            user=user,
            camera_contract=CAMERA_MEDIA_REVISION,
        )
        roundtrip_ms = (time.monotonic_ns() - lease_query_at + 999999) // 1000000
        deadline = camera_media_deadline(
            authority,
            sn=terminal.sn,
            lease_id=lease_id,
            user=user,
            roundtrip_ms=roundtrip_ms,
        )
        if authority.get("scope") != "stream" or authority.get("sn") != terminal.sn:
            refuse("camera_lease_mismatch")
        try:
            previous = validate_iot_state(
                await self.iot.remote_video_v2(
                    terminal.sn,
                    "state",
                    lease_id=lease_id,
                    org_id=terminal.org_id,
                    user=user,
                    camera_contract=CAMERA_MEDIA_REVISION,
                )
            )
        except HTTPException as error:
            if error.status_code != 404:
                raise
            previous = None
        if previous and previous["state"] in ("starting", "running"):
            if (
                previous["descriptor"] != descriptor
                or previous["resolved_codec"] != descriptor["tracks"][0]["codec"]
            ):
                refuse("conflict")
            identity = media_identity(
                terminal.sn, lease_id, previous["stream_instance_id"], descriptor
            )
            if previous["media_clock_id"] != authority["media_clock_id"]:
                with contextlib.suppress(Exception):
                    await self.iot.remote_video_v2(
                        terminal.sn,
                        "stop",
                        lease_id=lease_id,
                        payload={**identity, "reason": "media_clock_changed"},
                        org_id=terminal.org_id,
                        user=user,
                        camera_contract=CAMERA_MEDIA_REVISION,
                    )
                with contextlib.suppress(Exception):
                    await self.media.video_v2(
                        "stop", {**identity, "reason": "media_clock_changed"}
                    )
                refuse("camera_media_clock_changed")
            media = validate_video(
                "media_health", await self.media.video_v2("health", identity)
            )
            verify_media_response(media, identity, descriptor)
            return {**previous, "media": media, "result": "already_running"}
        stream_id = str(uuid5(UUID(admission["nonce"]), lease_id))
        identity = media_identity(terminal.sn, lease_id, stream_id, descriptor)
        request = validate_video(
            "media_start",
            {
                "contract_version": 3,
                "session_id": identity["session_id"],
                "operation_id": str(uuid5(UUID(stream_id), "camera-start")),
                "publisher": {"sn": terminal.sn, "stream_instance_id": stream_id},
                "lease_id": lease_id,
                "lease_expires_at": deadline,
                "descriptor": descriptor,
            },
        )
        attempted = False
        try:
            media = validate_video(
                "media_response", await self.media.video_v2("start", request)
            )
            verify_media_response(media, identity, descriptor)
            if media["operation_id"] != request["operation_id"] or media[
                "state"
            ] not in ("starting", "active"):
                refuse("camera_media_prepare_unconfirmed")
            unseal(admission_ticket, terminal, user, "ready")
            attempted = True
            state = validate_iot_state(
                await self.iot.remote_video_v2(
                    terminal.sn,
                    "start",
                    lease_id=lease_id,
                    payload=request,
                    camera_readiness=admission["nonce"],
                    org_id=terminal.org_id,
                    user=user,
                    camera_contract=CAMERA_MEDIA_REVISION,
                )
            )
            if any(
                state[k] != v
                for k, v in {
                    "sn": terminal.sn,
                    "lease_id": lease_id,
                    "stream_instance_id": stream_id,
                    "operation_id": request["operation_id"],
                    "command_id": request["operation_id"],
                    "descriptor": descriptor,
                    "media_clock_id": authority["media_clock_id"],
                    "resolved_codec": descriptor["tracks"][0]["codec"],
                    "state": "running",
                    "confirmation": "command_ack",
                }.items()
            ):
                refuse("camera_start_unconfirmed")
            return {**state, "media": media, "codec_reason": admission["codec_reason"]}
        except Exception, asyncio.CancelledError:
            # One exact identity scopes compensation; never create another attempt.
            if attempted:
                with contextlib.suppress(Exception):
                    await self.iot.remote_video_v2(
                        terminal.sn,
                        "stop",
                        lease_id=lease_id,
                        payload={**identity, "reason": "partial_start_failed"},
                        org_id=terminal.org_id,
                        user=user,
                        camera_contract=CAMERA_MEDIA_REVISION,
                    )
            with contextlib.suppress(Exception):
                await self.media.video_v2(
                    "stop", {**identity, "reason": "partial_start_failed"}
                )
            raise
