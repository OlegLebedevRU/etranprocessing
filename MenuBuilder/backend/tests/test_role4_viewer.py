from __future__ import annotations

import pytest
from httpx import ASGITransport, AsyncClient

from app.auth import create_tenant_token, get_current_user
from app.database import get_db
from app.main import app
from app.security.permissions import (
    ALL_PERMISSIONS,
    PERMISSION_MONITORING_VIEW,
    PERMISSION_REPORTS_PAYMENTS_VIEW,
)
from app.user_store import UserRecord


@pytest.fixture
def role3_user() -> UserRecord:
    return UserRecord(
        id=101,
        username="tenant_admin_3",
        md5_password="hash",
        org_id=42,
        role_id=3,
        role="user",
        is_superuser=False,
    )


@pytest.fixture
def role4_viewer() -> UserRecord:
    return UserRecord(
        id=102,
        username="viewer_4",
        md5_password="hash",
        org_id=42,
        role_id=4,
        role="viewer",
        is_superuser=False,
        permissions=[PERMISSION_MONITORING_VIEW, PERMISSION_REPORTS_PAYMENTS_VIEW],
    )


@pytest.fixture
def role3_token(role3_user: UserRecord) -> str:
    return create_tenant_token(role3_user, target_org_id=42)


@pytest.fixture
def role4_token(role4_viewer: UserRecord) -> str:
    return create_tenant_token(role4_viewer, target_org_id=42)


@pytest.mark.anyio
async def test_auth_me_permissions_and_role():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as ac:
        # Override current user as role 4
        viewer_dict = {
            "user_id": 102,
            "username": "viewer_4",
            "org_id": 42,
            "role_id": 4,
            "role": "viewer",
            "is_superuser": False,
            "can_switch_org": False,
            "permissions": [PERMISSION_MONITORING_VIEW],
        }
        app.dependency_overrides[get_current_user] = lambda: viewer_dict
        try:
            resp = await ac.get("/api/auth/me")
            assert resp.status_code == 200
            data = resp.json()
            assert data["role_id"] == 4
            assert data["role"] == "viewer"
            assert data["can_switch_org"] is False
            assert data["permissions"] == [PERMISSION_MONITORING_VIEW]
        finally:
            app.dependency_overrides.pop(get_current_user, None)

        # Test role 3 has full permissions
        user3_dict = {
            "user_id": 101,
            "username": "tenant_admin_3",
            "org_id": 42,
            "role_id": 3,
            "role": "user",
            "is_superuser": False,
            "can_switch_org": False,
        }
        app.dependency_overrides[get_current_user] = lambda: user3_dict
        try:
            resp = await ac.get("/api/auth/me")
            assert resp.status_code == 200
            data = resp.json()
            assert data["role_id"] == 3
            assert data["role"] == "user"
            assert set(data["permissions"]) == set(ALL_PERMISSIONS)
        finally:
            app.dependency_overrides.pop(get_current_user, None)


@pytest.mark.anyio
async def test_readonly_guard_blocks_mutations():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as ac:
        viewer_dict = {
            "user_id": 102,
            "username": "viewer_4",
            "org_id": 42,
            "role_id": 4,
            "role": "viewer",
            "is_superuser": False,
            "permissions": [
                PERMISSION_MONITORING_VIEW,
                PERMISSION_REPORTS_PAYMENTS_VIEW,
            ],
        }
        app.dependency_overrides[get_current_user] = lambda: viewer_dict
        try:
            # POST to reports should be blocked by readonly guard
            resp = await ac.post(
                "/api/reports/inkass/1/apply-calculation",
                json={"strategy": "no_change"},
            )
            assert resp.status_code == 403
            assert "только для чтения" in resp.json()["detail"]

            # POST to settings terminals update
            resp2 = await ac.patch(
                "/api/settings/terminals/1", json={"address": "new addr"}
            )
            assert resp2.status_code == 403
        finally:
            app.dependency_overrides.pop(get_current_user, None)


@pytest.mark.anyio
async def test_permission_guard_allows_and_blocks():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as ac:
        # Viewer WITHOUT monitoring:view
        viewer_dict = {
            "user_id": 102,
            "username": "viewer_4",
            "org_id": 42,
            "role_id": 4,
            "role": "viewer",
            "is_superuser": False,
            "permissions": [PERMISSION_REPORTS_PAYMENTS_VIEW],
        }
        app.dependency_overrides[get_current_user] = lambda: viewer_dict
        try:
            # Monitoring requires monitoring:view -> 403
            resp = await ac.get("/api/monitoring")
            assert resp.status_code == 403
            assert "не предоставлен" in resp.json()["detail"]
        finally:
            app.dependency_overrides.pop(get_current_user, None)


@pytest.mark.anyio
async def test_settings_users_rbac():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as ac:
        # Role 4 cannot access /api/settings/users
        viewer_dict = {
            "user_id": 102,
            "username": "viewer_4",
            "org_id": 42,
            "role_id": 4,
            "role": "viewer",
            "is_superuser": False,
            "permissions": [PERMISSION_MONITORING_VIEW],
        }
        app.dependency_overrides[get_current_user] = lambda: viewer_dict
        try:
            resp = await ac.get("/api/settings/users")
            assert resp.status_code == 403
            assert "только администратору" in resp.json()["detail"]
        finally:
            app.dependency_overrides.pop(get_current_user, None)


@pytest.mark.anyio
async def test_settings_users_validation():
    from unittest.mock import AsyncMock, MagicMock

    mock_db = AsyncMock()
    mock_res = MagicMock()
    mock_res.scalar_one_or_none.return_value = None
    mock_db.execute.return_value = mock_res

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as ac:
        admin_dict = {
            "user_id": 101,
            "username": "admin_3",
            "org_id": 42,
            "role_id": 3,
            "role": "user",
            "is_superuser": False,
        }
        app.dependency_overrides[get_current_user] = lambda: admin_dict
        app.dependency_overrides[get_db] = lambda: mock_db
        try:
            # Test invalid permission code validation
            resp = await ac.post(
                "/api/settings/users",
                json={
                    "username": "new_viewer",
                    "password": "secret_password",
                    "permissions": ["invalid:permission:code"],
                },
            )
            assert resp.status_code == 400
            assert "Недопустимые разрешения" in resp.json()["detail"]
        finally:
            app.dependency_overrides.pop(get_current_user, None)
            app.dependency_overrides.pop(get_db, None)
