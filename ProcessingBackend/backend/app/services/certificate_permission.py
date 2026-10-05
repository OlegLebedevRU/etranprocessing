"""Consume MB's factual paid certificate permission; PB owns PIN lifecycle."""

from typing import Literal

from fastapi import HTTPException
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.models import BillingOrder, BillingOrderItem, Org, OrgBillingSettings, Terminal


async def validate_pin_permission(
    db: AsyncSession,
    terminal: Terminal,
    purpose: Literal["setup", "renew"],
    order_item_id: int | None,
    *,
    admin_override: bool = False,
) -> BillingOrderItem | None:
    must_pay = False
    if settings.product_scope_split_enabled and not admin_override:
        org = await db.get(Org, terminal.org_id)
        if org is None or org.site_mode not in ("classic", "l4desk"):
            raise HTTPException(409, "tenant_product_not_classified")
        if org.site_mode == "classic":
            tariff = await db.scalar(
                select(OrgBillingSettings).where(
                    OrgBillingSettings.org_id == terminal.org_id
                )
            )
            if tariff is None:
                raise HTTPException(409, "certificate_tariff_not_configured")
            if tariff.billing_mode != "master":
                if not tariff.tenant_pin_creation_enabled:
                    raise HTTPException(403, "certificate_selfservice_disabled")
                charged = (
                    tariff.cert_charge_primary_issue
                    if terminal.cert_serial is None
                    else tariff.cert_charge_reissue
                )
                must_pay = (
                    tariff.cert_billing_mode == "per_operation"
                    and charged
                    and (tariff.cert_price_minor or 0) > 0
                )
    if order_item_id is not None:
        item = await db.scalar(
            select(BillingOrderItem)
            .join(BillingOrder, BillingOrder.id == BillingOrderItem.order_id)
            .where(
                BillingOrderItem.id == order_item_id,
                BillingOrderItem.terminal_id == terminal.id,
                BillingOrderItem.operation == "cert_pin",
                BillingOrder.org_id == terminal.org_id,
                BillingOrder.status == "paid",
            )
        )
        if (
            item is None
            or (item.cert_policy_snapshot or {}).get("purpose", "setup") != purpose
        ):
            raise HTTPException(403, "certificate_paid_permission_mismatch")
        return item
    if must_pay:
        raise HTTPException(402, "certificate_payment_required")
    return None
