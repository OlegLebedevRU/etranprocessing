from __future__ import annotations

import pytest
from httpx import ASGITransport, AsyncClient

from app.auth import create_master_token, create_tenant_token
from app.main import app
from app.user_store import UserRecord


@pytest.fixture
def master_token():
    su = UserRecord(
        id=1,
        username="o.lebedev",
        md5_password="eaf21fcabcffeb1f97f01a4fc02ece63",
        org_id=1,
        role="superuser",
        is_superuser=True,
    )
    return create_master_token(su)


@pytest.mark.anyio
async def test_admin_users_endpoints_forbidden_for_regular_user():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as ac:
        # Regular user token
        user = UserRecord(
            id=10,
            username="regular_user",
            md5_password="abc",
            org_id=2,
            role="user",
            is_superuser=False,
        )
        token = create_tenant_token(user, target_org_id=2)

        resp = await ac.get(
            "/api/admin/users",
            headers={"Authorization": f"Bearer {token}"},
        )
        assert resp.status_code == 403


@pytest.mark.anyio
async def test_admin_users_list_and_create(master_token):
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as ac:
        # Check list endpoint
        resp = await ac.get(
            "/api/admin/users",
            headers={"Authorization": f"Bearer {master_token}"},
        )
        # Should return 200 with list
        assert resp.status_code == 200
        data = resp.json()
        assert "total" in data
        assert "items" in data
