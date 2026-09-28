from types import SimpleNamespace

import pytest

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
