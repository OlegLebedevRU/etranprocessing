"""Pure decisions only: no ORM, credentials, network or implicit enrollment."""

from datetime import datetime
from typing import Literal

SubscriptionState = Literal[
    "deleted",
    "admin_disabled",
    "free",
    "payments_disabled",
    "unpaid",
    "active",
    "grace",
    "expired",
]


def subscription_state(
    *,
    deleted: bool,
    admin_active: bool,
    is_free: bool,
    payments_enabled: bool,
    paid_until: datetime | None,
    grace_until: datetime | None,
    now: datetime,
) -> SubscriptionState:
    """Preserve L4Desk precedence; calendar deadlines are caller-owned facts."""
    if deleted:
        return "deleted"
    if not admin_active:
        return "admin_disabled"
    if is_free:
        return "free"
    if not payments_enabled:
        return "payments_disabled"
    if paid_until is None:
        return "unpaid"
    if now < paid_until:
        return "active"
    if grace_until is not None and now < grace_until:
        return "grace"
    return "expired"


def subscription_allowed(state: SubscriptionState) -> bool:
    return state in ("free", "active", "grace")
