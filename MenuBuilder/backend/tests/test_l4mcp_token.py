from contextlib import asynccontextmanager
from datetime import UTC, datetime, timedelta
from types import SimpleNamespace
from uuid import uuid4

import pytest
from fastapi import HTTPException
from fastapi.security import HTTPAuthorizationCredentials
from jose import jwt
from starlette.requests import Request

from app import auth
from app.config import settings


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
    assert (user["role_id"], user["org_id"], user["token_type"]) == (
        5,
        1000,
        "api_token",
    )
    with pytest.raises(HTTPException) as denied:
        await auth.get_current_user(request("/api/admin/users"), creds)
    assert denied.value.status_code == 403
    active = False
    with pytest.raises(HTTPException) as revoked:
        await auth.get_current_user(request("/api/mcp/identity"), creds)
    assert revoked.value.status_code == 401
