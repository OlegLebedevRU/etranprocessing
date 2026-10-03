from unittest.mock import AsyncMock, MagicMock, patch

import httpx
import pytest
from fastapi import HTTPException
from httpx import ASGITransport, AsyncClient

from app.config import settings
from app.main import app
from app.services.smartcaptcha import verify_captcha


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.fixture(autouse=True)
def captcha_settings(monkeypatch):
    monkeypatch.setattr(settings, "smartcaptcha_enabled", True)
    monkeypatch.setattr(settings, "smartcaptcha_site_key", "test-client-key")
    monkeypatch.setattr(settings, "smartcaptcha_secret_key", "test-server-key")
    monkeypatch.setattr(settings, "smartcaptcha_allowed_hosts", ["portal.example"])


@pytest.mark.anyio
@pytest.mark.parametrize(
    "payload,expected",
    [
        ({"status": "ok", "host": "portal.example"}, None),
        ({"status": "failed"}, 400),
        ({"status": "ok", "host": "other.example"}, 400),
        ({"status": "ok"}, 400),
        ([], 503),
    ],
)
async def test_provider_validation(payload, expected):
    response = MagicMock()
    response.json.return_value = payload
    client = AsyncMock()
    client.post.return_value = response
    with patch("app.services.smartcaptcha.httpx.AsyncClient") as factory:
        factory.return_value.__aenter__.return_value = client
        if expected:
            with pytest.raises(HTTPException) as exc:
                await verify_captcha("test-token")
            assert exc.value.status_code == expected
        else:
            await verify_captcha("test-token")
        assert client.post.call_args.kwargs["data"] == {
            "secret": "test-server-key",
            "token": "test-token",
        }


@pytest.mark.anyio
async def test_outage_does_not_allow_login():
    with patch("app.services.smartcaptcha.httpx.AsyncClient") as factory:
        factory.return_value.__aenter__.side_effect = httpx.ConnectError("offline")
        with pytest.raises(HTTPException) as exc:
            await verify_captcha("test-token")
        assert exc.value.status_code == 503


@pytest.mark.anyio
async def test_unconfigured_is_closed(monkeypatch):
    monkeypatch.setattr(settings, "smartcaptcha_secret_key", "")
    with pytest.raises(HTTPException) as exc:
        await verify_captcha("test-token")
    assert exc.value.status_code == 503


@pytest.mark.anyio
async def test_login_requires_captcha_before_credentials():
    with patch("app.routers.auth.get_user_store") as store:
        async with AsyncClient(
            transport=ASGITransport(app), base_url="http://test"
        ) as client:
            for kwargs in [
                {"json": {"username": "test", "password": "invalid"}},
                {"auth": ("test", "invalid")},
            ]:
                response = await client.post("/api/auth/login", **kwargs)
                assert response.status_code == 400
            config = (await client.get("/api/auth/captcha")).json()
            assert config == {"enabled": True, "site_key": "test-client-key"}
            assert "test-server-key" not in str(config)
        store.assert_not_called()


@pytest.mark.anyio
async def test_registration_and_resend_require_captcha_before_writes():
    with patch("app.routers.registration.RegistrationService") as service:
        async with AsyncClient(
            transport=ASGITransport(app), base_url="http://test"
        ) as client:
            for path, body in [
                (
                    "/api/auth/register",
                    {"email": "user@example.com", "password": "example-password"},
                ),
                ("/api/auth/register/resend", {"email": "user@example.com"}),
            ]:
                response = await client.post(path, json=body)
                assert response.status_code == 400
        service.assert_not_called()
