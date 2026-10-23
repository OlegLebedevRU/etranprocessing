"""Production relay threads with loopback sockets; fake FFmpeg, no credentials."""

import socket
import time
import unittest
from unittest.mock import patch

from native_camera_relay import NativeRelay
from test_native_camera_rtp import IDR, PARAMETERS, packet


class FakeProcess:
    returncode = None

    def poll(self):
        return self.returncode

    def terminate(self):
        self.returncode = 0

    def wait(self, **_):
        return self.returncode


class RelayTest(unittest.TestCase):
    def setUp(self):
        self.sinks = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(2)]
        self.source = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.source.bind(("127.0.0.1", 0))
        self.source.settimeout(2)
        for sock in self.sinks:
            sock.bind(("127.0.0.1", 0))
            sock.settimeout(2)
        self.relay = NativeRelay(
            "rtsp://localhost/test",
            "ffmpeg",
            PARAMETERS,
            rtp_port=self.sinks[0].getsockname()[1],
            rtcp_port=self.sinks[1].getsockname()[1],
        )
        self.spawn = patch("native_camera_relay.subprocess.Popen", return_value=FakeProcess()).start()
        self.relay.start()

    def tearDown(self):
        self.relay.stop()
        self.source.close()
        for sock in self.sinks:
            sock.close()
        patch.stopall()

    def test_rtp_is_byte_exact_and_copy_has_no_metadata_filters(self):
        for sequence, payload in enumerate([*PARAMETERS.values(), IDR]):
            raw = packet(payload, sequence, marker=sequence == 3)
            self.source.sendto(raw, self.relay.sockets[0].getsockname())
            self.assertEqual(self.sinks[0].recv(2000), raw)
        self.assertTrue(self.relay.monitor.ready)
        self.assertIsNone(self.relay.health())
        command = self.spawn.call_args.args[0]
        self.assertEqual(command[command.index("-c:v") + 1], "copy")
        self.assertNotIn("-bsf:v", command)
        self.assertNotIn("-vf", command)

    def test_rtcp_feedback_uses_learned_local_source(self):
        self.source.sendto(b"rtcp-up", self.relay.sockets[1].getsockname())
        raw, sender = self.sinks[1].recvfrom(2000)
        self.assertEqual(raw, b"rtcp-up")
        self.sinks[1].sendto(b"rtcp-down", sender)
        self.assertEqual(self.source.recv(2000), b"rtcp-down")

    def test_changed_format_fails_without_forwarding_changed_packet(self):
        self.source.sendto(packet(PARAMETERS[33] + b"x"), self.relay.sockets[0].getsockname())
        deadline = time.monotonic() + 2
        while self.relay.failure is None and time.monotonic() < deadline:
            time.sleep(0.01)
        self.assertEqual(self.relay.health(), "camera_format_changed")
        self.assertEqual(self.relay.forwarded, 0)

    def test_cleanup_is_idempotent_and_generation_cannot_restart(self):
        self.relay.stop()
        self.relay.stop()
        self.assertTrue(all(not thread.is_alive() for thread in self.relay.threads))
        self.assertTrue(all(sock.fileno() == -1 for sock in self.relay.sockets))
        with self.assertRaises(ValueError):
            self.relay.start()


if __name__ == "__main__":
    unittest.main()
