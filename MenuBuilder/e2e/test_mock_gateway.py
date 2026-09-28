"""Contract checks for the isolated payment and email simulator."""

from __future__ import annotations

import base64
import json
import os
import secrets
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

SCRIPT = Path(__file__).with_name("mock_gateway.py")


class MockGatewayTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.cleanup_temp)
        self.shop_id = f"e2e_{secrets.token_hex(5)}"
        self.shop_secret = secrets.token_urlsafe(24)
        self.control_token = secrets.token_urlsafe(24)
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            self.port = probe.getsockname()[1]
        self.base_url = f"http://127.0.0.1:{self.port}"
        self.start_server()
        self.addCleanup(self.stop_server)

    def cleanup_temp(self) -> None:
        for attempt in range(10):
            try:
                self.temp.cleanup()
                return
            except PermissionError:
                if attempt == 9:
                    raise
                time.sleep(0.1)

    def start_server(self) -> None:
        environment = os.environ.copy()
        environment.update(
            {
                "E2E_MOCK_DB_PATH": str(Path(self.temp.name) / "mock.sqlite3"),
                "E2E_MOCK_SHOP_ID": self.shop_id,
                "E2E_MOCK_SHOP_SECRET": self.shop_secret,
                "E2E_MOCK_CONTROL_TOKEN": self.control_token,
                "E2E_MOCK_BIND": "127.0.0.1",
                "E2E_MOCK_PORT": str(self.port),
            }
        )
        self.server = subprocess.Popen(
            [sys.executable, str(SCRIPT)],
            env=environment,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        for _ in range(50):
            try:
                status, _ = self.request("GET", "/health")
                if status == 200:
                    return
            except URLError:
                time.sleep(0.05)
        error = self.server.stderr.read().decode() if self.server.stderr else ""
        self.fail(f"mock gateway failed to start: {error}")

    def stop_server(self) -> None:
        if self.server.poll() is None:
            self.server.terminate()
            self.server.wait(timeout=5)
        if self.server.stderr:
            self.server.stderr.close()

    def request(
        self,
        method: str,
        path: str,
        body: dict | None = None,
        *,
        authorization: str | None = None,
        idempotence_key: str | None = None,
    ) -> tuple[int, dict]:
        headers = {"Content-Type": "application/json"}
        if authorization:
            headers["Authorization"] = authorization
        if idempotence_key:
            headers["Idempotence-Key"] = idempotence_key
        data = json.dumps(body).encode() if body is not None else None
        request = Request(
            self.base_url + path, data=data, headers=headers, method=method
        )
        try:
            with urlopen(request, timeout=2) as response:
                return response.status, json.load(response)
        except HTTPError as response:
            with response:
                return response.code, json.load(response)

    def shop_auth(self) -> str:
        encoded = base64.b64encode(
            f"{self.shop_id}:{self.shop_secret}".encode()
        ).decode()
        return f"Basic {encoded}"

    def test_payment_idempotency_authoritative_status_and_restart(self) -> None:
        payload = {
            "amount": {"value": "100.00", "currency": "RUB"},
            "description": "E2E tenant top-up",
            "metadata": {"tenant_id": "123"},
        }
        key = secrets.token_urlsafe(20)
        self.assertEqual(
            self.request("POST", "/v3/payments", payload, idempotence_key=key)[0], 401
        )
        status, payment = self.request(
            "POST",
            "/v3/payments",
            payload,
            authorization=self.shop_auth(),
            idempotence_key=key,
        )
        self.assertEqual(status, 200)
        self.assertEqual(payment["status"], "pending")
        self.assertTrue(payment["test"])
        self.assertTrue(
            payment["confirmation"]["confirmation_url"].startswith(
                "https://example.invalid/"
            )
        )
        replay_status, replay = self.request(
            "POST",
            "/v3/payments",
            payload,
            authorization=self.shop_auth(),
            idempotence_key=key,
        )
        self.assertEqual(replay_status, 200)
        self.assertEqual(replay["id"], payment["id"])
        changed = {**payload, "amount": {"value": "200.00", "currency": "RUB"}}
        self.assertEqual(
            self.request(
                "POST",
                "/v3/payments",
                changed,
                authorization=self.shop_auth(),
                idempotence_key=key,
            )[0],
            409,
        )
        path = f"/_control/payments/{payment['id']}/status"
        self.assertEqual(self.request("POST", path, {"status": "succeeded"})[0], 401)
        self.assertEqual(
            self.request(
                "POST",
                path,
                {"status": "succeeded"},
                authorization=f"Bearer {self.control_token}",
            )[0],
            200,
        )
        self.assertEqual(
            self.request(
                "POST",
                path,
                {"status": "canceled"},
                authorization=f"Bearer {self.control_token}",
            )[0],
            409,
        )
        self.stop_server()
        self.start_server()
        status, persisted = self.request(
            "GET", f"/v3/payments/{payment['id']}", authorization=self.shop_auth()
        )
        self.assertEqual(status, 200)
        self.assertEqual(persisted["status"], "succeeded")
        self.assertTrue(persisted["paid"])

    def test_email_capture_rejects_real_recipients(self) -> None:
        email_path = "/backend-api/v1/send-email/l4desk-auth"
        self.assertEqual(
            self.request(
                "POST",
                email_path,
                {
                    "recipients": ["person@example.com"],
                    "subject": "test",
                    "message": "body",
                },
            )[0],
            400,
        )
        recipient = "operator@e2e.invalid"
        self.assertEqual(
            self.request(
                "POST",
                email_path,
                {
                    "recipients": [recipient],
                    "subject": "confirm",
                    "message": "secret link",
                },
            )[0],
            200,
        )
        path = f"/_control/emails?recipient={recipient}"
        self.assertEqual(self.request("GET", path)[0], 401)
        status, result = self.request(
            "GET", path, authorization=f"Bearer {self.control_token}"
        )
        self.assertEqual(status, 200)
        self.assertEqual(len(result["items"]), 1)
        self.assertEqual(result["items"][0]["message"], "secret link")


if __name__ == "__main__":
    unittest.main()
