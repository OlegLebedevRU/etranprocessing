"""Pre-lease camera admission. Tickets are opaque, scoped and never authority.

IoT141 owns the durable, single-use readiness fence. BFF workers share only an
environment-provisioned encryption key; no process-local authorization cache.
"""

from __future__ import annotations

import asyncio
import copy
import hashlib
import json
import time
from urllib.parse import quote
from uuid import UUID, uuid4

from cryptography.fernet import Fernet, InvalidToken

from app.config import settings
from app.services.camera_contract import (
    CAMERA_MEDIA_HEADERS,
    CAMERA_MEDIA_REVISION,
    CAMERA_REVISION,
    OBSERVATION_VALIDATORS,
)
from app.services.camera_platform import camera_request
from app.services.camera_video import (
    NativeCameraVideo,
    refuse,
    resolve_camera_codec,
    server_accepts,
    validate_browser,
)
from app.services.video_contract import validate_video

RUNTIME_HEADERS = CAMERA_MEDIA_HEADERS


def cipher() -> Fernet:
    if not settings.camera_monotonic_runtime_enabled:
        refuse("camera_runtime_disabled", 503)
    try:
        return Fernet(settings.camera_admission_ticket_key.encode())
    except ValueError:
        refuse("camera_runtime_not_configured", 503)


def scope(terminal, user):
    if not user.get("session_id") or not user.get("sub"):
        refuse("camera_session_required", 403)
    return [terminal.org_id, terminal.sn, str(user["sub"]), str(user["session_id"])]


def seal(value):
    return cipher().encrypt(json.dumps(value, separators=(",", ":")).encode()).decode()


def unseal(ticket, terminal, user, stage):
    try:
        value = json.loads(cipher().decrypt(ticket.encode(), ttl=45))
    except InvalidToken, ValueError, TypeError, AttributeError:
        refuse("camera_preparation_invalid", 409)
    if value.get("scope") != scope(terminal, user):
        refuse("camera_preparation_owner_mismatch", 403)
    if value.get("stage") != stage or time.time() >= value.get("deadline", 0):
        refuse("camera_preparation_expired")
    return value


def descriptor_sha256(descriptor):
    def normalized(value):
        if type(value) in (int, float):
            if (
                not -9007199254740991 <= value <= 9007199254740991
                or int(value) != value
            ):
                refuse("camera_descriptor_invalid")
            return int(value)
        if isinstance(value, dict):
            return {key: normalized(item) for key, item in value.items()}
        if isinstance(value, list):
            return [normalized(item) for item in value]
        return value

    return hashlib.sha256(
        json.dumps(
            normalized(descriptor),
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
            allow_nan=False,
        ).encode()
    ).hexdigest()


def physical_camera(view, mac, generation):
    camera = view.get("camera") or {}
    discovery = camera.get("discovery") or {}
    if (
        view.get("fresh") is not True
        or view.get("main_app_online") is not True
        or camera.get("stale") is True
        or discovery.get("state") != "ready"
        or discovery.get("physical_port") != 2
        or discovery.get("generation") != generation
        or not any(
            c.get("mac") == mac
            and c.get("port_verified") is True
            and c.get("auth") == "verified"
            for c in discovery.get("candidates", [])
        )
    ):
        refuse("camera_physical_binding_unverified")
    return camera


def observed_camera(view, camera):
    observation = view.get("camera_observation")
    if (
        view.get("contract_version") != 3
        or view.get("media_contract_revision") != "l4-media/3.0.0"
        or not OBSERVATION_VALIDATORS["camera_observation"].is_valid(observation)
        or not OBSERVATION_VALIDATORS["camera_capabilities"].is_valid(
            view.get("camera_capabilities")
        )
        or observation["source_id"] != camera.get("camera_id")
        or observation["actual_track"]["fps"]
        > (
            20
            if observation["actual_track"]["width"] > 1920
            or observation["actual_track"]["height"] > 1080
            else 30
        )
    ):
        refuse("camera_native_auto_not_negotiated")
    return {
        **camera,
        "actual_track": observation["actual_track"],
        "geometry_generation": observation["geometry_generation"],
        "quality_options": [
            option
            for option in observation["capabilities"]["quality_options"]
            if option in view["camera_capabilities"]["quality_options"]
        ],
    }


class CameraAdmission:
    def __init__(self, iot, media):
        self.native = NativeCameraVideo(iot, media)
        self.media = media

    async def request(
        self, terminal, user, action, body=None, *, revision=CAMERA_REVISION
    ):
        path = f"remote-input/devices/{quote(terminal.sn, safe='')}/camera/admission"
        if action:
            path += f"/{action}"
        try:
            async with asyncio.timeout(6):
                return await camera_request(
                    "POST" if action else "GET",
                    path,
                    terminal.org_id,
                    user,
                    body=body,
                    headers=CAMERA_MEDIA_HEADERS
                    if revision == CAMERA_MEDIA_REVISION
                    else RUNTIME_HEADERS,
                )
        except TimeoutError:
            refuse("camera_admission_unconfirmed", 504)

    async def prepare(self, terminal, user, selection):
        cipher()
        owner = scope(terminal, user)
        # One original overall preparation budget includes all provider I/O.
        deadline = time.time() + 30
        browser = validate_browser(selection.browser_codecs, int(time.time() * 1000))
        if not settings.camera_native_auto_enabled:
            refuse("camera_native_auto_not_negotiated")
        revision = CAMERA_MEDIA_REVISION
        view = await self.native.inventory(terminal.sn, terminal.org_id, user, revision)
        camera = physical_camera(
            view, selection.physical_camera_mac, selection.discovery_generation
        )
        if camera.get("camera_id") != selection.source_id:
            refuse("camera_source_mismatch")
        if revision == CAMERA_MEDIA_REVISION:
            camera = observed_camera(view, camera)
        server = validate_video(
            "media_capabilities",
            await self.media.video_v2("capabilities"),
        )
        codec, reason = resolve_camera_codec(
            selection.codec_policy, camera, server, browser, revision
        )
        if camera["actual_track"]["codec"] != codec or not server_accepts(
            server, camera["actual_track"], revision
        ):
            # Changing camera configuration would require a separate authorized
            # operation. Never acquire a stream lease merely to probe a format.
            refuse("camera_actual_format_unsupported")
        descriptor = validate_video(
            "descriptor",
            {
                "contract_version": 3,
                "descriptor_revision": str(uuid4()),
                "source_type": "camera",
                "source_id": selection.source_id,
                "geometry_generation": camera["geometry_generation"]
                if revision == CAMERA_MEDIA_REVISION
                else max(1, selection.discovery_generation),
                "profile_id": selection.profile_id,
                "tracks": [copy.deepcopy(camera["actual_track"])],
            },
        )
        choices = camera["quality_options"]
        if {"profile_id": selection.profile_id} not in choices:
            refuse("camera_quality_policy_unsupported", 422)
        # This is the trusted backend descriptor path used by native media
        # preparation: fresh actual_track + negotiated media capabilities.
        response = await self.request(
            terminal,
            user,
            "prepare",
            {
                "descriptor": descriptor,
                "physical_camera_mac": selection.physical_camera_mac,
            },
            revision=revision,
        )
        if time.time() >= deadline:
            refuse("camera_preparation_expired")
        if response.get("configured") is not True:
            refuse("camera_prepare_unconfirmed")
        return {
            "ticket": seal(
                {
                    "scope": owner,
                    "stage": "prepared",
                    "deadline": deadline,
                    "descriptor": descriptor,
                    "mac": selection.physical_camera_mac,
                    "generation": selection.discovery_generation,
                    "codec_reason": reason,
                    "policy": selection.codec_policy,
                }
            )
        }

    async def check(self, value, terminal, user):
        revision = CAMERA_MEDIA_REVISION
        if (
            revision == CAMERA_MEDIA_REVISION
            and not settings.camera_native_auto_enabled
        ):
            refuse("camera_native_auto_not_negotiated")
        view = await self.native.inventory(terminal.sn, terminal.org_id, user, revision)
        camera = physical_camera(view, value["mac"], value["generation"])
        if revision == CAMERA_MEDIA_REVISION:
            camera = observed_camera(view, camera)
            if (
                camera["geometry_generation"]
                != value["descriptor"]["geometry_generation"]
            ):
                refuse("camera_descriptor_changed")
        if (
            camera.get("actual_track") != value["descriptor"]["tracks"][0]
            or camera.get("camera_id") != value["descriptor"]["source_id"]
            or {"profile_id": value["descriptor"]["profile_id"]}
            not in camera["quality_options"]
        ):
            refuse("camera_descriptor_changed")

    async def enrollment(self, terminal, user, ticket):
        value = unseal(ticket, terminal, user, "prepared")
        await self.check(value, terminal, user)
        response = await self.request(
            terminal, user, "", revision=CAMERA_MEDIA_REVISION
        )
        unseal(ticket, terminal, user, "prepared")
        return {"enrolled": response.get("enrolled") is True}

    async def readiness(self, terminal, user, ticket):
        value = unseal(ticket, terminal, user, "prepared")
        await self.check(value, terminal, user)
        state = await self.request(terminal, user, "", revision=CAMERA_MEDIA_REVISION)
        unseal(ticket, terminal, user, "prepared")
        if state.get("enrolled") is not True:
            refuse("camera_not_enrolled")
        # Original readiness budget includes HTTP transit; never restart it on receipt.
        deadline = time.time() + 5
        query = await self.request(
            terminal,
            user,
            "readiness",
            revision=CAMERA_MEDIA_REVISION,
        )
        try:
            parsed_nonce = UUID(query["readiness_nonce"])
            nonce = str(parsed_nonce)
            if parsed_nonce.version != 4 or nonce != query["readiness_nonce"]:
                raise ValueError("Invalid nonce")
        except KeyError, TypeError, ValueError, AttributeError:
            refuse("camera_readiness_unconfirmed")
        binding = query.get("binding") or {}
        descriptor_hash = descriptor_sha256(value["descriptor"])
        if (
            query.get("type") != "camera_readiness_query"
            or query.get("camera_admission_revision")
            != RUNTIME_HEADERS["X-L4-Camera-Admission"]
            or query.get("sn") != terminal.sn
            or query.get("issuer_epoch") != state.get("epoch")
            or not state.get("boot")
            or binding.get("boot_id") != state["boot"]
            or binding.get("descriptor_sha256") != descriptor_hash
            or binding.get("source_id") != value["descriptor"]["source_id"]
            or binding.get("codec") != value["descriptor"]["tracks"][0]["codec"]
            or binding.get("physical_camera_mac") != value["mac"]
            or binding.get("path_generation")
            != f"port2/{value['generation']}/{value['descriptor']['geometry_generation']}"
            or time.time() >= deadline
        ):
            refuse("camera_readiness_unconfirmed")
        return {
            "ticket": seal(
                {**value, "stage": "ready", "deadline": deadline, "nonce": nonce}
            )
        }
