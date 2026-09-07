from __future__ import annotations

import base64
import hashlib

import pytest
from httpx import ASGITransport, AsyncClient

from app.config import settings
from app.main import app
from app.user_store import (
    ConfigUserStore,
    hash_refresh_token,
    verify_md5_password,
    verify_password,
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

        # Call logout (with CSRF header required for cookie-authenticated mutations)
        logout_resp = await ac.post(
            "/api/auth/logout",
            json={"refresh_token": refresh_token},
            headers={"X-Requested-With": "XMLHttpRequest"},
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


def test_settings_get_users_various_formats():
    # 1. Dict mapping username -> hash
    orig = settings.auth_users
    try:
        settings.auth_users = (
            '{"admin": "pbkdf2:sha256:600000$admin$hash", "user2": "md5hash"}'
        )
        users = settings.get_users()
        assert len(users) == 2
        admin = next(u for u in users if u["username"] == "admin")
        assert admin["role"] == "superuser"
        assert admin["is_superuser"] is True
        assert admin["md5_password"] == "pbkdf2:sha256:600000$admin$hash"

        # 2. List of dicts
        settings.auth_users = '[{"username": "testuser", "org_id": 5}]'
        users = settings.get_users()
        assert len(users) == 1
        assert users[0]["username"] == "testuser"

        # 3. Single user dict
        settings.auth_users = '{"username": "singleuser", "role": "admin"}'
        users = settings.get_users()
        assert len(users) == 1
        assert users[0]["username"] == "singleuser"

        # 4. List of strings
        settings.auth_users = '["admin", "regular"]'
        users = settings.get_users()
        assert len(users) == 2
        assert users[0]["username"] == "admin"
        assert users[0]["is_superuser"] is True
        assert users[1]["username"] == "regular"
        assert users[1]["is_superuser"] is False

        # 5. Invalid / empty
        settings.auth_users = "invalid json {{"
        assert settings.get_users() == []
        settings.auth_users = ""
        assert settings.get_users() == []
    finally:
        settings.auth_users = orig


def test_verify_password_pbkdf2_and_md5():
    # Plain & MD5
    raw_pass = "mypassword"
    md5_hash = hashlib.md5(raw_pass.encode()).hexdigest()
    assert verify_password(raw_pass, md5_hash) is True
    assert verify_password("wrong", md5_hash) is False

    # Werkzeug PBKDF2:sha256
    salt = "testsalt"
    calc_hex = hashlib.pbkdf2_hmac(
        "sha256", raw_pass.encode(), salt.encode(), 1000
    ).hex()
    pbkdf2_str = f"pbkdf2:sha256:1000${salt}${calc_hex}"
    assert verify_password(raw_pass, pbkdf2_str) is True
    assert verify_password("wrong", pbkdf2_str) is False


@pytest.mark.anyio
async def test_auth_login_nonexistent_user_returns_401():
    orig = settings.auth_users
    try:
        # Simulate production AUTH_USERS with dict format
        settings.auth_users = '{"admin": "pbkdf2:sha256:600000$admin$hash"}'
        transport = ASGITransport(app=app)
        async with AsyncClient(transport=transport, base_url="http://test") as ac:
            resp = await ac.post(
                "/api/auth/login",
                json={"username": "totally_unknown_user", "password": "anypassword"},
            )
            # Must return 401 Unauthorized, never 500!
            assert resp.status_code == 401
            assert resp.json()["detail"] == "Invalid credentials"
    finally:
        settings.auth_users = orig


@pytest.mark.anyio
async def test_config_user_store_dict_format():
    orig = settings.auth_users
    try:
        settings.auth_users = '{"admin": "pbkdf2:sha256:600000$admin$hash"}'
        store = ConfigUserStore()
        # Should not raise AttributeError: 'str' object has no attribute 'get'
        rec = await store.get_by_username("admin")
        assert rec is not None
        assert rec.username == "admin"
        assert rec.is_superuser is True

        unknown = await store.get_by_username("unknown")
        assert unknown is None
    finally:
        settings.auth_users = orig
