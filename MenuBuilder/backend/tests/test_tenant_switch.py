from datetime import UTC, datetime, timedelta
from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from fastapi import HTTPException
from fastapi.security import HTTPAuthorizationCredentials
from httpx import ASGITransport, AsyncClient
from jose import jwt
from starlette.requests import Request

from app.auth import (
    create_access_token,
    create_master_token,
    create_tenant_token,
    decode_token,
    get_current_user,
    require_tenant_context,
)
from app.config import settings
from app.main import app
from app.models import Org
from app.services.jwt_issuer import jwt_issuer_client
from app.user_store import ConfigUserStore, UserRecord, get_user_store


def _request_with_headers(headers: dict[str, str]) -> Request:
    return Request(
        {
            "type": "http",
            "method": "GET",
            "path": "/",
            "headers": [
                (name.lower().encode(), value.encode())
                for name, value in headers.items()
            ],
        }
    )


@pytest.mark.anyio
async def test_canonical_nginx_headers_preserve_switched_tenant_context():
    request = _request_with_headers(
        {
            "X-User-Id": "17",
            "X-Org-Id": "223",
            "X-User-Role": "superuser",
        }
    )

    # 1. By default, trust_proxy_identity_headers is False -> 401 Unauthorized
    settings.trust_proxy_identity_headers = False
    with pytest.raises(HTTPException) as exc_info:
        await get_current_user(request, None)
    assert exc_info.value.status_code == 401

    # 2. When explicitly enabled, trust forwarded identity headers
    settings.trust_proxy_identity_headers = True
    try:
        user = await get_current_user(request, None)
        assert user["user_id"] == 17
        assert user["org_id"] == 223
        assert user["role"] == "superuser"
        assert user["is_superuser"] is True
    finally:
        settings.trust_proxy_identity_headers = False


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
async def test_user_store_no_hardcoded_superuser():
    """Verify that o.lebedev is NOT a superuser if config does not mark them as superuser."""
    store = ConfigUserStore()
    with patch.object(
        settings,
        "auth_users",
        '[{"username":"o.lebedev","org_id":1,"role":"user","is_superuser":false}]',
    ):
        user = await store.get_by_username("o.lebedev")
        assert user is not None
        assert user.is_superuser is False
        assert user.role == "user"


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

        store = get_user_store()
        await store.create_session(
            user_id=1,
            refresh_token="su_test_refresh_switch",
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
                cookies={"refreshToken": "su_test_refresh_switch"},
                headers={"Authorization": f"Bearer {su_token}"},
            )
            assert resp.status_code == 200
            data = resp.json()
            assert data["org_id"] == 223
            assert data["org_name"] == "DEMO Company"
            assert data["is_superuser"] is True
            assert data["is_impersonated"] is True
            assert "access_token" in data

            # Verify the issued tenant token contains expected claims
            issued_claims = decode_token(data["access_token"])
            # Issuer v2 contract: sub is str(userId) ("1"), username/orig_sub are "o.lebedev", orgId is int
            assert issued_claims["sub"] == "1"
            assert issued_claims["username"] == "o.lebedev"
            assert issued_claims["org"] == "223"
            assert issued_claims["orgId"] == 223
            assert issued_claims["token_type"] == "tenant"
            assert issued_claims["orig_sub"] == "o.lebedev"
            assert issued_claims["is_imp"] is True


@pytest.mark.anyio
async def test_step2_superuser_switch_refresh_and_session_count():
    """Verify Step 2 requirements:
    (a) superuser: login -> switch(223) -> refresh with expired access token -> new token has orgId=223
    (b) switch does not increase user_sessions count
    (e) switch(0) -> token orgId=0, org_name='Платформа'
    """
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # 1. Login superuser
        login_resp = await client.post(
            "/api/auth/login",
            json={
                "username": "o.lebedev",
                "password": "eaf21fcabcffeb1f97f01a4fc02ece63",
            },
        )
        assert login_resp.status_code == 200
        cookies = dict(login_resp.cookies)
        assert "refreshToken" in cookies
        assert "accessToken" in cookies

        store = get_user_store()
        sessions_before = len(store._in_memory_sessions)

        # 2. Switch to 223
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
            switch_resp = await client.post(
                "/api/admin/tenants/switch",
                json={"org_id": 223},
                cookies=cookies,
                headers={
                    "Authorization": f"Bearer {login_resp.json()['access_token']}"
                },
            )
            assert switch_resp.status_code == 200
            switch_data = switch_resp.json()
            assert switch_data["org_id"] == 223
            assert switch_data["is_superuser"] is True
            assert switch_data["is_impersonated"] is True

        # (b) Verify switch did not create a new session
        sessions_after = len(store._in_memory_sessions)
        assert sessions_after == sessions_before

        # Update cookies with new accessToken
        cookies.update(dict(switch_resp.cookies))

        # (a) Refresh with expired access token
        expired_token = create_access_token(
            {"sub": "o.lebedev", "org": "999", "role": "superuser"},
            expires_delta=-timedelta(minutes=10),
        )

        refresh_resp = await client.post(
            "/api/auth/refresh",
            cookies=cookies,
            headers={"Authorization": f"Bearer {expired_token}"},
        )
        assert refresh_resp.status_code == 200
        refreshed_data = refresh_resp.json()
        assert refreshed_data["org_id"] == 223
        claims = decode_token(refreshed_data["access_token"])
        assert claims["orgId"] == 223

        # Update cookies with rotated refresh token and new access token from refresh
        cookies.update(dict(refresh_resp.cookies))

        # (e) switch(0) -> platform context
        switch0_resp = await client.post(
            "/api/admin/tenants/switch",
            json={"org_id": 0},
            cookies=cookies,
            headers={"Authorization": f"Bearer {refreshed_data['access_token']}"},
        )
        assert switch0_resp.status_code == 200
        switch0_data = switch0_resp.json()
        assert switch0_data["org_id"] == 0
        assert switch0_data["org_name"] == "Платформа"
        assert switch0_data["is_impersonated"] is False
        claims0 = decode_token(switch0_data["access_token"])
        assert claims0["orgId"] == 0


@pytest.mark.anyio
async def test_step2_superuser_login_with_last_org():
    """Verify (d): login superuser with last_org_id=223 -> first token with orgId=223 and one issuer call."""
    store = get_user_store()
    user = await store.get_by_username("o.lebedev")
    assert user is not None
    await store.set_user_last_org(user.id, 223)

    mock_target_org = Org(
        org_id=223,
        org_name="DEMO Company",
        name="DEMO Company",
        status=1,
        is_active=True,
    )
    mock_session = AsyncMock()
    mock_session.get.return_value = mock_target_org

    with (
        patch("app.routers.auth.get_user_store", return_value=store),
        patch("app.routers.auth.async_session") as mock_async_session,
        patch.object(
            jwt_issuer_client, "issue_tokens", wraps=jwt_issuer_client.issue_tokens
        ) as spy_issuer,
    ):
        mock_async_session.return_value.__aenter__.return_value = mock_session
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.post(
                "/api/auth/login",
                json={
                    "username": "o.lebedev",
                    "password": "eaf21fcabcffeb1f97f01a4fc02ece63",
                },
            )
            assert resp.status_code == 200
            data = resp.json()
            assert data["org_id"] == 223
            token_claims = decode_token(data["access_token"])
            assert token_claims["orgId"] == 223
            assert spy_issuer.call_count == 1


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


@pytest.mark.anyio
async def test_step3_admin_accessible_with_impersonated_tenant_token():
    """Rule #1: Superuser endpoints check is_superuser, not token_type or org_id=0.
    An impersonated superuser in org 223 has full access to /api/admin/* endpoints.
    """
    token = create_access_token(
        {
            "sub": "o.lebedev",
            "org": "223",
            "org_id": 223,
            "role": "superuser",
            "is_superuser": True,
            "token_type": "tenant",
            "is_imp": True,
        }
    )

    mock_orgs = [
        Org(org_id=1, org_name="PLATERRA", name="PLATERRA", status=1, is_active=True),
        Org(org_id=223, org_name="DEMO", name="DEMO", status=1, is_active=True),
    ]
    mock_session = AsyncMock()
    mock_result = MagicMock()
    mock_scalars = MagicMock()
    mock_scalars.all.return_value = mock_orgs
    mock_result.scalars.return_value = mock_scalars
    mock_session.execute.return_value = mock_result

    with patch("app.routers.admin_tenants.async_session") as mock_async_session:
        mock_async_session.return_value.__aenter__.return_value = mock_session
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.get(
                "/api/admin/tenants/available",
                headers={"Authorization": f"Bearer {token}"},
            )
            assert resp.status_code == 200
            assert len(resp.json()) == 2


@pytest.mark.anyio
async def test_step3_require_tenant_context_detail():
    """require_tenant_context raises 403 with detail='Выберите организацию' when org_id in (None, 0)."""
    # 1. org_id = 0
    user_platform = {"user_id": 1, "org_id": 0, "is_superuser": True}
    with pytest.raises(HTTPException) as exc_0:
        await require_tenant_context(user_platform)
    assert exc_0.value.status_code == 403
    assert exc_0.value.detail == "Выберите организацию"

    # 2. org_id = None
    user_none = {"user_id": 1, "org_id": None, "is_superuser": True}
    with pytest.raises(HTTPException) as exc_none:
        await require_tenant_context(user_none)
    assert exc_none.value.status_code == 403
    assert exc_none.value.detail == "Выберите организацию"

    # 3. org_id > 0 succeeds
    user_valid = {"user_id": 1, "org_id": 223, "is_superuser": False}
    result = await require_tenant_context(user_valid)
    assert result == user_valid


@pytest.mark.anyio
async def test_step4_ttl_and_me_expires_at():
    """Verify Step 4 requirements:
    - jwt_expire_minutes is 60
    - jwt_refresh_expire_days is 30
    - GET /auth/me returns valid ISO expires_at from token exp
    """
    assert settings.jwt_expire_minutes == 60
    assert settings.jwt_refresh_expire_days == 30

    exp_epoch = int(datetime.now(UTC).timestamp()) + 3600
    token = create_access_token(
        {
            "sub": "o.lebedev",
            "org": "1",
            "role": "superuser",
            "is_superuser": True,
            "exp": exp_epoch,
        }
    )

    mock_session = AsyncMock()
    mock_result = MagicMock()
    mock_result.scalar_one_or_none.return_value = "PLATERRA"
    mock_session.execute.return_value = mock_result

    with patch("app.routers.auth.async_session") as mock_async_session:
        mock_async_session.return_value.__aenter__.return_value = mock_session
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.get(
                "/api/auth/me",
                headers={"Authorization": f"Bearer {token}"},
            )
            assert resp.status_code == 200
            data = resp.json()
            assert data["expires_at"] is not None
            parsed_dt = datetime.fromisoformat(data["expires_at"])
            assert int(parsed_dt.timestamp()) == exp_epoch


@pytest.mark.anyio
async def test_step5_csrf_and_cookie_auth():
    """Verify Step 5 requirements:
    - Mutating request authenticated via cookie without X-Requested-With -> 403 CSRF check failed
    - Mutating request authenticated via cookie WITH X-Requested-With: XMLHttpRequest -> 200
    - Mutating request authenticated via Bearer token without X-Requested-With -> 200 (not blocked)
    - access_token (snake_case) cookie is ignored -> 401
    """
    token = create_access_token(
        {
            "sub": "o.lebedev",
            "org": "1",
            "role": "superuser",
            "is_superuser": True,
        }
    )

    store = get_user_store()
    await store.create_session(user_id=1, refresh_token="su_csrf_test_refresh")

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
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            # 1. Cookie auth + POST without X-Requested-With -> 403 CSRF
            resp_no_csrf = await client.post(
                "/api/admin/tenants/switch",
                json={"org_id": 223},
                cookies={"accessToken": token, "refreshToken": "su_csrf_test_refresh"},
            )
            assert resp_no_csrf.status_code == 403
            assert resp_no_csrf.json()["detail"] == "CSRF check failed"

            # 2. Cookie auth + POST WITH X-Requested-With -> 200 OK
            resp_with_csrf = await client.post(
                "/api/admin/tenants/switch",
                json={"org_id": 223},
                cookies={"accessToken": token, "refreshToken": "su_csrf_test_refresh"},
                headers={"X-Requested-With": "XMLHttpRequest"},
            )
            assert resp_with_csrf.status_code == 200

            # 3. Bearer auth + POST without X-Requested-With -> 200 OK (not blocked)
            resp_bearer = await client.post(
                "/api/admin/tenants/switch",
                json={"org_id": 223},
                cookies={"refreshToken": "su_csrf_test_refresh"},
                headers={"Authorization": f"Bearer {token}"},
            )
            assert resp_bearer.status_code == 200

            # 4. access_token (snake_case) cookie is not accepted
            client.cookies.clear()
            resp_snake_case = await client.get(
                "/api/auth/me",
                cookies={"access_token": token},
            )
            assert resp_snake_case.status_code == 401


@pytest.mark.anyio
async def test_step6_authorization_matrix_and_audit(caplog):
    """Verify Step 6 requirements:
    - Matrix [role x endpoint type x org match/mismatch]
    - Audit logging for impersonated superuser mutations
    """
    import logging

    regular_token = create_access_token(
        {
            "sub": "user1",
            "org": "223",
            "org_id": 223,
            "role": "user",
            "is_superuser": False,
        }
    )
    su_imp_token = create_access_token(
        {
            "sub": "o.lebedev",
            "orig_sub": "o.lebedev",
            "org": "223",
            "org_id": 223,
            "role": "superuser",
            "is_superuser": True,
            "is_imp": True,
        }
    )
    su_platform_token = create_access_token(
        {
            "sub": "o.lebedev",
            "org": "0",
            "org_id": 0,
            "role": "superuser",
            "is_superuser": True,
        }
    )

    mock_reports_db = AsyncMock()
    reports_res = MagicMock()
    reports_res.scalar_one_or_none.return_value = "Europe/Moscow"
    reports_res.scalar.return_value = 0
    reports_res.fetchall.return_value = []
    mock_reports_db.execute.return_value = reports_res
    mock_reports_db.scalar.return_value = 0

    mock_tenants_db = AsyncMock()
    tenants_res = MagicMock()
    tenants_res.scalar_one_or_none.return_value = Org(
        org_id=223, org_name="DEMO", is_active=True
    )
    tenants_scalars = MagicMock()
    tenants_scalars.all.return_value = [
        Org(org_id=223, org_name="DEMO", is_active=True)
    ]
    tenants_res.scalars.return_value = tenants_scalars
    mock_tenants_db.execute.return_value = tenants_res

    mock_dash_db = AsyncMock()
    mock_dash_db.scalar.return_value = 0

    with (
        patch("app.routers.reports.async_session") as mock_reports_session,
        patch("app.routers.dashboard.async_session") as mock_dash_session,
        patch("app.routers.admin_tenants.async_session") as mock_tenants_session,
    ):
        mock_reports_session.return_value.__aenter__.return_value = mock_reports_db
        mock_dash_session.return_value.__aenter__.return_value = mock_dash_db
        mock_tenants_session.return_value.__aenter__.return_value = mock_tenants_db

        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            # 1. Regular user:
            # 1.1 Tenant endpoint with matching org -> 200
            r1 = await client.get(
                "/api/reports/payments?org_id=223",
                headers={"Authorization": f"Bearer {regular_token}"},
            )
            assert r1.status_code == 200

            # 1.2 Tenant endpoint with mismatched org -> 403
            r2 = await client.get(
                "/api/reports/payments?org_id=999",
                headers={"Authorization": f"Bearer {regular_token}"},
            )
            assert r2.status_code == 403

            # 1.3 Platform admin endpoint -> 403
            r3 = await client.get(
                "/api/admin/tenants/available",
                headers={"Authorization": f"Bearer {regular_token}"},
            )
            assert r3.status_code == 403

            # 2. Impersonated superuser in org 223:
            # 2.1 Tenant endpoint with matching org -> 200
            r4 = await client.get(
                "/api/reports/payments?org_id=223",
                headers={"Authorization": f"Bearer {su_imp_token}"},
            )
            assert r4.status_code == 200

            # 2.2 Tenant endpoint with mismatched org -> 403
            r5 = await client.get(
                "/api/reports/payments?org_id=999",
                headers={"Authorization": f"Bearer {su_imp_token}"},
            )
            assert r5.status_code == 403

            # 2.3 Platform admin endpoint -> 200
            r6 = await client.get(
                "/api/admin/tenants/available",
                headers={"Authorization": f"Bearer {su_imp_token}"},
            )
            assert r6.status_code == 200

            # 2.4 Mutation with impersonation triggers audit logging
            with caplog.at_level(logging.INFO, logger="app.auth"):
                caplog.clear()
                # Calling mutating switch with impersonated token
                await client.post(
                    "/api/admin/tenants/switch",
                    json={"org_id": 223},
                    cookies={"refreshToken": "su_csrf_test_refresh"},
                    headers={
                        "Authorization": f"Bearer {su_imp_token}",
                        "X-Requested-With": "XMLHttpRequest",
                    },
                )
                assert any(
                    "audit impersonated action user=o.lebedev" in rec.message
                    for rec in caplog.records
                )

            # 3. Superuser in platform context (org_id = 0):
            # 3.1 Tenant endpoint -> 403 "Выберите организацию"
            r7 = await client.get(
                "/api/stats",
                headers={"Authorization": f"Bearer {su_platform_token}"},
            )
            assert r7.status_code == 403
            assert r7.json()["detail"] == "Выберите организацию"

            # 3.2 Platform admin endpoint -> 200
            r8 = await client.get(
                "/api/admin/tenants/available",
                headers={"Authorization": f"Bearer {su_platform_token}"},
            )
            assert r8.status_code == 200


@pytest.mark.anyio
async def test_step9_switch_via_auth_alias_with_cookie():
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # 1. Login superuser
        login_resp = await client.post(
            "/api/auth/login",
            json={
                "username": "o.lebedev",
                "password": "eaf21fcabcffeb1f97f01a4fc02ece63",
            },
        )
        assert login_resp.status_code == 200
        cookies = dict(login_resp.cookies)
        assert "refreshToken" in cookies
        assert "accessToken" in cookies

        # 2. Switch to org_id: 0 via /api/auth/switch-tenant with cookies and X-Requested-With
        switch0_resp = await client.post(
            "/api/auth/switch-tenant",
            json={"org_id": 0},
            cookies=cookies,
            headers={"X-Requested-With": "XMLHttpRequest"},
        )
        assert switch0_resp.status_code == 200
        data0 = switch0_resp.json()
        assert data0["org_id"] == 0
        assert data0["org_name"] == "Платформа"
        assert data0["is_impersonated"] is False

        cookies.update(dict(switch0_resp.cookies))

        # 3. Switch to active org (223)
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
            switch_resp = await client.post(
                "/api/auth/switch-tenant",
                json={"org_id": 223},
                cookies=cookies,
                headers={"X-Requested-With": "XMLHttpRequest"},
            )
            assert switch_resp.status_code == 200
            data_switched = switch_resp.json()
            assert data_switched["org_id"] == 223
            assert data_switched["is_impersonated"] is True


@pytest.mark.anyio
async def test_step9_switch_by_sid_claim_without_refresh_cookie():
    store = get_user_store()
    user = await store.get_by_username("o.lebedev")
    assert user is not None
    session = await store.create_session(
        user_id=user.id,
        refresh_token="test_sid_switch_refresh_token",
        ip_address="127.0.0.1",
        user_agent="pytest",
        expires_in_seconds=3600,
        active_org_id=0,
    )
    tokens = jwt_issuer_client._generate_mock_tokens(
        user_id=user.id,
        org_id=0,
        role_id=1,
        username="o.lebedev",
        role="superuser",
        is_superuser=True,
        sid=session.id,
    )
    token = tokens["accessToken"]
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # Call /api/admin/tenants/switch without refreshToken cookie
        resp = await client.post(
            "/api/admin/tenants/switch",
            json={"org_id": 0},
            headers={"Authorization": f"Bearer {token}"},
        )
        assert resp.status_code == 200


@pytest.mark.anyio
async def test_step9_hs256_rejected_when_mock_disabled(monkeypatch):
    monkeypatch.setattr(settings, "jwt_issuer_mock_enabled", False)
    payload = {
        "sub": "o.lebedev",
        "userId": 1,
        "roleId": 1,
        "orgId": 0,
        "role": "superuser",
        "is_superuser": True,
        "exp": int((datetime.now(UTC) + timedelta(hours=1)).timestamp()),
    }
    hs256_token = jwt.encode(payload, "mock_secret", algorithm="HS256")
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        resp = await client.get(
            "/api/auth/me",
            headers={"Authorization": f"Bearer {hs256_token}"},
        )
        assert resp.status_code == 401


@pytest.mark.anyio
async def test_step9_numeric_role_normalized():
    # 1. role="1", roleId=1, orgId=0 -> role="superuser", is_superuser=True
    token_su = jwt.encode(
        {"sub": "1", "userId": 1, "role": "1", "roleId": 1, "orgId": 0},
        "mock_secret",
        algorithm="HS256",
    )
    req = _request_with_headers({"Authorization": f"Bearer {token_su}"})
    user_su = await get_current_user(
        req, HTTPAuthorizationCredentials(scheme="Bearer", credentials=token_su)
    )
    assert user_su["role"] == "superuser"
    assert user_su["is_superuser"] is True

    # 2. role="3", roleId=3 -> role="user", is_superuser=False
    token_usr = jwt.encode(
        {"sub": "2", "userId": 2, "role": "3", "roleId": 3, "orgId": 10},
        "mock_secret",
        algorithm="HS256",
    )
    req_usr = _request_with_headers({"Authorization": f"Bearer {token_usr}"})
    user_usr = await get_current_user(
        req_usr, HTTPAuthorizationCredentials(scheme="Bearer", credentials=token_usr)
    )
    assert user_usr["role"] == "user"
    assert user_usr["is_superuser"] is False


@pytest.mark.anyio
async def test_step9_is_imp_derived_for_v1_token():
    # 1. roleId=1, orgId=223 without is_imp in payload -> is_impersonated is True
    token_imp = jwt.encode(
        {"sub": "1", "userId": 1, "role": "superuser", "roleId": 1, "orgId": 223},
        "mock_secret",
        algorithm="HS256",
    )
    req_imp = _request_with_headers({"Authorization": f"Bearer {token_imp}"})
    user_imp = await get_current_user(
        req_imp, HTTPAuthorizationCredentials(scheme="Bearer", credentials=token_imp)
    )
    assert user_imp["is_impersonated"] is True

    # 2. roleId=1, orgId=0 without is_imp in payload -> is_impersonated is False
    token_plat = jwt.encode(
        {"sub": "1", "userId": 1, "role": "superuser", "roleId": 1, "orgId": 0},
        "mock_secret",
        algorithm="HS256",
    )
    req_plat = _request_with_headers({"Authorization": f"Bearer {token_plat}"})
    user_plat = await get_current_user(
        req_plat, HTTPAuthorizationCredentials(scheme="Bearer", credentials=token_plat)
    )
    assert user_plat["is_impersonated"] is False
