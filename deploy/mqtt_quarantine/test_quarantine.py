import sqlite3
import unittest
from unittest.mock import patch

import collector
import edge


class ObservationTest(unittest.TestCase):
    def test_correlates_multiline_error_once_and_filters_other_ports(self):
        collector.events.clear()
        collector.pending.clear()
        with patch.object(collector.time, "time", return_value=1795510800):
            prefix = "2026-11-24T10:00:00.000000000Z [error] <0.33.0> "
            collector.consume(
                prefix
                + "User 'test-sn' failed to authenticate: user 'test-sn' - invalid credentials"
            )
            collector.consume(prefix + "MQTT connection failed: access refused for user 'test-sn'")
            collector.consume(
                prefix
                + "Rejected MQTT connection 192.0.2.10:5441 -> 127.0.0.1:8883 with Connect Reason Code"
            )
            collector.consume(prefix + "Rejected MQTT connection 192.0.2.10:5441 -> 127.0.0.1:1883")
            self.assertEqual(len(collector.events), 1)
            key, value = next(iter(collector.events.items()))
            self.assertEqual(key[1:], ("192.0.2.10", "test-sn", "credentials"))
            self.assertEqual(value[0], 1)

    def test_preview_changes_when_neighbor_appears(self):
        obs = dict(
            rows=[dict(ip="192.0.2.1", login="sn", last=1000)],
            active={},
            success=[],
            sample_at=990,
            at=999,
            live=True,
            incomplete=False,
            history_since=0,
        )
        a = edge.preview("192.0.2.1", obs, 1000)
        obs["active"] = {"192.0.2.1": ["another"]}
        b = edge.preview("192.0.2.1", obs, 1000)
        self.assertTrue(a["fresh"])
        self.assertNotEqual(a["token"], b["token"])


class DecisionTest(unittest.TestCase):
    def test_newer_release_fences_replayed_block(self):
        db = sqlite3.connect(":memory:")
        calls = []
        with patch.object(edge, "apply", side_effect=lambda ip, expiry: calls.append((ip, expiry))):
            executor = edge.Executor(db)
            executor.command(dict(ip="192.0.2.1", revision=3, expiry=0))
            with self.assertRaises(edge.Conflict):
                executor.command(dict(ip="192.0.2.1", revision=2, expiry=0))
            self.assertEqual(calls, [("192.0.2.1", 0)])
        db.close()


if __name__ == "__main__":
    unittest.main()
