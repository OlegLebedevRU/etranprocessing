"""Product ownership comes from reviewed tenant policy, never technical rows."""

from typing import Literal

from fastapi import HTTPException
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.models import Org, Terminal

Product = Literal["classic", "l4desk"]

CLASSIC_TARIFF_FIELDS = {
    "monthly_price_minor",
    "currency",
    "billing_mode",
    "min_billing_periods",
    "allowed_billing_periods",
    "default_selection_mode",
    "cert_billing_mode",
    "cert_price_minor",
    "tenant_pin_creation_enabled",
    "cert_charge_primary_issue",
    "cert_charge_reissue",
}


def validate_product_update(org: Org, changes: dict) -> None:
    if not settings.product_scope_split_enabled:
        return
    mode = org.site_mode
    if mode not in ("classic", "l4desk"):
        raise HTTPException(409, "Продукт организации ещё не классифицирован")
    expected = {
        "site_mode": mode,
        "default_site": mode,
        "classic_licenses_enabled": mode == "classic",
        "l4desk_licenses_enabled": mode == "l4desk",
    }
    if any(
        value is not None and key in expected and value != expected[key]
        for key, value in changes.items()
    ):
        raise HTTPException(
            409, "Смена продукта организации требует отдельной миграции"
        )
    if mode == "l4desk" and CLASSIC_TARIFF_FIELDS & changes.keys():
        raise HTTPException(409, "Тариф Classic для L4Desk заморожен")


async def authorize_subscription_ui(
    db: AsyncSession, user: dict, tenant_id: int
) -> None:
    if not settings.product_scope_split_enabled:
        return
    if user.get("is_superuser"):
        return  # Tenant selection/ownership remains the caller's responsibility.
    if tenant_id != int(user.get("org_id") or 0):
        raise HTTPException(403, "Организация вне активного контекста")
    if await tenant_product(db, tenant_id) != "l4desk":
        raise HTTPException(403, "Раздел подписок недоступен в Classic")


async def tenant_product(db: AsyncSession, tenant_id: int) -> Product:
    if not settings.product_scope_split_enabled:
        return "classic"
    org = await db.get(Org, tenant_id)
    if org is None:
        raise HTTPException(404, "Организация не найдена")
    if org.site_mode in ("classic", "l4desk"):
        return org.site_mode
    raise HTTPException(409, "Продукт организации ещё не классифицирован")


async def validate_terminal_transfer(
    db: AsyncSession, terminal: Terminal, target_tenant_id: int
) -> None:
    if not settings.product_scope_split_enabled or target_tenant_id == terminal.org_id:
        return
    if terminal.l4desk_subscription_enabled:
        raise HTTPException(409, "Перенос терминала L4Desk требует отдельной миграции")
    source = await tenant_product(db, terminal.org_id)
    target = await tenant_product(db, target_tenant_id)
    if source != target or source == "l4desk":
        raise HTTPException(409, "Перенос между продуктами требует отдельной миграции")
