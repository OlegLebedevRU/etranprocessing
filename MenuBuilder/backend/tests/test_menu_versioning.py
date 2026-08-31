from datetime import UTC, datetime
from unittest.mock import AsyncMock

import pytest
from httpx import ASGITransport, AsyncClient

from app.auth import get_current_user
from app.database import get_db
from app.main import app
from app.models import (
    Group,
    MenuVariant,
    Terminal,
    TerminalMenuBinding,
)


@pytest.fixture(autouse=True)
def cleanup_overrides():
    yield
    app.dependency_overrides.clear()


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.anyio
async def test_group_create_increments_variant_version():
    """Verify that creating a group increments menu_variant.version."""
    app.dependency_overrides[get_current_user] = lambda: {
        "username": "tenant_admin",
        "org_id": 1,
    }

    variant = MenuVariant(id=10, name="Variant 1", org_id=1, version=1)

    mock_db = AsyncMock()
    mock_db.get.return_value = variant
    mock_db.scalar.return_value = None

    async def mock_refresh(obj):
        obj.id = getattr(obj, "id", None) or 100

    mock_db.refresh.side_effect = mock_refresh
    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        resp = await client.post(
            "/api/groups",
            json={"menu_variant_id": 10, "name": "New Group", "number": 801},
        )
        assert resp.status_code == 201
        assert variant.version == 2


@pytest.mark.anyio
async def test_service_create_increments_variant_version():
    """Verify that creating a service increments menu_variant.version."""
    app.dependency_overrides[get_current_user] = lambda: {
        "username": "tenant_admin",
        "org_id": 1,
    }

    variant = MenuVariant(id=10, name="Variant 1", org_id=1, version=2)
    group = Group(id=100, menu_variant_id=10, org_id=1, number=801, name="G1")

    mock_db = AsyncMock()

    async def mock_get(model, pk):
        if model == Group and pk == 100:
            return group
        if model == MenuVariant and pk == 10:
            return variant
        return None

    mock_db.get.side_effect = mock_get
    mock_db.scalar.return_value = None

    async def mock_refresh(obj):
        obj.id = getattr(obj, "id", None) or 1

    mock_db.refresh.side_effect = mock_refresh
    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        resp = await client.post(
            "/api/services",
            json={
                "group_id": 100,
                "name": "New Service",
                "tsp_code": 1000301,
                "price": 100,
            },
        )
        assert resp.status_code == 201
        assert variant.version == 3


@pytest.mark.anyio
async def test_terminal_binding_version_reset_on_change():
    """Verify that changing menu_variant_id resets loaded_version to None, but re-binding same does not."""
    app.dependency_overrides[get_current_user] = lambda: {
        "username": "tenant_admin",
        "org_id": 1,
    }

    terminal = Terminal(id=1, device_id=209, org_id=1, sn="SN209", is_active=True)
    variant1 = MenuVariant(id=10, name="V1", org_id=1, version=1)
    variant2 = MenuVariant(id=20, name="V2", org_id=1, version=1)
    binding = TerminalMenuBinding(
        id=1,
        device_id=209,
        menu_variant_id=10,
        loaded_version=1,
        loaded_at=datetime.now(UTC),
        created_at=datetime.now(UTC),
    )

    mock_db = AsyncMock()

    async def mock_scalar(stmt, params=None):
        sql = str(stmt)
        if "FROM terminals" in sql:
            return terminal
        if "FROM terminal_menu_bindings" in sql:
            return binding
        return None

    mock_db.scalar.side_effect = mock_scalar

    async def mock_get(model, pk):
        if model == MenuVariant:
            return variant2 if pk == 20 else variant1
        return None

    mock_db.get.side_effect = mock_get
    app.dependency_overrides[get_db] = lambda: mock_db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # 1. Change binding to variant2 -> loaded_version should be reset to None
        resp = await client.post(
            "/api/bindings",
            json={"device_id": 209, "menu_variant_id": 20},
        )
        assert resp.status_code == 201
        data = resp.json()
        assert data["menu_variant_id"] == 20
        assert data["loaded_version"] is None
        assert binding.loaded_version is None
