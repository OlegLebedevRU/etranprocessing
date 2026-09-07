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


@pytest.fixture
def switched_superuser_token() -> str:
    su = UserRecord(
        id=1,
        username="o.lebedev",
        md5_password="hash",
        org_id=0,
        role_id=1,
        role="superuser",
        is_superuser=True,
    )
    return create_tenant_token(su, target_org_id=42)


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


@pytest.mark.anyio
async def test_cookie_based_authentication(
    role3_token: str, role4_token: str, switched_superuser_token: str
):
    """Verify that require_permission and require_readonly_guard work with cookie-based auth (accessToken)."""
    from unittest.mock import AsyncMock, MagicMock, patch

    mock_session = AsyncMock()
    mock_session.scalar.return_value = 0
    mock_res = MagicMock()
    mock_res.fetchall.return_value = []
    mock_session.execute.return_value = mock_res
    mock_cm = AsyncMock()
    mock_cm.__aenter__.return_value = mock_session

    transport = ASGITransport(app=app)
    with patch("app.routers.monitoring.async_session", return_value=mock_cm):
        async with AsyncClient(
            transport=transport,
            base_url="http://test",
            cookies={"accessToken": role3_token},
        ) as ac_role3:
            # 1. Role 3 via cookie -> 200 OK
            resp_role3 = await ac_role3.get("/api/monitoring")
            assert resp_role3.status_code == 200

        async with AsyncClient(
            transport=transport,
            base_url="http://test",
            cookies={"accessToken": switched_superuser_token},
        ) as ac_su:
            # 2. Superuser switched to tenant via cookie -> 200 OK
            resp_su = await ac_su.get("/api/monitoring")
            assert resp_su.status_code == 200

        async with AsyncClient(
            transport=transport,
            base_url="http://test",
            cookies={"accessToken": role4_token},
        ) as ac_role4:
            # 3. Role 4 with monitoring:view via cookie -> 200 OK
            resp_role4 = await ac_role4.get("/api/monitoring")
            assert resp_role4.status_code == 200

            # 4. Role 4 via cookie trying mutating method -> 403 Forbidden by readonly guard
            resp_role4_mut = await ac_role4.post(
                "/api/reports/inkass/1/apply-calculation",
                headers={"X-Requested-With": "XMLHttpRequest"},
                json={"strategy": "no_change"},
            )
            assert resp_role4_mut.status_code == 403
            assert "только для чтения" in resp_role4_mut.json()["detail"]

        async with AsyncClient(transport=transport, base_url="http://test") as ac_unauth:
            # 5. No credentials/cookie -> 401 Unauthorized
            resp_unauth = await ac_unauth.get("/api/monitoring")
            assert resp_unauth.status_code == 401
