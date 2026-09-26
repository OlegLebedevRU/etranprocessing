"""Create one test tenant terminal through the canonical HTTP onboarding API.

The operation ID is persisted before the request so reruns replay the same saga.
The plaintext PIN is printed only for the operator to enter on the test Agent.
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


def _save_manifest(manifest: dict) -> None:
    descriptor = os.open(MANIFEST, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle)


def main() -> None:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if manifest.get("status") != "smoke_passed":
        raise RuntimeError("Complete registration and payment smoke first")

    login = _json_request(
        "POST",
        f"{BACKEND}/api/auth/login",
        {"username": manifest["email"], "password": manifest["password"]},
    )
    auth = {"Authorization": f"Bearer {login['access_token']}"}
    profile = _json_request("GET", f"{BACKEND}/api/auth/me", headers=auth)
    if int(profile["org_id"]) != int(manifest["tenant_id"]):
        raise RuntimeError("Authenticated tenant differs from E2E manifest")

    operation_id = manifest.get("terminal_operation_id")
    if not operation_id:
        operation_id = str(uuid.uuid4())
        manifest["terminal_operation_id"] = operation_id
        manifest["terminal_correlation_id"] = f"e2e-onboard-{uuid.uuid4()}"
        _save_manifest(manifest)

    result = _json_request(
        "POST",
        f"{BACKEND}/api/settings/terminals",
        {
            "operation_id": operation_id,
            "correlation_id": manifest["terminal_correlation_id"],
            "note": "Isolated L4Desk 17E test terminal",
        },
        headers=auth,
    )
    if int(result["tenant_id"]) != int(manifest["tenant_id"]):
        raise RuntimeError("Onboarded terminal belongs to a different tenant")

    manifest.update(
        {
            "terminal_id": int(result["terminal_id"]),
            "terminal_device_id": int(result["device_id"]),
            "terminal_sn": result["sn"],
        }
    )
    _save_manifest(manifest)

    readiness = result["readiness"]
    print(
        f"terminal_id={result['terminal_id']} device_id={result['device_id']} "
        f"sn={result['sn']} iot={readiness['iot']} "
        f"certificate={readiness['certificate']} online={readiness['online']}"
    )
    pin = result.get("pin")
    if readiness["iot"] != "ready" or readiness["certificate"] != "issued":
        raise RuntimeError(
            "Provisioning or PIN issuance is incomplete; retry the same operation"
        )
    if not isinstance(pin, str) or re.fullmatch(r"\d{6}", pin) is None:
        raise RuntimeError("No active six-digit PIN returned; inspect readiness")
    print(f"PIN={pin} expires_at={result['pin_expires_at']}")


if __name__ == "__main__":
    main()
