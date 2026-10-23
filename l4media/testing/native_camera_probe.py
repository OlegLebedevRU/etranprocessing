"""Read-only host probe for the native camera acceptance producer.

The live input remains HEVC copy. A separate null-sink host decode verifies
picture types; neither step encodes or rewrites level/geometry metadata.
Authenticated URLs and FFmpeg diagnostics must never escape this module.
"""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator

PACKAGE = Path(__file__).resolve().parents[2] / "contracts/siplite-camera-v1"
SCHEMA = json.loads((PACKAGE / "hevc.schema.json").read_text(encoding="utf-8"))
TRACK = Draft202012Validator(dict(SCHEMA, oneOf=[{"$ref": "#/$defs/camera_track"}]))


class ProbeFailure(ValueError):
    """Only fixed non-secret codes are exposed to the acceptance harness."""


@dataclass(frozen=True)
class NativeProbe:
    track: dict[str, Any]
    checked_at_ms: int
    valid_until_ms: int
    decoded_frames: int
    sample_sha256: str
    vcl_sha256: str
    sps: bytes = field(repr=False)
    parameter_sets: dict[int, bytes] = field(repr=False)


def nal_units(data: bytes) -> list[bytes]:
    return [unit for unit in re.split(b"\x00\x00\x00?\x01", data) if len(unit) >= 2]


def inspect_capture(encoded: bytes, headers: str, pictures: str, *, checked_at_ms: int) -> NativeProbe:
    """Validate actual copied bytes plus independent host decode observations."""
    if not 0 < len(encoded) <= 8 * 1024 * 1024:
        raise ProbeFailure("camera_probe_resource_limit")
    header = next((line for line in headers.splitlines() if "Video:" in line), "")
    geometry = re.search(r"(\d{2,5})x(\d{2,5}).*?([\d.]+) fps", header)
    if "Video: hevc" not in header or geometry is None:
        raise ProbeFailure("camera_native_geometry_unknown")
    width, height, fps = map(float, geometry.groups())
    if not fps.is_integer() or fps <= 0:
        raise ProbeFailure("camera_native_fps_unsupported")

    def values(name: str) -> set[int]:
        return {int(value) for value in re.findall(r"\b" + re.escape(name) + r"\b[^\r\n]*=\s*(\d+)", headers)}

    required = {
        "general_profile_space": {0},
        "general_tier_flag": {0},
        "general_profile_idc": {1},
        "chroma_format_idc": {1},
        "bit_depth_luma_minus8": {0},
        "bit_depth_chroma_minus8": {0},
        "sps_max_num_reorder_pics": {0},
    }
    if any(values(name) != expected for name, expected in required.items()):
        raise ProbeFailure("camera_native_profile_unsupported")
    levels = values("general_level_idc")
    if len(levels) != 1:
        raise ProbeFailure("camera_native_level_unknown")
    units = nal_units(encoded)
    kinds = [(unit[0] >> 1) & 63 for unit in units]
    for kind in (32, 33, 34):
        parameters = [unit for unit in units if (unit[0] >> 1) & 63 == kind]
        if len(parameters) < 2 or len(set(parameters)) != 1:
            raise ProbeFailure("camera_parameter_sets_unstable")
    picture_types = re.findall(r"\btype:\s*([IPB])\b", pictures)
    if len(picture_types) < 40 or "B" in picture_types:
        raise ProbeFailure("camera_picture_sequence_unsupported")
    decoded_sizes = set(re.findall(r"\bs:(\d+x\d+)\b", pictures))
    if decoded_sizes != {f"{int(width)}x{int(height)}"}:
        raise ProbeFailure("camera_decoded_geometry_mismatch")
    keyframes = [i for i, kind in enumerate(picture_types) if kind == "I"]
    intervals = {second - first for first, second in zip(keyframes, keyframes[1:])}
    if len(intervals) != 1 or not set(kinds).intersection({19, 20, 21}):
        raise ProbeFailure("camera_keyframe_interval_unknown")
    milliseconds = next(iter(intervals)) * 1000 / fps
    if not milliseconds.is_integer():
        raise ProbeFailure("camera_keyframe_interval_unsupported")
    track = {
        "track_id": "video-main",
        "kind": "video",
        "direction": "producer_to_viewers",
        "codec": "h265",
        "codec_profile": "main",
        "bit_depth": 8,
        "chroma": "420",
        "clock_rate": 90000,
        "payload_type": 96,
        "width": int(width),
        "height": int(height),
        "fps": int(fps),
        "feedback": {"pli": False, "fir": False},
        "max_b_frames": 0,
        "keyframe_interval_ms": int(milliseconds),
        "repeat_parameter_sets": True,
        "packetization": "rfc7798",
        "profile_space": 0,
        "profile_id": 1,
        "tier_flag": 0,
        "level_id": next(iter(levels)),
        "sprop_max_don_diff": 0,
    }
    if not TRACK.is_valid(track):
        raise ProbeFailure("camera_native_format_outside_contract")
    vcl = b"".join(unit for unit in units if (unit[0] >> 1) & 63 < 32)
    return NativeProbe(
        track,
        checked_at_ms,
        checked_at_ms + 55000,
        len(picture_types),
        hashlib.sha256(encoded).hexdigest(),
        hashlib.sha256(vcl).hexdigest(),
        next(unit for unit in units if (unit[0] >> 1) & 63 == 33),
        {kind: next(unit for unit in units if (unit[0] >> 1) & 63 == kind) for kind in (32, 33, 34)},
    )


def probe_native(url: str, ffmpeg: str, *, timeout: float = 15.0) -> NativeProbe:
    """One original deadline covers RTSP copy and the independent host decode."""
    checked_at_ms = int(time.time() * 1000)
    deadline = time.monotonic() + timeout
    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    try:
        copied = subprocess.run(
            [
                ffmpeg,
                "-hide_banner",
                "-loglevel",
                "info",
                "-rtsp_transport",
                "tcp",
                "-i",
                url,
                "-map",
                "0:v:0",
                "-an",
                "-frames:v",
                "50",
                "-c:v",
                "copy",
                "-bsf:v",
                "trace_headers",
                "-f",
                "hevc",
                "pipe:1",
            ],
            capture_output=True,
            timeout=max(0.001, deadline - time.monotonic()),
            creationflags=flags,
        )
        if copied.returncode or not 0 < len(copied.stdout) <= 8 * 1024 * 1024:
            raise ProbeFailure("camera_encoded_probe_failed")
        decoded = subprocess.run(
            [
                ffmpeg,
                "-hide_banner",
                "-loglevel",
                "info",
                "-f",
                "hevc",
                "-i",
                "pipe:0",
                "-map",
                "0:v:0",
                "-an",
                "-vf",
                "showinfo",
                "-f",
                "null",
                "-",
            ],
            input=copied.stdout,
            capture_output=True,
            timeout=max(0.001, deadline - time.monotonic()),
            creationflags=flags,
        )
        if decoded.returncode:
            raise ProbeFailure("camera_host_decode_failed")
    except subprocess.TimeoutExpired, OSError:
        raise ProbeFailure("camera_probe_unavailable") from None
    return inspect_capture(
        copied.stdout,
        copied.stderr.decode("utf-8", errors="replace"),
        decoded.stderr.decode("utf-8", errors="replace"),
        checked_at_ms=checked_at_ms,
    )
