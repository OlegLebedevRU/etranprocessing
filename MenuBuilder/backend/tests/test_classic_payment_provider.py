"""Classic provider selection, authoritative verification and admin simulation."""

from itertools import product
from uuid import uuid4

import pytest
from fastapi import HTTPException

from app.config import settings
from app.models import BillingOrder
from app.services.payment_provider import (
    FreeOrderProvider,
    MockPaymentProvider,
    YooKassaPaymentProvider,
    get_payment_provider,
)


@pytest.fixture
def anyio_backend():
    return "asyncio"


def order(provider=None):
    return BillingOrder(
        id=uuid4(), org_id=1, amount_minor=12345, currency="RUB", provider=provider
    )


@pytest.mark.parametrize("enabled,su,simulate", list(product([False, True], repeat=3)))
def test_selection_matrix(monkeypatch, enabled, su, simulate):
    monkeypatch.setattr(settings, "yookassa_enabled", enabled)
    value = order()
    if simulate and not su:
        with pytest.raises(HTTPException) as caught:
            get_payment_provider(value, is_superuser=su, simulate=simulate)
        assert caught.value.status_code == 403
    elif not enabled and not su:
        with pytest.raises(HTTPException) as caught:
            get_payment_provider(value, is_superuser=su, simulate=simulate)
        assert caught.value.status_code == 503
    else:
        selected = get_payment_provider(value, is_superuser=su, simulate=simulate)
        assert isinstance(
            selected,
            MockPaymentProvider if simulate or not enabled else YooKassaPaymentProvider,
        )


@pytest.mark.parametrize("enabled", [False, True])
def test_tenant_cannot_confirm_simulated_order(monkeypatch, enabled):
    monkeypatch.setattr(settings, "yookassa_enabled", enabled)
    with pytest.raises(HTTPException) as caught:
        get_payment_provider(order("simulation"), is_superuser=False)
    assert caught.value.status_code == 403


def test_real_payment_does_not_fall_back_to_simulation(monkeypatch):
    monkeypatch.setattr(settings, "yookassa_enabled", False)
    with pytest.raises(HTTPException) as caught:
        get_payment_provider(order("yookassa"), is_superuser=True)
    assert caught.value.status_code == 503


@pytest.mark.parametrize("enabled", [False, True])
def test_zero_price_is_not_an_emulated_payment(monkeypatch, enabled):
    monkeypatch.setattr(settings, "yookassa_enabled", enabled)
    value = order()
    value.amount_minor = 0
    assert isinstance(
        get_payment_provider(value, is_superuser=False), FreeOrderProvider
    )


@pytest.mark.anyio
@pytest.mark.parametrize(
    "failure",
    [None, "amount", "currency", "tenant", "purpose", "order", "paid", "status", "id"],
)
async def test_authoritative_verification(monkeypatch, failure):
    value = order("yookassa")
    value.provider_order_id = "provider-payment-id"
    provider = YooKassaPaymentProvider(value)
    remote = {
        "id": value.provider_order_id,
        "paid": True,
        "status": "succeeded",
        "amount": {"value": "123.45", "currency": "RUB"},
        "metadata": provider.metadata(),
    }
    if failure == "amount":
        remote["amount"]["value"] = "123.44"
    elif failure == "currency":
        remote["amount"]["currency"] = "USD"
    elif failure == "tenant":
        remote["metadata"]["tenant_id"] = "339"
    elif failure == "purpose":
        remote["metadata"]["purpose"] = "subscription"
    elif failure == "order":
        remote["metadata"]["order_id"] = str(uuid4())
    elif failure == "paid":
        remote["paid"] = False
    elif failure == "status":
        remote["status"] = "pending"
    elif failure == "id":
        remote["id"] = "another-payment"

    class Client:
        async def get_payment(self, payment_id):
            assert payment_id == value.provider_order_id
            return remote

    monkeypatch.setattr(
        "app.services.payment_provider.get_yookassa_client", lambda: Client()
    )
    assert await provider.verify_payment(str(value.id)) is (failure is None)


@pytest.mark.anyio
async def test_real_checkout_reuses_existing_client_and_stores_reference(monkeypatch):
    monkeypatch.setattr(settings, "yookassa_return_url_base", "https://example.invalid")
    monkeypatch.setattr(settings, "yookassa_receipt_enabled", True)
    value = order()
    provider = YooKassaPaymentProvider(value, "billing@example.invalid")

    class Client:
        async def create_payment(self, **kwargs):
            assert kwargs["amount_kopecks"] == 12345
            assert kwargs["idempotence_key"] == str(value.id)
            assert kwargs["metadata"] == provider.metadata()
            assert kwargs["customer_email"] == "billing@example.invalid"
            return {
                "id": "payment-created",
                "amount": {"value": "123.45", "currency": "RUB"},
                "metadata": provider.metadata(),
                "confirmation": {"confirmation_url": "https://example.invalid/pay"},
            }

    monkeypatch.setattr(
        "app.services.payment_provider.get_yookassa_client", lambda: Client()
    )
    assert (
        await provider.create_checkout(12345, "RUB", str(value.id))
        == "https://example.invalid/pay"
    )
    assert value.provider == "yookassa"
    assert value.provider_order_id == "payment-created"
