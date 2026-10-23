"""Test-only CTLv1 lifecycle; no device provisioning or camera mutation."""

from __future__ import annotations

import copy
import hashlib
import json
import time
from collections import OrderedDict
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from uuid import UUID

from jsonschema import Draft202012Validator, FormatChecker

WIRE = json.loads(Path(__file__).with_name("native_ctl_v1.schema.json").read_text())
VALIDATORS = {
    name: Draft202012Validator(dict(WIRE, oneOf=[{"$ref": f"#/$defs/{name}"}]), format_checker=FormatChecker())
    for name in (
        "inventory_get",
        "stream_start",
        "stream_stop",
        "lease_renew",
        "ack",
        "nack",
        "presence",
        "stream_event",
    )
}


@dataclass
class Active:
    lease: str
    stream: str
    expires_at_ms: int
    monotonic_deadline: float


class NativeControl:
    def __init__(
        self,
        sn: str,
        start: Callable,
        stop: Callable,
        fresh: Callable,
        *,
        wall_ms: Callable = lambda: int(time.time() * 1000),
        monotonic: Callable = time.monotonic,
    ):
        self.sn, self.start_media, self.stop_media, self.fresh = sn, start, stop, fresh
        self.wall_ms, self.monotonic = wall_ms, monotonic
        self.active: Active | None = None
        self.cache: OrderedDict[str, tuple[str, dict]] = OrderedDict()
        self.retired: list[tuple[str, str, str]] = []

    def response(self, command: dict, result: str, *, code: str | None = None) -> dict:
        value = {"v": 1, "type": "nack" if code else "ack", "sn": self.sn, "command_id": command["command_id"]}
        if command.get("lease_id"):
            value["lease_id"] = command["lease_id"]
        if code:
            value.update(code=code, message="Native camera test producer rejected this command")
        else:
            value["result"] = result
            if command.get("stream_instance_id"):
                value["stream_instance_id"] = command["stream_instance_id"]
        if not VALIDATORS[value["type"]].is_valid(value):
            raise ValueError("invalid_native_control_reply")
        return value

    def remember(self, command: dict, digest: str, reply: dict) -> dict:
        if not VALIDATORS[reply["type"]].is_valid(reply):
            raise ValueError("invalid_native_control_reply")
        self.cache[command["command_id"]] = (digest, copy.deepcopy(reply))
        while len(self.cache) > 32:
            self.cache.popitem(last=False)
        return reply

    def handle(self, command: dict) -> dict | None:
        kind = command.get("type")
        if kind not in ("inventory_get", "stream_start", "stream_stop", "lease_renew"):
            return None
        if not VALIDATORS[kind].is_valid(command):
            return None
        command = copy.deepcopy(command)
        for key in ("command_id", "lease_id", "stream_instance_id"):
            if command.get(key):
                command[key] = str(UUID(command[key]))
        if command["sn"] != self.sn:
            return self.response(command, "", code="invalid_sn")
        digest = hashlib.sha256(json.dumps(command, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        cached = self.cache.get(command["command_id"])
        if cached:
            return (
                copy.deepcopy(cached[1]) if cached[0] == digest else self.response(command, "", code="invalid_payload")
            )
        self.expire()
        now = self.wall_ms()
        if not command["issued_at_ms"] <= now < command["expires_at_ms"]:
            return self.remember(command, digest, self.response(command, "", code="expired"))
        if kind == "inventory_get":
            reply = self.response(command, "inventory")
            reply["inventory"] = {
                "displays": [],
                "cameras": [
                    {
                        "camera_id": "cam:ip000001",
                        "name": "TEST native RTSP passthrough / no ESP32 acceptance",
                        "available": bool(self.fresh()),
                    }
                ],
            }
            return self.remember(command, digest, reply)
        if kind == "stream_start":
            if (
                command["mode"] != "usb-camera"
                or command["source_id"] != "cam:ip000001"
                or command["profile"] != "default"
            ):
                reply = self.response(command, "", code="unsupported")
            elif self.active:
                same = (self.active.lease, self.active.stream) == (command["lease_id"], command["stream_instance_id"])
                reply = (
                    self.response(command, "already_running")
                    if same
                    else self.response(command, "", code="lease_mismatch")
                )
            elif not self.fresh():
                reply = self.response(command, "", code="unsupported")
            else:
                deadline = self.monotonic() + min(60, (command["expires_at_ms"] - now) / 1000)
                try:
                    self.start_media(command)
                except Exception:
                    # Process exceptions may contain private RTSP URLs. No reflection.
                    self.stop_media()
                    reply = self.response(command, "", code="inject_failed")
                else:
                    if self.monotonic() >= deadline:
                        self.stop_media()
                        reply = self.response(command, "", code="expired")
                    else:
                        self.active = Active(
                            command["lease_id"], command["stream_instance_id"], command["expires_at_ms"], deadline
                        )
                        reply = self.response(command, "started")
            return self.remember(command, digest, reply)
        if self.active is None:
            reply = (
                self.response(command, "already_stopped")
                if kind == "stream_stop"
                else self.response(command, "", code="lease_invalid")
            )
        elif (command["lease_id"], command.get("stream_instance_id")) != (self.active.lease, self.active.stream):
            reply = self.response(command, "", code="lease_mismatch")
        elif kind == "stream_stop":
            self.stop_media()
            self.retired.append((self.active.stream, "stopped", "owner_stop"))
            self.active = None
            reply = self.response(command, "stopped")
        else:
            candidate = self.monotonic() + min(60, (command["expires_at_ms"] - now) / 1000)
            if candidate > self.active.monotonic_deadline:
                self.active.monotonic_deadline = candidate
                self.active.expires_at_ms = command["expires_at_ms"]
            reply = self.response(command, "renewed")
            reply["applied_deadline_ms"] = self.active.expires_at_ms
        return self.remember(command, digest, reply)

    def expire(self) -> str | None:
        if self.active and self.monotonic() >= self.active.monotonic_deadline:
            stream = self.active.stream
            self.stop_media()
            self.active = None
            self.retired.append((stream, "stopped", "lease_expired"))
            return stream
        return None

    def fail(self, stream: str, reason: str = "test_camera_source_failed") -> bool:
        if self.active is None or self.active.stream != stream:
            return False
        self.stop_media()
        self.active = None
        self.retired.append((stream, "failed", reason))
        return True
