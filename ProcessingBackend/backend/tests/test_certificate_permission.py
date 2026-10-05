from itertools import product
from unittest.mock import AsyncMock

import pytest
from fastapi import HTTPException

from app.config import settings
from app.models import BillingOrderItem, Org, OrgBillingSettings, Terminal
from app.services.certificate_permission import validate_pin_permission


@pytest.mark.anyio
async def test_classic_provider_tariff_matrix_matches_producer(monkeypatch):
    monkeypatch.setattr(settings, "product_scope_split_enabled", True)
    cases = 0
    for purpose, su, enabled, master, charged in product(
        ["setup", "renew"], [False, True], [False, True], [False, True], [False, True]
    ):
        db = AsyncMock()
        db.get.return_value = Org(site_mode="classic")
        db.scalar.return_value = OrgBillingSettings(
            billing_mode="master" if master else "prepaid",
            tenant_pin_creation_enabled=enabled,
            cert_billing_mode="per_operation",
            cert_price_minor=100,
            cert_charge_primary_issue=charged,
            cert_charge_reissue=charged,
        )
        terminal = Terminal(id=773, org_id=1, cert_serial="A" * 40)
        expected = (
            None
            if su or master or (enabled and not charged)
            else 403
            if not enabled
            else 402
        )
        if expected is None:
            assert (
                await validate_pin_permission(
                    db, terminal, purpose, None, admin_override=su
                )
                is None
            )
        else:
            with pytest.raises(HTTPException) as exc:
                await validate_pin_permission(
                    db, terminal, purpose, None, admin_override=su
                )
            assert exc.value.status_code == expected
        cases += 1
    assert cases == 32


@pytest.mark.anyio
@pytest.mark.parametrize("item_purpose", [None, "setup", "renew"])
@pytest.mark.parametrize("requested_purpose", ["setup", "renew"])
async def test_paid_item_is_scoped_and_cannot_cross_purpose(
    monkeypatch, item_purpose, requested_purpose
):
    monkeypatch.setattr(settings, "product_scope_split_enabled", False)
    db = AsyncMock()
    item = BillingOrderItem(
        id=7, cert_policy_snapshot={"purpose": item_purpose} if item_purpose else None
    )
    db.scalar.return_value = item
    if (item_purpose or "setup") == requested_purpose:
        assert (
            await validate_pin_permission(
                db, Terminal(id=773, org_id=1), requested_purpose, 7
            )
            is item
        )
    else:
        with pytest.raises(HTTPException) as exc:
            await validate_pin_permission(
                db, Terminal(id=773, org_id=1), requested_purpose, 7
            )
        assert exc.value.status_code == 403
    statement = str(db.scalar.call_args.args[0])
    assert "billing_orders.status" in statement
    assert "billing_orders.org_id" in statement
    assert "billing_order_items.terminal_id" in statement
