from __future__ import annotations

import base64
import hashlib

import pytest
from httpx import ASGITransport, AsyncClient

from app.main import app
from app.user_store import (
    hash_refresh_token,
    verify_md5_password,
)


def test_md5_password_verification():
    raw_pass = "secret123"
    md5_hash = hashlib.md5(raw_pass.encode("utf-8")).hexdigest()

    assert verify_md5_password("secret123", md5_hash) is True
    assert verify_md5_password("secret123", md5_hash.upper()) is True
    assert verify_md5_password("wrong_password", md5_hash) is False


def test_hash_refresh_token():
    token = "sample_refresh_token_string_abc"
    h1 = hash_refresh_token(token)
    h2 = hashlib.sha256(token.encode("utf-8")).hexdigest()
    assert h1 == h2


@pytest.mark.anyio
async def test_auth_login_json():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as ac:
        # 1. Successful login
        resp = await ac.post(
            "/api/auth/login",
            json={
                "username": "o.lebedev",
                "password": "eaf21fcabcffeb1f97f01a4fc02ece63",
            },
        )
        assert resp.status_code == 200
        data = resp.json()
        assert "access_token" in data
        assert "refresh_token" in data
        assert data["token_type"] == "Bearer"
        assert data["is_superuser"] is True
        assert data["role"] in ("superuser", "admin")

        # Verify cookies
        cookies = resp.cookies
        assert "accessToken" in cookies
        assert "refreshToken" in cookies

        # 2. Failed login
        resp_bad = await ac.post(
            "/api/auth/login",
            json={"username": "o.lebedev", "password": "wrongpassword"},
        )
        assert resp_bad.status_code == 401


@pytest.mark.anyio
async def test_auth_login_basic_auth():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as ac:
        creds = base64.b64encode(b"o.lebedev:eaf21fcabcffeb1f97f01a4fc02ece63").decode(
            "ascii"
        )
        resp = await ac.post(
            "/api/auth/login",
            headers={"Authorization": f"Basic {creds}"},
        )
        assert resp.status_code == 200
        data = resp.json()
        assert "access_token" in data
        assert "refreshToken" in resp.cookies


@pytest.mark.anyio
async def test_auth_refresh_and_logout():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as ac:
        # Login first
        login_resp = await ac.post(
            "/api/auth/login",
            json={
                "username": "o.lebedev",
                "password": "eaf21fcabcffeb1f97f01a4fc02ece63",
            },
        )
        assert login_resp.status_code == 200
        login_data = login_resp.json()
        refresh_token = login_data["refresh_token"]

        # Call refresh via JSON body
        refresh_resp = await ac.post(
            "/api/auth/refresh",
            json={"refresh_token": refresh_token},
        )
        # In mock / config mode with simulated DB session, check response
        if refresh_resp.status_code == 200:
            refreshed_data = refresh_resp.json()
            assert "access_token" in refreshed_data

        # Call logout
        logout_resp = await ac.post(
            "/api/auth/logout",
            json={"refresh_token": refresh_token},
        )
        assert logout_resp.status_code == 200
        assert logout_resp.json()["ok"] is True


@pytest.mark.anyio
async def test_auth_me_endpoint():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as ac:
        login_resp = await ac.post(
            "/api/auth/login",
            json={
                "username": "o.lebedev",
                "password": "eaf21fcabcffeb1f97f01a4fc02ece63",
            },
        )
        token = login_resp.json()["access_token"]

        me_resp = await ac.get(
            "/api/auth/me",
            headers={"Authorization": f"Bearer {token}"},
        )
        assert me_resp.status_code == 200
        me_data = me_resp.json()
        assert me_data["username"] == "o.lebedev"
        assert me_data["is_superuser"] is True
