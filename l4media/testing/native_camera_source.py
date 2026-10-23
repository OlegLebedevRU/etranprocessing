# /// script
# requires-python = ">=3.14"
# dependencies = ["paho-mqtt==2.1.0", "jsonschema==4.26.0"]
# ///
"""Explicit TEST main_app producer, native CTLv1 plus unchanged RTSP HEVC copy.

Use an existing authorized test terminal after quiescing its installed l4desk.
This is server-chain evidence, never ESP32/physical-port discovery acceptance.
Credentials are loaded only from a private file outside the repository.
"""

from __future__ import annotations

import argparse
import copy
import json
import queue
import re
import threading
import time
from datetime import UTC, datetime
from pathlib import Path
from typing import Any
from urllib.parse import quote, urlsplit, urlunsplit
from uuid import uuid4

import paho.mqtt.client as mqtt
from jsonschema import Draft202012Validator, FormatChecker
from native_camera_control import VALIDATORS, NativeControl
from native_camera_probe import PACKAGE, NativeProbe, ProbeFailure, probe_native
from native_camera_relay import NativeRelay
from paho.mqtt.enums import CallbackAPIVersion
from paho.mqtt.packettypes import PacketTypes
from paho.mqtt.properties import Properties

REVISION = "siplite-camera/1.0.0"
LOCAL = json.loads((PACKAGE / "local-camera.schema.json").read_text())
SNAPSHOT = Draft202012Validator(
    dict(LOCAL, oneOf=[{"$ref": "#/$defs/camera_snapshot"}]), format_checker=FormatChecker()
)


def utc() -> str:
    return datetime.now(UTC).isoformat().replace("+00:00", "Z")


def decode_command(raw: bytes) -> dict:
    if not 0 < len(raw) <= 8192:
        raise ValueError("command_size")

    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("duplicate_key")
            result[key] = value
        return result

    value = json.loads(raw, object_pairs_hook=unique)
    if not isinstance(value, dict):
        raise ValueError("command_object_required")
    pending: list[tuple[Any, int]] = [(value, 1)]
    while pending:
        node, depth = pending.pop()
        if depth > 12:
            raise ValueError("command_depth")
        if isinstance(node, dict):
            pending.extend((item, depth + 1) for item in node.values() if isinstance(item, (dict, list)))
        elif isinstance(node, list):
            pending.extend((item, depth + 1) for item in node if isinstance(item, (dict, list)))
    return value


def gauge(probe: NativeProbe | None, fresh: bool) -> dict:
    snapshot = copy.deepcopy(json.loads((PACKAGE / "fixtures/local_offline_snapshot.json").read_text())["payload"])
    snapshot.update(
        hardware_id="TEST-host-passthrough-not-ESP32",
        snapshot_id=str(uuid4()),
        auth_source="none",
        has_credentials=False,
        actual_track=copy.deepcopy(probe.track) if fresh and probe else None,
    )
    snapshot["discovery"].update(state="idle", candidates=[])
    snapshot["capabilities"].update(methods=[], ingest=["rtsp_tcp"])
    SNAPSHOT.validate(snapshot)
    camera = {
        key: value
        for key, value in snapshot.items()
        if key not in ("v", "hardware_id", "contract_revision", "snapshot_id")
    }
    camera.update(camera_contract_revision=REVISION, status="read_only" if fresh else "unavailable", stale=not fresh)
    observed = (
        datetime.fromtimestamp(probe.checked_at_ms / 1000, UTC).isoformat().replace("+00:00", "Z") if probe else None
    )
    return {
        "200": 84,
        "300": [
            {
                "450": {
                    "schema_version": 1,
                    "snapshot_id": snapshot["snapshot_id"],
                    "reason": "TEST-host-native-passthrough-no-port2-proof",
                    "operation_id": None,
                    "observed_at": observed,
                    "freshness_max_age_sec": 55,
                    "cameras": [camera],
                }
            }
        ],
    }


class Source:
    def __init__(self, args):
        self.args = args
        private = json.loads(args.camera_config.read_text(encoding="utf-8"))
        parsed = urlsplit(private["url"])
        if parsed.scheme != "rtsp" or not parsed.hostname or parsed.username or parsed.password:
            raise ValueError("private_camera_config_invalid")
        host = parsed.hostname + (f":{parsed.port}" if parsed.port else "")
        self.url = urlunsplit(
            (
                parsed.scheme,
                quote(private["username"], safe="") + ":" + quote(private["password"], safe="") + "@" + host,
                parsed.path,
                parsed.query,
                parsed.fragment,
            )
        )
        self.probe: NativeProbe | None = None
        self.relay: NativeRelay | None = None
        self.halt = threading.Event()
        self.disconnected = threading.Event()
        self.commands: queue.Queue = queue.Queue(maxsize=16)
        self.control = NativeControl(args.sn, self.start, self.stop, self.fresh)
        self.starts = 0
        self.announced = False
        self.client = mqtt.Client(CallbackAPIVersion.VERSION2, client_id="main_app", protocol=mqtt.MQTTv5)
        self.client.will_set(f"dev/{args.sn}/app", "app_offline", qos=1, retain=True)
        self.client.on_connect, self.client.on_message, self.client.on_disconnect = (
            self.connected,
            self.received,
            self.lost,
        )
        args.state_dir.mkdir(parents=True, exist_ok=True)

    def record(self, event: str, **fields) -> None:
        # Only fixed labels, safe track data and UUIDs are passed here.
        value = {"at": utc(), "event": event, **fields}
        with (self.args.state_dir / "events.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(value) + "\n")
        print(json.dumps(value), flush=True)

    def presence(self, online: bool) -> dict:
        value = {
            "v": 1,
            "type": "presence",
            "agent": "TEST-native-camera-passthrough",
            "version": "server-ready-test-only",
            "status": "online" if online else "offline",
            "desktop_available": False,
            "timestamp": utc(),
            "inventory": {
                "displays": [],
                "cameras": [
                    {
                        "camera_id": "cam:ip000001",
                        "name": "TEST real camera / no ESP32 or port2 acceptance",
                        "available": self.fresh(),
                    }
                ],
            }
            if online
            else None,
        }
        VALIDATORS["presence"].validate(value)
        return value

    def publish_ctl(self, payload: dict, *, retain=False) -> None:
        VALIDATORS[payload["type"]].validate(payload)
        self.client.publish(f"dev/{self.args.sn}/ctl", json.dumps(payload), qos=1, retain=retain)

    def publish_gauge(self) -> None:
        payload = gauge(self.probe, self.fresh())
        # Root observation time is the actual probe/last matching live parameter set,
        # never merely the time a cached gauge was republished.
        if self.relay and self.relay.monitor.last_parameters_at is not None:
            age = time.monotonic() - self.relay.monitor.last_parameters_at
            payload["300"][0]["450"]["observed_at"] = (
                datetime.fromtimestamp(time.time() - age, UTC).isoformat().replace("+00:00", "Z")
            )
        props = Properties(PacketTypes.PUBLISH)
        props.CorrelationData = str(uuid4()).encode("ascii")
        props.UserProperty = [
            ("event_type_code", "84"),
            ("dev_event_id", "0"),
            ("dev_timestamp", str(int(time.time()))),
        ]
        self.client.publish(f"dev/{self.args.sn}/evt", json.dumps(payload), qos=1, retain=False, properties=props)

    def connected(self, client, userdata, flags, reason_code, properties) -> None:
        if reason_code.is_failure:
            self.disconnected.set()
            return
        client.subscribe(f"srv/{self.args.sn}/ctl", qos=1)
        client.publish(f"dev/{self.args.sn}/app", "app_online", qos=1, retain=True)
        self.publish_ctl(self.presence(True), retain=True)
        self.publish_gauge()
        self.record("connected", test_only=True)

    def lost(self, client, userdata, flags, reason_code, properties) -> None:
        self.disconnected.set()

    def received(self, client, userdata, message) -> None:
        if message.retain:
            return
        try:
            self.commands.put_nowait(decode_command(message.payload))
        except ValueError, RecursionError, queue.Full:
            self.record("invalid_or_overflow_command")

    def fresh(self) -> bool:
        if self.probe is None:
            return False
        if self.relay:
            checked = self.relay.monitor.last_parameters_at
            return self.relay.health() is None and checked is not None and time.monotonic() - checked < 55
        return int(time.time() * 1000) < self.probe.valid_until_ms

    def start(self, command: dict) -> None:
        if self.probe is None:
            raise ValueError("probe_required")
        self.relay = NativeRelay(
            self.url,
            self.args.ffmpeg,
            self.probe.parameter_sets,
            rtp_port=self.args.rtp_port,
            rtcp_port=self.args.rtcp_port,
        )
        self.relay.start()
        self.starts += 1
        self.announced = False
        self.record("start", stream_instance_id=command["stream_instance_id"], producer_starts=self.starts)

    def stop(self) -> None:
        if self.relay:
            relay, self.relay = self.relay, None
            relay.stop()
            self.record(
                "physical_stop",
                forwarded_packets=relay.forwarded,
                rtp_gaps=relay.monitor.gaps,
                rtp_duplicates=relay.monitor.duplicates,
            )
        self.announced = False

    def event(self, stream: str, state: str, reason: str | None = None) -> None:
        value = {
            "v": 1,
            "type": "stream_event",
            "sn": self.args.sn,
            "stream_instance_id": stream,
            "state": state,
            "reason": reason,
            "timestamp": utc(),
        }
        self.publish_ctl(value)
        self.record("stream_" + state, stream_instance_id=stream, reason=reason)
        if state == "failed":
            (self.args.state_dir / "last-failure.json").write_text(json.dumps(value), encoding="utf-8")

    def run(self) -> None:
        self.probe = probe_native(self.url, self.args.ffmpeg)
        self.record(
            "native_probe", track=self.probe.track, host_decoded_frames=self.probe.decoded_frames, test_only=True
        )
        self.client.connect("127.0.0.1", self.args.mqtt_port, keepalive=20)
        self.client.loop_start()
        next_probe, next_gauge = time.monotonic() + 35, time.monotonic() + 10
        try:
            while not self.halt.is_set():
                if (self.args.state_dir / "STOP").exists():
                    self.record("operator_stop_file")
                    break
                if self.disconnected.is_set():
                    self.record("transport_disconnected")
                    break  # No automatic stream restart/reconnect ownership recovery.
                try:
                    command = self.commands.get(timeout=0.05)
                except queue.Empty:
                    command = None
                if command:
                    reply = self.control.handle(command)
                    if reply:
                        self.publish_ctl(reply)
                        self.record(
                            "command_result",
                            command_type=command["type"],
                            command_id=command["command_id"],
                            result=reply.get("result"),
                            code=reply.get("code"),
                        )
                self.control.expire()
                if self.control.active and self.relay:
                    reason = self.relay.health()
                    if self.args.fail_file.exists():
                        self.args.fail_file.unlink()
                        reason = "test_camera_source_failed"
                    if reason:
                        self.control.fail(self.control.active.stream, reason)
                    elif self.relay.monitor.ready and not self.announced:
                        self.event(self.control.active.stream, "running")
                        self.announced = True
                while self.control.retired:
                    self.event(*self.control.retired.pop(0))
                replay = self.args.state_dir / "REPLAY_LAST_FAILURE"
                if replay.exists():
                    replay.unlink()
                    saved = (self.args.state_dir / "last-failure.json").read_bytes()
                    previous = decode_command(saved)
                    if previous.get("sn") != self.args.sn or previous.get("state") != "failed":
                        raise ValueError("invalid_saved_failure")
                    self.publish_ctl(previous)
                    self.record("replayed_old_failure", stream_instance_id=previous.get("stream_instance_id"))
                now = time.monotonic()
                if self.relay is None and now >= next_probe:
                    try:
                        self.probe = probe_native(self.url, self.args.ffmpeg)
                        self.record(
                            "native_probe",
                            track=self.probe.track,
                            host_decoded_frames=self.probe.decoded_frames,
                            test_only=True,
                        )
                    except ProbeFailure:
                        self.probe = None
                        self.record("probe_unavailable")
                    next_probe = time.monotonic() + 35
                if now >= next_gauge:
                    self.publish_gauge()
                    next_gauge = time.monotonic() + 10
        except KeyboardInterrupt:
            self.record("operator_exit")
        finally:
            self.stop()
            self.probe = None
            if self.client.is_connected():
                self.publish_gauge()
                self.publish_ctl(self.presence(False), retain=True)
                pending = self.client.publish(f"dev/{self.args.sn}/app", "app_offline", qos=1, retain=True)
                pending.wait_for_publish(timeout=3)
            self.client.disconnect()
            self.client.loop_stop()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sn", required=True)
    parser.add_argument("--camera-config", type=Path, required=True)
    parser.add_argument("--state-dir", type=Path, required=True)
    parser.add_argument("--fail-file", type=Path, required=True)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--mqtt-port", type=int, default=1883)
    parser.add_argument("--rtp-port", type=int, default=5004)
    parser.add_argument("--rtcp-port", type=int, default=5005)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9._-]{1,64}", args.sn):
        parser.error("invalid SN")
    try:
        Source(args).run()
    except Exception as exc:
        # Exception details can include private URL/schema data; never reflect them.
        print(json.dumps({"event": "source_failed", "error_type": type(exc).__name__}), flush=True)
        raise SystemExit(1) from None


if __name__ == "__main__":
    main()
