"""Tests for /api/ListMenuFile endpoint in ProcessingBackend.

Covers:
- Menu tree structure generation
- Lookup by variant_id
- Lookup by device_id (query param & mTLS X-Client-Cert-DN header)
- Fallback to first variant
"""

from unittest.mock import AsyncMock, MagicMock

import pytest
from httpx import ASGITransport, AsyncClient

from app.database import get_db
from app.dependencies import get_current_terminal
from app.main import app
from app.models import (
    Group,
    MenuVariant,
    MenuVariantSnapshot,
    ServiceMenu,
    Terminal,
    TerminalMenuBinding,
)
from app.routers.list_menu import _build_menu_tree


@pytest.fixture(autouse=True)
def cleanup_overrides():
    yield
    app.dependency_overrides.clear()


@pytest.fixture
def anyio_backend():
    return "asyncio"


def _setup_menu_data():
    variant = MenuVariant(id=1, org_id=1, name="Standard Menu", version=1)
    binding = TerminalMenuBinding(id=1, device_id=773, menu_variant_id=1)

    root_group = Group(
        id=1,
        menu_variant_id=1,
        org_id=1,
        number=0,
        name="Раздел главного меню",
        parent_id=None,
    )
    group1 = Group(
        id=10,
        menu_variant_id=1,
        org_id=1,
        number=1,
        name="Мобильная связь",
        parent_id=1,
    )
    group2 = Group(
        id=20,
        menu_variant_id=1,
        org_id=1,
        number=2,
        name="Интернет",
        parent_id=1,
    )

    svc_root = ServiceMenu(
        id=99,
        menu_variant_id=1,
        group_id=1,
        tsp_code=500,
        name="Прямая услуга",
        protypenumber=1,
        printname="Direct",
        price=100,
    )
    svc1 = ServiceMenu(
        id=100,
        menu_variant_id=1,
        group_id=10,
        tsp_code=1001,
        name="МТС",
        protypenumber=1,
        printname="MTS",
        price=0,
    )
    svc2 = ServiceMenu(
        id=101,
        menu_variant_id=1,
        group_id=10,
        tsp_code=1002,
        name="Билайн",
        protypenumber=1,
        printname="Beeline",
        price=0,
    )
    svc3 = ServiceMenu(
        id=102,
        menu_variant_id=1,
        group_id=20,
        tsp_code=2001,
        name="Ростелеком",
        protypenumber=2,
        printname="RT",
        price=0,
    )

    return variant, binding, [root_group, group1, group2], [svc_root, svc1, svc2, svc3]


@pytest.mark.anyio
async def test_list_menu_file_requires_terminal_identity():
    mock_db = AsyncMock()
    mock_db.scalar = AsyncMock(return_value=None)

    async def override_get_db():
        yield mock_db

    app.dependency_overrides[get_db] = override_get_db

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.get("/api/ListMenuFile?device_id=773")

    assert resp.status_code == 401


@pytest.mark.anyio
async def test_list_menu_file_for_authenticated_terminal():
    variant, binding, groups, services = _setup_menu_data()

    mock_db = AsyncMock()

    async def get_side_effect(model, pk):
        if model == MenuVariant and pk == 1:
            return variant
        return None

    mock_db.get = AsyncMock(side_effect=get_side_effect)

    def execute_side_effect(stmt):
        stmt_str = str(stmt)
        result = MagicMock()
        if "terminal_menu_bindings" in stmt_str:
            result.scalar_one_or_none.return_value = binding
        elif "groups" in stmt_str:
            result.scalars.return_value.all.return_value = groups
        elif "services" in stmt_str:
            result.scalars.return_value.all.return_value = services
        else:
            result.scalar_one_or_none.return_value = None
            result.scalars.return_value.all.return_value = []
        return result

    mock_db.execute = AsyncMock(side_effect=execute_side_effect)
    mock_db.scalar = AsyncMock(return_value=binding)

    async def override_get_db():
        yield mock_db

    app.dependency_overrides[get_db] = override_get_db
    app.dependency_overrides[get_current_terminal] = lambda: Terminal(
        id=1,
        device_id=773,
        sn="term-001",
        org_id=1,
        is_active=True,
    )

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.get("/api/ListMenuFile")

    assert resp.status_code == 200
    data = resp.json()
    assert data["name"] == "root"
    # Root group is collapsed: direct services first, then child groups
    assert len(data["items"]) == 3
    assert data["items"][0]["name"] == "Прямая услуга"
    assert data["items"][0]["code"] == 500
    assert data["items"][0]["price"] == "100"

    assert data["items"][1]["name"] == "Мобильная связь"
    assert len(data["items"][1]["items"]) == 2
    assert data["items"][1]["items"][0]["name"] == "МТС"
    assert data["items"][1]["items"][0]["code"] == 1001
    assert data["items"][1]["items"][1]["name"] == "Билайн"
    assert data["items"][1]["items"][1]["code"] == 1002

    assert data["items"][2]["name"] == "Интернет"
    assert len(data["items"][2]["items"]) == 1
    assert data["items"][2]["items"][0]["name"] == "Ростелеком"


@pytest.mark.anyio
def test_build_menu_tree_collapses_root_and_orders_services_first():
    root = Group(
        id=1,
        menu_variant_id=1,
        org_id=1,
        number=0,
        name="Раздел главного меню",
        parent_id=None,
    )
    cat1 = Group(
        id=2, menu_variant_id=1, org_id=1, number=801, name="Стрижки", parent_id=1
    )
    subcat = Group(
        id=3,
        menu_variant_id=1,
        org_id=1,
        number=802,
        name="Короткие",
        parent_id=2,
    )

    s_root = ServiceMenu(
        id=1,
        menu_variant_id=1,
        group_id=1,
        tsp_code=100,
        name="Кофе",
        printname=None,
        price=0,
        protypenumber=1,
    )
    s_cat1 = ServiceMenu(
        id=2,
        menu_variant_id=1,
        group_id=2,
        tsp_code=200,
        name="Мытье головы",
        printname="Мытье",
        price=150,
        protypenumber=2,
    )
    s_subcat = ServiceMenu(
        id=3,
        menu_variant_id=1,
        group_id=3,
        tsp_code=300,
        name="Под машинку",
        printname=None,
        price=400,
        protypenumber=3,
    )

    tree = _build_menu_tree([root, cat1, subcat], [s_root, s_cat1, s_subcat])

    assert tree["name"] == "root"
    # Root has direct service first, then cat1
    assert len(tree["items"]) == 2
    assert tree["items"][0]["name"] == "Кофе"
    assert tree["items"][0]["code"] == 100

    cat1_node = tree["items"][1]
    assert cat1_node["name"] == "Стрижки"
    # Inside cat1: direct service first, then subcat
    assert len(cat1_node["items"]) == 2
    assert cat1_node["items"][0]["name"] == "Мытье головы"
    assert cat1_node["items"][0]["price"] == "150"
    assert cat1_node["items"][1]["name"] == "Короткие"

    subcat_node = cat1_node["items"][1]
    assert len(subcat_node["items"]) == 1
    assert subcat_node["items"][0]["name"] == "Под машинку"
    assert subcat_node["items"][0]["price"] == "400"


def test_build_menu_tree_empty():
    assert _build_menu_tree([], []) == {"name": "root", "items": []}


def test_build_menu_tree_more_than_one_root_raises_value_error():
    g1 = Group(
        id=1, menu_variant_id=1, org_id=1, number=0, name="Корень 1", parent_id=None
    )
    g2 = Group(
        id=2, menu_variant_id=1, org_id=1, number=1, name="Корень 2", parent_id=None
    )

    with pytest.raises(
        ValueError, match="Menu variant cannot have more than 1 root group"
    ):
        _build_menu_tree([g1, g2], [])


@pytest.mark.anyio
async def test_list_menu_updates_existing_snapshot():
    variant, binding, groups, services = _setup_menu_data()
    existing_snapshot = MenuVariantSnapshot(
        id=5,
        menu_variant_id=1,
        version=1,
        snapshot_data={"old": "data"},
    )

    mock_db = AsyncMock()

    async def get_side_effect(model, pk):
        if model == MenuVariant and pk == 1:
            return variant
        return None

    mock_db.get = AsyncMock(side_effect=get_side_effect)

    def execute_side_effect(stmt):
        stmt_str = str(stmt)
        result = MagicMock()
        if "terminal_menu_bindings" in stmt_str:
            result.scalar_one_or_none.return_value = binding
        elif "groups" in stmt_str:
            result.scalars.return_value.all.return_value = groups
        elif "services" in stmt_str:
            result.scalars.return_value.all.return_value = services
        else:
            result.scalar_one_or_none.return_value = None
            result.scalars.return_value.all.return_value = []
        return result

    mock_db.execute = AsyncMock(side_effect=execute_side_effect)

    # First call: binding, Second call: snapshot
    mock_db.scalar = AsyncMock(side_effect=[binding, existing_snapshot])

    async def override_get_db():
        yield mock_db

    app.dependency_overrides[get_db] = override_get_db
    app.dependency_overrides[get_current_terminal] = lambda: Terminal(
        id=1,
        device_id=773,
        sn="term-001",
        org_id=1,
        is_active=True,
    )

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.get("/api/ListMenuFile")

    assert resp.status_code == 200
    assert existing_snapshot.snapshot_data["version"] == 1
    assert "tree" in existing_snapshot.snapshot_data
    assert existing_snapshot.snapshot_data["tree"]["name"] == "root"
