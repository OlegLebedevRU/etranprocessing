"""Isolated YooKassa and registration email simulator for L4Desk E2E runs.

Run only on a private test network. No payment or email is sent externally.
"""

from __future__ import annotations

import base64
import binascii
import hmac
import json
import os
import sqlite3
import uuid
from datetime import UTC, datetime
from decimal import Decimal, InvalidOperation
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit


def _required_env(name: str) -> str:
    value = os.environ.get(name, "")
    if not value:
        raise RuntimeError(f"{name} is required")
    return value


DB_PATH = Path(_required_env("E2E_MOCK_DB_PATH"))
SHOP_ID = _required_env("E2E_MOCK_SHOP_ID")
SHOP_SECRET = _required_env("E2E_MOCK_SHOP_SECRET")
CONTROL_TOKEN = _required_env("E2E_MOCK_CONTROL_TOKEN")
if not SHOP_ID.startswith("e2e_"):
    raise RuntimeError("E2E_MOCK_SHOP_ID must start with e2e_")
CHECKOUT_BASE = os.environ.get(
    "E2E_MOCK_CHECKOUT_BASE", "https://example.invalid/e2e-checkout"
).rstrip("/")


def _connect() -> sqlite3.Connection:
    connection = sqlite3.connect(DB_PATH, timeout=10)
    connection.row_factory = sqlite3.Row
    return connection


def _initialize() -> None:
    DB_PATH.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    descriptor = os.open(DB_PATH, os.O_CREAT | os.O_RDWR, 0o600)
    os.close(descriptor)
    with _connect() as connection:
        connection.executescript(
            """
            CREATE TABLE IF NOT EXISTS payments (
                id TEXT PRIMARY KEY,
                idempotence_key TEXT NOT NULL UNIQUE,
                request_json TEXT NOT NULL,
                response_json TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS emails (
                id TEXT PRIMARY KEY,
                recipient TEXT NOT NULL,
                subject TEXT NOT NULL,
                message TEXT NOT NULL,
                created_at TEXT NOT NULL
            );
            """
        )


class GatewayHandler(BaseHTTPRequestHandler):
    server_version = "L4DeskE2EMock/1"

    def log_message(self, format: str, *args: object) -> None:
        # URLs can contain payment IDs or recipient addresses; do not log them.
        del format, args

    def _json(self, status: HTTPStatus, payload: object) -> None:
        encoded = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def _body(self) -> dict[str, object] | None:
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            return None
        if length < 1 or length > 65536:
            return None
        try:
            parsed = json.loads(self.rfile.read(length))
        except UnicodeDecodeError, json.JSONDecodeError:
            return None
        return parsed if isinstance(parsed, dict) else None

    def _shop_auth(self) -> bool:
        header = self.headers.get("Authorization", "")
        if not header.startswith("Basic "):
            return False
        try:
            raw = base64.b64decode(header[6:], validate=True).decode("utf-8")
        except ValueError, UnicodeDecodeError, binascii.Error:
            return False
        return hmac.compare_digest(raw, f"{SHOP_ID}:{SHOP_SECRET}")

    def _control_auth(self) -> bool:
        header = self.headers.get("Authorization", "")
        return hmac.compare_digest(header, f"Bearer {CONTROL_TOKEN}")

    def do_GET(self) -> None:
        path = urlsplit(self.path)
        if path.path == "/health":
            self._json(HTTPStatus.OK, {"status": "ok", "external_delivery": False})
            return
        if path.path.startswith("/v3/payments/"):
            if not self._shop_auth():
                self._json(
                    HTTPStatus.UNAUTHORIZED, {"type": "error", "code": "unauthorized"}
                )
                return
            payment_id = path.path.removeprefix("/v3/payments/")
            with _connect() as connection:
                row = connection.execute(
                    "SELECT response_json FROM payments WHERE id = ?", (payment_id,)
                ).fetchone()
            self._json(
                HTTPStatus.OK if row else HTTPStatus.NOT_FOUND,
                json.loads(row["response_json"])
                if row
                else {"type": "error", "code": "not_found"},
            )
            return
        if path.path == "/_control/emails":
            if not self._control_auth():
                self._json(HTTPStatus.UNAUTHORIZED, {"code": "unauthorized"})
                return
            recipient = parse_qs(path.query).get("recipient", [""])[0]
            if not recipient.endswith(".invalid"):
                self._json(HTTPStatus.BAD_REQUEST, {"code": "test_recipient_required"})
                return
            with _connect() as connection:
                rows = connection.execute(
                    "SELECT id, recipient, subject, message, created_at FROM emails "
                    "WHERE recipient = ? ORDER BY created_at DESC LIMIT 10",
                    (recipient,),
                ).fetchall()
            self._json(HTTPStatus.OK, {"items": [dict(row) for row in rows]})
            return
        self._json(HTTPStatus.NOT_FOUND, {"code": "not_found"})

    def do_POST(self) -> None:
        path = urlsplit(self.path).path
        if path == "/v3/payments":
            self._create_payment()
            return
        if path.startswith("/backend-api/v1/send-email/"):
            self._capture_email()
            return
        if path.startswith("/_control/payments/") and path.endswith("/status"):
            self._set_status(
                path.removeprefix("/_control/payments/").removesuffix("/status")
            )
            return
        self._json(HTTPStatus.NOT_FOUND, {"code": "not_found"})

    def _create_payment(self) -> None:
        if not self._shop_auth():
            self._json(
                HTTPStatus.UNAUTHORIZED, {"type": "error", "code": "unauthorized"}
            )
            return
        key = self.headers.get("Idempotence-Key", "")
        body = self._body()
        amount = body.get("amount") if body else None
        value = amount.get("value") if isinstance(amount, dict) else None
        currency = amount.get("currency") if isinstance(amount, dict) else None
        if body is None:
            self._json(
                HTTPStatus.BAD_REQUEST, {"type": "error", "code": "invalid_request"}
            )
            return
        try:
            parsed_amount = Decimal(value) if isinstance(value, str) else Decimal(0)
            exponent = parsed_amount.as_tuple().exponent
            valid_amount = (
                parsed_amount.is_finite()
                and parsed_amount > 0
                and isinstance(exponent, int)
                and exponent >= -2
            )
        except InvalidOperation:
            valid_amount = False
        if not key or len(key) > 255 or not valid_amount or currency != "RUB":
            self._json(
                HTTPStatus.BAD_REQUEST, {"type": "error", "code": "invalid_request"}
            )
            return
        request_json = json.dumps(body, sort_keys=True, separators=(",", ":"))
        payment_id = f"e2e-{uuid.uuid4()}"
        response = {
            "id": payment_id,
            "status": "pending",
            "paid": False,
            "amount": amount,
            "confirmation": {
                "type": "redirect",
                "confirmation_url": f"{CHECKOUT_BASE}/{payment_id}",
            },
            "created_at": datetime.now(UTC).isoformat(),
            "description": body.get("description", ""),
            "metadata": body.get("metadata", {}),
            "recipient": {"account_id": SHOP_ID},
            "test": True,
        }
        if "receipt" in body:
            response["receipt"] = body["receipt"]
        with _connect() as connection:
            connection.execute("BEGIN IMMEDIATE")
            prior = connection.execute(
                "SELECT request_json, response_json FROM payments WHERE idempotence_key = ?",
                (key,),
            ).fetchone()
            if prior:
                if prior["request_json"] != request_json:
                    self._json(
                        HTTPStatus.CONFLICT,
                        {"type": "error", "code": "invalid_request"},
                    )
                    return
                response = json.loads(prior["response_json"])
            else:
                connection.execute(
                    "INSERT INTO payments (id, idempotence_key, request_json, response_json) "
                    "VALUES (?, ?, ?, ?)",
                    (payment_id, key, request_json, json.dumps(response)),
                )
        self._json(HTTPStatus.OK, response)

    def _capture_email(self) -> None:
        body = self._body()
        if body is None:
            self._json(HTTPStatus.BAD_REQUEST, {"code": "invalid_request"})
            return
        recipients = body.get("recipients") if body else None
        if (
            not isinstance(recipients, list)
            or not recipients
            or any(
                not isinstance(item, str) or not item.endswith(".invalid")
                for item in recipients
            )
        ):
            self._json(HTTPStatus.BAD_REQUEST, {"code": "test_recipient_required"})
            return
        subject = body.get("subject", "")
        message = body.get("message", "")
        if not isinstance(subject, str) or not isinstance(message, str):
            self._json(HTTPStatus.BAD_REQUEST, {"code": "invalid_request"})
            return
        message_id = f"e2e-mail-{uuid.uuid4()}"
        with _connect() as connection:
            connection.executemany(
                "INSERT INTO emails (id, recipient, subject, message, created_at) "
                "VALUES (?, ?, ?, ?, ?)",
                [
                    (
                        message_id,
                        recipient,
                        subject,
                        message,
                        datetime.now(UTC).isoformat(),
                    )
                    for recipient in recipients
                ],
            )
        self._json(
            HTTPStatus.OK, {"status": "captured", "postbox_message_id": message_id}
        )

    def _set_status(self, payment_id: str) -> None:
        if not self._control_auth():
            self._json(HTTPStatus.UNAUTHORIZED, {"code": "unauthorized"})
            return
        body = self._body()
        new_status = body.get("status") if body else None
        if new_status not in ("succeeded", "canceled"):
            self._json(HTTPStatus.BAD_REQUEST, {"code": "invalid_status"})
            return
        with _connect() as connection:
            connection.execute("BEGIN IMMEDIATE")
            row = connection.execute(
                "SELECT response_json FROM payments WHERE id = ?", (payment_id,)
            ).fetchone()
            if not row:
                self._json(HTTPStatus.NOT_FOUND, {"code": "not_found"})
                return
            response = json.loads(row["response_json"])
            if response["status"] not in ("pending", new_status):
                self._json(HTTPStatus.CONFLICT, {"code": "final_status_immutable"})
                return
            response["status"] = new_status
            response["paid"] = new_status == "succeeded"
            connection.execute(
                "UPDATE payments SET response_json = ? WHERE id = ?",
                (json.dumps(response), payment_id),
            )
        self._json(HTTPStatus.OK, response)


def main() -> None:
    _initialize()
    host = os.environ.get("E2E_MOCK_BIND", "127.0.0.1")
    port = int(os.environ.get("E2E_MOCK_PORT", "8080"))
    ThreadingHTTPServer((host, port), GatewayHandler).serve_forever()


if __name__ == "__main__":
    main()
