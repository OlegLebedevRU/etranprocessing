"""Payment provider abstraction.

Returns 501 when no provider is configured.
No fake success path.
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


def get_payment_provider() -> PaymentProvider | None:
    """Get the configured payment provider, or None if not configured."""
    # No provider configured yet — return None
    # When a provider is added, check settings here
    return None
