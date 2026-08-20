import re
from datetime import UTC, datetime
from unittest.mock import AsyncMock, MagicMock

import pytest
from httpx import ASGITransport, AsyncClient

from app.auth import create_access_token
from app.database import get_db
from app.main import app
from app.models import (
    License,
    Org,
    OrgBillingSettings,
    Terminal,
    TerminalType,
)
from app.routers.admin_terminals import generate_device_sn


@pytest.fixture(autouse=True)
def reset_dependency_overrides():
    app.dependency_overrides.clear()
    yield
    app.dependency_overrides.clear()


@pytest.fixture
def anyio_backend():
    return "asyncio"


def test_sn_generation_formula():
    """Verify SN generation formula matches platform spec: a4b<7-digit device_id>c<5-digit random>d<DDMMYY>."""
    device_id = 42
    sn = generate_device_sn(device_id)

    assert sn.startswith("a4b0000042c")
    pattern = r"^a4b0000042c\d{5}d\d{6}$"
    assert re.match(pattern, sn) is not None

    device_id_large = 1234567
    sn_large = generate_device_sn(device_id_large)
    assert sn_large.startswith("a4b1234567c")
    assert re.match(r"^a4b1234567c\d{5}d\d{6}$", sn_large) is not None


@pytest.mark.anyio
async def test_admin_endpoints_require_superuser():
    """Verify regular users receive 403 Forbidden for all admin endpoints."""
    reg_token = create_access_token(
        {"sub": "test", "org": "1", "role": "user", "token_type": "tenant"}
    )
    headers = {"Authorization": f"Bearer {reg_token}"}

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # Organizations
        resp = await client.get("/api/admin/organizations", headers=headers)
        assert resp.status_code == 403

        resp = await client.post(
            "/api/admin/organizations",
            json={"org_name": "Test", "name": "Test"},
            headers=headers,
        )
        assert resp.status_code == 403

        # Terminals
        resp = await client.get("/api/admin/terminals", headers=headers)
        assert resp.status_code == 403

        resp = await client.get("/api/admin/terminals/next-device-id", headers=headers)
        assert resp.status_code == 403

        resp = await client.post(
            "/api/admin/terminals",
            json={"device_id": 100, "org_id": 1},
            headers=headers,
        )
        assert resp.status_code == 403

        resp = await client.post(
            "/api/admin/terminals/1/generate-pin",
            headers=headers,
        )
        assert resp.status_code == 403


@pytest.mark.anyio
async def test_admin_organizations_flow():
    """Verify superuser can list, create, and update organizations and their licensing policy."""
    su_token = create_access_token(
        {
            "sub": "o.lebedev",
            "role": "superuser",
            "is_superuser": True,
            "token_type": "master",
        }
    )
    headers = {"Authorization": f"Bearer {su_token}"}

    mock_db = AsyncMock()

    # 1. Test listing organizations
    mock_org = Org(
        org_id=1,
        org_name="Platerra Group",
        name="Platerra",
        status=1,
        is_active=True,
    )
    mock_settings = OrgBillingSettings(
        org_id=1,
        monthly_price_minor=150_000,
        currency="RUB",
        cert_billing_mode="per_operation",
        cert_price_minor=50_000,
        tenant_pin_creation_enabled=True,
        cert_charge_primary_issue=True,
        cert_charge_reissue=True,
    )

    async def mock_execute(stmt):
        res = MagicMock()
        sql_str = str(stmt)
        if "FROM orgs" in sql_str and "org_id" in sql_str and "SELECT" in sql_str:
            res.scalars.return_value.all.return_value = [mock_org]
        elif "FROM org_billing_settings" in sql_str:
            res.scalars.return_value.all.return_value = [mock_settings]
        elif "FROM org_statuses" in sql_str:
            res.scalar_one_or_none.return_value = None
        else:
            res.scalars.return_value.all.return_value = []
        return res

    mock_db.execute = AsyncMock(side_effect=mock_execute)
    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # List orgs
        resp = await client.get("/api/admin/organizations", headers=headers)
        assert resp.status_code == 200
        data = resp.json()
        assert len(data) == 1
        assert data[0]["org_id"] == 1
        assert data[0]["org_name"] == "Platerra Group"
        assert data[0]["monthly_price_minor"] == 150_000
        assert data[0]["cert_billing_mode"] == "per_operation"
        assert data[0]["tenant_pin_creation_enabled"] is True

        # 2. Test create organization
        mock_db.get.return_value = None  # not existing yet
        create_payload = {
            "org_id": 2,
            "org_name": "New Retail Org",
            "name": "NewRetail",
            "is_active": True,
            "monthly_price_minor": 200_000,
            "currency": "RUB",
            "cert_billing_mode": "none",
            "tenant_pin_creation_enabled": False,
        }
        resp = await client.post(
            "/api/admin/organizations", json=create_payload, headers=headers
        )
        assert resp.status_code == 201
        created_org = resp.json()
        assert created_org["org_id"] == 2
        assert created_org["org_name"] == "New Retail Org"
        assert created_org["monthly_price_minor"] == 200_000

        # 3. Test update organization
        mock_db.get.side_effect = lambda model, id_val: (
            mock_org if model == Org else mock_settings
        )
        update_payload = {
            "org_name": "Platerra Updated",
            "monthly_price_minor": 180_000,
            "cert_billing_mode": "none",
        }
        resp = await client.put(
            "/api/admin/organizations/1", json=update_payload, headers=headers
        )
        assert resp.status_code == 200
        updated_org = resp.json()
        assert updated_org["org_id"] == 1
        assert updated_org["org_name"] == "Platerra Updated"
        assert updated_org["monthly_price_minor"] == 180_000


@pytest.mark.anyio
async def test_admin_terminals_flow():
    """Verify superuser can get next device id, create, list, update, generate pin, and set license/status."""
    su_token = create_access_token(
        {
            "sub": "o.lebedev",
            "role": "superuser",
            "is_superuser": True,
            "token_type": "master",
        }
    )
    headers = {"Authorization": f"Bearer {su_token}"}

    mock_db = AsyncMock()

    # Next Device ID test
    mock_res_device_ids = MagicMock()
    mock_res_device_ids.scalars.return_value.all.return_value = [1, 2, 3, 5]

    async def mock_execute(stmt):
        res = MagicMock()
        sql_str = str(stmt)
        if "SELECT terminals.device_id" in sql_str:
            return mock_res_device_ids
        elif "count" in sql_str:
            res.scalar_one.return_value = 1
            return res
        elif (
            "terminals.device_id ==" in sql_str
            or "terminals.device_id =" in sql_str
            or "terminals.sn ==" in sql_str
            or "terminals.sn =" in sql_str
        ):
            res.scalar_one_or_none.return_value = None
            return res
        elif "FROM terminals" in sql_str:
            term = Terminal(
                id=1,
                device_id=101,
                sn="a4b0000101c12345d200826",
                org_id=1,
                is_active=True,
                address="Moscow",
                note="Center",
                terminal_type_id=0,
            )
            res.all.return_value = [(term, "Platerra Org", "Стандартный")]
            res.scalar_one_or_none.return_value = None
            res.scalars.return_value.first.return_value = None
            res.scalars.return_value.all.return_value = []
            return res
        elif "FROM licenses" in sql_str:
            lic = License(
                id=1,
                terminal_id=1,
                org_id=1,
                expires_at=datetime(2028, 1, 1, tzinfo=UTC),
                is_active=True,
                billing_period_months=1,
                renewal_enabled=True,
            )
            res.scalars.return_value.all.return_value = [lic]
            res.scalars.return_value.first.return_value = lic
            return res
        elif "FROM certificate_pins" in sql_str:
            res.scalars.return_value.all.return_value = []
            res.scalars.return_value.first.return_value = None
            res.scalar_one_or_none.return_value = None
            return res
        else:
            res.scalars.return_value.all.return_value = []
            res.scalar_one_or_none.return_value = None
            res.all.return_value = []
            return res

    def mock_add(instance):
        if hasattr(instance, "id") and getattr(instance, "id", None) is None:
            instance.id = 1
        if hasattr(instance, "org_id") and getattr(instance, "org_id", None) is None:
            instance.org_id = 1

    async def mock_refresh(instance):
        if hasattr(instance, "id") and getattr(instance, "id", None) is None:
            instance.id = 1
        if hasattr(instance, "org_id") and getattr(instance, "org_id", None) is None:
            instance.org_id = 1

    async def mock_flush():
        pass

    mock_db.add = MagicMock(side_effect=mock_add)
    mock_db.refresh = AsyncMock(side_effect=mock_refresh)
    mock_db.flush = AsyncMock(side_effect=mock_flush)
    mock_db.commit = AsyncMock(return_value=None)
    mock_db.execute = AsyncMock(side_effect=mock_execute)
    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # 1. Get next device ID
        resp = await client.get("/api/admin/terminals/next-device-id", headers=headers)
        assert resp.status_code == 200
        data = resp.json()
        assert data["next_device_id"] == 4  # gap between 3 and 5

        # 2. Terminal types dictionary
        mock_db.execute = AsyncMock(side_effect=mock_execute)
        resp = await client.get("/api/admin/terminal-types", headers=headers)
        assert resp.status_code == 200
        types = resp.json()
        assert len(types) >= 1

        def mock_get(model, id_val):
            if model == Org:
                return Org(org_id=id_val, org_name="Platerra", name="Platerra")
            if model == Terminal:
                return Terminal(
                    id=id_val,
                    device_id=101,
                    sn="a4b0000101c12345d200826",
                    org_id=1,
                    is_active=True,
                    terminal_type_id=0,
                )
            if model == TerminalType:
                return TerminalType(id=id_val, name="Стандартный")
            return None

        mock_db.get = AsyncMock(side_effect=mock_get)
        create_payload = {
            "device_id": 202,
            "org_id": 1,
            "terminal_type_id": 0,
            "address": "Nevsky 1",
            "note": "Office branch",
            "is_active": True,
            "billing_period_months": 3,
        }
        resp = await client.post(
            "/api/admin/terminals", json=create_payload, headers=headers
        )
        assert resp.status_code == 201
        created_term = resp.json()
        assert created_term["device_id"] == 202
        assert created_term["sn"].startswith("a4b0000202c")
        assert created_term["address"] == "Nevsky 1"
        assert created_term["is_active"] is True

        # 4. List terminals
        resp = await client.get(
            "/api/admin/terminals?page=1&page_size=10", headers=headers
        )
        assert resp.status_code == 200
        list_data = resp.json()
        assert list_data["total"] == 1
        assert len(list_data["items"]) == 1
        assert list_data["items"][0]["device_id"] == 101

        # 5. Generate PIN for terminal
        resp = await client.post("/api/admin/terminals/1/generate-pin", headers=headers)
        assert resp.status_code == 200
        pin_data = resp.json()
        assert len(pin_data["pin"]) == 6
        assert pin_data["terminal_id"] == 1
        assert pin_data["device_id"] == 101
        assert "expires_at" in pin_data

        # 6. Set license date
        license_payload = {
            "expires_at": "2029-01-01T00:00:00Z",
            "is_active": True,
            "renewal_enabled": True,
        }
        resp = await client.post(
            "/api/admin/terminals/1/set-license",
            json=license_payload,
            headers=headers,
        )
        assert resp.status_code == 200
        lic_resp = resp.json()
        assert lic_resp["terminal_id"] == 1
        assert "2029" in lic_resp["expires_at"]

        # 7. Set status
        resp = await client.post(
            "/api/admin/terminals/1/set-status",
            json={"is_active": False},
            headers=headers,
        )
        assert resp.status_code == 200
        status_resp = resp.json()
        assert status_resp["is_active"] is False
