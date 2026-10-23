"""Bounded RFC7798 SRST/DONL0 monitor; forwarded RTP bytes stay unchanged.

Reference: https://www.rfc-editor.org/rfc/rfc7798.html#section-4.4
Only parameter sets are assembled; coded video frames are never buffered here.
"""

from __future__ import annotations

import time
from collections.abc import Callable


class NativeRtpMonitor:
    def __init__(self, expected: dict[int, bytes], *, monotonic: Callable = time.monotonic):
        if set(expected) != {32, 33, 34}:
            raise ValueError("complete_parameter_sets_required")
        self.expected, self.monotonic = dict(expected), monotonic
        self.seen: set[int] = set()
        self.ready = False
        self.failure: str | None = None
        self.last_parameters_at: float | None = None
        self.packets = self.gaps = self.duplicates = 0
        self.ssrc: int | None = None
        self.sequence: int | None = None
        self.timestamp: int | None = None
        self.damaged = self.idr = self.complete = False
        self.fragment: tuple[int, bytearray | None] | None = None

    def reject(self, reason: str) -> bool:
        self.failure = reason
        return False

    def nal(self, unit: bytes) -> bool:
        if len(unit) < 2 or unit[0] & 0x81 or not unit[1] & 7 or unit[1] & 0xF8:
            return self.reject("unsupported_hevc_header")
        kind = (unit[0] >> 1) & 63
        if kind >= 48:
            return self.reject("unsupported_hevc_packetization")
        if kind in self.expected:
            if unit != self.expected[kind]:
                return self.reject("camera_format_changed")
            self.seen.add(kind)
            if self.seen == set(self.expected):
                self.last_parameters_at = self.monotonic()
        if kind in (19, 20, 21) and len(unit) > 2 and unit[2] & 128:
            self.idr = self.complete = True
        return True

    def accept(self, packet: bytes) -> bool:
        if self.failure:
            return False
        if not 14 <= len(packet) <= 1472 or packet[0] >> 6 != 2 or packet[1] & 127 != 96:
            return self.reject("unsupported_rtp_packet")
        end = len(packet)
        offset = 12 + 4 * (packet[0] & 15)
        if packet[0] & 16:
            if offset + 4 > end:
                return self.reject("malformed_rtp_extension")
            offset += 4 + 4 * int.from_bytes(packet[offset + 2 : offset + 4], "big")
        if packet[0] & 32:
            padding = packet[-1]
            if not padding or padding > end - offset:
                return self.reject("malformed_rtp_padding")
            end -= padding
        if offset + 2 > end:
            return self.reject("malformed_rtp_payload")
        ssrc = int.from_bytes(packet[8:12], "big")
        if self.ssrc is not None and self.ssrc != ssrc:
            return self.reject("camera_ssrc_changed")
        self.ssrc = ssrc
        sequence = int.from_bytes(packet[2:4], "big")
        gap = 0 if self.sequence is None else (sequence - self.sequence - 1) & 65535
        if self.sequence is not None and gap >= 32768:
            self.duplicates += 1
            return False
        timestamp = int.from_bytes(packet[4:8], "big")
        if timestamp != self.timestamp:
            self.timestamp = timestamp
            self.damaged = self.idr = self.complete = False
            self.fragment = None
        if gap:
            self.gaps += gap
            self.damaged = True
        self.sequence = sequence
        payload = packet[offset:end]
        kind = (payload[0] >> 1) & 63
        if payload[0] & 0x81 or not payload[1] & 7 or payload[1] & 0xF8:
            return self.reject("unsupported_hevc_header")
        if kind < 48:
            if self.fragment is not None:
                self.damaged = True
                self.fragment = None
            if not self.nal(payload):
                return False
        elif kind == 48:
            if self.fragment is not None:
                self.damaged = True
                self.fragment = None
            position, count = 2, 0
            while position + 2 <= len(payload):
                size = int.from_bytes(payload[position : position + 2], "big")
                position += 2
                if size < 2 or position + size > len(payload):
                    return self.reject("malformed_hevc_aggregation")
                if not self.nal(payload[position : position + size]):
                    return False
                position += size
                count += 1
            if position != len(payload) or count < 2:
                return self.reject("malformed_hevc_aggregation")
        elif kind == 49:
            if len(payload) < 4 or payload[2] & 0xC0 == 0xC0:
                return self.reject("malformed_hevc_fragment")
            original = payload[2] & 63
            if original >= 48:
                return self.reject("unsupported_hevc_packetization")
            if payload[2] & 128:
                if self.fragment is not None:
                    self.damaged = True
                parameter = (
                    bytearray(bytes([(payload[0] & 0x81) | original << 1, payload[1]]))
                    if original in self.expected
                    else None
                )
                self.fragment = (original, parameter)
                if original in (19, 20, 21) and payload[3] & 128:
                    self.idr = True
            if self.fragment is None or self.fragment[0] != original:
                self.damaged = True
            else:
                data = self.fragment[1]
                if data is not None:
                    if len(data) + len(payload) - 3 > 8192:
                        return self.reject("parameter_set_resource_limit")
                    data.extend(payload[3:])
                if payload[2] & 64:
                    if data is not None and not self.damaged and not self.nal(bytes(data)):
                        return False
                    self.complete = self.idr
                    self.fragment = None
        else:
            return self.reject("unsupported_hevc_packetization")
        if (
            packet[1] & 128
            and self.fragment is None
            and self.idr
            and self.complete
            and not self.damaged
            and self.seen == set(self.expected)
        ):
            self.ready = True
        self.packets += 1
        return True
