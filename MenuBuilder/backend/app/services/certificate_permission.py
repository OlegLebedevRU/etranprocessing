"""Classic tariff admission shared by setup, onboarding and native renewal."""

from datetime import UTC, datetime
from typing import Literal

from fastapi import HTTPException
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.models import (
    BillingOrder,
    BillingOrderItem,
    CertificatePin,
    OrgBillingSettings,
    Terminal,
)
from app.services.cert_billing import (
    resolve_cert_policy,
    resolve_effective_price,
    resolve_operation_type,
)
from app.services.product_scope import tenant_product


async def certificate_permission(
    db: AsyncSession,
    terminal: Terminal,
    *,
    purpose: Literal["setup", "renew"],
    is_superuser: bool = False,
    order_id: str | None = None,
) -> int | None:
    """Return a paid order item, or None for a server-authorized free operation."""
    if not settings.product_scope_split_enabled or is_superuser:
        return None
    if await tenant_product(db, terminal.org_id) == "l4desk":
        return None
    tariff = await db.scalar(
        select(OrgBillingSettings).where(OrgBillingSettings.org_id == terminal.org_id)
    )
    if tariff is None:
        raise HTTPException(409, "Тариф сертификатов не настроен")
    if tariff.billing_mode == "master":
        return None
    if not tariff.tenant_pin_creation_enabled:
        raise HTTPException(403, "Самостоятельная выдача PIN отключена")
    price = resolve_effective_price(
        resolve_cert_policy(tariff), resolve_operation_type(terminal.cert_serial)
    )
    if price <= 0:
        return None
    if purpose == "setup" or not order_id:
        paid_pin = await db.scalar(
            select(CertificatePin.id)
            .join(BillingOrderItem, BillingOrderItem.id == CertificatePin.order_item_id)
            .join(BillingOrder, BillingOrder.id == BillingOrderItem.order_id)
            .where(
                CertificatePin.terminal_id == terminal.id,
                CertificatePin.org_id == terminal.org_id,
                CertificatePin.purpose == purpose,
                CertificatePin.status == "pending",
                CertificatePin.expires_at > datetime.now(UTC),
                BillingOrder.org_id == terminal.org_id,
                BillingOrder.status == "paid",
                BillingOrderItem.terminal_id == terminal.id,
                BillingOrderItem.operation == "cert_pin",
            )
        )
        if paid_pin is not None:
            return await db.scalar(
                select(CertificatePin.order_item_id).where(
                    CertificatePin.id == paid_pin
                )
            )
    elif order_id:
        from uuid import UUID

        try:
            identity = UUID(order_id)
        except ValueError as exc:
            raise HTTPException(422, "Некорректный заказ") from exc
        item = await db.scalar(
            select(BillingOrderItem)
            .join(BillingOrder, BillingOrder.id == BillingOrderItem.order_id)
            .where(
                BillingOrder.id == identity,
                BillingOrder.org_id == terminal.org_id,
                BillingOrder.status == "paid",
                BillingOrderItem.terminal_id == terminal.id,
                BillingOrderItem.operation == "cert_pin",
                BillingOrderItem.cert_policy_snapshot["purpose"].as_string() == "renew",
            )
        )
        if item is not None:
            return item.id
    raise HTTPException(
        402,
        {
            "code": "certificate_payment_required",
            "message": "Для этой операции требуется подтверждённая оплата сертификата",
            "terminal_id": terminal.id,
            "purpose": purpose,
            "checkout_path": f"/api/billing/terminals/{terminal.id}/certificate-pin?purpose={purpose}",
        },
    )
