"""Payment provider abstraction for MenuBuilder.

Mock provider always returns success — for development and testing.
Replace with real provider integration when ready.
"""

import logging
from typing import Protocol

logger = logging.getLogger(__name__)


class PaymentProvider(Protocol):
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
    """Mock provider that always succeeds. For dev/test only."""

    async def create_checkout(
        self,
        amount_minor: int,
        currency: str,
        order_id: str,
    ) -> str:
        logger.info(
            "MockPaymentProvider: checkout order=%s amount=%d %s",
            order_id,
            amount_minor,
            currency,
        )
        # Return a mock URL pointing to the confirm endpoint
        return f"/api/billing/orders/{order_id}/confirm"

    async def verify_payment(self, provider_order_id: str) -> bool:
        logger.info("MockPaymentProvider: verify order=%s → True", provider_order_id)
        return True


_provider: PaymentProvider | None = None


def get_payment_provider() -> PaymentProvider:
    """Get the configured payment provider. Returns mock if none configured."""
    global _provider
    if _provider is None:
        _provider = MockPaymentProvider()
    return _provider
