from datetime import UTC, datetime
from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from httpx import ASGITransport, AsyncClient

from app.auth import get_current_user
from app.database import get_db
from app.main import app
from app.models import (
    Group,
    MenuVariant,
    MenuVariantSnapshot,
    Service,
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


@pytest.mark.anyio
async def test_list_menu_file_creates_snapshot_and_updates_binding():
    """Verify ListMenuFile creates MenuVariantSnapshot and sets loaded_version on TerminalMenuBinding."""
    variant = MenuVariant(id=10, name="Main Menu", org_id=1, version=2)
    group = Group(id=100, menu_variant_id=10, org_id=1, number=801, name="Group 1")
    service = Service(
        id=1,
        menu_variant_id=10,
        group_id=100,
        tsp_code=1000301,
        name="Service A",
        price=1000,
    )
    binding = TerminalMenuBinding(
        id=1,
        device_id=209,
        menu_variant_id=10,
        loaded_version=1,
        loaded_at=datetime.now(UTC),
    )

    mock_session = AsyncMock()

    async def mock_get(model, pk):
        if model == MenuVariant and pk == 10:
            return variant
        return None

    mock_session.get.side_effect = mock_get

    async def mock_scalar(stmt, params=None):
        sql = str(stmt)
        if "FROM terminal_menu_bindings" in sql:
            return binding
        if "FROM menu_variant_snapshots" in sql:
            return None  # snapshot does not exist yet
        if "FROM menu_variants" in sql:
            return variant
        return None

    mock_session.scalar.side_effect = mock_scalar

    async def mock_execute(stmt, params=None):
        sql = str(stmt)
        res = MagicMock()
        if "FROM groups" in sql:
            res.scalars.return_value.all.return_value = [group]
        elif "FROM services" in sql:
            res.scalars.return_value.all.return_value = [service]
        else:
            res.scalars.return_value.all.return_value = []
        return res

    mock_session.execute.side_effect = mock_execute

    mock_cm = AsyncMock()
    mock_cm.__aenter__.return_value = mock_session
    mock_cm.__aexit__.return_value = None

    with patch("app.main.async_session", return_value=mock_cm):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.get("/api/ListMenuFile?device_id=209")
            assert resp.status_code == 200
            data = resp.json()
            assert data["name"] == "root"
            assert len(data["items"]) == 1

            # Binding loaded_version should be updated to variant.version (2)
            assert binding.loaded_version == 2
            assert binding.loaded_at is not None

            # Added snapshot should have been added to session
            assert mock_session.add.called
            added_snapshot = mock_session.add.call_args[0][0]
            assert isinstance(added_snapshot, MenuVariantSnapshot)
            assert added_snapshot.version == 2
            assert (
                added_snapshot.snapshot_data["services_by_tsp"]["1000301"]["name"]
                == "Service A"
            )
