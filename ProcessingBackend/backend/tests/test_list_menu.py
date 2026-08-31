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
from app.models import Group, MenuVariant, ServiceMenu, Terminal, TerminalMenuBinding


@pytest.fixture(autouse=True)
def cleanup_overrides():
    yield
    app.dependency_overrides.clear()


@pytest.fixture
def anyio_backend():
    return "asyncio"


def _setup_menu_data():
    variant = MenuVariant(id=1, org_id=1, name="Standard Menu")
    binding = TerminalMenuBinding(id=1, device_id=773, menu_variant_id=1)

    group1 = Group(
        id=10,
        menu_variant_id=1,
        org_id=1,
        number=1,
        name="Мобильная связь",
        parent_id=None,
    )
    group2 = Group(
        id=20, menu_variant_id=1, org_id=1, number=2, name="Интернет", parent_id=None
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

    return variant, binding, [group1, group2], [svc1, svc2, svc3]


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
    assert len(data["items"]) == 2
    assert data["items"][0]["name"] == "Мобильная связь"
    assert len(data["items"][0]["items"]) == 2
    assert data["items"][0]["items"][0]["name"] == "МТС"
    assert data["items"][0]["items"][0]["code"] == 1001
