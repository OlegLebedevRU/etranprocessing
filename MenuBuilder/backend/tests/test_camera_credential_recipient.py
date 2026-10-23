"""PB authority client validates identity/algorithm/expiry without echoing inputs."""

import json
import secrets

import httpx
import pytest
from cryptography.hazmat.primitives import serialization

from app.config import settings
from app.services.camera_credential_recipient import (
    RecipientUnavailable,
    fetch_credential_recipient,
)
from tests.test_camera_credential_vault import SCOPE, recipient


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.anyio
@pytest.mark.parametrize(
    "change",
    [
        None,
        "sn",
        "device_id",
        "cert_serial",
        "recipient_cert_sha256",
        "unknown",
        "not_valid_after",
        "pem",
        "redirect",
    ],
)
async def test_authority_response_checked(monkeypatch, change):
    target = recipient()[1]
    token = secrets.token_urlsafe(32)
    monkeypatch.setattr(settings, "processing_backend_service_token", token)
    monkeypatch.setattr(settings, "processing_backend_url", "http://test")
    value = {
        "sn": target.sn,
        "device_id": target.device_id,
        "cert_serial": target.cert_serial,
        "recipient_cert_sha256": target.recipient_cert_sha256,
        "certificate_pem": target.certificate.public_bytes(
            serialization.Encoding.PEM
        ).decode(),
        "not_valid_after": target.certificate.not_valid_after_utc.isoformat(),
    }
    if change in {"sn", "cert_serial", "recipient_cert_sha256", "not_valid_after"}:
        value[change] = "invalid"
    elif change == "device_id":
        value[change] = 702
    elif change == "unknown":
        value["unknown"] = True
    elif change == "pem":
        value["certificate_pem"] *= 2

    async def serve(request):
        assert request.headers["x-internal-service-key"] == token
        assert request.url.params["org_id"] == "1"
        return httpx.Response(
            302 if change == "redirect" else 200, content=json.dumps(value)
        )

    async with httpx.AsyncClient(transport=httpx.MockTransport(serve)) as client:
        if change is None:
            assert (
                await fetch_credential_recipient(
                    tenant_id=1, device_id=SCOPE.device_id, sn=SCOPE.sn, client=client
                )
            ).recipient_cert_sha256 == target.recipient_cert_sha256
        else:
            with pytest.raises(
                RecipientUnavailable, match="^credential_recipient_unavailable$"
            ):
                await fetch_credential_recipient(
                    tenant_id=1, device_id=SCOPE.device_id, sn=SCOPE.sn, client=client
                )
