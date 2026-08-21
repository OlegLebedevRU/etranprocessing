from unittest.mock import AsyncMock, MagicMock

import pytest

from app.main import _build_menu_tree
from app.models import Group, MenuVariant, Service
from app.services.legacy_menu import (
    LEGACY_CATEGORIES,
    LEGACY_SERVICES,
    ROOT_GROUP_NAME,
    ROOT_GROUP_NUMBER,
    import_legacy_menu_variant,
)


@pytest.fixture
def anyio_backend():
    return "asyncio"


def test_legacy_data_counts_and_structure():
    assert len(LEGACY_CATEGORIES) == 12
    assert len(LEGACY_SERVICES) == 49

    cat_numbers = {c["number"] for c in LEGACY_CATEGORIES}
    assert cat_numbers == set(range(801, 813))

    for s in LEGACY_SERVICES:
        assert s["group_number"] in cat_numbers
        assert s["tsp_code"] >= 1000301
        assert s["price"] >= 0
        assert s["protypenumber"] in (99001, 99002, 990021)


@pytest.mark.anyio
async def test_import_legacy_menu_variant():
    added_objects = []

    mock_session = AsyncMock()
    mock_session.add = MagicMock()

    # Simulate execute for variant check
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

    variant = await import_legacy_menu_variant(mock_session, org_id=424)

    assert variant.name == "Легаси меню"
    assert variant.org_id == 424

    variants = [o for o in added_objects if isinstance(o, MenuVariant)]
    groups = [o for o in added_objects if isinstance(o, Group)]
    services = [o for o in added_objects if isinstance(o, Service)]

    assert len(variants) == 1
    assert len(groups) == 13  # 1 root + 12 categories
    assert len(services) == 49

    root_grp = next(g for g in groups if g.number == ROOT_GROUP_NUMBER)
    assert root_grp.name == ROOT_GROUP_NAME
    assert root_grp.parent_id is None

    sub_groups = [g for g in groups if g.number != ROOT_GROUP_NUMBER]
    assert len(sub_groups) == 12
    for g in sub_groups:
        assert g.parent_id == root_grp.id
        assert g.org_id == 424

    # Build tree check
    tree = _build_menu_tree(groups, services)
    assert tree["name"] == "root"
    assert len(tree["items"]) == 1
    assert tree["items"][0]["name"] == ROOT_GROUP_NAME
    assert len(tree["items"][0]["items"]) == 12

    # Check some services in tree
    first_cat = tree["items"][0]["items"][0]  # Стрижки
    assert first_cat["name"] == "Стрижки"
    assert len(first_cat["items"]) == 11
    # Check мужская стрижка
    mens_cut = next(s for s in first_cat["items"] if s["code"] == 1000301)
    assert mens_cut["name"] == "Мужская стрижка"
    assert mens_cut["price"] == "600"
    assert mens_cut["printname"] == "Стрижка стандартная"
    assert mens_cut["prototypeid"] == 990021


@pytest.mark.anyio
async def test_import_legacy_menu_variant_idempotent():
    existing_variant = MenuVariant(id=10, org_id=1, name="Легаси меню")
    mock_session = AsyncMock()
    mock_session.add = MagicMock()

    mock_variant_res = MagicMock()
    mock_variant_res.scalar_one_or_none.return_value = existing_variant
    mock_session.execute.return_value = mock_variant_res

    variant = await import_legacy_menu_variant(mock_session, org_id=1)
    assert variant.id == 10
    assert variant.name == "Легаси меню"
    assert variant.org_id == 1
