"""Classic payment adapters. Simulation requires an authenticated superuser."""

from decimal import Decimal, InvalidOperation
from typing import Protocol

from fastapi import HTTPException

from app.config import settings
from app.models import BillingOrder
from app.services.yookassa import get_yookassa_client


class PaymentProvider(Protocol):
    name: str

    async def create_checkout(
        self,
        amount_minor: int,
        currency: str,
        order_id: str,
    ) -> str:
        """Create a checkout session and return the payment URL."""
        ...

    async def verify_payment(self, provider_order_id: str) -> bool:
        """Verify that a payment was completed."""
        ...


class MockPaymentProvider:
    """Explicit administrative simulation, recorded on the order."""

    name = "simulation"

    def __init__(self, order: BillingOrder, *, is_superuser: bool):
        if not is_superuser:
            raise HTTPException(
                403, "Эмуляция оплаты доступна только суперпользователю"
            )
        self.order = order

    async def create_checkout(
        self,
        amount_minor: int,
        currency: str,
        order_id: str,
    ) -> str:
        self.order.provider = "simulation"
        return f"/api/billing/orders/{order_id}/confirm"

    async def verify_payment(self, provider_order_id: str) -> bool:
        return provider_order_id == str(self.order.id)


class YooKassaPaymentProvider:
    name = "yookassa"

    def __init__(self, order: BillingOrder, email: str | None = None):
        self.order = order
        self.email = email

    def metadata(self) -> dict[str, str]:
        return {
            "purpose": "classic_billing",
            "order_id": str(self.order.id),
            "tenant_id": str(self.order.org_id),
        }

    def validate_remote(self, remote: dict) -> bool:
        try:
            amount = Decimal(str(remote["amount"]["value"])) * 100
        except KeyError, TypeError, ValueError, InvalidOperation:
            return False
        metadata = remote.get("metadata")
        return (
            amount.is_finite()
            and amount == self.order.amount_minor
            and remote["amount"].get("currency") == self.order.currency
            and isinstance(metadata, dict)
            and all(
                metadata.get(key) == value for key, value in self.metadata().items()
            )
        )

    async def create_checkout(
        self, amount_minor: int, currency: str, order_id: str
    ) -> str:
        if (amount_minor, currency, order_id) != (
            self.order.amount_minor,
            self.order.currency,
            str(self.order.id),
        ):
            raise HTTPException(409, "Несовпадение заказа оплаты")
        if currency != "RUB" or amount_minor <= 0:
            raise HTTPException(422, "Для оплаты требуется положительная сумма в RUB")
        if settings.yookassa_receipt_enabled and (
            not self.email or "@" not in self.email
        ):
            raise HTTPException(422, "Для чека требуется email организации")
        if not settings.yookassa_return_url_base:
            raise HTTPException(503, "Адрес возврата оплаты не настроен")
        client = get_yookassa_client()
        if self.order.provider_order_id:
            remote = await client.get_payment(self.order.provider_order_id)
        else:
            remote = await client.create_payment(
                amount_kopecks=amount_minor,
                idempotence_key=str(self.order.id),
                return_url=settings.yookassa_return_url_base.rstrip("/") + "/billing",
                description="Оплата Classic: лицензии и сертификаты",
                customer_email=self.email,
                metadata=self.metadata(),
            )
        if not self.validate_remote(remote) or not isinstance(remote.get("id"), str):
            raise HTTPException(502, "Несовпадение ответа платёжного провайдера")
        payment_url = (remote.get("confirmation") or {}).get("confirmation_url")
        if not isinstance(payment_url, str) or not payment_url.startswith("https://"):
            raise HTTPException(502, "Провайдер не вернул адрес оплаты")
        self.order.provider = "yookassa"
        self.order.provider_order_id = remote["id"]
        return payment_url

    async def verify_payment(self, provider_order_id: str) -> bool:
        if provider_order_id != str(self.order.id) or not self.order.provider_order_id:
            return False
        remote = await get_yookassa_client().get_payment(self.order.provider_order_id)
        return (
            self.validate_remote(remote)
            and remote.get("id") == self.order.provider_order_id
            and remote.get("status") == "succeeded"
            and remote.get("paid") is True
        )


class FreeOrderProvider:
    """A server-calculated zero-price order needs no payment or simulation."""

    name = "free"

    def __init__(self, order: BillingOrder):
        self.order = order

    async def create_checkout(
        self, amount_minor: int, currency: str, order_id: str
    ) -> str:
        self.order.provider = self.name
        return f"/api/billing/orders/{order_id}/confirm"

    async def verify_payment(self, provider_order_id: str) -> bool:
        return self.order.amount_minor == 0 and provider_order_id == str(self.order.id)


def get_payment_provider(
    order: BillingOrder,
    *,
    is_superuser: bool,
    email: str | None = None,
    simulate: bool = False,
) -> PaymentProvider:
    """Stored provider wins over rollout flags; there is no tenant mock fallback."""
    if simulate or order.provider == "simulation":
        return MockPaymentProvider(order, is_superuser=is_superuser)
    if order.amount_minor == 0 and order.provider in (None, "free"):
        return FreeOrderProvider(order)
    if order.provider == "yookassa":
        if not settings.is_yookassa_enabled:
            raise HTTPException(503, "Оплата YooKassa выключена")
        return YooKassaPaymentProvider(order, email)
    if order.provider is not None:
        raise HTTPException(409, "Неизвестный способ подтверждения оплаты")
    if settings.is_yookassa_enabled:
        return YooKassaPaymentProvider(order, email)
    if is_superuser:
        return MockPaymentProvider(order, is_superuser=True)
    raise HTTPException(503, "Платные операции пока недоступны")
