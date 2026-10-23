"""Test-only publisher payload/identity checks; no live MQTT connection."""

import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock

import paho.mqtt.client as mqtt
from native_camera_probe import NativeProbe
from native_camera_source import LOCAL, SNAPSHOT, Source, decode_command, gauge


class SourceTest(unittest.TestCase):
    def test_command_bounds_and_duplicate_keys(self):
        for raw in (b"[]", b'{"v":1,"v":2}', b"x" * 8193, b'{"a":' * 13 + b"{}" + b"}" * 13):
            with self.assertRaises(ValueError):
                decode_command(raw)
        self.assertEqual(decode_command(b'{"v":1}'), {"v": 1})

    def test_gauge_never_claims_writers_port_verification_or_hardware(self):
        payload = gauge(None, False)["300"][0]["450"]
        camera = payload["cameras"][0]
        self.assertIn("no-port2-proof", payload["reason"])
        self.assertEqual(camera["capabilities"]["methods"], [])
        self.assertEqual(camera["discovery"]["candidates"], [])
        self.assertEqual(camera["discovery"]["state"], "idle")
        self.assertEqual(camera["auth_source"], "none")
        self.assertFalse(camera["has_credentials"])
        self.assertIsNone(camera["actual_track"])
        local = {key: value for key, value in camera.items() if key in LOCAL["$defs"]["camera_snapshot"]["properties"]}
        local.update(
            v=1,
            hardware_id="test",
            contract_revision=camera["camera_contract_revision"],
            snapshot_id=payload["snapshot_id"],
        )
        SNAPSHOT.validate(local)

    def test_main_app_presence_is_plain_and_only_ctl_is_subscribed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            private = root / "private.json"
            private.write_text(
                json.dumps({"url": "rtsp://localhost/test", "username": "test-only", "password": "placeholder"})
            )
            args = SimpleNamespace(camera_config=private, state_dir=root, sn="synthetic-native-test")
            source = Source(args)
            self.assertEqual(source.client._client_id, b"main_app")
            self.assertEqual(source.client._protocol, mqtt.MQTTv5)
            self.assertEqual(source.client._will_payload, b"app_offline")
            self.assertEqual(source.client._will_topic, b"dev/synthetic-native-test/app")
            client = Mock()
            source.client = client
            source.connected(client, None, None, SimpleNamespace(is_failure=False), None)
            client.subscribe.assert_called_once_with("srv/synthetic-native-test/ctl", qos=1)
            calls = client.publish.call_args_list
            self.assertEqual(calls[0].args, ("dev/synthetic-native-test/app", "app_online"))
            self.assertTrue(calls[0].kwargs["retain"])
            control = json.loads(calls[1].args[1])
            self.assertIn("TEST", control["agent"])
            self.assertFalse(control["desktop_available"])
            gauge_call = calls[2]
            self.assertFalse(gauge_call.kwargs["retain"])
            self.assertIn(("event_type_code", "84"), gauge_call.kwargs["properties"].UserProperty)
            self.assertNotIn("placeholder", json.dumps([call.args for call in calls]))

    def test_gauge_does_not_relabel_stale_track_as_fresh(self):
        source = object.__new__(Source)
        source.probe = NativeProbe({}, 1, 2, 50, "a", "b", b"sps", {})
        source.relay = None
        self.assertFalse(source.fresh())
        self.assertIsNone(gauge(source.probe, source.fresh())["300"][0]["450"]["cameras"][0]["actual_track"])


if __name__ == "__main__":
    unittest.main()
