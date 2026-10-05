from itertools import product
from unittest.mock import AsyncMock

import pytest
from fastapi import HTTPException

from app.config import settings
from app.models import Org, OrgBillingSettings, Terminal
from app.services.certificate_permission import certificate_permission


@pytest.mark.anyio
async def test_classic_tariff_matrix(monkeypatch):
    monkeypatch.setattr(settings, "product_scope_split_enabled", True)
    cases = 0
    for purpose, su, enabled, master, charged in product(
        ["setup", "renew"], [False, True], [False, True], [False, True], [False, True]
    ):
        db = AsyncMock()
        db.get.return_value = Org(site_mode="classic")
        db.scalar.side_effect = [
            OrgBillingSettings(
                billing_mode="master" if master else "prepaid",
                tenant_pin_creation_enabled=enabled,
                cert_billing_mode="per_operation",
                cert_price_minor=100,
                cert_charge_primary_issue=charged,
                cert_charge_reissue=charged,
                currency="RUB",
            ),
            None,
        ]
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
                await certificate_permission(
                    db, terminal, purpose=purpose, is_superuser=su
                )
                is None
            )
        else:
            with pytest.raises(HTTPException) as exc:
                await certificate_permission(
                    db, terminal, purpose=purpose, is_superuser=su
                )
            assert exc.value.status_code == expected
        cases += 1
    assert cases == 32


@pytest.mark.anyio
async def test_l4desk_never_reads_classic_tariff(monkeypatch):
    monkeypatch.setattr(settings, "product_scope_split_enabled", True)
    db = AsyncMock()
    db.get.return_value = Org(site_mode="l4desk")
    assert (
        await certificate_permission(db, Terminal(org_id=10000), purpose="renew")
        is None
    )
    db.scalar.assert_not_called()


@pytest.mark.anyio
async def test_paid_renewal_confirmation_does_not_issue_or_expose_pin():
    from app.models import BillingOrder, BillingOrderItem
    from app.routers.billing import _apply_cert_pin_item

    db = AsyncMock()
    await _apply_cert_pin_item(
        db, BillingOrder(), BillingOrderItem(cert_policy_snapshot={"purpose": "renew"})
    )
    db.execute.assert_not_called()
    db.add.assert_not_called()
