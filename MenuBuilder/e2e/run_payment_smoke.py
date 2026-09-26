"""Run an isolated registration and payment smoke through deployed HTTP APIs.

The runner stores its generated credentials in the private mock volume and prints
only IDs and verdict. It never calls the real YooKassa endpoint.
"""

from __future__ import annotations

import json
import os
import re
import secrets
import time
import uuid
from pathlib import Path
from urllib.error import HTTPError
from urllib.parse import parse_qs, quote, urlsplit
from urllib.request import Request, urlopen

BACKEND = os.environ.get("E2E_BACKEND_URL", "http://test-backend:8000").rstrip("/")
MOCK = os.environ.get("E2E_MOCK_URL", "http://127.0.0.1:8080").rstrip("/")
CONTROL_TOKEN = os.environ["E2E_MOCK_CONTROL_TOKEN"]
WEBHOOK_SECRET = os.environ["E2E_WEBHOOK_SECRET"]
MANIFEST = Path(os.environ.get("E2E_MANIFEST_PATH", "/data/e2e_manifest.json"))


def _json_request(
    method: str,
    url: str,
    body: dict | None = None,
    *,
    headers: dict[str, str] | None = None,
) -> dict:
    request = Request(
        url,
        data=json.dumps(body).encode() if body is not None else None,
        method=method,
        headers={"Content-Type": "application/json", **(headers or {})},
    )
    try:
        with urlopen(request, timeout=20) as response:
            result = json.load(response)
            if not isinstance(result, dict):
                raise TypeError("Unexpected non-object API response")
            return result
    except HTTPError as error:
        with error:
            raise RuntimeError(
                f"HTTP {error.code} from {urlsplit(url).path}"
            ) from error


def _confirmation_token(email: str) -> str:
    headers = {"Authorization": f"Bearer {CONTROL_TOKEN}"}
    for _ in range(30):
        messages = _json_request(
            "GET", f"{MOCK}/_control/emails?recipient={quote(email)}", headers=headers
        )["items"]
        if messages:
            link = re.search(
                r"https?://[^\s]+/register/confirm\?[^\s]+", messages[0]["message"]
            )
            if link:
                token = parse_qs(urlsplit(link.group()).query).get("token", [""])[0]
                if token:
                    return token
        time.sleep(1)
    raise RuntimeError("Registration email was not captured by the test sink")


def _save_manifest(manifest: dict) -> None:
    descriptor = os.open(MANIFEST, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle)


def _payment_exists(payment_id: int, headers: dict[str, str]) -> bool:
    request = Request(
        f"{BACKEND}/api/v1/finance/payments/{payment_id}", headers=headers
    )
    try:
        with urlopen(request, timeout=20):
            return True
    except HTTPError as error:
        if error.code == 404:
            return False
        raise RuntimeError(f"Payment lookup failed with HTTP {error.code}") from error


def main() -> None:
    if _json_request("GET", f"{MOCK}/health").get("external_delivery") is not False:
        raise RuntimeError("Expected a mock gateway with external delivery disabled")

    if MANIFEST.exists():
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        if manifest.get("status") not in (
            "awaiting_confirmation",
            "payment_started",
        ):
            raise RuntimeError("Previous E2E run exists; inspect manifest before retry")
    else:
        email = os.environ.get("E2E_REGISTRATION_EMAIL") or (
            f"l4desk-e2e-{uuid.uuid4().hex[:12]}@e2e.invalid"
        )
        email_gateway = os.environ.get("E2E_EMAIL_GATEWAY_URL", "")
        if not email.endswith(".invalid") and (
            not email_gateway or "mock-gateway" in email_gateway
        ):
            raise RuntimeError("Real recipient requires an explicit email gateway")
        password = secrets.token_urlsafe(24)
        manifest: dict[str, str | int] = {
            "email": email,
            "password": password,
            "status": "registration_started",
        }
        _save_manifest(manifest)
        _json_request(
            "POST",
            f"{BACKEND}/api/auth/register",
            {
                "email": email,
                "password": password,
                "terms_version": "v1",
                "timezone": "Europe/Moscow",
            },
        )
        if email.endswith(".invalid"):
            token = _confirmation_token(email)
            _json_request(
                "POST", f"{BACKEND}/api/auth/register/confirm", {"token": token}
            )
        else:
            manifest["status"] = "awaiting_confirmation"
            _save_manifest(manifest)
            print(
                "AWAITING_EMAIL_CONFIRMATION; click the registration link, then rerun"
            )
            return

    email = manifest["email"]
    password = manifest["password"]
    login = _json_request(
        "POST", f"{BACKEND}/api/auth/login", {"username": email, "password": password}
    )
    auth = {"Authorization": f"Bearer {login['access_token']}"}
    if manifest.get("payment_id"):
        prior_payment_id = int(manifest["payment_id"])
        if _payment_exists(prior_payment_id, auth):
            raise RuntimeError("Prior payment exists; inspect it before retry")
        manifest["unpersisted_payment_id"] = prior_payment_id
        manifest.pop("payment_id")
        manifest.pop("provider_payment_id", None)
        _save_manifest(manifest)
    profile = _json_request("GET", f"{BACKEND}/api/auth/me", headers=auth)
    tenant_id = int(profile["org_id"])
    user_id = int(profile["user_id"])
    if tenant_id <= 0 or user_id <= 0:
        raise RuntimeError("Test user has no active tenant")
    manifest.update(
        {"tenant_id": tenant_id, "user_id": user_id, "status": "payment_started"}
    )
    _save_manifest(manifest)
    before = _json_request("GET", f"{BACKEND}/api/v1/finance/balance", headers=auth)
    amount_rubles = 10
    operation_id = str(uuid.uuid4())
    payment = _json_request(
        "POST",
        f"{BACKEND}/api/v1/finance/payments",
        {"amount_rubles": amount_rubles, "idempotence_key": operation_id},
        headers=auth,
    )
    payment_id = int(payment["id"])
    provider_id = payment["provider_payment_id"]
    if not provider_id.startswith("e2e-") or not payment["confirmation_url"].startswith(
        "https://example.invalid/"
    ):
        raise RuntimeError("Payment was not routed to the isolated mock")
    manifest.update({"payment_id": payment_id, "provider_payment_id": provider_id})
    _save_manifest(manifest)

    control = {"Authorization": f"Bearer {CONTROL_TOKEN}"}
    provider_payment = _json_request(
        "POST",
        f"{MOCK}/_control/payments/{provider_id}/status",
        {"status": "succeeded"},
        headers=control,
    )
    webhook = {
        "type": "notification",
        "event": "payment.succeeded",
        "object": provider_payment,
    }
    webhook_headers = {"X-YooKassa-Webhook-Secret": WEBHOOK_SECRET}
    _json_request(
        "POST",
        f"{BACKEND}/api/v1/finance/yookassa/webhook",
        webhook,
        headers=webhook_headers,
    )
    first = _json_request(
        "GET", f"{BACKEND}/api/v1/finance/payments/{payment_id}", headers=auth
    )
    if first["status"] != "succeeded" or not first["ledger_transaction_id"]:
        raise RuntimeError("Webhook did not post a successful payment")
    _json_request(
        "POST",
        f"{BACKEND}/api/v1/finance/yookassa/webhook",
        webhook,
        headers=webhook_headers,
    )
    polled = _json_request(
        "POST", f"{BACKEND}/api/v1/finance/payments/{payment_id}/poll", headers=auth
    )
    after = _json_request("GET", f"{BACKEND}/api/v1/finance/balance", headers=auth)
    if polled["ledger_transaction_id"] != first["ledger_transaction_id"]:
        raise RuntimeError("Duplicate webhook or poll produced a second ledger posting")
    if after["balance_kopecks"] - before["balance_kopecks"] != amount_rubles * 100:
        raise RuntimeError("Balance projection does not match the test payment")
    manifest.update(
        {
            "ledger_transaction_id": first["ledger_transaction_id"],
            "status": "smoke_passed",
        }
    )
    _save_manifest(manifest)
    print(
        f"PASS tenant_id={tenant_id} user_id={user_id} payment_id={payment_id} "
        f"ledger_transaction_id={first['ledger_transaction_id']}"
    )


if __name__ == "__main__":
    main()
