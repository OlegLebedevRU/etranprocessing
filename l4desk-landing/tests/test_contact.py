import importlib.util
import pathlib
import unittest
import uuid
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("contact_server", pathlib.Path(__file__).parents[1] / "contact/server.py")
server = importlib.util.module_from_spec(spec)
spec.loader.exec_module(server)


class ContactTests(unittest.TestCase):
    def setUp(self):
        self.relay = server.Relay()
        self.relay.secret = "test-only"
        self.relay.gateway = "https://example.com"
        self.relay.owner = "owner@example.com"
        self.data = {"name": "Visitor", "email": "visitor@example.com", "message": "Discuss integration", "topic": "AI-агент и L4MCP", "smart-token": "test-only", "request_id": str(uuid.uuid4()), "consent": True}

    def test_captcha_failure_never_sends(self):
        with patch.object(server, "remote_json", return_value={"status": "failed"}), patch.object(self.relay, "send") as send:
            with self.assertRaises(server.ContactError) as raised:
                self.relay.submit(self.data, "192.0.2.1")
            self.assertEqual(raised.exception.status, 400)
            send.assert_not_called()

    def test_captcha_outage_fails_closed(self):
        with patch.object(server, "remote_json", side_effect=TimeoutError), patch.object(self.relay, "send") as send:
            with self.assertRaises(server.ContactError) as raised:
                self.relay.submit(self.data, "192.0.2.1")
            self.assertEqual(raised.exception.status, 503)
            send.assert_not_called()

    def test_recipient_cannot_override_owner_and_retry_does_not_duplicate(self):
        self.data["recipients"] = ["attacker@example.com"]
        with patch.object(self.relay, "captcha") as captcha, patch.object(self.relay, "send") as send:
            result = self.relay.submit(self.data, "192.0.2.1")
            self.assertEqual(result, self.relay.submit(self.data, "192.0.2.1"))
            self.assertEqual(send.call_count, 2)
            self.assertEqual(send.call_args_list[0].args[0], ["owner@example.com"])
            self.assertEqual(send.call_args_list[1].args[0], ["visitor@example.com"])
            self.assertNotIn("Discuss integration", send.call_args_list[1].args[2])
            captcha.assert_called_once()

    def test_copy_failure_does_not_repeat_owner_email(self):
        with patch.object(self.relay, "captcha"), patch.object(self.relay, "send", side_effect=[None, TimeoutError]) as send:
            result = self.relay.submit(self.data, "192.0.2.1")
            self.assertTrue(result["ok"])
            self.assertFalse(result["copy_sent"])
            self.assertEqual(result, self.relay.submit(self.data, "192.0.2.1"))
            self.assertEqual(send.call_count, 2)

    def test_other_ip_cannot_reuse_request_id(self):
        with patch.object(self.relay, "captcha"), patch.object(self.relay, "send"):
            self.relay.submit(self.data, "192.0.2.1")
            with self.assertRaises(server.ContactError) as raised:
                self.relay.submit(self.data, "192.0.2.2")
            self.assertEqual(raised.exception.status, 409)

    def test_ip_limit_before_provider_call(self):
        with patch.object(self.relay, "captcha"), patch.object(self.relay, "send") as send:
            for _ in range(3):
                self.data["request_id"] = str(uuid.uuid4())
                self.relay.submit(self.data, "192.0.2.1")
            self.data["request_id"] = str(uuid.uuid4())
            with self.assertRaises(server.ContactError) as raised:
                self.relay.submit(self.data, "192.0.2.1")
            self.assertEqual(raised.exception.status, 429)
            self.assertEqual(send.call_count, 6)

    def test_invalid_and_oversize_data_rejected(self):
        for field, value in (("email", "x@example.com\r\nBcc: other@example.com"), ("message", "x" * 2001), ("consent", False), ("smart-token", ""), ("topic", "unknown")):
            with self.subTest(field=field), self.assertRaises(server.ContactError):
                server.validate_form({**self.data, field: value})

    def test_provider_response_must_confirm_acceptance(self):
        with patch.object(server, "remote_json", return_value={"status": "queued"}):
            with self.assertRaises(ValueError):
                self.relay.send([self.relay.owner], "test", "test")

    def test_same_owner_email_sends_once(self):
        self.data["email"] = self.relay.owner
        with patch.object(self.relay, "captcha"), patch.object(self.relay, "send") as send:
            self.relay.submit(self.data, "192.0.2.1")
            send.assert_called_once()


if __name__ == "__main__":
    unittest.main()
