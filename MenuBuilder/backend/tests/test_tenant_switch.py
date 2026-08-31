from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from fastapi import HTTPException
from httpx import ASGITransport, AsyncClient
from starlette.requests import Request

from app.auth import (
    create_access_token,
    create_master_token,
    create_tenant_token,
    decode_token,
    get_current_user,
)
from app.main import app
from app.models import Org
from app.user_store import UserRecord, get_user_store


def _request_with_headers(headers: dict[str, str]) -> Request:
    return Request(
        {
            "type": "http",
            "headers": [
                (name.lower().encode(), value.encode())
                for name, value in headers.items()
            ],
        }
    )


@pytest.mark.anyio
async def test_canonical_nginx_headers_preserve_switched_tenant_context():
    user = await get_current_user(
        _request_with_headers(
            {
                "X-User-Id": "17",
                "X-Org-Id": "223",
                "X-User-Role": "superuser",
            }
        ),
        None,
    )

    assert user["user_id"] == 17
    assert user["org_id"] == 223
    assert user["role"] == "superuser"


@pytest.mark.anyio
async def test_legacy_nginx_identity_headers_are_not_trusted():
    request = _request_with_headers(
        {"jwt-sub": "17", "jwt-org": "223", "jwt-role": "superuser"}
    )

    with pytest.raises(HTTPException) as exc_info:
        await get_current_user(request, None)

    assert exc_info.value.status_code == 401


@pytest.fixture(autouse=True)
def reset_dependency_overrides():
    app.dependency_overrides.clear()
    yield
    app.dependency_overrides.clear()


@pytest.mark.anyio
async def test_user_store_recognition():
    store = get_user_store()

    # o.lebedev should be recognized as superuser
    user_oleg = await store.get_by_username("o.lebedev")
    assert user_oleg is not None
    assert user_oleg.is_superuser is True
    assert user_oleg.role == "superuser"

    # test user should be regular user
    user_test = await store.get_by_username("test")
    assert user_test is not None
    assert user_test.is_superuser is False
    assert user_test.role == "user"


@pytest.mark.anyio
async def test_token_creation_and_claims():
    user = UserRecord(
        username="o.lebedev",
        md5_password="hash",
        org_id=1,
        role="superuser",
        is_superuser=True,
    )

    # Master token
    master_jwt = create_master_token(user)
    master_claims = decode_token(master_jwt)
    assert master_claims["sub"] == "o.lebedev"
    assert master_claims["role"] == "superuser"
    assert master_claims["token_type"] == "master"
    assert master_claims["is_superuser"] is True
    assert master_claims["can_switch_org"] is True

    # Tenant token for switched org
    tenant_jwt = create_tenant_token(user, target_org_id=223)
    tenant_claims = decode_token(tenant_jwt)
    assert tenant_claims["sub"] == "o.lebedev"
    assert tenant_claims["org"] == "223"
    assert tenant_claims["org_id"] == 223
    assert tenant_claims["token_type"] == "tenant"
    assert tenant_claims["orig_sub"] == "o.lebedev"
    assert tenant_claims["is_imp"] is True


@pytest.mark.anyio
async def test_admin_tenants_available_endpoint_authorization():
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # 1. Regular user trying to list available tenants -> 403 Forbidden
        reg_token = create_access_token(
            {"sub": "test", "org": "1", "role": "user", "token_type": "tenant"}
        )
        resp = await client.get(
            "/api/admin/tenants/available",
            headers={"Authorization": f"Bearer {reg_token}"},
        )
        assert resp.status_code == 403
        assert "Superuser privilege required" in resp.text

        # 2. Superuser listing available tenants
        su_token = create_access_token(
            {
                "sub": "o.lebedev",
                "role": "superuser",
                "is_superuser": True,
                "token_type": "master",
            }
        )
        mock_orgs = [
            Org(
                org_id=1,
                org_name="PLATERRA",
                name="ПЛАТЕРРА",
                status=1,
                is_active=True,
            ),
            Org(
                org_id=223,
                org_name="DEMO Company",
                name="DEMO Company",
                status=1,
                is_active=True,
            ),
        ]

        mock_session = AsyncMock()
        mock_result = MagicMock()
        mock_scalars = MagicMock()
        mock_scalars.all.return_value = mock_orgs
        mock_result.scalars.return_value = mock_scalars
        mock_session.execute.return_value = mock_result

        with patch("app.routers.admin_tenants.async_session") as mock_async_session:
            mock_async_session.return_value.__aenter__.return_value = mock_session
            resp = await client.get(
                "/api/admin/tenants/available",
                headers={"Authorization": f"Bearer {su_token}"},
            )
            assert resp.status_code == 200
            data = resp.json()
            assert len(data) == 2
            assert data[0]["org_id"] == 1
            assert data[0]["org_name"] == "PLATERRA"
            assert data[1]["org_id"] == 223


@pytest.mark.anyio
async def test_admin_tenants_switch_endpoint():
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # 1. Regular user trying to switch tenant -> 403 Forbidden
        reg_token = create_access_token(
            {"sub": "test", "org": "1", "role": "user", "token_type": "tenant"}
        )
        resp = await client.post(
            "/api/admin/tenants/switch",
            json={"org_id": 223},
            headers={"Authorization": f"Bearer {reg_token}"},
        )
        assert resp.status_code == 403

        # 2. Superuser switching to existing org
        su_token = create_access_token(
            {
                "sub": "o.lebedev",
                "role": "superuser",
                "is_superuser": True,
                "token_type": "master",
            }
        )
        mock_target_org = Org(
            org_id=223,
            org_name="DEMO Company",
            name="DEMO Company",
            status=1,
            is_active=True,
        )

        mock_session = AsyncMock()
        mock_result = MagicMock()
        mock_result.scalar_one_or_none.return_value = mock_target_org
        mock_session.execute.return_value = mock_result

        with patch("app.routers.admin_tenants.async_session") as mock_async_session:
            mock_async_session.return_value.__aenter__.return_value = mock_session
            resp = await client.post(
                "/api/admin/tenants/switch",
                json={"org_id": 223},
                headers={"Authorization": f"Bearer {su_token}"},
            )
            assert resp.status_code == 200
            data = resp.json()
            assert data["org_id"] == 223
            assert data["org_name"] == "DEMO Company"
            assert "access_token" in data

            # Verify the issued tenant token contains expected claims
            issued_claims = decode_token(data["access_token"])
            assert issued_claims["sub"] == "o.lebedev"
            assert issued_claims["org"] == "223"
            assert issued_claims["org_id"] == 223
            assert issued_claims["token_type"] == "tenant"
            assert issued_claims["orig_sub"] == "o.lebedev"
            assert issued_claims["is_imp"] is True


@pytest.mark.anyio
async def test_auth_me_endpoint_returns_superuser_flag():
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        su_token = create_access_token(
            {
                "sub": "o.lebedev",
                "org": "1",
                "role": "superuser",
                "is_superuser": True,
                "token_type": "tenant",
            }
        )

        mock_session = AsyncMock()
        mock_result = MagicMock()
        mock_result.scalar_one_or_none.return_value = "PLATERRA"
        mock_session.execute.return_value = mock_result

        with patch("app.routers.auth.async_session") as mock_async_session:
            mock_async_session.return_value.__aenter__.return_value = mock_session
            resp = await client.get(
                "/api/auth/me",
                headers={"Authorization": f"Bearer {su_token}"},
            )
            assert resp.status_code == 200
            data = resp.json()
            assert data["username"] == "o.lebedev"
            assert data["org_id"] == 1
            assert data["role"] == "superuser"
            assert data["is_superuser"] is True
            assert data["can_switch_org"] is True
            assert data["org_name"] == "PLATERRA"
