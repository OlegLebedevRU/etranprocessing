from unittest.mock import AsyncMock, MagicMock

import pytest

from app.main import _build_menu_tree
from app.models import Group, MenuVariant, Service
from app.services.legacy_menu import (
    import_all_legacy_menus,
    import_legacy_menu_for_org,
    load_legacy_menu_data,
)


@pytest.fixture
def anyio_backend():
    return "asyncio"


def test_legacy_data_counts_and_structure():
    groups, services = load_legacy_menu_data()
    assert len(groups) == 580
    assert len(services) == 2441

    org_ids = {g["org_id"] for g in groups}
    assert len(org_ids) == 71
    assert 424 in org_ids
    assert 1 in org_ids

    # Check 424 groups & services counts
    groups_424 = [g for g in groups if g["org_id"] == 424]
    services_424 = [
        s for s in services if s["group_id"] in {g["Id"] for g in groups_424}
    ]
    assert len(groups_424) == 13
    assert len(services_424) == 49

    for s in services:
        assert s["tsp_code"] > 0
        assert s["price"] >= 0
        assert s["name"]


@pytest.mark.anyio
async def test_import_legacy_menu_for_org_424():
    added_objects = []

    mock_session = AsyncMock()
    mock_session.add = MagicMock()

    mock_variant_res = MagicMock()
    mock_variant_res.scalar_one_or_none.return_value = None
    mock_session.execute.return_value = mock_variant_res

    def mock_add(obj):
        if (
            isinstance(obj, (MenuVariant, Group, Service))
            and getattr(obj, "id", None) is None
        ):
            obj.id = len(added_objects) + 1
        added_objects.append(obj)

    mock_session.add.side_effect = mock_add

    variant = await import_legacy_menu_for_org(mock_session, org_id=424)
    assert variant is not None
    assert variant.name == "Легаси меню"
    assert variant.org_id == 424

    variants = [o for o in added_objects if isinstance(o, MenuVariant)]
    groups = [o for o in added_objects if isinstance(o, Group)]
    services = [o for o in added_objects if isinstance(o, Service)]

    assert len(variants) == 1
    assert len(groups) == 13  # 1 root + 12 categories
    assert len(services) == 49

    root_grp = next(g for g in groups if g.parent_id is None)
    assert root_grp.name == "Раздел главного меню"
    assert root_grp.number == 0

    sub_groups = [g for g in groups if g.parent_id is not None]
    assert len(sub_groups) == 12
    for g in sub_groups:
        assert g.parent_id == root_grp.id
        assert g.org_id == 424

    # Build tree check
    tree = _build_menu_tree(groups, services)
    assert tree["name"] == "root"
    assert len(tree["items"]) == 1
    assert tree["items"][0]["name"] == "Раздел главного меню"
    assert len(tree["items"][0]["items"]) == 12

    first_cat = tree["items"][0]["items"][0]  # Стрижки
    assert first_cat["name"] == "Стрижки"
    assert len(first_cat["items"]) == 11
    mens_cut = next(s for s in first_cat["items"] if s["code"] == 1000301)
    assert mens_cut["name"] == "Мужская стрижка"
    assert mens_cut["price"] == "600"
    assert mens_cut["prototypeid"] == 990021


@pytest.mark.anyio
async def test_import_legacy_menu_for_org_1():
    added_objects = []

    mock_session = AsyncMock()
    mock_session.add = MagicMock()

    mock_variant_res = MagicMock()
    mock_variant_res.scalar_one_or_none.return_value = None
    mock_session.execute.return_value = mock_variant_res

    def mock_add(obj):
        if (
            isinstance(obj, (MenuVariant, Group, Service))
            and getattr(obj, "id", None) is None
        ):
            obj.id = len(added_objects) + 1
        added_objects.append(obj)

    mock_session.add.side_effect = mock_add

    variant = await import_legacy_menu_for_org(mock_session, org_id=1)
    assert variant is not None
    assert variant.name == "Легаси меню"
    assert variant.org_id == 1

    groups = [o for o in added_objects if isinstance(o, Group)]
    services = [o for o in added_objects if isinstance(o, Service)]

    assert len(groups) == 7
    assert len(services) == 7


@pytest.mark.anyio
async def test_import_legacy_menu_idempotent():
    existing_variant = MenuVariant(id=10, org_id=1, name="Легаси меню")
    mock_session = AsyncMock()
    mock_session.add = MagicMock()

    mock_variant_res = MagicMock()
    mock_variant_res.scalar_one_or_none.return_value = existing_variant
    mock_session.execute.return_value = mock_variant_res

    variant = await import_legacy_menu_for_org(mock_session, org_id=1)
    assert variant is not None
    assert variant.id == 10
    assert variant.name == "Легаси меню"
    assert variant.org_id == 1


@pytest.mark.anyio
async def test_import_all_legacy_menus_subset():
    added_objects = []

    mock_session = AsyncMock()
    mock_session.add = MagicMock()

    mock_variant_res = MagicMock()
    mock_variant_res.scalar_one_or_none.return_value = None
    mock_session.execute.return_value = mock_variant_res

    def mock_add(obj):
        if (
            isinstance(obj, (MenuVariant, Group, Service))
            and getattr(obj, "id", None) is None
        ):
            obj.id = len(added_objects) + 1
        added_objects.append(obj)

    mock_session.add.side_effect = mock_add

    result = await import_all_legacy_menus(mock_session, org_ids=[1, 424])
    assert len(result) == 2
    assert 1 in result
    assert 424 in result
