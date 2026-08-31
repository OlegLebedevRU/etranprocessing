from datetime import UTC, datetime
from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from httpx import ASGITransport, AsyncClient

from app.auth import create_access_token
from app.database import get_db
from app.main import app
from app.models import Group, MenuVariant, Service, Terminal, TerminalMenuBinding


@pytest.fixture(autouse=True)
def reset_dependency_overrides():
    app.dependency_overrides.clear()
    yield
    app.dependency_overrides.clear()


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.fixture
def tenant1_headers():
    token = create_access_token(
        {"sub": "user1", "org": "1", "role": "user", "token_type": "tenant"}
    )
    return {"Authorization": f"Bearer {token}"}


@pytest.fixture
def tenant2_headers():
    token = create_access_token(
        {"sub": "user2", "org": "2", "role": "user", "token_type": "tenant"}
    )
    return {"Authorization": f"Bearer {token}"}


@pytest.fixture
def superuser_headers():
    token = create_access_token(
        {
            "sub": "superadmin",
            "role": "superuser",
            "is_superuser": True,
            "token_type": "master",
        }
    )
    return {"Authorization": f"Bearer {token}"}


# =========================================================================
# 1. Menu Variants Multi-Tenancy & Auth Tests
# =========================================================================


@pytest.mark.anyio
async def test_menu_variants_auth_required():
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # GET /api/menu-variants
        resp = await client.get("/api/menu-variants")
        assert resp.status_code == 401

        # GET /api/menu-variants/1
        resp = await client.get("/api/menu-variants/1")
        assert resp.status_code == 401

        # POST /api/menu-variants
        resp = await client.post("/api/menu-variants", json={"name": "Test"})
        assert resp.status_code == 401

        # DELETE /api/menu-variants/1
        resp = await client.delete("/api/menu-variants/1")
        assert resp.status_code == 401

        # POST /api/menu-variants/duplicate
        resp = await client.post(
            "/api/menu-variants/duplicate", json={"source_variant_id": 1}
        )
        assert resp.status_code == 401


@pytest.mark.anyio
async def test_menu_variants_tenant_isolation(tenant1_headers, tenant2_headers):
    mock_db = AsyncMock()

    v1 = MenuVariant(
        id=1,
        org_id=1,
        name="Tenant 1 Variant",
        created_at=datetime.now(UTC),
        updated_at=datetime.now(UTC),
    )
    v2 = MenuVariant(
        id=2,
        org_id=2,
        name="Tenant 2 Variant",
        created_at=datetime.now(UTC),
        updated_at=datetime.now(UTC),
    )

    async def mock_execute(stmt, *args, **kwargs):
        res = MagicMock()
        # If query has where MenuVariant.org_id == 1
        stmt_str = str(stmt)
        if (
            "menu_variants.org_id = :org_id_1" in stmt_str
            or "menu_variants.org_id =" in stmt_str
        ):
            res.scalars.return_value.all.return_value = [v1]
        else:
            res.scalars.return_value.all.return_value = [v1, v2]
        return res

    mock_db.execute = AsyncMock(side_effect=mock_execute)
    mock_db.get = AsyncMock(
        side_effect=lambda model, ident, **kw: (
            v1 if ident == 1 else (v2 if ident == 2 else None)
        )
    )

    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # Tenant 1 lists variants -> gets only Tenant 1 variant
        resp = await client.get("/api/menu-variants", headers=tenant1_headers)
        assert resp.status_code == 200
        items = resp.json()
        assert len(items) == 1
        assert items[0]["org_id"] == 1

        # Tenant 1 gets Tenant 1 variant -> 200
        resp = await client.get("/api/menu-variants/1", headers=tenant1_headers)
        assert resp.status_code == 200
        assert resp.json()["id"] == 1

        # Tenant 1 gets Tenant 2 variant -> 404
        resp = await client.get("/api/menu-variants/2", headers=tenant1_headers)
        assert resp.status_code == 404

        # Tenant 1 deletes Tenant 2 variant -> 404
        resp = await client.delete("/api/menu-variants/2", headers=tenant1_headers)
        assert resp.status_code == 404

        # Tenant 1 duplicates Tenant 2 variant -> 404
        resp = await client.post(
            "/api/menu-variants/duplicate",
            json={"source_variant_id": 2},
            headers=tenant1_headers,
        )
        assert resp.status_code == 404


@pytest.mark.anyio
async def test_create_variant_sets_org_id(tenant1_headers):
    mock_db = AsyncMock()
    mock_db.scalar = AsyncMock(return_value=None)  # No existing variant with same name

    created_variants = []

    def mock_add(inst):
        if isinstance(inst, MenuVariant):
            inst.id = 10
            inst.created_at = datetime.now(UTC)
            inst.updated_at = datetime.now(UTC)
            created_variants.append(inst)

    mock_db.add = MagicMock(side_effect=mock_add)
    mock_db.commit = AsyncMock()
    mock_db.refresh = AsyncMock()

    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        resp = await client.post(
            "/api/menu-variants",
            json={"name": "Новый вариант"},
            headers=tenant1_headers,
        )
        assert resp.status_code == 201
        data = resp.json()
        assert data["org_id"] == 1
        assert data["name"] == "Новый вариант"
        assert len(created_variants) == 1
        assert created_variants[0].org_id == 1


# =========================================================================
# 2. Groups Multi-Tenancy & Auth Tests
# =========================================================================


@pytest.mark.anyio
async def test_groups_auth_and_isolation(tenant1_headers, tenant2_headers):
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # Unauthenticated requests -> 401
        assert (await client.get("/api/groups?menu_variant_id=1")).status_code == 401
        assert (await client.get("/api/groups/1")).status_code == 401
        assert (
            await client.post(
                "/api/groups",
                json={"menu_variant_id": 1, "name": "G1"},
            )
        ).status_code == 401
        assert (
            await client.put("/api/groups/1", json={"name": "G1"})
        ).status_code == 401
        assert (await client.delete("/api/groups/1")).status_code == 401

    mock_db = AsyncMock()

    v1 = MenuVariant(id=1, org_id=1, name="Variant 1")
    v2 = MenuVariant(id=2, org_id=2, name="Variant 2")
    g1 = Group(
        id=1, menu_variant_id=1, org_id=1, number=1, name="Group 1", parent_id=None
    )
    g2 = Group(
        id=2, menu_variant_id=2, org_id=2, number=1, name="Group 2", parent_id=None
    )

    def mock_get(model, ident, **kw):
        if model == MenuVariant:
            return v1 if ident == 1 else (v2 if ident == 2 else None)
        if model == Group:
            return g1 if ident == 1 else (g2 if ident == 2 else None)
        return None

    mock_db.get = AsyncMock(side_effect=mock_get)
    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # Tenant 1 listing groups for Tenant 2's variant -> 404
        resp = await client.get(
            "/api/groups?menu_variant_id=2", headers=tenant1_headers
        )
        assert resp.status_code == 404

        # Tenant 1 creating group in Tenant 2's variant -> 404
        resp = await client.post(
            "/api/groups",
            json={"menu_variant_id": 2, "name": "Hack Group"},
            headers=tenant1_headers,
        )
        assert resp.status_code == 404

        # Tenant 1 getting Tenant 2's group -> 404
        resp = await client.get("/api/groups/2", headers=tenant1_headers)
        assert resp.status_code == 404

        # Tenant 1 updating Tenant 2's group -> 404
        resp = await client.put(
            "/api/groups/2",
            json={"name": "Hacked"},
            headers=tenant1_headers,
        )
        assert resp.status_code == 404

        # Tenant 1 deleting Tenant 2's group -> 404
        resp = await client.delete("/api/groups/2", headers=tenant1_headers)
        assert resp.status_code == 404


# =========================================================================
# 3. Services Multi-Tenancy & Auth Tests
# =========================================================================


@pytest.mark.anyio
async def test_services_auth_and_isolation(tenant1_headers, tenant2_headers):
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # Unauthenticated requests -> 401
        assert (await client.get("/api/services")).status_code == 401
        assert (await client.get("/api/services/1")).status_code == 401
        assert (
            await client.get("/api/services/free-tsp?group_id=1")
        ).status_code == 401
        assert (
            await client.post(
                "/api/services",
                json={"group_id": 1, "tsp_code": 100, "name": "S1"},
            )
        ).status_code == 401
        assert (
            await client.put("/api/services/1", json={"name": "S1"})
        ).status_code == 401
        assert (await client.delete("/api/services/1")).status_code == 401

    mock_db = AsyncMock()

    v1 = MenuVariant(id=1, org_id=1, name="Variant 1")
    v2 = MenuVariant(id=2, org_id=2, name="Variant 2")
    g1 = Group(
        id=1, menu_variant_id=1, org_id=1, number=1, name="Group 1", parent_id=None
    )
    g2 = Group(
        id=2, menu_variant_id=2, org_id=2, number=1, name="Group 2", parent_id=None
    )
    s1 = Service(id=1, menu_variant_id=1, group_id=1, tsp_code=100, name="Service 1")
    s2 = Service(id=2, menu_variant_id=2, group_id=2, tsp_code=100, name="Service 2")

    def mock_get(model, ident, **kw):
        if model == MenuVariant:
            return v1 if ident == 1 else (v2 if ident == 2 else None)
        if model == Group:
            return g1 if ident == 1 else (g2 if ident == 2 else None)
        if model == Service:
            return s1 if ident == 1 else (s2 if ident == 2 else None)
        return None

    mock_db.get = AsyncMock(side_effect=mock_get)
    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # Tenant 1 listing services in Tenant 2's group -> 404
        resp = await client.get("/api/services?group_id=2", headers=tenant1_headers)
        assert resp.status_code == 404

        # Tenant 1 listing services in Tenant 2's variant -> 404
        resp = await client.get(
            "/api/services?menu_variant_id=2", headers=tenant1_headers
        )
        assert resp.status_code == 404

        # Tenant 1 creating service in Tenant 2's group -> 404
        resp = await client.post(
            "/api/services",
            json={"group_id": 2, "tsp_code": 105, "name": "Hack Service"},
            headers=tenant1_headers,
        )
        assert resp.status_code == 404

        # Tenant 1 getting Tenant 2's service -> 404
        resp = await client.get("/api/services/2", headers=tenant1_headers)
        assert resp.status_code == 404

        # Tenant 1 updating Tenant 2's service -> 404
        resp = await client.put(
            "/api/services/2",
            json={"name": "Hacked"},
            headers=tenant1_headers,
        )
        assert resp.status_code == 404

        # Tenant 1 deleting Tenant 2's service -> 404
        resp = await client.delete("/api/services/2", headers=tenant1_headers)
        assert resp.status_code == 404

        # Tenant 1 free-tsp for Tenant 2's group -> 404
        resp = await client.get(
            "/api/services/free-tsp?group_id=2", headers=tenant1_headers
        )
        assert resp.status_code == 404


# =========================================================================
# 4. Terminal Bindings Multi-Tenancy & Auth Tests
# =========================================================================


@pytest.mark.anyio
async def test_terminal_bindings_auth_and_isolation(tenant1_headers, tenant2_headers):
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # POST/DELETE bindings without auth -> 401
        assert (
            await client.post(
                "/api/bindings",
                json={"device_id": 1001, "menu_variant_id": 1},
            )
        ).status_code == 401
        assert (await client.delete("/api/bindings/1")).status_code == 401

    mock_db = AsyncMock()

    t1 = Terminal(id=1, device_id=1001, org_id=1, sn="SN1", is_active=True)
    t2 = Terminal(id=2, device_id=2001, org_id=2, sn="SN2", is_active=True)
    v1 = MenuVariant(id=1, org_id=1, name="Variant 1")
    v2 = MenuVariant(id=2, org_id=2, name="Variant 2")
    b1 = TerminalMenuBinding(
        id=1, device_id=1001, menu_variant_id=1, created_at=datetime.now(UTC)
    )
    b2 = TerminalMenuBinding(
        id=2, device_id=2001, menu_variant_id=2, created_at=datetime.now(UTC)
    )

    def mock_get(model, ident, **kw):
        if model == MenuVariant:
            return v1 if ident == 1 else (v2 if ident == 2 else None)
        if model == TerminalMenuBinding:
            return b1 if ident == 1 else (b2 if ident == 2 else None)
        return None

    async def mock_scalar(stmt, *args, **kwargs):
        stmt_str = str(stmt)
        if (
            "terminals.device_id = :device_id_1" in stmt_str
            or "terminals.device_id =" in stmt_str
        ):
            # Check which device_id
            param_dict = stmt.compile().params
            did = param_dict.get("device_id_1") or param_dict.get("device_id")
            if did == 1001:
                return t1
            if did == 2001:
                return t2
        if "terminal_menu_bindings.device_id" in stmt_str:
            param_dict = stmt.compile().params
            did = param_dict.get("device_id_1") or param_dict.get("device_id")
            if did == 1001:
                return b1
            if did == 2001:
                return b2
        return None

    mock_db.get = AsyncMock(side_effect=mock_get)
    mock_db.scalar = AsyncMock(side_effect=mock_scalar)
    mock_db.commit = AsyncMock()
    mock_db.refresh = AsyncMock()

    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # Tenant 1 trying to bind Tenant 2's terminal -> 404
        resp = await client.post(
            "/api/bindings",
            json={"device_id": 2001, "menu_variant_id": 1},
            headers=tenant1_headers,
        )
        assert resp.status_code == 404

        # Tenant 1 trying to bind own terminal to Tenant 2's variant -> 404
        resp = await client.post(
            "/api/bindings",
            json={"device_id": 1001, "menu_variant_id": 2},
            headers=tenant1_headers,
        )
        assert resp.status_code == 404

        # Tenant 1 binding own terminal to own variant -> 201
        resp = await client.post(
            "/api/bindings",
            json={"device_id": 1001, "menu_variant_id": 1},
            headers=tenant1_headers,
        )
        assert resp.status_code == 201
        assert resp.json()["device_id"] == 1001

        # Tenant 1 deleting Tenant 2's binding -> 404
        resp = await client.delete("/api/bindings/2", headers=tenant1_headers)
        assert resp.status_code == 404

        # Tenant 1 deleting own binding -> 200
        resp = await client.delete("/api/bindings/1", headers=tenant1_headers)
        assert resp.status_code == 200


# =========================================================================
# 5. Stats and Inkass Report Multi-Tenancy & Auth Tests
# =========================================================================


@pytest.mark.anyio
async def test_stats_and_inkass_auth_and_isolation(tenant1_headers, tenant2_headers):
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # Unauthenticated stats -> 401
        assert (await client.get("/api/stats")).status_code == 401
        # Unauthenticated inkass -> 401
        assert (await client.get("/api/reports/inkass")).status_code == 401

    mock_session = AsyncMock()
    mock_session.scalar = AsyncMock(return_value=5)

    async def mock_execute(stmt, *args, **kwargs):
        res = MagicMock()
        res.scalar.return_value = 1
        res.fetchall.return_value = [
            (
                1,
                1001,
                "SN1001",
                datetime(2026, 8, 20, 12, 0, tzinfo=UTC),
                {"TotalSum": "5000", "TotalCount": "10"},
                1,
            )
        ]
        return res

    mock_session.execute = AsyncMock(side_effect=mock_execute)

    mock_cm = AsyncMock()
    mock_cm.__aenter__.return_value = mock_session
    mock_cm.__aexit__.return_value = None

    with patch("app.main.async_session", return_value=mock_cm):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            # Stats endpoint
            resp = await client.get("/api/stats", headers=tenant1_headers)
            assert resp.status_code == 200
            data = resp.json()
            assert "groups" in data
            assert "services" in data
            assert "tsp_codes" in data
            assert "avg_price" in data

            # Inkass endpoint
            resp = await client.get("/api/reports/inkass", headers=tenant1_headers)
            assert resp.status_code == 200
            data = resp.json()
            assert data["total"] == 1
            assert len(data["items"]) == 1
            assert data["items"][0]["org_id"] == 1
            assert data["items"][0]["device_id"] == 1001


# =========================================================================
# 6. Group and Service Starting Numbering Tests (801 and 1000301)
# =========================================================================


@pytest.mark.anyio
async def test_group_numbering_starts_at_801(tenant1_headers):
    mock_db = AsyncMock()

    v1 = MenuVariant(id=1, org_id=1, name="Variant 1")
    parent_g = Group(
        id=10,
        menu_variant_id=1,
        org_id=1,
        number=801,
        name="Parent Group",
        parent_id=None,
    )

    def mock_get(model, ident, **kw):
        if model == MenuVariant and ident == 1:
            return v1
        if model == Group and ident == 10:
            return parent_g
        return None

    async def mock_refresh(obj):
        if not obj.id:
            obj.id = 1

    mock_db.get = AsyncMock(side_effect=mock_get)
    # First call: no groups in DB, max_num is None -> should assign 801
    mock_db.scalar = AsyncMock(return_value=None)
    mock_db.commit = AsyncMock()
    mock_db.refresh = AsyncMock(side_effect=mock_refresh)

    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # Create root group with number 0 -> assigned 801
        resp = await client.post(
            "/api/groups",
            json={"menu_variant_id": 1, "name": "First Group", "number": 0},
            headers=tenant1_headers,
        )
        assert resp.status_code == 201
        assert resp.json()["number"] == 801

        # Second call: max_num is 801 -> subgroup should be assigned 802
        mock_db.scalar = AsyncMock(return_value=801)
        resp = await client.post(
            "/api/groups",
            json={
                "menu_variant_id": 1,
                "name": "Subgroup",
                "parent_id": 10,
                "number": 0,
            },
            headers=tenant1_headers,
        )
        assert resp.status_code == 201
        assert resp.json()["number"] == 802


@pytest.mark.anyio
async def test_service_numbering_ranges(tenant1_headers):
    mock_db = AsyncMock()

    v1 = MenuVariant(id=1, org_id=1, name="Variant 1")
    g1 = Group(
        id=1, menu_variant_id=1, org_id=1, number=801, name="Group 1", parent_id=None
    )

    def mock_get(model, ident, **kw):
        if model == MenuVariant and ident == 1:
            return v1
        if model == Group and ident == 1:
            return g1
        return None

    async def mock_refresh(obj):
        if not obj.id:
            obj.id = 1

    mock_db.get = AsyncMock(side_effect=mock_get)
    mock_db.commit = AsyncMock()
    mock_db.refresh = AsyncMock(side_effect=mock_refresh)

    # When no services are used yet
    res_mock = MagicMock()
    res_mock.fetchall.return_value = []
    mock_db.execute = AsyncMock(return_value=res_mock)
    mock_db.scalar = AsyncMock(return_value=None)

    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # 1. Check free-tsp endpoint starts from 1001301 for custom services
        resp = await client.get(
            "/api/services/free-tsp?group_id=1", headers=tenant1_headers
        )
        assert resp.status_code == 200
        free_codes = resp.json()
        assert len(free_codes) > 0
        assert free_codes[0]["tsp_code"] == 1001301
        assert free_codes[1]["tsp_code"] == 1001302

        # 2. Check free-tsp endpoint with from_catalog=true starts from 1000301
        resp_cat = await client.get(
            "/api/services/free-tsp?group_id=1&from_catalog=true",
            headers=tenant1_headers,
        )
        assert resp_cat.status_code == 200
        free_cat_codes = resp_cat.json()
        assert len(free_cat_codes) > 0
        assert free_cat_codes[0]["tsp_code"] == 1000301

        # 3. Create custom service with auto tsp_code (0) -> assigns 1001301
        resp = await client.post(
            "/api/services",
            json={"group_id": 1, "name": "Service Auto", "tsp_code": 0},
            headers=tenant1_headers,
        )
        assert resp.status_code == 201
        assert resp.json()["tsp_code"] == 1001301

        # 4. Next creation when 1001301 is in used_codes -> assigns 1001302
        res_mock.fetchall.return_value = [(1001301,)]
        resp = await client.post(
            "/api/services",
            json={"group_id": 1, "name": "Service Auto 2", "tsp_code": 0},
            headers=tenant1_headers,
        )
        assert resp.status_code == 201
        assert resp.json()["tsp_code"] == 1001302
