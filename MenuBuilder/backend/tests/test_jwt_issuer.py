from __future__ import annotations

import asyncio
import base64
import hashlib
import hmac
import json
from unittest.mock import AsyncMock, MagicMock, patch

import httpx
import pytest
from fastapi import HTTPException

from app.config import settings
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


@pytest.mark.anyio
async def test_single_flight_deduplication():
    """Verify that parallel issue_tokens calls with the same key wait for a single HTTP call."""
    client = JwtIssuerClient()
    client.mock_enabled = False
    client.secret = "TEST_SECRET"

    call_count = 0

    async def mock_post(*args, **kwargs):
        nonlocal call_count
        call_count += 1
        await asyncio.sleep(0.05)
        mock_resp = MagicMock()
        mock_resp.status_code = 200
        mock_resp.json.return_value = {
            "accessToken": "single_flight_token",
            "refreshToken": "refresh_token",
            "expiresIn": 900,
        }
        return mock_resp

    with patch("httpx.AsyncClient.post", side_effect=mock_post):
        res1, res2 = await asyncio.gather(
            client.issue_tokens(user_id=10, org_id=20, role_id=3),
            client.issue_tokens(user_id=10, org_id=20, role_id=3),
        )

    assert call_count == 1
    assert res1["accessToken"] == "single_flight_token"
    assert res2["accessToken"] == "single_flight_token"


@pytest.mark.anyio
async def test_jwt_issuer_retry_on_5xx():
    """Verify that 5xx triggers a retry and succeeds on attempt 2."""
    client = JwtIssuerClient()
    client.mock_enabled = False
    client.secret = "TEST_SECRET"

    call_count = 0

    async def mock_post(*args, **kwargs):
        nonlocal call_count
        call_count += 1
        mock_resp = MagicMock()
        if call_count == 1:
            mock_resp.status_code = 502
            mock_resp.text = "Bad Gateway"
        else:
            mock_resp.status_code = 200
            mock_resp.json.return_value = {
                "accessToken": "retry_success_token",
                "refreshToken": "refresh_token",
            }
        return mock_resp

    with patch("httpx.AsyncClient.post", side_effect=mock_post), patch(
        "asyncio.sleep", new_callable=AsyncMock
    ):
        tokens = await client.issue_tokens(user_id=11, org_id=21, role_id=3)

    assert call_count == 2
    assert tokens["accessToken"] == "retry_success_token"


@pytest.mark.anyio
async def test_jwt_issuer_retry_on_network_error():
    """Verify that network error triggers retry and raises 503 if still failing."""
    client = JwtIssuerClient()
    client.mock_enabled = False
    client.secret = "TEST_SECRET"

    call_count = 0

    async def mock_post(*args, **kwargs):
        nonlocal call_count
        call_count += 1
        raise httpx.ConnectError("Connection refused")

    with (
        patch("httpx.AsyncClient.post", side_effect=mock_post),
        patch("asyncio.sleep", new_callable=AsyncMock),
        pytest.raises(HTTPException) as exc_info,
    ):
        await client.issue_tokens(user_id=12, org_id=22, role_id=3)

    assert call_count == 2
    assert exc_info.value.status_code == 503


@pytest.mark.anyio
async def test_jwt_issuer_cache_hit_and_miss():
    """Verify in-process token cache hit and miss when jwt_issuer_token_cache_enabled is True."""
    client = JwtIssuerClient()
    client.mock_enabled = False
    client.secret = "TEST_SECRET"

    call_count = 0

    async def mock_post(*args, **kwargs):
        nonlocal call_count
        call_count += 1
        mock_resp = MagicMock()
        mock_resp.status_code = 200
        mock_resp.json.return_value = {
            "accessToken": f"token_{call_count}",
            "refreshToken": "refresh_token",
            "expiresIn": 3600,
        }
        return mock_resp

    settings.jwt_issuer_token_cache_enabled = True
    try:
        with patch("httpx.AsyncClient.post", side_effect=mock_post):
            # 1. First call: cache miss -> network call
            t1 = await client.issue_tokens(user_id=100, org_id=1, role_id=3)
            assert call_count == 1
            assert t1["accessToken"] == "token_1"

            # 2. Second call with same key -> cache hit -> no network call
            t2 = await client.issue_tokens(user_id=100, org_id=1, role_id=3)
            assert call_count == 1
            assert t2["accessToken"] == "token_1"

            # 3. Third call with different org_id -> cache miss -> network call
            t3 = await client.issue_tokens(user_id=100, org_id=2, role_id=3)
            assert call_count == 2
            assert t3["accessToken"] == "token_2"
    finally:
        settings.jwt_issuer_token_cache_enabled = False


@pytest.mark.anyio
async def test_jwt_issuer_cache_invalidation():
    """Verify that invalidate_cache_for_user evicts tokens for that user."""
    client = JwtIssuerClient()
    client.mock_enabled = False
    client.secret = "TEST_SECRET"

    call_count = 0

    async def mock_post(*args, **kwargs):
        nonlocal call_count
        call_count += 1
        mock_resp = MagicMock()
        mock_resp.status_code = 200
        mock_resp.json.return_value = {
            "accessToken": f"token_{call_count}",
            "expiresIn": 3600,
        }
        return mock_resp

    settings.jwt_issuer_token_cache_enabled = True
    try:
        with patch("httpx.AsyncClient.post", side_effect=mock_post):
            await client.issue_tokens(user_id=200, org_id=1, role_id=3)
            assert call_count == 1

            client.invalidate_cache_for_user(200)

            # Next call must miss cache
            await client.issue_tokens(user_id=200, org_id=1, role_id=3)
            assert call_count == 2
    finally:
        settings.jwt_issuer_token_cache_enabled = False


@pytest.mark.anyio
async def test_jwt_issuer_cache_margin_expiration():
    """Verify that tokens with <= 10 min remaining are treated as expired and re-issued."""
    import time

    client = JwtIssuerClient()
    client.mock_enabled = False
    client.secret = "TEST_SECRET"

    call_count = 0

    async def mock_post(*args, **kwargs):
        nonlocal call_count
        call_count += 1
        mock_resp = MagicMock()
        mock_resp.status_code = 200
        mock_resp.json.return_value = {
            "accessToken": f"token_{call_count}",
            "expiresIn": 3600,
        }
        return mock_resp

    settings.jwt_issuer_token_cache_enabled = True
    try:
        # Prepopulate cache entry with exp = now + 500s (< 600s margin)
        key = (300, 1)
        client._token_cache[key] = ({"accessToken": "stale_token"}, time.time() + 500)

        with patch("httpx.AsyncClient.post", side_effect=mock_post):
            res = await client.issue_tokens(user_id=300, org_id=1, role_id=3)
            assert call_count == 1
            assert res["accessToken"] == "token_1"
    finally:
        settings.jwt_issuer_token_cache_enabled = False
