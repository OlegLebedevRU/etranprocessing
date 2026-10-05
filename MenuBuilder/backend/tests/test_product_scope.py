from itertools import product
from unittest.mock import AsyncMock

import pytest
from fastapi import HTTPException

from app.config import settings
from app.models import Org, Terminal
from app.services.product_scope import tenant_product, validate_terminal_transfer
from app.services.subscriptions import check_terminal, subscription_context


@pytest.mark.anyio
async def test_transfer_matrix(monkeypatch):
    monkeypatch.setattr(settings, "product_scope_split_enabled", True)
    for source, target, enrolled, same in product(
        ["classic", "l4desk"], ["classic", "l4desk"], [False, True], [False, True]
    ):
        db = AsyncMock()
        db.get.side_effect = [Org(site_mode=source), Org(site_mode=target)]
        terminal = Terminal(org_id=1, l4desk_subscription_enabled=enrolled)
        if same or (not enrolled and source == target == "classic"):
            await validate_terminal_transfer(db, terminal, 1 if same else 2)
        else:
            with pytest.raises(HTTPException) as exc:
                await validate_terminal_transfer(db, terminal, 1 if same else 2)
            assert exc.value.status_code == 409


@pytest.mark.anyio
async def test_unclassified_tenant_rejected_after_rollout(monkeypatch):
    monkeypatch.setattr(settings, "product_scope_split_enabled", True)
    db = AsyncMock()
    db.get.return_value = Org(site_mode="both")
    with pytest.raises(HTTPException) as exc:
        await tenant_product(db, 1)
    assert exc.value.status_code == 409


@pytest.mark.anyio
async def test_classic_terminal_ignores_tenant_technical_profile(monkeypatch):
    monkeypatch.setattr(settings, "product_scope_split_enabled", True)
    db = AsyncMock()
    runtime = Terminal(org_id=1, l4desk_subscription_enabled=False)
    assert await check_terminal(db, 1, runtime) is None
    db.get.assert_not_called()
    db.scalar.assert_not_called()


@pytest.mark.anyio
async def test_enrolled_terminal_without_profile_is_denied(monkeypatch):
    monkeypatch.setattr(settings, "product_scope_split_enabled", True)
    db = AsyncMock()
    db.get.return_value = None
    with pytest.raises(HTTPException) as exc:
        await check_terminal(
            db, 1, Terminal(org_id=1, l4desk_subscription_enabled=True)
        )
    assert exc.value.status_code == 403


@pytest.mark.anyio
async def test_free_pool_requires_explicit_enrollment_and_same_tenant(monkeypatch):
    monkeypatch.setattr(settings, "product_scope_split_enabled", True)
    db = AsyncMock()
    await subscription_context(db, 1)
    statement = str(db.scalar.call_args.args[0])
    assert "JOIN terminals" in statement
    assert "terminals.org_id" in statement
    assert "terminals.l4desk_subscription_enabled IS true" in statement


@pytest.mark.parametrize("mode", ["classic", "l4desk"])
@pytest.mark.parametrize(
    "field",
    [
        "site_mode",
        "default_site",
        "classic_licenses_enabled",
        "l4desk_licenses_enabled",
    ],
)
@pytest.mark.parametrize("same", [False, True])
def test_admin_product_update_matrix(monkeypatch, mode, field, same):
    from app.services.product_scope import validate_product_update

    monkeypatch.setattr(settings, "product_scope_split_enabled", True)
    canonical = {
        "site_mode": mode,
        "default_site": mode,
        "classic_licenses_enabled": mode == "classic",
        "l4desk_licenses_enabled": mode == "l4desk",
    }
    value = canonical[field]
    opposite = (
        (not value)
        if isinstance(value, bool)
        else ("l4desk" if value == "classic" else "classic")
    )
    changes = {field: value if same else opposite}
    if same:
        validate_product_update(Org(site_mode=mode), changes)
    else:
        with pytest.raises(HTTPException) as exc:
            validate_product_update(Org(site_mode=mode), changes)
        assert exc.value.status_code == 409


def test_l4desk_tariff_is_frozen_but_noncommercial_edit_allowed(monkeypatch):
    from app.services.product_scope import (
        CLASSIC_TARIFF_FIELDS,
        validate_product_update,
    )

    monkeypatch.setattr(settings, "product_scope_split_enabled", True)
    org = Org(site_mode="l4desk")
    validate_product_update(org, {"name": "Renamed", "is_active": False})
    for field in CLASSIC_TARIFF_FIELDS:
        with pytest.raises(HTTPException) as exc:
            validate_product_update(org, {field: None})
        assert exc.value.status_code == 409
        validate_product_update(Org(site_mode="classic"), {field: None})


@pytest.mark.anyio
async def test_admin_product_switch_rejected_before_writes(monkeypatch):
    from app.routers.admin_organizations import update_organization
    from app.schemas import AdminOrgUpdate

    monkeypatch.setattr(settings, "product_scope_split_enabled", True)
    db = AsyncMock()
    db.get.return_value = Org(org_id=1, site_mode="classic")
    with pytest.raises(HTTPException) as exc:
        await update_organization(
            1, AdminOrgUpdate(site_mode="l4desk"), db=db, user={"is_superuser": True}
        )
    assert exc.value.status_code == 409
    db.commit.assert_not_called()
    db.execute.assert_not_called()
