"""Packet-level acceptance monitor checks; no camera/network side effects."""

import struct
import unittest

from native_camera_rtp import NativeRtpMonitor

PARAMETERS = {kind: bytes([kind << 1, 1, 8, kind]) for kind in (32, 33, 34)}
IDR = bytes([19 << 1, 1, 128, 7])


def packet(payload, sequence=0, timestamp=0, *, marker=False, ssrc=1, flags=0):
    return struct.pack("!BBHII", 128 | flags, 96 | (128 if marker else 0), sequence, timestamp, ssrc) + payload


class RtpTest(unittest.TestCase):
    def seed(self, monitor, *, first=0):
        for offset, unit in enumerate(PARAMETERS.values()):
            self.assertTrue(monitor.accept(packet(unit, (first + offset) & 65535)))

    def test_ready_requires_parameters_and_complete_random_access(self):
        monitor = NativeRtpMonitor(PARAMETERS)
        self.assertTrue(monitor.accept(packet(IDR, marker=True)))
        self.assertFalse(monitor.ready)
        self.seed(monitor, first=1)
        self.assertFalse(monitor.ready)
        self.assertTrue(monitor.accept(packet(IDR, 4, 1, marker=True)))
        self.assertTrue(monitor.ready)
        self.assertEqual(monitor.packets, 5)

    def test_aggregation_and_input_are_unchanged(self):
        monitor = NativeRtpMonitor(PARAMETERS, monotonic=lambda: 12)
        units = [*PARAMETERS.values(), IDR]
        payload = bytes([48 << 1, 1]) + b"".join(struct.pack("!H", len(unit)) + unit for unit in units)
        raw = packet(payload, marker=True)
        original = bytes(raw)
        self.assertTrue(monitor.accept(raw))
        self.assertTrue(monitor.ready)
        self.assertEqual(raw, original)
        self.assertEqual(monitor.last_parameters_at, 12)

    def test_fragmented_parameter_and_idr(self):
        monitor = NativeRtpMonitor(PARAMETERS)
        sequence = 0
        for kind, unit in PARAMETERS.items():
            self.assertTrue(monitor.accept(packet(bytes([98, 1, 128 | kind]) + unit[2:3], sequence)))
            self.assertTrue(monitor.accept(packet(bytes([98, 1, 64 | kind]) + unit[3:], sequence + 1)))
            sequence += 2
        self.assertTrue(monitor.accept(packet(bytes([98, 1, 128 | 19, 128]), sequence)))
        self.assertFalse(monitor.ready)
        self.assertTrue(monitor.accept(packet(bytes([98, 1, 64 | 19, 7]), sequence + 1, marker=True)))
        self.assertTrue(monitor.ready)

    def test_gap_duplicate_wrap_and_recovery(self):
        monitor = NativeRtpMonitor(PARAMETERS)
        self.seed(monitor, first=65534)
        self.assertFalse(monitor.accept(packet(PARAMETERS[34], 0)))
        self.assertEqual(monitor.duplicates, 1)
        self.assertTrue(monitor.accept(packet(IDR, 2, 1, marker=True)))
        self.assertEqual(monitor.gaps, 1)
        self.assertFalse(monitor.ready)
        self.assertTrue(monitor.accept(packet(IDR, 3, 2, marker=True)))
        self.assertTrue(monitor.ready)

    def test_missing_or_overwritten_fragment_cannot_be_ready(self):
        for broken in ("missing_start", "second_start", "marker_before_end", "interrupted"):
            with self.subTest(broken=broken):
                monitor = NativeRtpMonitor(PARAMETERS)
                self.seed(monitor)
                if broken != "missing_start":
                    monitor.accept(packet(bytes([98, 1, 147, 128]), 3, 1))
                if broken == "missing_start":
                    monitor.accept(packet(bytes([98, 1, 83, 7]), 3, 1, marker=True))
                elif broken == "second_start":
                    monitor.accept(packet(bytes([98, 1, 147, 128]), 4, 1))
                    monitor.accept(packet(bytes([98, 1, 83, 7]), 5, 1, marker=True))
                elif broken == "marker_before_end":
                    monitor.accept(packet(bytes([98, 1, 19, 7]), 4, 1, marker=True))
                else:
                    monitor.accept(packet(IDR, 4, 1, marker=True))
                self.assertFalse(monitor.ready)

    def test_parameter_change_and_ssrc_change_are_sticky(self):
        for raw, reason in (
            (packet(PARAMETERS[33] + b"x", 3), "camera_format_changed"),
            (packet(IDR, 3, ssrc=2), "camera_ssrc_changed"),
        ):
            monitor = NativeRtpMonitor(PARAMETERS)
            self.seed(monitor)
            self.assertFalse(monitor.accept(raw))
            self.assertEqual(monitor.failure, reason)
            self.assertFalse(monitor.accept(packet(IDR, 4, 1, marker=True)))

    def test_reject_malformed_headers_and_packetizations(self):
        payloads = [
            b"\x60\x01",
            b"\x60\x01\x00\x02\x40\x01",
            b"\x60\x01\x00\x08\x40\x01",
            b"\x62\x01\xd3\x80",
            b"\x64\x01\x00",
            b"\xa6\x01\x80",
            b"\x26\x00\x80",
            b"\x26\x09\x80",
            b"\x27\x01\x80",
        ]
        for payload in payloads:
            with self.subTest(payload=payload.hex()):
                monitor = NativeRtpMonitor(PARAMETERS)
                self.assertFalse(monitor.accept(packet(payload)))
                self.assertIsNotNone(monitor.failure)
        for raw in (
            b"x",
            packet(IDR, flags=16),
            packet(IDR, flags=32) + b"\x00",
            packet(IDR, flags=15),
            packet(IDR) + b"x" * 1500,
        ):
            self.assertFalse(NativeRtpMonitor(PARAMETERS).accept(raw))

    def test_valid_extension_csrc_and_padding(self):
        raw = packet(b"\x00" * 4 + b"\xbe\xde\x00\x01" + b"\x00" * 4 + IDR + b"\x00\x02", flags=49)
        self.assertTrue(NativeRtpMonitor(PARAMETERS).accept(raw))

    def test_only_parameter_sets_use_bounded_assembly(self):
        monitor = NativeRtpMonitor(PARAMETERS)
        monitor.accept(packet(bytes([98, 1, 160]) + b"x" * 1000))
        for sequence in range(1, 8):
            self.assertTrue(monitor.accept(packet(bytes([98, 1, 32]) + b"x" * 1000, sequence)))
        self.assertFalse(monitor.accept(packet(bytes([98, 1, 32]) + b"x" * 1000, 8)))
        self.assertEqual(monitor.failure, "parameter_set_resource_limit")
        monitor = NativeRtpMonitor(PARAMETERS)
        monitor.accept(packet(bytes([98, 1, 147, 128])))
        for sequence in range(1, 20):
            self.assertTrue(monitor.accept(packet(bytes([98, 1, 19]) + b"x" * 1000, sequence)))
        assert monitor.fragment is not None
        self.assertIsNone(monitor.fragment[1])

    def test_require_all_three_parameter_sets(self):
        with self.assertRaises(ValueError):
            NativeRtpMonitor({33: PARAMETERS[33]})


if __name__ == "__main__":
    unittest.main()
