from __future__ import annotations

import base64
import hashlib
import hmac
import json

import pytest

from app.services.jwt_issuer import JwtIssuerClient


def test_hmac_sha256_request_signing():
    """Verify that build_signed_request produces an HMAC-SHA256 signature conforming to the Cloud Function contract."""
    client = JwtIssuerClient()
    client.secret = "TEST_SECRET_KEY_12345"

    signed_req = client.build_signed_request(
        user_id=123,
        org_id=424,
        role_id=2,
    )

    assert "params" in signed_req
    params = signed_req["params"]

    assert params["orgId"] == 424
    assert params["userId"] == 123
    assert params["roleId"] == 2
    assert params["clientId"] == "menubuilder-backend"
    assert params["aud"] == "menubuilder"
    assert params["iss"] == "external-jwt-issuer"
    assert params["kid"] == "menubuilder-rs256-key-1"

    timestamp = params["timestamp"]
    nonce = params["nonce"]
    req_body = params["requestBody"]
    sig = params["signature"]

    # Verify signature starts with sha256=
    assert sig.startswith("sha256=")
    expected_b64 = sig[len("sha256=") :]

    # Recompute HMAC locally
    signed_payload = f"{timestamp}.{nonce}.{req_body}"
    h = hmac.new(
        b"TEST_SECRET_KEY_12345",
        signed_payload.encode("utf-8"),
        hashlib.sha256,
    )
    recomputed_b64 = base64.b64encode(h.digest()).decode("ascii")

    assert expected_b64 == recomputed_b64

    # Verify inner JSON content
    body_obj = json.loads(req_body)
    assert body_obj["orgId"] == 424
    assert body_obj["userId"] == 123
    assert body_obj["roleId"] == 2
    assert body_obj["clientId"] == "menubuilder-backend"


@pytest.mark.anyio
async def test_mock_jwt_generation():
    """Verify mock token generation in test mode."""
    client = JwtIssuerClient()
    client.mock_enabled = True

    tokens = await client.issue_tokens(
        user_id=548,
        org_id=424,
        role_id=3,
        username="Snoxin",
        role="user",
        is_superuser=False,
    )

    assert "accessToken" in tokens
    assert "refreshToken" in tokens
    assert tokens["orgId"] == 424
    assert tokens["userId"] == 548
    assert tokens["roleId"] == 3
    assert tokens["tokenType"] == "Bearer"
    assert tokens["expiresIn"] > 0
