# /// script
# requires-python = ">=3.14"
# dependencies = ["paho-mqtt==2.1.0", "jsonschema==4.26.0"]
# ///
"""Authorized TASK-40 camera passthrough, never a native hardware capability probe.

Run only while the installed svc_desk is quiesced. Credentials stay in a private
JSON file outside Git. FFmpeg copies the camera's encoded HEVC; it never encodes.
The existing local Mosquitto and leo4proxy carry all control and media traffic.
"""

from __future__ import annotations

import argparse
import copy
import json
import queue
import re
import socket
import subprocess
import threading
import time
from datetime import UTC, datetime
from pathlib import Path
from urllib.parse import quote, urlsplit, urlunsplit
from uuid import uuid4

import paho.mqtt.client as mqtt
from jsonschema import Draft202012Validator, FormatChecker
from paho.mqtt.enums import CallbackAPIVersion

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = json.loads((ROOT / "contracts/hevc-v2/hevc.schema.json").read_text(encoding="utf-8"))


def now_ms():
    return int(time.time() * 1000)


def utc():
    return datetime.now(UTC).isoformat().replace("+00:00", "Z")


def validate(kind, payload):
    Draft202012Validator(
        dict(SCHEMA, oneOf=[{"$ref": f"#/$defs/{kind}"}]),
        format_checker=FormatChecker(),
    ).validate(payload)
    return payload


def nal_units(data):
    return [x for x in re.split(b"\x00\x00\x00?\x01", data) if len(x) >= 2]


class Source:
    def __init__(self, args):
        self.args = args
        private = json.loads(args.camera_config.read_text(encoding="utf-8"))
        u = urlsplit(private["url"])
        self.url = urlunsplit(
            (
                u.scheme,
                quote(private["username"], safe="") + ":" + quote(private["password"], safe="") + "@" + u.netloc,
                u.path,
                u.query,
                u.fragment,
            )
        )
        self.boot_id = str(uuid4())
        self.capability = None
        self.sps = None
        self.command = None
        self.process = None
        self.sockets = []
        self.halt = threading.Event()
        self.commands = queue.Queue(maxsize=16)
        self.ready = False
        self.forwarded = 0
        self.started = 0
        self.deadline = 0
        self.last_packet_at = 0
        self.failure_requested = False
        self.epoch = None
        self.epoch_lock = threading.Lock()
        self.client = mqtt.Client(CallbackAPIVersion.VERSION2, client_id="svc_desk")
        self.client.will_set(f"dev/{args.sn}/ctl", json.dumps(self.presence(False)), qos=1, retain=True)
        self.client.on_connect = self.connected
        self.client.on_message = self.received
        self.args.state_dir.mkdir(parents=True, exist_ok=True)

    def record(self, event, **fields):
        # Never log commands, descriptors containing credentials, or FFmpeg stderr.
        entry = {"at": utc(), "event": event, **fields}
        with (self.args.state_dir / "events.jsonl").open("a", encoding="utf-8") as f:
            f.write(json.dumps(entry) + "\n")
        print(json.dumps(entry), flush=True)

    def inventory(self):
        return {
            "displays": [],
            "cameras": [
                {
                    "camera_id": "test-rtsp-camera",
                    "name": "TEST real RTSP camera / encoded passthrough",
                    "available": True,
                }
            ],
        }

    def presence(self, online):
        return {
            "v": 1,
            "type": "presence",
            "agent": "l4desk",
            "version": "task40-camera-test-source",
            "capabilities": ["video_contract_v2"],
            "status": "online" if online else "offline",
            "desktop_available": False,
            "session_id": 1 if online else None,
            "inventory": self.inventory() if online else None,
            "timestamp": utc(),
        }

    def publish(self, payload, kind=None, retain=False):
        if kind:
            validate(kind, payload)
        self.client.publish(f"dev/{self.args.sn}/ctl", json.dumps(payload), qos=1, retain=retain)

    def connected(self, client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            self.halt.set()
            return
        client.subscribe(f"srv/{self.args.sn}/ctl", qos=1)
        self.publish(self.presence(True), retain=True)
        self.record("connected", agent_instance_id=self.boot_id)

    def received(self, client, userdata, message):
        try:
            if message.retain or len(message.payload) > 32768:
                return

            def unique(pairs):
                result = {}
                for k, v in pairs:
                    if k in result:
                        raise ValueError("duplicate")
                    result[k] = v
                return result

            value = json.loads(message.payload, object_pairs_hook=unique)
            self.commands.put_nowait(value)
        except ValueError, queue.Full:
            self.record("invalid_command")

    def probe(self):
        started = now_ms()
        result = subprocess.run(
            [
                self.args.ffmpeg,
                "-hide_banner",
                "-loglevel",
                "info",
                "-rtsp_transport",
                "tcp",
                "-i",
                self.url,
                "-map",
                "0:v:0",
                "-an",
                "-frames:v",
                "26",
                "-c:v",
                "copy",
                "-bsf:v",
                "hevc_metadata=level=4,trace_headers",
                "-f",
                "hevc",
                "pipe:1",
            ],
            capture_output=True,
            timeout=4.5,
            creationflags=subprocess.CREATE_NO_WINDOW,
        )
        text = result.stderr.decode("utf-8", errors="replace")
        header = next((line for line in text.splitlines() if "Video:" in line), "")
        match = re.search(r"Video: hevc \(Main\), yuvj?420p\([^)]*\), (\d+)x(\d+), ([\d.]+) fps", header)
        if result.returncode or not match:
            raise ValueError("camera_probe_failed")
        width, height, fps = map(float, match.groups())
        if width != 1920 or height != 1080 or fps != 20:
            raise ValueError("camera_geometry_changed")
        level = re.findall(r"\bgeneral_level_idc\b[^\r\n]*=\s*(\d+)", text)
        reorder = re.findall(r"\bsps_max_num_reorder_pics\b[^\r\n]*=\s*(\d+)", text)
        units = nal_units(result.stdout)
        if not level or not reorder or any(int(n) for n in reorder):
            raise ValueError("camera_reordering_unsupported")
        kinds = {(n[0] >> 1) & 63 for n in units}
        if not {32, 33, 34} <= kinds or not kinds.intersection({19, 20, 21}):
            raise ValueError("camera_parameter_sets_missing")
        pictures = [n for n in units if ((n[0] >> 1) & 63) < 32 and len(n) > 2 and n[2] & 128]
        keyframes = [i for i, n in enumerate(pictures) if ((n[0] >> 1) & 63) in (19, 20, 21)]
        if len(keyframes) < 2 or any(b - a != 20 for a, b in zip(keyframes, keyframes[1:])):
            raise ValueError("camera_gop_changed")
        if any(sum(((n[0] >> 1) & 63) == kind for n in units) < 2 for kind in (32, 33, 34)):
            raise ValueError("camera_parameter_sets_not_repeated")
        self.sps = next(n for n in units if ((n[0] >> 1) & 63) == 33)
        track = {
            "track_id": "video-main",
            "kind": "video",
            "direction": "producer_to_viewers",
            "bit_depth": 8,
            "chroma": "420",
            "clock_rate": 90000,
            "payload_type": 96,
            "width": int(width),
            "height": int(height),
            "fps": int(fps),
            "feedback": {"pli": False, "fir": False},
            "max_b_frames": 0,
            "keyframe_interval_ms": 1000,
            "repeat_parameter_sets": True,
            "codec": "h265",
            "codec_profile": "main",
            "packetization": "rfc7798",
            "profile_space": 0,
            "profile_id": 1,
            "tier_flag": 0,
            "level_id": int(level[-1]),
            "sprop_max_don_diff": 0,
        }
        cap = {
            "agent_instance_id": self.boot_id,
            "capability_revision": str(uuid4()),
            "checked_at_ms": now_ms(),
            "valid_until_ms": now_ms() + 55000,
            "source_id": "test-rtsp-camera",
            "binding": {
                "process_arch": "x64",
                "adapter_id": "test-rtsp-camera",
                "driver_revision": "camera-encoded-SPS",
                "component_revision": "task40-RTSP-copy",
                "interactive_session_id": 1,
                "geometry_generation": 1,
            },
            "video": [
                {
                    "codec": "h265",
                    "backend": "camera",
                    "hardware": False,
                    "status": "ready",
                    "reason_code": None,
                    "profiles": [{"profile_id": "default", "track": track}],
                    "probe_evidence": "encoded_sequence",
                }
            ],
        }
        self.capability = validate("capabilities", cap)
        self.record(
            "fresh_camera_probe",
            duration_ms=now_ms() - started,
            width=int(width),
            height=int(height),
            fps=int(fps),
            level_id=int(level[-1]),
            hardware=False,
        )
        return cap

    def event(self, state, reason=None):
        command = self.command
        if not command:
            return
        value = {
            k: command[k]
            for k in ("sn", "lease_id", "stream_instance_id", "command_id", "operation_id", "agent_instance_id")
        }
        value.update(v=2, type="stream_event", state=state, timestamp=utc())
        if state == "running":
            value.update(confirmation="agent_ready", actual_descriptor=copy.deepcopy(command["descriptor"]))
            self.publish(value, "ready")
        else:
            value.pop("command_id")
            value.pop("operation_id")
            value["descriptor_revision"] = command["descriptor"]["descriptor_revision"]
            value["reason"] = reason
            # Terminal errors retain the producer domain; no GPU diagnosis is invented.
            value["error"] = {
                "domain": "producer",
                "code": "producer_lost",
                "scope": "stream",
                "codec": "h265",
                "backend": "camera",
                "stage": "capture",
                "current_stream_terminated": True,
                "recovery_class": "manual_action_required",
                "suggested_action": "check_source",
                "auto_restart_allowed": False,
                "attempt": 0,
            }
            self.publish(value, "error")
        self.record(
            "stream_" + state,
            stream_instance_id=command["stream_instance_id"],
            reason=reason,
            forwarded_packets=self.forwarded,
            producer_starts=self.started,
        )

    def relay(self, sock, epoch, control=False):
        seen = set()
        keyframe = False
        feedback_target = None
        while not self.halt.is_set() and sock in self.sockets:
            try:
                packet, address = sock.recvfrom(65535)
            except TimeoutError, OSError:
                continue
            with self.epoch_lock:
                if self.epoch != epoch:
                    break
            if control:
                if address[1] != self.args.rtcp_port:
                    feedback_target = address
                    sock.sendto(packet, ("127.0.0.1", self.args.rtcp_port))
                elif feedback_target:
                    sock.sendto(packet, feedback_target)
                continue
            if len(packet) < 14 or packet[0] >> 6 != 2 or packet[1] & 127 != 96:
                continue
            offset = 12 + 4 * (packet[0] & 15)
            if packet[0] & 16:
                if len(packet) < offset + 4:
                    continue
                offset += 4 + 4 * int.from_bytes(packet[offset + 2 : offset + 4], "big")
            body = packet[offset:]
            if len(body) < 2:
                continue
            kind = (body[0] >> 1) & 63
            units = [body]
            if kind == 48:
                units = []
                position = 2
                while position + 2 <= len(body):
                    size = int.from_bytes(body[position : position + 2], "big")
                    position += 2
                    if size < 2 or position + size > len(body):
                        break
                    units.append(body[position : position + size])
                    position += size
            mismatch = False
            for unit in units:
                kind = (unit[0] >> 1) & 63
                if kind == 33 and unit != self.sps:
                    mismatch = True
                seen.add(kind)
                if kind in (19, 20, 21) or (kind == 49 and len(unit) > 2 and unit[2] & 63 in (19, 20, 21)):
                    keyframe = True
            if mismatch:
                self.failure_requested = True
                continue
            sock.sendto(packet, ("127.0.0.1", self.args.rtp_port))
            with self.epoch_lock:
                if self.epoch != epoch:
                    break
                self.forwarded += 1
                self.last_packet_at = time.monotonic()
                if not self.ready and keyframe and packet[1] & 128 and {32, 33, 34} <= seen:
                    self.ready = True

    def stop(self, reason="owner_stop", failed=False):
        with self.epoch_lock:
            self.epoch = None
        if self.process:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=2)
            self.process = None
        sockets, self.sockets = self.sockets, []
        for sock in sockets:
            sock.close()
        if self.command:
            if failed:
                self.event("failed", reason)
            else:
                # G0 has no STOP ACK/stopped envelope. Keep physical exit evidence
                # locally instead of inventing a wire event or a fake failure.
                self.record("stream_stopped", stream_instance_id=self.command["stream_instance_id"], reason=reason)
        self.command = None
        self.ready = False

    def handle(self, value):
        if value.get("sn", self.args.sn) != self.args.sn:
            return
        kind = value.get("type")
        if value.get("v") == 1:
            if kind == "inventory_get":
                self.publish(
                    {
                        "v": 1,
                        "type": "ack",
                        "sn": self.args.sn,
                        "command_id": value["command_id"],
                        "result": "inventory",
                        "inventory": self.inventory(),
                    }
                )
            return  # No input injection and no legacy stream emulation.
        definitions = {
            "inventory_get": "inventory_get",
            "stream_start": "start",
            "stream_stop": "stop",
            "lease_renew": "lease_renew",
        }
        if kind not in definitions:
            return
        validate(definitions[kind], value)
        if not value["issued_at_ms"] <= now_ms() < value["expires_at_ms"]:
            return
        if kind == "inventory_get":
            cap = self.probe()
            self.publish(
                {
                    "v": 2,
                    "type": "ack",
                    "sn": self.args.sn,
                    "command_id": value["command_id"],
                    "result": "inventory",
                    "video_capabilities": cap,
                },
                "capabilities_ack",
            )
        elif kind == "stream_start":
            cap = self.capability
            if self.command or not cap or now_ms() >= cap["valid_until_ms"]:
                return
            if (
                value["agent_instance_id"] != self.boot_id
                or value["capability_revision"] != cap["capability_revision"]
                or value["descriptor"]["tracks"] != [cap["video"][0]["profiles"][0]["track"]]
                or value["descriptor"]["source_id"] != cap["source_id"]
                or value["descriptor"]["source_type"] != "usb-camera"
                or value["resolved_codec"] != "h265"
                or value["delivery"]
                != {"host": "localhost", "rtp_port": self.args.rtp_port, "rtcp_port": self.args.rtcp_port}
            ):
                return
            self.command = copy.deepcopy(value)
            with self.epoch_lock:
                self.epoch = value["stream_instance_id"]
            self.deadline = value["lease_expires_at_ms"]
            self.forwarded = 0
            self.failure_requested = False
            for control in (False, True):
                sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                sock.bind(("127.0.0.1", 0))
                sock.settimeout(0.2)
                self.sockets.append(sock)
                threading.Thread(target=self.relay, args=(sock, self.epoch, control), daemon=True).start()
            rtp, rtcp = [s.getsockname()[1] for s in self.sockets]
            self.process = subprocess.Popen(
                [
                    self.args.ffmpeg,
                    "-hide_banner",
                    "-loglevel",
                    "error",
                    "-rtsp_transport",
                    "tcp",
                    "-i",
                    self.url,
                    "-map",
                    "0:v:0",
                    "-an",
                    "-c:v",
                    "copy",
                    "-bsf:v",
                    "dump_extra=freq=keyframe,hevc_metadata=level=4",
                    "-f",
                    "rtp",
                    "-payload_type",
                    "96",
                    f"rtp://127.0.0.1:{rtp}?rtcpport={rtcp}&pkt_size=1200",
                ],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                creationflags=subprocess.CREATE_NO_WINDOW,
            )
            self.started += 1
            self.last_packet_at = time.monotonic()
            self.publish(
                {
                    "v": 2,
                    "type": "ack",
                    "sn": self.args.sn,
                    "command_id": value["command_id"],
                    "lease_id": value["lease_id"],
                    "stream_instance_id": value["stream_instance_id"],
                    "result": "accepted",
                    "state": "starting",
                    "descriptor_revision": value["descriptor"]["descriptor_revision"],
                },
                "start_ack",
            )
            self.record("producer_started", pid=self.process.pid, stream_instance_id=value["stream_instance_id"])
        elif (
            self.command
            and all(value[k] == self.command[k] for k in ("lease_id", "stream_instance_id", "agent_instance_id"))
            and value["descriptor_revision"] == self.command["descriptor"]["descriptor_revision"]
        ):
            if kind == "stream_stop":
                self.stop()
            elif value["descriptor_revision"] == self.command["descriptor"]["descriptor_revision"]:
                self.deadline = value["lease_expires_at_ms"]

    def run(self):
        self.client.connect("127.0.0.1", self.args.mqtt_port, keepalive=30)
        self.client.loop_start()
        last_presence = 0
        announced = False
        try:
            while not self.halt.is_set() and not (self.args.state_dir / "STOP").exists():
                if time.monotonic() - last_presence >= 30:
                    self.publish(self.presence(True), retain=True)
                    last_presence = time.monotonic()
                try:
                    value = self.commands.get(timeout=0.1)
                    self.handle(value)
                except queue.Empty:
                    pass
                except Exception as exc:
                    # Exception text can contain authenticated camera URLs: type only.
                    self.record("command_failed", error_type=type(exc).__name__)
                if self.command:
                    if self.args.fail_file.exists():
                        self.args.fail_file.unlink()
                        self.failure_requested = True
                    if (
                        self.failure_requested
                        or self.process is None
                        or self.process.poll() is not None
                        or time.monotonic() - self.last_packet_at > 5
                    ):
                        self.stop("test_camera_source_failed", failed=True)
                        announced = False
                    elif now_ms() >= self.deadline:
                        self.stop("lease_expired")
                        announced = False
                    elif self.ready and not announced:
                        self.event("running")
                        announced = True
                else:
                    announced = False
        except KeyboardInterrupt:
            pass
        finally:
            self.stop("test_source_shutdown")
            self.publish(self.presence(False), retain=True)
            self.client.disconnect()
            self.client.loop_stop()
            self.halt.set()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sn", required=True)
    parser.add_argument("--camera-config", type=Path, required=True)
    parser.add_argument("--state-dir", type=Path, required=True)
    parser.add_argument("--fail-file", type=Path, required=True)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--mqtt-port", type=int, default=1883)
    parser.add_argument("--rtp-port", type=int, default=5004)
    parser.add_argument("--rtcp-port", type=int, default=5005)
    parser.add_argument("--probe-only", action="store_true")
    args = parser.parse_args()
    try:
        source = Source(args)
        if args.probe_only:
            source.probe()
        else:
            source.run()
    except Exception as exc:
        print(
            json.dumps(
                {
                    "event": "source_failed",
                    "error_type": type(exc).__name__,
                    "schema_path": list(getattr(exc, "absolute_schema_path", [])),
                    "value_path": list(getattr(exc, "absolute_path", [])),
                }
            ),
            flush=True,
        )
        raise SystemExit(1) from None


if __name__ == "__main__":
    main()
