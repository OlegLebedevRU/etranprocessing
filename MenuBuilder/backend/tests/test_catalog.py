from unittest.mock import AsyncMock, MagicMock

import pytest
from httpx import ASGITransport, AsyncClient

from app.auth import get_current_user
from app.database import get_db
from app.main import app
from app.models import (
    CatalogCategory,
    CatalogItem,
    MenuVariant,
    Service,
)


@pytest.fixture(autouse=True)
def cleanup_overrides():
    yield
    app.dependency_overrides.clear()


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.fixture
def tenant_headers():
    return {"Authorization": "Bearer test_token"}


@pytest.mark.anyio
async def test_catalog_category_depth_limit(tenant_headers):
    app.dependency_overrides[get_current_user] = lambda: {
        "username": "admin",
        "org_id": 1,
        "role": "admin",
    }

    mock_db = AsyncMock()

    cat1 = CatalogCategory(id=1, org_id=1, name="Level 1", parent_id=None, sort_order=0)
    cat2 = CatalogCategory(id=2, org_id=1, name="Level 2", parent_id=1, sort_order=0)
    cat3 = CatalogCategory(id=3, org_id=1, name="Level 3", parent_id=2, sort_order=0)

    async def mock_get(model, pk):
        if model == CatalogCategory:
            if pk == 1:
                return cat1
            if pk == 2:
                return cat2
            if pk == 3:
                return cat3
        return None

    async def mock_refresh(obj):
        if not getattr(obj, "id", None):
            obj.id = 10

    mock_db.get.side_effect = mock_get
    mock_db.refresh.side_effect = mock_refresh
    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # 1. Create root category (depth = 1) -> Success
        resp = await client.post(
            "/api/catalog/categories",
            json={"name": "Root", "sort_order": 0},
            headers=tenant_headers,
        )
        assert resp.status_code == 201
        assert resp.json()["depth"] == 1

        # 2. Create subcategory with parent_id=1 (depth = 2) -> Success
        resp = await client.post(
            "/api/catalog/categories",
            json={"name": "Sub", "parent_id": 1, "sort_order": 0},
            headers=tenant_headers,
        )
        assert resp.status_code == 201
        assert resp.json()["depth"] == 2

        # 3. Create subcategory with parent_id=2 (depth = 3) -> Success
        resp = await client.post(
            "/api/catalog/categories",
            json={"name": "Leaf", "parent_id": 2, "sort_order": 0},
            headers=tenant_headers,
        )
        assert resp.status_code == 201
        assert resp.json()["depth"] == 3

        # 4. Create subcategory with parent_id=3 (depth = 4) -> 400 Bad Request
        resp = await client.post(
            "/api/catalog/categories",
            json={"name": "Too Deep", "parent_id": 3, "sort_order": 0},
            headers=tenant_headers,
        )
        assert resp.status_code == 400
        assert "Превышена максимальная глубина" in resp.json()["detail"]


@pytest.mark.anyio
async def test_catalog_item_creation_and_tsp_range(tenant_headers):
    app.dependency_overrides[get_current_user] = lambda: {
        "username": "admin",
        "org_id": 1,
        "role": "admin",
    }

    mock_db = AsyncMock()
    cat1 = CatalogCategory(id=1, org_id=1, name="Level 1", parent_id=None, sort_order=0)

    async def mock_get(model, pk):
        if model == CatalogCategory and pk == 1:
            return cat1
        return None

    async def mock_refresh(obj):
        if not getattr(obj, "id", None):
            obj.id = 100

    mock_db.get.side_effect = mock_get
    mock_db.refresh.side_effect = mock_refresh

    # Empty used codes
    res_mock = MagicMock()
    res_mock.fetchall.return_value = []
    mock_db.execute.return_value = res_mock
    mock_db.scalar.return_value = None

    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # 1. Create item with auto tsp_code (0) -> assigns 1000301
        resp = await client.post(
            "/api/catalog/items",
            json={
                "category_id": 1,
                "name": "Auto TSP Item",
                "price": 500,
            },
            headers=tenant_headers,
        )
        assert resp.status_code == 201
        assert resp.json()["tsp_code"] == 1000301

        # 2. Reject item with TSP outside catalog range (e.g. 1001301)
        resp_invalid = await client.post(
            "/api/catalog/items",
            json={
                "category_id": 1,
                "tsp_code": 1001301,
                "name": "Invalid TSP Item",
            },
            headers=tenant_headers,
        )
        assert resp_invalid.status_code == 400


@pytest.mark.anyio
async def test_catalog_propagate_to_menus(tenant_headers):
    app.dependency_overrides[get_current_user] = lambda: {
        "username": "admin",
        "org_id": 1,
        "role": "admin",
    }

    mock_db = AsyncMock()

    cat_item = CatalogItem(
        id=10,
        org_id=1,
        category_id=1,
        tsp_code=1000301,
        name="Новое имя",
        price=700,
        protypenumber=0,
    )

    variant = MenuVariant(id=5, org_id=1, name="Основное меню", version=1)

    service = Service(
        id=20,
        menu_variant_id=5,
        group_id=1,
        tsp_code=1000301,
        name="Старое имя",
        price=500,
        protypenumber=0,
        catalog_item_id=10,
    )

    # Mock execute results
    # 1. select CatalogItem
    res_cat = MagicMock()
    res_cat.scalars.return_value.all.return_value = [cat_item]

    # 2. select Service
    res_svc = MagicMock()
    res_svc.scalars.return_value.all.return_value = [service]

    # 3. select MenuVariant
    res_var = MagicMock()
    res_var.scalars.return_value.all.return_value = [variant]

    mock_db.execute.side_effect = [res_cat, res_svc, res_var]
    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        resp = await client.post(
            "/api/catalog/propagate-to-menus",
            headers=tenant_headers,
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data["updated_variants_count"] == 1
        assert data["updated_services_count"] == 1
        assert data["affected_variant_names"] == ["Основное меню"]
        assert service.name == "Новое имя"
        assert service.price == 700
        assert variant.version == 2
