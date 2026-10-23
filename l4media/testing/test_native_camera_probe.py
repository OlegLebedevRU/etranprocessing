"""Offline checks use real parameter sets and synthetic decoder observations."""

import json
import subprocess
import unittest
from pathlib import Path
from unittest.mock import patch

from native_camera_probe import ProbeFailure, inspect_capture, probe_native

PARAMETERS = json.loads((Path(__file__).parent / "fixtures/native_hevc_parameters.json").read_text())["parameter_sets"]
ENCODED = (
    b"".join(b"\0\0\0\1" + bytes.fromhex(nal) for _ in range(3) for nal in PARAMETERS.values())
    + b"\0\0\0\1\x26\x01\x80"
)
HEADERS = "Video: hevc (Main), yuv420p, 2592x1944, 20 fps\n" + "\n".join(
    f"{name} = {value}"
    for name, value in {
        "general_profile_space": 0,
        "general_tier_flag": 0,
        "general_profile_idc": 1,
        "general_level_idc": 153,
        "chroma_format_idc": 1,
        "bit_depth_luma_minus8": 0,
        "bit_depth_chroma_minus8": 0,
        "sps_max_num_reorder_pics": 0,
    }.items()
)
PICTURES = "\n".join(f"s:2592x1944 type:{'I' if index % 20 == 0 else 'P'}" for index in range(50))


class NativeProbeTests(unittest.TestCase):
    def test_native_level_raster_and_sps_are_preserved(self):
        probe = inspect_capture(ENCODED, HEADERS, PICTURES, checked_at_ms=1000)
        self.assertEqual(
            (probe.track["width"], probe.track["height"], probe.track["fps"], probe.track["level_id"]),
            (2592, 1944, 20, 153),
        )
        self.assertEqual(probe.track["keyframe_interval_ms"], 1000)
        self.assertEqual(probe.sps.hex(), PARAMETERS["33"])
        self.assertEqual(probe.valid_until_ms, 56000)

    def test_unsupported_and_unknown_evidence_never_becomes_ready(self):
        for headers, pictures in (
            (HEADERS.replace("general_profile_idc = 1", "general_profile_idc = 2"), PICTURES),
            (HEADERS.replace("sps_max_num_reorder_pics = 0", "sps_max_num_reorder_pics = 1"), PICTURES),
            (HEADERS.replace("general_level_idc = 153", "general_level_idc = 0"), PICTURES),
            (HEADERS.replace("20 fps", "0 fps"), PICTURES),
            (HEADERS.replace("20 fps", "25 fps"), PICTURES),
            (HEADERS, PICTURES.replace("type:P", "type:B", 1)),
            (HEADERS, PICTURES.replace("s:2592x1944", "s:1920x1080")),
            (HEADERS, ""),
        ):
            with self.subTest(headers=headers, pictures=pictures[:10]), self.assertRaises(ProbeFailure):
                inspect_capture(ENCODED, headers, pictures, checked_at_ms=1000)

    def test_unstable_parameter_sets_are_rejected(self):
        altered = ENCODED + b"\0\0\0\1" + bytes.fromhex(PARAMETERS["33"]) + b"\xff"
        with self.assertRaisesRegex(ProbeFailure, "parameter_sets_unstable"):
            inspect_capture(altered, HEADERS, PICTURES, checked_at_ms=1000)

    def test_live_copy_has_no_level_filter_or_encoder_and_deadline_is_shared(self):
        copied = subprocess.CompletedProcess([], 0, ENCODED, HEADERS.encode())
        decoded = subprocess.CompletedProcess([], 0, b"", PICTURES.encode())
        with patch("native_camera_probe.subprocess.run", side_effect=[copied, decoded]) as run:
            probe = probe_native("rtsp://example.invalid/camera", "ffmpeg")
        first, second = run.call_args_list
        command = first.args[0]
        self.assertEqual(command[command.index("-c:v") + 1], "copy")
        self.assertEqual(command[command.index("-bsf:v") + 1], "trace_headers")
        self.assertNotIn("hevc_metadata", " ".join(command))
        self.assertEqual(second.kwargs["input"], ENCODED)
        self.assertLessEqual(second.kwargs["timeout"], first.kwargs["timeout"])
        self.assertEqual(probe.track["level_id"], 153)

    def test_timeout_never_reflects_authenticated_url(self):
        secret_url = "rtsp://synthetic-user:synthetic-private@example.invalid/camera"
        with patch("native_camera_probe.subprocess.run", side_effect=subprocess.TimeoutExpired([secret_url], 1)):
            with self.assertRaises(ProbeFailure) as raised:
                probe_native(secret_url, "ffmpeg")
        self.assertEqual(str(raised.exception), "camera_probe_unavailable")
        self.assertNotIn("synthetic-private", str(raised.exception))


if __name__ == "__main__":
    unittest.main()
