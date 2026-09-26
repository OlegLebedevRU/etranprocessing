"""Onboard a reassigned E2E tenant without changing its old manifest.

The old terminal record remains in the manifest for audit. The stable operation
ID makes an uncertain HTTP response safe to retry.
"""

from __future__ import annotations

import json
import os
import re
import uuid
from pathlib import Path
from urllib.error import HTTPError
from urllib.parse import urlsplit
from urllib.request import Request, urlopen

BACKEND = os.environ.get("E2E_BACKEND_URL", "http://test-backend:8000").rstrip("/")


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


def main() -> None:
    manifest = json.loads(
        Path(os.environ["E2E_MANIFEST_PATH"]).read_text(encoding="utf-8")
    )
    target_id = int(os.environ["E2E_TARGET_TENANT_ID"])
    if (
        manifest.get("status") != "registration_confirmed"
        or int(manifest["tenant_id"]) == target_id
    ):
        raise RuntimeError("Expected a confirmed registration in the old tenant")

    login = _json_request(
        "POST",
        f"{BACKEND}/api/auth/login",
        {"username": manifest["email"], "password": manifest["password"]},
    )
    auth = {"Authorization": f"Bearer {login['access_token']}"}
    profile = _json_request("GET", f"{BACKEND}/api/auth/me", headers=auth)
    if int(profile["org_id"]) != target_id:
        raise RuntimeError("Authenticated user does not belong to target tenant")

    operation_id = str(
        uuid.uuid5(uuid.NAMESPACE_URL, f"l4desk-e2e-reassign:{target_id}")
    )
    result = _json_request(
        "POST",
        f"{BACKEND}/api/settings/terminals",
        {
            "operation_id": operation_id,
            "correlation_id": f"e2e-reassign-{operation_id}",
            "note": "Isolated L4Desk 17E free quota test terminal",
        },
        headers=auth,
    )
    if int(result["tenant_id"]) != target_id:
        raise RuntimeError("Onboarded terminal belongs to another tenant")
    readiness = result["readiness"]
    print(
        f"tenant_id={target_id} terminal_id={result['terminal_id']} "
        f"device_id={result['device_id']} sn={result['sn']} "
        f"iot={readiness['iot']} certificate={readiness['certificate']} "
        f"online={readiness['online']}"
    )
    if readiness["iot"] != "ready" or readiness["certificate"] != "issued":
        raise RuntimeError("Provisioning or PIN issuance is incomplete")
    pin = result.get("pin")
    if not isinstance(pin, str) or re.fullmatch(r"\d{6}", pin) is None:
        raise RuntimeError("No active six-digit PIN returned")
    print(f"PIN={pin} expires_at={result['pin_expires_at']}")


if __name__ == "__main__":
    try:
        main()
    # CLI boundary: an HTTP failure must not echo credentials or encoded source.
    except Exception as exc:  # noqa: BLE001
        print(f"FAILED {type(exc).__name__}: {str(exc).splitlines()[0][:160]}")
        raise SystemExit(1) from None
