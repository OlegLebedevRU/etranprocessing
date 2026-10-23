"""Offline native producer lifecycle and authority checks; no network/hardware."""

import unittest
from typing import Any
from uuid import uuid4

from native_camera_control import VALIDATORS, NativeControl


class NativeControlTests(unittest.TestCase):
    def setUp(self):
        self.wall, self.mono = 1000000, 100.0
        self.starts, self.stops = [], []
        self.fresh = True
        self.control = NativeControl(
            "synthetic-native-test",
            lambda c: self.starts.append(c),
            lambda: self.stops.append(True),
            lambda: self.fresh,
            wall_ms=lambda: self.wall,
            monotonic=lambda: self.mono,
        )

    def handle(self, command):
        reply = self.control.handle(command)
        assert reply is not None
        return reply

    def active(self):
        current = self.control.active
        assert current is not None
        return current

    def command(self, kind: str = "stream_start", **changes: Any):
        value = {
            "v": 1,
            "type": kind,
            "sn": "synthetic-native-test",
            "command_id": str(uuid4()),
            "issued_at_ms": self.wall,
            "expires_at_ms": self.wall + 30000,
        }
        if kind != "inventory_get":
            value.update(lease_id=str(uuid4()), stream_instance_id=str(uuid4()))
        if kind == "stream_start":
            value.update(mode="usb-camera", source_id="cam:ip000001", profile="default")
        value.update(changes)
        return value

    def test_duplicate_is_not_another_start_and_conflicting_body_is_rejected(self):
        command = self.command()
        first = self.handle(command)
        self.assertEqual(first["result"], "started")
        self.assertEqual(self.handle(command), first)
        upper = {**command, "command_id": command["command_id"].upper()}
        self.assertEqual(self.handle(upper), first)
        self.assertEqual(len(self.starts), 1)
        changed = {**command, "expires_at_ms": command["expires_at_ms"] + 1000}
        self.assertEqual(self.handle(changed)["code"], "invalid_payload")
        self.assertEqual(len(self.starts), 1)

    def test_expired_wrong_source_unknown_profile_and_stale_probe_do_not_start(self):
        invalid: tuple[dict[str, Any], ...] = (
            {"expires_at_ms": self.wall},
            {"source_id": "disp:other"},
            {"profile": "other"},
            {"mode": "desktop"},
        )
        for changes in invalid:
            reply = self.handle(self.command(**changes))
            self.assertEqual(reply["type"], "nack")
        self.fresh = False
        self.assertEqual(self.handle(self.command())["type"], "nack")
        self.assertEqual(self.starts, [])

    def test_original_deadline_includes_media_startup(self):
        def slow_start(command):
            self.starts.append(command)
            self.wall += 5000
            self.mono += 5

        self.control.start_media = slow_start
        self.handle(self.command())
        self.assertEqual(self.active().monotonic_deadline, 130)
        self.mono = 130
        self.assertIsNotNone(self.control.expire())
        self.assertEqual(len(self.stops), 1)
        self.assertIsNone(self.control.expire())

    def test_startup_after_deadline_is_cleaned_without_success(self):
        def slow_start(command):
            self.wall += 35000
            self.mono += 35

        self.control.start_media = slow_start
        self.assertEqual(self.handle(self.command())["code"], "expired")
        self.assertIsNone(self.control.active)
        self.assertEqual(len(self.stops), 1)

    def test_foreign_stop_and_late_failure_do_not_touch_new_epoch(self):
        old = self.command()
        self.handle(old)
        foreign = self.command("stream_stop")
        self.assertEqual(self.handle(foreign)["code"], "lease_mismatch")
        self.assertEqual(self.stops, [])
        self.assertTrue(self.control.fail(old["stream_instance_id"]))
        new = self.command()
        self.handle(new)
        self.assertFalse(self.control.fail(old["stream_instance_id"]))
        self.assertEqual(self.active().stream, new["stream_instance_id"])
        self.assertEqual(len(self.starts), 2)

    def test_renew_cannot_shorten_deadline_and_wall_clock_cannot_extend_expiry(self):
        first = self.command()
        self.handle(first)
        renew = self.command(
            "lease_renew",
            lease_id=first["lease_id"],
            stream_instance_id=first["stream_instance_id"],
            expires_at_ms=self.wall + 10000,
            ttl_sec=60,
        )
        self.assertEqual(self.handle(renew)["result"], "renewed")
        self.assertEqual(self.active().monotonic_deadline, 130)
        self.wall -= 3600000
        self.mono = 130
        self.assertIsNotNone(self.control.expire())
        self.assertIsNone(self.control.active)

    def test_inventory_is_honest_test_camera_without_desktop_hardware_claim(self):
        reply = self.handle(self.command("inventory_get"))
        self.assertTrue(VALIDATORS["ack"].is_valid(reply))
        self.assertEqual(reply["inventory"]["displays"], [])
        self.assertIn("TEST", reply["inventory"]["cameras"][0]["name"])

    def test_malformed_input_is_not_reflected_and_failed_start_is_not_replayed(self):
        bad = self.command(password="synthetic-private")
        self.assertIsNone(self.control.handle(bad))
        self.assertEqual(self.starts, [])
        command = self.command()

        def fail(command):
            raise RuntimeError("synthetic-private-url")

        self.control.start_media = fail
        first = self.handle(command)
        self.assertEqual(first["code"], "inject_failed")
        self.assertNotIn("synthetic-private", str(first))
        self.assertEqual(self.handle(command), first)
        self.assertEqual(len(self.stops), 1)


if __name__ == "__main__":
    unittest.main()
