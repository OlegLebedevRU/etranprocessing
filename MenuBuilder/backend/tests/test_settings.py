import hashlib
from datetime import UTC, datetime, timedelta
from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from httpx import ASGITransport, AsyncClient

from app.auth import create_access_token
from app.database import get_db
from app.main import app
from app.models import EmailLog, EmailVerification, Org, Terminal, User


@pytest.fixture(autouse=True)
def reset_dependency_overrides():
    app.dependency_overrides.clear()
    yield
    app.dependency_overrides.clear()


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.fixture
def role3_headers():
    token = create_access_token(
        {
            "sub": "tenant_user",
            "org": "10",
            "org_id": 10,
            "role": "user",
            "role_id": 3,
            "id": 101,
            "is_superuser": False,
            "token_type": "tenant",
        }
    )
    return {"Authorization": f"Bearer {token}"}


@pytest.fixture
def superuser_headers():
    token = create_access_token(
        {
            "sub": "admin_su",
            "org": "10",
            "org_id": 10,
            "role": "superuser",
            "role_id": 1,
            "id": 1,
            "is_superuser": True,
            "token_type": "master",
        }
    )
    return {"Authorization": f"Bearer {token}"}


@pytest.mark.anyio
async def test_settings_profile_get_role_3(role3_headers):
    mock_db = AsyncMock()
    mock_org = Org(
        org_id=10,
        org_name="Test Org 10",
        timezone="Europe/Moscow",
        email="old@test.org",
        phone="+79991112233",
        notify_by_email=True,
        send_reports=True,
        is_email_verified=False,
        email_verified_at=None,
    )

    async def mock_execute(stmt):
        res = MagicMock()
        sql_str = str(stmt)
        if "FROM orgs" in sql_str:
            res.scalar_one_or_none.return_value = mock_org
        return res

    mock_db.execute = AsyncMock(side_effect=mock_execute)
    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        resp = await client.get("/api/settings/profile", headers=role3_headers)
        assert resp.status_code == 200
        data = resp.json()
        assert data["username"] == "tenant_user"
        assert data["org_id"] == 10
        assert data["org_name"] == "Test Org 10"
        assert data["is_readonly"] is False
        assert data["is_email_verified"] is False
        assert data["notify_by_email"] is True
        assert data["send_reports"] is True


@pytest.mark.anyio
async def test_settings_profile_superuser_readonly(superuser_headers):
    mock_db = AsyncMock()
    mock_org = Org(
        org_id=10,
        org_name="Test Org 10",
        timezone="Europe/Moscow",
        email="su_view@test.org",
        phone="+79991112233",
        notify_by_email=True,
        send_reports=True,
        is_email_verified=True,
        email_verified_at=datetime.now(UTC),
    )

    async def mock_execute(stmt):
        res = MagicMock()
        sql_str = str(stmt)
        if "FROM orgs" in sql_str:
            res.scalar_one_or_none.return_value = mock_org
        return res

    mock_db.execute = AsyncMock(side_effect=mock_execute)
    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # Superuser can read
        resp = await client.get("/api/settings/profile", headers=superuser_headers)
        assert resp.status_code == 200
        data = resp.json()
        assert data["is_readonly"] is True
        assert data["is_superuser"] is True

        # Superuser CANNOT modify profile
        resp_patch = await client.patch(
            "/api/settings/profile",
            json={"phone": "+70000000000"},
            headers=superuser_headers,
        )
        assert resp_patch.status_code == 403


@pytest.mark.anyio
async def test_settings_profile_role_3_update(role3_headers):
    mock_db = AsyncMock()
    mock_org = Org(
        org_id=10,
        org_name="Test Org 10",
        timezone="Europe/Moscow",
        email="old@test.org",
        phone="+79991112233",
        notify_by_email=True,
        send_reports=True,
        is_email_verified=False,
        email_verified_at=None,
    )

    async def mock_execute(stmt):
        res = MagicMock()
        sql_str = str(stmt)
        if "FROM orgs" in sql_str:
            res.scalar_one_or_none.return_value = mock_org
        return res

    mock_db.execute = AsyncMock(side_effect=mock_execute)
    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        resp = await client.patch(
            "/api/settings/profile",
            json={
                "phone": "+78889990011",
                "timezone": "Asia/Yekaterinburg",
                "notify_by_email": False,
                "send_reports": False,
            },
            headers=role3_headers,
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data["phone"] == "+78889990011"
        assert data["timezone"] == "Asia/Yekaterinburg"
        assert data["notify_by_email"] is False
        assert data["send_reports"] is False


@pytest.mark.anyio
async def test_settings_change_password_requires_verified_email(role3_headers):
    mock_db = AsyncMock()
    mock_org = Org(
        org_id=10,
        org_name="Test Org 10",
        email="unverified@test.org",
        is_email_verified=False,
    )

    async def mock_execute(stmt):
        res = MagicMock()
        sql_str = str(stmt)
        if "FROM orgs" in sql_str:
            res.scalar_one_or_none.return_value = mock_org
        return res

    mock_db.execute = AsyncMock(side_effect=mock_execute)
    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        resp = await client.post(
            "/api/settings/profile/change-password",
            json={"old_password": "oldpassword123", "new_password": "newpassword123"},
            headers=role3_headers,
        )
        assert resp.status_code == 400
        assert "ранее подтвержденном email" in resp.json()["detail"]


@pytest.mark.anyio
async def test_settings_email_verification_flow(role3_headers):
    mock_db = AsyncMock()
    mock_org = Org(
        org_id=10,
        org_name="Test Org 10",
        email=None,
        is_email_verified=False,
    )

    verifications: list[EmailVerification] = []
    logs: list[EmailLog] = []

    def mock_add(obj):
        if isinstance(obj, EmailVerification):
            verifications.append(obj)
        elif isinstance(obj, EmailLog):
            logs.append(obj)

    mock_db.add = MagicMock(side_effect=mock_add)

    async def mock_execute(stmt):
        res = MagicMock()
        sql_str = str(stmt)
        if "FROM orgs" in sql_str:
            res.scalar_one_or_none.return_value = mock_org
        elif "FROM email_verifications" in sql_str:
            res.scalar_one_or_none.return_value = (
                verifications[-1] if verifications else None
            )
        return res

    mock_db.execute = AsyncMock(side_effect=mock_execute)
    app.dependency_overrides[get_db] = lambda: mock_db

    # 1. Request verification
    fake_response = {
        "status": "sent",
        "device_id": "sys-verify-org-10",
        "recipients": ["new@company.com"],
        "postbox_message_id": "test-postbox-id-123",
        "storage_path": "/function/storage/terem-files/sys-verify-org-10/msg.txt",
    }

    with patch(
        "app.services.email_service.ServerlessEmailClient.send_email",
        new=AsyncMock(return_value=fake_response),
    ):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.post(
                "/api/settings/profile/request-email-verification",
                json={"email": "new@company.com"},
                headers=role3_headers,
            )
            assert resp.status_code == 200
            data = resp.json()
            assert data["ok"] is True
            assert len(verifications) == 1
            assert len(logs) == 1
            # Check log stored storage_path
            assert (
                logs[0].storage_path
                == "/function/storage/terem-files/sys-verify-org-10/msg.txt"
            )
            assert logs[0].postbox_message_id == "test-postbox-id-123"

            otp_code = verifications[0].otp_code

            # 2. Confirm OTP - wrong code
            resp_wrong = await client.post(
                "/api/settings/profile/confirm-email-otp",
                json={"code": "000000"},
                headers=role3_headers,
            )
            assert resp_wrong.status_code == 400
            assert "Неверный код" in resp_wrong.json()["detail"]

            # 3. Confirm OTP - correct code
            resp_ok = await client.post(
                "/api/settings/profile/confirm-email-otp",
                json={"code": otp_code},
                headers=role3_headers,
            )
            assert resp_ok.status_code == 200
            assert mock_org.is_email_verified is True
            assert mock_org.email == "new@company.com"


@pytest.mark.anyio
async def test_settings_change_password_success_after_verified(role3_headers):
    mock_db = AsyncMock()
    mock_org = Org(
        org_id=10,
        org_name="Test Org 10",
        email="verified@test.org",
        is_email_verified=True,
    )
    old_pw = "secret123"
    hashed = hashlib.md5(old_pw.encode("utf-8")).hexdigest()
    mock_user = User(
        id=101,
        username="tenant_user",
        md5_password=hashed,
        org_id=10,
        role_id=3,
    )

    async def mock_execute(stmt):
        res = MagicMock()
        sql_str = str(stmt)
        if "FROM orgs" in sql_str:
            res.scalar_one_or_none.return_value = mock_org
        elif "FROM users" in sql_str:
            res.scalar_one_or_none.return_value = mock_user
        return res

    mock_db.execute = AsyncMock(side_effect=mock_execute)
    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # Wrong old password
        resp_wrong = await client.post(
            "/api/settings/profile/change-password",
            json={"old_password": "wrong_password", "new_password": "brandnewpass"},
            headers=role3_headers,
        )
        assert resp_wrong.status_code == 400
        assert "Неверный текущий пароль" in resp_wrong.json()["detail"]

        # Correct old password
        resp_ok = await client.post(
            "/api/settings/profile/change-password",
            json={"old_password": old_pw, "new_password": "brandnewpass"},
            headers=role3_headers,
        )
        assert resp_ok.status_code == 200
        assert resp_ok.json()["ok"] is True
        assert mock_user.md5_password == hashlib.md5(b"brandnewpass").hexdigest()


@pytest.mark.anyio
async def test_settings_confirm_email_token_link():
    mock_db = AsyncMock()
    raw_token = "valid_sample_token_12345"
    token_hash = hashlib.sha256(raw_token.encode("utf-8")).hexdigest()

    mock_org = Org(
        org_id=10,
        org_name="Test Org 10",
        email=None,
        is_email_verified=False,
    )
    mock_verif = EmailVerification(
        id=1,
        org_id=10,
        email="token_verified@example.com",
        token_hash=token_hash,
        otp_code="123456",
        attempts_left=5,
        expires_at=datetime.now(UTC) + timedelta(hours=1),
        is_used=False,
    )

    async def mock_execute(stmt):
        res = MagicMock()
        sql_str = str(stmt)
        if "FROM email_verifications" in sql_str:
            res.scalar_one_or_none.return_value = mock_verif
        elif "FROM orgs" in sql_str:
            res.scalar_one_or_none.return_value = mock_org
        return res

    mock_db.execute = AsyncMock(side_effect=mock_execute)
    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        resp = await client.get(
            f"/api/settings/profile/confirm-email-token?token={raw_token}"
        )
        assert resp.status_code == 200
        assert mock_verif.is_used is True
        assert mock_org.is_email_verified is True
        assert mock_org.email == "token_verified@example.com"


@pytest.mark.anyio
async def test_settings_terminals_flow(role3_headers, superuser_headers):
    mock_db = AsyncMock()
    t1 = Terminal(
        id=1,
        device_id=101,
        sn="term-101",
        org_id=10,
        address="Initial Address",
        note="Initial Note",
        timezone="Europe/Moscow",
        is_active=True,
        cert_serial=None,
        cert_not_valid_after=None,
        created_at=None,
        updated_at=None,
    )
    mock_db.refresh = AsyncMock()

    async def mock_execute(stmt):
        res = MagicMock()
        sql_str = str(stmt)
        if "terminals.id ==" in sql_str or "terminals.id =" in sql_str:
            res.scalar_one_or_none.return_value = t1
        elif "FROM terminals" in sql_str:
            res.scalars.return_value.all.return_value = [t1]
            res.scalar_one_or_none.return_value = t1
        else:
            res.scalar_one_or_none.return_value = None
            res.scalars.return_value.all.return_value = []
        return res

    mock_db.execute = AsyncMock(side_effect=mock_execute)
    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # 1. List terminals
        resp_list = await client.get("/api/settings/terminals", headers=role3_headers)
        assert resp_list.status_code == 200
        data = resp_list.json()
        items = data["items"] if isinstance(data, dict) and "items" in data else data
        assert len(items) == 1
        assert items[0]["sn"] == "term-101"

        # 2. Superuser list terminals
        resp_su_list = await client.get(
            "/api/settings/terminals", headers=superuser_headers
        )
        assert resp_su_list.status_code == 200

        # 3. Superuser patch terminal -> 403 Forbidden
        resp_su_patch = await client.patch(
            "/api/settings/terminals/1",
            json={"address": "New Address"},
            headers=superuser_headers,
        )
        assert resp_su_patch.status_code == 403

        # 4. Role 3 patch terminal of own org -> Success
        resp_patch = await client.patch(
            "/api/settings/terminals/1",
            json={
                "address": "Updated Kiosk St 5",
                "note": "Updated Note 1",
                "timezone": "Asia/Novosibirsk",
            },
            headers=role3_headers,
        )
        assert resp_patch.status_code == 200
        updated = resp_patch.json()
        assert updated["address"] == "Updated Kiosk St 5"
        assert updated["note"] == "Updated Note 1"
        assert updated["timezone"] == "Asia/Novosibirsk"


@pytest.mark.anyio
async def test_settings_terminals_pagination_and_comma_search(role3_headers):
    mock_db = AsyncMock()
    t1 = Terminal(
        id=1,
        device_id=101,
        sn="term-101",
        org_id=10,
        address="Address 101",
        note="Note 101",
        timezone="Europe/Moscow",
        is_active=True,
        cert_serial=None,
        cert_not_valid_after=None,
        created_at=None,
        updated_at=None,
    )
    t2 = Terminal(
        id=2,
        device_id=102,
        sn="term-102",
        org_id=10,
        address="Address 102",
        note="Note 102",
        timezone="Europe/Moscow",
        is_active=True,
        cert_serial=None,
        cert_not_valid_after=None,
        created_at=None,
        updated_at=None,
    )

    async def mock_execute(stmt):
        res = MagicMock()
        sql_str = str(stmt).lower()
        if "count(" in sql_str:
            res.scalar_one.return_value = 2
        else:
            res.scalars.return_value.all.return_value = [t1, t2]
        return res

    mock_db.execute = AsyncMock(side_effect=mock_execute)
    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # Test pagination
        resp = await client.get(
            "/api/settings/terminals?page=1&page_size=50&sort_by=device_id&sort_order=desc",
            headers=role3_headers,
        )
        assert resp.status_code == 200
        data = resp.json()
        assert "items" in data
        assert data["total_count"] == 2
        assert data["page"] == 1
        assert data["page_size"] == 50
        assert resp.headers.get("x-total-count") == "2"

        # Test comma search
        resp_search = await client.get(
            "/api/settings/terminals?search=101,102&page=1&page_size=50",
            headers=role3_headers,
        )
        assert resp_search.status_code == 200
        data_search = resp_search.json()
        assert len(data_search["items"]) == 2
