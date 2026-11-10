from contextlib import asynccontextmanager
from datetime import UTC, datetime, timedelta
from types import SimpleNamespace
from uuid import uuid4

import jwt
import pytest
from fastapi import HTTPException
from fastapi.security import HTTPAuthorizationCredentials
from starlette.requests import Request

from app import auth
from app.config import settings


@pytest.mark.parametrize(
    "changes",
    [{"aud": "wrong"}, {"iss": "wrong"}, {"exp": 1}, {"token_type": "tenant"}],
)
def test_api_token_domain_is_enforced(monkeypatch, changes):
    import secrets

    monkeypatch.setattr(settings, "jwt_secret_hex", secrets.token_hex(32))
    monkeypatch.setattr(settings, "jwt_issuer_mock_enabled", False)
    payload = {
        "sub": "owner",
        "jti": str(uuid4()),
        "token_type": "api_token",
        "iss": "menubuilder-api-token",
        "aud": "l4mcp",
        "exp": datetime.now(UTC) + timedelta(minutes=10),
    }
    payload.update(changes)
    token = jwt.encode(payload, settings.jwt_secret_bytes, algorithm="HS256")
    with pytest.raises(HTTPException) as exc:
        auth.decode_token(token)
    assert exc.value.status_code == 401


@pytest.mark.anyio
async def test_l4mcp_token_is_live_scoped_and_revocable(monkeypatch):
    import secrets

    secret_hex = secrets.token_hex(32)
    monkeypatch.setattr(settings, "jwt_secret_hex", secret_hex)
    jti = str(uuid4())
    token = jwt.encode(
        {
            "sub": "owner",
            "jti": jti,
            "orgId": 1000,
            "roleId": 5,
            "token_type": "api_token",
            "iss": "menubuilder-api-token",
            "aud": "l4mcp",
            "exp": datetime.now(UTC) + timedelta(minutes=10),
        },
        bytes.fromhex(secret_hex),
        algorithm="HS256",
    )
    token_user = SimpleNamespace(
        id=7,
        username="owner",
        org_id=1000,
        role_id=5,
        role="l4desk_owner",
        is_active=True,
    )
    active = True

    class FakeResult:
        def one_or_none(self):
            return (SimpleNamespace(jti=jti), token_user) if active else None

    class FakeSession:
        async def execute(self, statement):
            return FakeResult()

    @asynccontextmanager
    async def fake_session():
        yield FakeSession()

    monkeypatch.setattr(auth, "async_session", fake_session)
    creds = HTTPAuthorizationCredentials(scheme="Bearer", credentials=token)

    def request(path):
        return Request({"type": "http", "method": "GET", "path": path, "headers": []})

    user = await auth.get_current_user(request("/api/mcp/identity"), creds)
    history_user = await auth.get_current_user(request("/api/mcp/events/773"), creds)
    assert history_user["org_id"] == 1000
    assert (user["role_id"], user["org_id"], user["token_type"]) == (
        5,
        1000,
        "api_token",
    )
    with pytest.raises(HTTPException) as denied:
        await auth.get_current_user(request("/api/admin/users"), creds)
    assert denied.value.status_code == 403
    active = False
    with pytest.raises(HTTPException) as history_revoked:
        await auth.get_current_user(request("/api/mcp/events/773"), creds)
    assert history_revoked.value.status_code == 401
    with pytest.raises(HTTPException) as revoked:
        await auth.get_current_user(request("/api/mcp/identity"), creds)
    assert revoked.value.status_code == 401
