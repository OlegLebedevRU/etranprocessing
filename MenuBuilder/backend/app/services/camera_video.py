"""Native camera orchestration on the accepted main_app/CTL-v1 adapter."""

from __future__ import annotations

import asyncio
import copy
import json
import time
from typing import Any, Literal, NoReturn
from uuid import UUID, uuid4, uuid5

from fastapi import HTTPException
from pydantic import BaseModel, ConfigDict

from app.services.camera_contract import CAMERA_REVISION, MEDIA_VALIDATORS
from app.services.camera_platform import camera_request


class BrowserCodecs(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)
    h264: bool | None
    h265: bool | None
    checked_at_ms: int
    valid_until_ms: int


class CameraVideoState(BaseModel):
    model_config = ConfigDict(extra="forbid")
    camera_contract_revision: Literal["siplite-camera/1.0.0", "siplite-camera/1.1.0"]
    version: Literal[2]
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


def server_accepts(server: dict, track: dict, revision: str = CAMERA_REVISION) -> bool:
    if revision not in server.get("camera_contract_revisions", []) or not server.get(
        "dependencies_ready"
    ):
        return False
    for fmt in server.get("camera_formats", []):
        if (
            fmt.get("runtime_verified") is True
            and track["codec"] == fmt["codec"]
            and track["codec_profile"] in fmt["codec_profiles"]
            and track["width"] <= fmt["max_width"]
            and track["height"] <= fmt["max_height"]
            and track["fps"] <= fmt["max_fps"]
            and all(
                track[key] == fmt[key]
                for key in ("bit_depth", "chroma", "max_b_frames", "packetization")
            )
            and (track["codec"] != "h265" or track["level_id"] in fmt["level_ids"])
            and (
                track["width"] <= 1920
                and track["height"] <= 1080
                or track["fps"] <= fmt["max_fps_above_full_hd"]
            )
        ):
            return True
    return False


def resolve_camera_codec(
    policy: str,
    camera: dict,
    server: dict,
    browser: BrowserCodecs,
    revision: str = CAMERA_REVISION,
) -> tuple[str, str]:
    if policy not in ("auto", "h264", "h265"):
        refuse("camera_codec_policy_invalid", 422)
    track = camera.get("actual_track")
    if not isinstance(track, dict) or not MEDIA_VALIDATORS["camera_track"].is_valid(
        track
    ):
        refuse("camera_actual_track_unknown")
    supported = {track["codec"]}
    for field in camera.get("capabilities", {}).get("fields", []):
        if field.get("path") == "video0.codec" and field.get("write") is True:
            supported.update(field.get("values", []))

    def available(codec):
        browser_support = getattr(browser, codec)
        if browser_support is None:
            return "unknown"
        if codec not in supported or browser_support is False:
            return "unsupported"
        formats = [
            value
            for value in server.get("camera_formats", [])
            if value.get("codec") == codec
        ]
        if not formats or not any(
            value.get("runtime_verified") is True for value in formats
        ):
            return "unknown"
        if track["codec"] == codec and not server_accepts(server, track, revision):
            return "unsupported"
        return "ready"

    if revision not in server.get("camera_contract_revisions", []) or not server.get(
        "dependencies_ready"
    ):
        refuse("camera_server_unavailable")
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

    async def inventory(self, sn, org_id, user, revision=CAMERA_REVISION):
        value = await self.iot.remote_video_v2(
            sn,
            "inventory",
            org_id=org_id,
            user=user,
            camera_contract=True if revision == CAMERA_REVISION else revision,
        )
        if value.get("camera_contract_revision") != revision:
            refuse("camera_contract_not_negotiated")
        return value

    async def switch_codec(self, *, terminal, lease_id, codec, camera, user, deadline):
        from app.services.video_contract import verify_publisher_lease

        timeout_ms = min(30000, int((deadline - time.time()) * 1000))
        if timeout_ms < 1000:
            refuse("camera_codec_apply_timeout", 504)

        authority = await self.iot.remote_video_v2(
            terminal.sn,
            "lease",
            lease_id=lease_id,
            org_id=terminal.org_id,
            user=user,
            camera_contract=True,
        )
        verify_publisher_lease({"lease": authority}, lease_id, user)
        if authority.get("scope") != "stream" or authority.get("sn") != terminal.sn:
            refuse("camera_lease_mismatch")
        operation = str(uuid4())
        task = await camera_request(
            "POST",
            "device-tasks/",
            terminal.org_id,
            user,
            body={
                "ext_task_id": operation,
                "device_id": terminal.device_id,
                "method_code": 161,
                "ttl": 1,
                "camera_contract_revision": CAMERA_REVISION,
                "payload": {
                    "dt": [
                        {
                            "camera_id": camera["camera_id"],
                            "timeout_ms": timeout_ms,
                            "expected_revision": camera["revision"],
                            "patch": {"video0": {"codec": codec}},
                            "allow_disruptive": True,
                        }
                    ]
                },
            },
        )
        task_id = str(UUID(task["id"]))
        while time.time() < deadline:
            result = await camera_request(
                "GET", f"device-tasks/{task_id}", terminal.org_id, user
            )
            header = result.get("header") or {}
            if (
                header.get("device_id") != terminal.device_id
                or header.get("method_code") != 161
            ):
                refuse("camera_command_identity_mismatch")
            results = result.get("results") or []
            if results:
                outcome = results[0]
                if outcome.get("status_code") != 200:
                    refuse("camera_codec_apply_failed")
                body = outcome.get("result")
                if isinstance(body, str):
                    try:
                        body = json.loads(body)
                    except ValueError:
                        refuse("camera_codec_apply_unconfirmed")
                if not isinstance(body, dict) or not body.get("snapshot_id"):
                    refuse("camera_codec_apply_unconfirmed")
                # Gauge operation + snapshot is independently confirmed before using new SPS.
                gauges = await camera_request(
                    "GET",
                    "gauges/",
                    terminal.org_id,
                    user,
                    params={"device_id": terminal.device_id, "type": "84"},
                )
                from app.services.camera_platform import camera_snapshot

                snapshot = camera_snapshot(gauges, terminal.device_id) or {}
                if (
                    snapshot.get("operation_id") == task_id
                    and snapshot.get("snapshot_id") == body["snapshot_id"]
                ):
                    return
            elif result.get("status") in (4, 5, 6, 7):
                refuse("camera_codec_apply_unconfirmed")
            await asyncio.sleep(0.5)
        refuse("camera_codec_apply_timeout", 504)

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
            verify_publisher_lease,
        )

        if (
            not settings.video_contract_v2_enabled
            or not settings.l4desk_session_orchestration_enabled
        ):
            refuse("camera_control_disabled")

        admission = None
        if admission_ticket is not None or settings.camera_monotonic_runtime_enabled:
            admission = unseal(admission_ticket, terminal, user, "ready")
            if (
                admission["descriptor"]["source_id"] != source_id
                or admission["policy"] != policy
            ):
                refuse("camera_preparation_selection_changed")
            await CameraAdmission(self.iot, self.media).check(admission, terminal, user)

        revision = (
            admission["descriptor"]["camera_contract_revision"]
            if admission
            else CAMERA_REVISION
        )
        selected_contract = True if revision == CAMERA_REVISION else revision

        if source_id != "cam:ip000001" or profile_id != "default":
            refuse("camera_source_mismatch", 422)
        authority = await self.iot.remote_video_v2(
            terminal.sn,
            "lease",
            lease_id=lease_id,
            org_id=terminal.org_id,
            user=user,
            camera_contract=selected_contract,
        )
        deadline = verify_publisher_lease({"lease": authority}, lease_id, user)
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
                    camera_contract=selected_contract,
                )
            )
        except HTTPException as error:
            if error.status_code != 404:
                raise
            previous = None
        if previous and previous["state"] in ("starting", "running"):
            if previous.get("camera_contract_revision") != revision:
                refuse("conflict")
            descriptor = previous["descriptor"]
            if descriptor["source_id"] != source_id or policy not in (
                "auto",
                previous["resolved_codec"],
            ):
                refuse("conflict")
            identity = media_identity(
                terminal.sn, lease_id, previous["stream_instance_id"], descriptor
            )
            media = validate_video(
                "media_health",
                await self.media.video_v2(
                    "health", identity, camera_contract=selected_contract
                ),
            )
            verify_media_response(media, identity, descriptor)
            return {**previous, "media": media, "result": "already_running"}
        if admission:
            descriptor = admission["descriptor"]
            codec = descriptor["tracks"][0]["codec"]
            reason = admission["codec_reason"]
        else:
            browser = validate_browser(browser, int(time.time() * 1000))
            view = await self.inventory(terminal.sn, terminal.org_id, user)
            if (
                view.get("main_app_online") is not True
                or view.get("fresh") is not True
                or not (view.get("producer_adapter") or {}).get("ready")
            ):
                refuse(
                    (view.get("producer_adapter") or {}).get("reason")
                    or "camera_capabilities_unavailable"
                )
            server = validate_video(
                "media_capabilities",
                await self.media.video_v2(
                    "capabilities", camera_contract=selected_contract
                ),
            )
            camera = view["camera"]
            prepare_deadline = min(time.time() + 30, deadline - 15)
            # At most H265 then H264, entirely before media prepare/START. Never retry an ambiguous write.
            for _ in range(2):
                codec, reason = resolve_camera_codec(policy, camera, server, browser)
                if camera["actual_track"]["codec"] == codec:
                    break
                try:
                    async with asyncio.timeout(max(0, prepare_deadline - time.time())):
                        await self.switch_codec(
                            terminal=terminal,
                            lease_id=lease_id,
                            codec=codec,
                            camera=camera,
                            user=user,
                            deadline=prepare_deadline,
                        )
                        view = await self.inventory(terminal.sn, terminal.org_id, user)
                except TimeoutError:
                    refuse("camera_codec_apply_timeout", 504)
                if (
                    view.get("main_app_online") is not True
                    or view.get("fresh") is not True
                    or not (view.get("producer_adapter") or {}).get("ready")
                ):
                    refuse("camera_codec_apply_unconfirmed")
                camera = view["camera"]
            codec, reason = resolve_camera_codec(policy, camera, server, browser)
            if camera["actual_track"]["codec"] != codec or not server_accepts(
                server, camera["actual_track"]
            ):
                refuse("camera_actual_format_unsupported", 422)
            browser = validate_browser(browser, int(time.time() * 1000))
            descriptor = validate_video(
                "descriptor",
                {
                    "descriptor_revision": str(uuid4()),
                    "source_type": "camera",
                    "source_id": source_id,
                    "geometry_generation": max(
                        1, view["camera"]["discovery"]["generation"]
                    ),
                    "profile_id": "default",
                    "camera_contract_revision": CAMERA_REVISION,
                    "tracks": [copy.deepcopy(camera["actual_track"])],
                },
            )
        # The same admitted attempt cannot allocate another route/producer when
        # its response is lost. IoT's durable nonce consumption remains authority.
        stream_id = (
            str(uuid5(UUID(admission["nonce"]), lease_id))
            if admission
            else str(uuid4())
        )
        identity = media_identity(terminal.sn, lease_id, stream_id, descriptor)
        request = validate_video(
            "media_start",
            {
                "contract_version": 2,
                "session_id": identity["session_id"],
                "operation_id": str(uuid5(UUID(stream_id), "camera-start"))
                if admission
                else str(uuid4()),
                "publisher": {"sn": terminal.sn, "stream_instance_id": stream_id},
                "lease_id": lease_id,
                "lease_expires_at": deadline,
                "descriptor": descriptor,
            },
        )
        attempted = False
        try:
            media = validate_video(
                "media_response",
                await self.media.video_v2(
                    "start", request, camera_contract=selected_contract
                ),
            )
            verify_media_response(media, identity, descriptor)
            if media["operation_id"] != request["operation_id"] or media[
                "state"
            ] not in ("starting", "active"):
                refuse("camera_media_prepare_unconfirmed")
            if admission:
                unseal(admission_ticket, terminal, user, "ready")
            attempted = True
            state = validate_iot_state(
                await self.iot.remote_video_v2(
                    terminal.sn,
                    "start",
                    lease_id=lease_id,
                    payload=request,
                    **({"camera_readiness": admission["nonce"]} if admission else {}),
                    org_id=terminal.org_id,
                    user=user,
                    camera_contract=selected_contract,
                )
            )
            if (
                state["sn"] != terminal.sn
                or state["lease_id"] != lease_id
                or state["stream_instance_id"] != stream_id
                or state["operation_id"] != request["operation_id"]
                or state["command_id"] != request["operation_id"]
                or state["descriptor"] != descriptor
                or state["resolved_codec"] != codec
                or state["state"] != "running"
                or state["confirmation"] != "command_ack"
            ):
                refuse("camera_start_unconfirmed")
            return {**state, "media": media, "codec_reason": reason}
        except Exception, asyncio.CancelledError:
            # Cleanup must be scoped even on cancellation. Deadlines remain the final authority.
            import contextlib

            if attempted:
                with contextlib.suppress(Exception):
                    await self.iot.remote_video_v2(
                        terminal.sn,
                        "stop",
                        lease_id=lease_id,
                        payload={**identity, "reason": "partial_start_failed"},
                        org_id=terminal.org_id,
                        user=user,
                        camera_contract=selected_contract,
                    )
            with contextlib.suppress(Exception):
                await self.media.video_v2(
                    "stop",
                    {**identity, "reason": "partial_start_failed"},
                    camera_contract=selected_contract,
                )
            raise
