import base64
import json
from unittest.mock import patch

import pytest
from fastapi import HTTPException
from httpx import ASGITransport, AsyncClient

from app.config import settings
from app.main import app
from app.services import native_captcha
from app.services.smartcaptcha import captcha_config, verify_captcha


def answered(challenge):
    a, b = map(int, challenge["question"].split(" + "))
    return f"{challenge['token']}.{a + b}"


def test_native_signature_expiry_and_format():
    with patch.object(native_captcha.time, "time", return_value=1000):
        issued = native_captcha.challenge("fixture-only-secret")
        valid = answered(issued)
        native_captcha.verify("fixture-only-secret", valid)
        payload = json.loads(base64.urlsafe_b64decode(issued["token"].split(".")[0] + "=="))
        assert set(payload) == {"v", "a", "b", "exp"}
        for value in (None, "", valid + ".extra", issued["token"] + ".999", valid.replace(".", ".x", 1), "x" * 3000):
            with pytest.raises(ValueError):
                native_captcha.verify("fixture-only-secret", value)
        with pytest.raises(ValueError):
            native_captcha.verify("wrong-fixture-secret", valid)
    with patch.object(native_captcha.time, "time", return_value=1600), pytest.raises(ValueError):
        native_captcha.verify("fixture-only-secret", valid)
    with pytest.raises(RuntimeError):
        native_captcha.challenge("")
    with pytest.raises(RuntimeError):
        native_captcha.verify("", valid)


@pytest.mark.anyio
async def test_native_dispatch_and_public_endpoint(monkeypatch):
    monkeypatch.setattr(settings, "captcha_provider", "native")
    monkeypatch.setattr(settings, "captcha_native_secret", "fixture-only-secret")
    with patch("app.services.smartcaptcha.httpx.AsyncClient") as external:
        config = captcha_config()
        await verify_captcha(answered(config["challenge"]))
        with pytest.raises(HTTPException) as exc:
            await verify_captcha(config["challenge"]["token"] + ".999")
        assert exc.value.status_code == 400
        external.assert_not_called()
    async with AsyncClient(transport=ASGITransport(app), base_url="http://test") as client:
        response = await client.get("/api/auth/captcha")
    assert response.status_code == 200
    assert response.json()["provider"] == "native"
    assert "fixture-only-secret" not in response.text
    monkeypatch.setattr(settings, "captcha_native_secret", "")
    with pytest.raises(HTTPException) as exc:
        await verify_captcha(None)
    assert exc.value.status_code == 503
    with pytest.raises(HTTPException) as exc:
        captcha_config()
    assert exc.value.status_code == 503


@pytest.mark.anyio
async def test_explicit_provider_overrides_legacy_flag(monkeypatch):
    monkeypatch.setattr(settings, "smartcaptcha_enabled", False)
    monkeypatch.setattr(settings, "captcha_provider", "native")
    monkeypatch.setattr(settings, "captcha_native_secret", "fixture-only-secret")
    with pytest.raises(HTTPException):
        await verify_captcha(None)
    monkeypatch.setattr(settings, "captcha_provider", "off")
    await verify_captcha(None)
    assert captcha_config()["enabled"] is False
