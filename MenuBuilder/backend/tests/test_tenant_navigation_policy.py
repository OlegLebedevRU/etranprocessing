from types import SimpleNamespace

import pytest
from fastapi import HTTPException

from app.routers import auth as auth_router


@pytest.mark.anyio
async def test_auth_me_exposes_current_tenant_navigation_policy(monkeypatch):
    row = SimpleNamespace(
        org_name="Example",
        timezone="Europe/Moscow",
        site_mode="l4desk",
        default_site="l4desk",
        classic_licenses_enabled=False,
        l4desk_licenses_enabled=True,
    )

    class Session:
        async def __aenter__(self):
            return self

        async def __aexit__(self, *_args):
            return None

        async def execute(self, _query):
            return SimpleNamespace(first=lambda: row)

    monkeypatch.setattr(auth_router, "async_session", Session)
    response = await auth_router.me(
        {"org_id": 339, "role_id": 3, "role": "user", "username": "tenant-user"}
    )
    assert response.site_mode == "l4desk"
    assert response.default_site == "l4desk"
    assert response.classic_licenses_enabled is False
    assert response.l4desk_licenses_enabled is True


@pytest.mark.anyio
async def test_auth_me_rejects_missing_organization(monkeypatch):
    class Session:
        async def __aenter__(self):
            return self

        async def __aexit__(self, *_args):
            return None

        async def execute(self, _query):
            return SimpleNamespace(first=lambda: None)

    monkeypatch.setattr(auth_router, "async_session", Session)
    with pytest.raises(HTTPException) as error:
        await auth_router.me({"org_id": 339, "role_id": 3, "username": "tenant-user"})
    assert error.value.status_code == 404


@pytest.mark.anyio
async def test_auth_me_rejects_unavailable_organization_policy(monkeypatch):
    class Session:
        async def __aenter__(self):
            return self

        async def __aexit__(self, *_args):
            return None

        async def execute(self, _query):
            raise OSError("database unavailable")

    monkeypatch.setattr(auth_router, "async_session", Session)
    with pytest.raises(HTTPException) as error:
        await auth_router.me({"org_id": 339, "role_id": 3, "username": "tenant-user"})
    assert error.value.status_code == 503
