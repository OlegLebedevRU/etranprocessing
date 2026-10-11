import asyncio
import base64
import hashlib
import hmac
import json
import time
from unittest.mock import patch

import httpx
import pytest
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from fastapi import HTTPException
from fastapi.testclient import TestClient
from jose import jwt

from app.auth import decode_token
from app.config import settings
from app.jwt_signer import JwtSigner, SignedParams, SignerSettings, app
from app.services.jwt_issuer import ISSUER_V2_ACCESS_MANDATORY_CLAIMS, JwtIssuerClient


@pytest.fixture
def signer_material(tmp_path):
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    private = tmp_path / "ephemeral.pem"
    private.write_bytes(
        key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        )
    )
    public = (
        key.public_key()
        .public_bytes(
            serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo
        )
        .decode()
    )
    return private, public


@pytest.fixture
def signer(signer_material):
    private, public = signer_material
    return JwtSigner(
        SignerSettings(
            JWT_SIGNER_PRIVATE_KEY_FILE=str(private),
            JWT_PUBLIC_KEY=public,
            SERVICE_TO_YC_SERVICE_SECRET="fixture-service-secret",
        )
    )


def signed_params(**kwargs):
    client = JwtIssuerClient(secret="fixture-service-secret")
    return SignedParams.model_validate(
        client.build_signed_request(user_id=123, org_id=42, role_id=2, **kwargs)[
            "params"
        ]
    )


def resign(params, **changes):
    body = json.loads(params.requestBody)
    body.update(changes)
    text = json.dumps(body, separators=(",", ":"))
    outer = params.model_dump()
    outer.update({k: v for k, v in changes.items() if k in outer})
    outer["requestBody"] = text
    message = f"{outer['timestamp']}.{outer['nonce']}.{text}"
    outer["signature"] = (
        "sha256="
        + base64.b64encode(
            hmac.new(
                b"fixture-service-secret", message.encode(), hashlib.sha256
            ).digest()
        ).decode()
    )
    return SignedParams.model_validate(outer)


def test_real_signature_and_menu_contract(signer, signer_material, monkeypatch):
    result = signer.issue(
        signed_params(
            username="fixture",
            is_superuser=True,
            is_imp=True,
            orig_sub="original",
            sid=7,
            access_ttl_minutes=15,
            refresh_ttl_days=2,
        )
    )
    claims = jwt.decode(
        result["accessToken"],
        signer_material[1],
        algorithms=["RS256"],
        audience=settings.jwt_issuer_aud,
        issuer=settings.jwt_issuer_iss,
    )
    assert ISSUER_V2_ACCESS_MANDATORY_CLAIMS <= claims.keys()
    assert (claims["org"], claims["sub"], claims["role"]) == ("42", "123", "2")
    assert claims["sid"] == 7 and claims["orig_sub"] == "original"
    assert claims["is_superuser"] is True and claims["is_imp"] is True
    assert claims["exp"] - claims["iat"] == result["expiresIn"] == 900
    assert result["refreshExpiresIn"] == 172800
    assert result["refreshToken"] != result["accessToken"]
    assert (
        jwt.get_unverified_header(result["accessToken"])["kid"]
        == settings.jwt_issuer_kid
    )
    monkeypatch.setattr(settings, "jwt_public_key", signer_material[1])
    monkeypatch.setattr(settings, "jwt_issuer_mock_enabled", False)
    assert decode_token(result["accessToken"])["orgId"] == 42


def test_retries_are_idempotent_and_changed_nonce_body_is_rejected(signer):
    params = signed_params()
    assert signer.issue(params) == signer.issue(params)
    altered = resign(signed_params(username="other"), nonce=params.nonce)
    with pytest.raises(HTTPException) as exc:
        signer.issue(altered)
    assert exc.value.status_code == 409


@pytest.mark.parametrize(
    "field,value,code",
    [
        ("signature", "sha256=wrong", 401),
        ("orgId", 999, 400),
        ("kid", "other-key", 403),
    ],
)
def test_unsigned_envelope_changes_are_rejected(signer, field, value, code):
    with pytest.raises(HTTPException) as exc:
        signer.issue(signed_params().model_copy(update={field: value}))
    assert exc.value.status_code == code


@pytest.mark.parametrize(
    "changes,code",
    [
        ({"timestamp": int(time.time()) - 1000}, 401),
        ({"timestamp": int(time.time()) + 1000}, 401),
        ({"aud": "other"}, 403),
        ({"clientId": "other"}, 403),
        ({"iss": "other"}, 403),
        ({"accessTtlMinutes": 61}, 400),
        ({"refreshTtlDays": 31}, 400),
        ({"isImp": True}, 400),
        ({"isSuperuser": "true"}, 400),
    ],
)
def test_signed_policy_violations_are_rejected(signer, changes, code):
    with pytest.raises(HTTPException) as exc:
        signer.issue(resign(signed_params(), **changes))
    assert exc.value.status_code == code


def test_nonce_capacity_does_not_evict_live_replay_protection(signer):
    signer.config.nonce_capacity = 1
    first = signed_params()
    result = signer.issue(first)
    with pytest.raises(HTTPException) as exc:
        signer.issue(signed_params())
    assert exc.value.status_code == 429
    assert signer.issue(first) == result


def test_startup_rejects_missing_secret_and_wrong_key(signer_material):
    private, public = signer_material
    with pytest.raises(ValueError, match="authentication secret"):
        JwtSigner(
            SignerSettings(
                JWT_SIGNER_PRIVATE_KEY_FILE=str(private),
                JWT_PUBLIC_KEY=public,
                SERVICE_TO_YC_SERVICE_SECRET="",
            )
        )
    with pytest.raises(ValueError, match="do not match"):
        JwtSigner(
            SignerSettings(
                JWT_SIGNER_PRIVATE_KEY_FILE=str(private),
                SERVICE_TO_YC_SERVICE_SECRET="fixture-service-secret",
            )
        )


def test_http_lifespan_and_endpoint(signer_material, monkeypatch):
    private, public = signer_material
    monkeypatch.setenv("JWT_SIGNER_PRIVATE_KEY_FILE", str(private))
    monkeypatch.setenv("JWT_PUBLIC_KEY", public)
    monkeypatch.setenv("SERVICE_TO_YC_SERVICE_SECRET", "fixture-service-secret")
    with TestClient(app) as client:
        assert client.get("/health").status_code == 200
        response = client.post(
            "/etranprocessing/jwt", json={"params": signed_params().model_dump()}
        )
        assert response.status_code == 200
        assert response.json()["refreshToken"]
        assert client.get("/docs").status_code == 404


@pytest.mark.anyio
@pytest.mark.parametrize("fallback", [False, True])
async def test_internal_http_flow_and_optional_reserve(
    signer, signer_material, monkeypatch, fallback
):
    monkeypatch.setattr(
        settings, "jwt_issuer_internal_url", "http://signer.invalid/etranprocessing/jwt"
    )
    monkeypatch.setattr(settings, "jwt_issuer_url", "https://reserve.invalid/jwt")
    monkeypatch.setattr(settings, "jwt_issuer_fallback_enabled", fallback)
    monkeypatch.setattr(settings, "jwt_public_key", signer_material[1])
    client = JwtIssuerClient(secret="fixture-service-secret", mock_enabled=False)
    calls = []

    async def post(url, **kwargs):
        calls.append(url)
        if fallback and len(calls) <= 2:
            raise httpx.ConnectError("fixture unavailable")
        return httpx.Response(
            200,
            json=signer.issue(SignedParams.model_validate(kwargs["json"]["params"])),
        )

    with patch("httpx.AsyncClient.post", side_effect=post):
        result = await client.issue_tokens(
            user_id=123,
            org_id=42,
            role_id=2,
            username="fixture",
            is_superuser=True,
            sid=7,
        )
    assert decode_token(result["accessToken"])["sid"] == 7
    assert calls == (
        [settings.jwt_issuer_internal_url] * 2 + [settings.jwt_issuer_url]
        if fallback
        else [settings.jwt_issuer_internal_url]
    )


@pytest.mark.anyio
async def test_missing_secret_never_issues_mock_tokens():
    with pytest.raises(HTTPException) as exc:
        await JwtIssuerClient(secret="", mock_enabled=False).issue_tokens(
            user_id=123, org_id=42, role_id=2
        )
    assert exc.value.status_code == 503


@pytest.mark.anyio
@pytest.mark.parametrize("status_code", [400, 401, 403])
async def test_auth_rejection_never_falls_back(monkeypatch, status_code):
    monkeypatch.setattr(
        settings, "jwt_issuer_internal_url", "http://signer.invalid/jwt"
    )
    monkeypatch.setattr(settings, "jwt_issuer_fallback_enabled", True)
    with (
        patch(
            "httpx.AsyncClient.post", return_value=httpx.Response(status_code)
        ) as post,
        pytest.raises(HTTPException),
    ):
        await JwtIssuerClient(
            secret="fixture-service-secret", mock_enabled=False
        ).issue_tokens(user_id=123, org_id=42, role_id=2)
    assert post.call_count == 1


@pytest.mark.anyio
async def test_distinct_sessions_do_not_share_issuance(
    signer, signer_material, monkeypatch
):
    monkeypatch.setattr(
        settings, "jwt_issuer_internal_url", "http://signer.invalid/jwt"
    )
    monkeypatch.setattr(settings, "jwt_public_key", signer_material[1])
    client = JwtIssuerClient(secret="fixture-service-secret", mock_enabled=False)

    async def post(_url, **kwargs):
        await asyncio.sleep(0.01)
        return httpx.Response(
            200,
            json=signer.issue(SignedParams.model_validate(kwargs["json"]["params"])),
        )

    with patch("httpx.AsyncClient.post", side_effect=post) as mocked:
        first, second = await asyncio.gather(
            client.issue_tokens(user_id=123, org_id=42, role_id=2, sid=1),
            client.issue_tokens(user_id=123, org_id=42, role_id=2, sid=2),
        )
    assert mocked.call_count == 2
    assert jwt.get_unverified_claims(first["accessToken"])["sid"] == 1
    assert jwt.get_unverified_claims(second["accessToken"])["sid"] == 2


@pytest.mark.anyio
async def test_invalid_signature_response_never_falls_back(
    signer, signer_material, monkeypatch
):
    monkeypatch.setattr(
        settings, "jwt_issuer_internal_url", "http://signer.invalid/jwt"
    )
    monkeypatch.setattr(settings, "jwt_issuer_fallback_enabled", True)
    # Configured production public key differs from the ephemeral signer key.
    with (
        patch(
            "httpx.AsyncClient.post",
            return_value=httpx.Response(200, json=signer.issue(signed_params())),
        ) as post,
        pytest.raises(HTTPException) as exc,
    ):
        await JwtIssuerClient(
            secret="fixture-service-secret", mock_enabled=False
        ).issue_tokens(user_id=123, org_id=42, role_id=2)
    assert exc.value.status_code == 502
    assert post.call_count == 1


@pytest.mark.anyio
async def test_disabled_reserve_stays_unused(monkeypatch):
    monkeypatch.setattr(
        settings, "jwt_issuer_internal_url", "http://signer.invalid/jwt"
    )
    monkeypatch.setattr(settings, "jwt_issuer_fallback_enabled", False)
    with (
        patch(
            "httpx.AsyncClient.post",
            side_effect=httpx.ConnectError("fixture unavailable"),
        ) as post,
        pytest.raises(HTTPException) as exc,
    ):
        await JwtIssuerClient(
            secret="fixture-service-secret", mock_enabled=False
        ).issue_tokens(user_id=123, org_id=42, role_id=2)
    assert exc.value.status_code == 503
    assert post.call_count == 2
    assert all(
        call.args[0] == settings.jwt_issuer_internal_url for call in post.call_args_list
    )


@pytest.mark.anyio
async def test_internal_and_reserve_use_separate_authentication_secrets(signer, signer_material, monkeypatch):
    monkeypatch.setattr(settings, "jwt_issuer_internal_url", "http://signer.invalid/jwt")
    monkeypatch.setattr(settings, "jwt_issuer_url", "https://reserve.invalid/jwt")
    monkeypatch.setattr(settings, "jwt_issuer_internal_secret", "fixture-service-secret")
    monkeypatch.setattr(settings, "service_to_yc_service_secret", "fixture-reserve-secret")
    monkeypatch.setattr(settings, "jwt_issuer_fallback_enabled", True)
    monkeypatch.setattr(settings, "jwt_public_key", signer_material[1])
    reserve = JwtSigner(signer.config.model_copy(update={"service_secret": "fixture-reserve-secret"}))
    calls = []

    async def post(url, **kwargs):
        params = SignedParams.model_validate(kwargs["json"]["params"])
        calls.append(url)
        if url == settings.jwt_issuer_internal_url:
            signer.issue(params)  # Authentication must succeed before simulated outage.
            raise httpx.ConnectError("fixture outage")
        return httpx.Response(200, json=reserve.issue(params))

    with patch("httpx.AsyncClient.post", side_effect=post):
        result = await JwtIssuerClient(mock_enabled=False).issue_tokens(user_id=123, org_id=42, role_id=2)
    assert result["accessToken"]
    assert calls == [settings.jwt_issuer_internal_url] * 2 + [settings.jwt_issuer_url]
