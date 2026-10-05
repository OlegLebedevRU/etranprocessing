from collections import Counter
from datetime import UTC, datetime, timedelta
from itertools import product

from etranprocessing_access import subscription_allowed, subscription_state


def test_exhaustive_branch_partition():
    now = datetime(2026, 10, 5, tzinfo=UTC)
    deadlines = [
        (None, None),
        (now + timedelta(days=1), now + timedelta(days=4)),
        (now - timedelta(days=1), now + timedelta(days=2)),
        (now - timedelta(days=4), now - timedelta(days=1)),
    ]
    states = Counter()
    allowed_count = 0
    for deleted, active, free, enabled, (paid, grace) in product(
        [False, True], [False, True], [False, True], [False, True], deadlines
    ):
        state = subscription_state(
            deleted=deleted,
            admin_active=active,
            is_free=free,
            payments_enabled=enabled,
            paid_until=paid,
            grace_until=grace,
            now=now,
        )
        states[state] += 1
        allowed_count += subscription_allowed(state)
        if deleted or not active:
            assert not subscription_allowed(state)
    assert states == {
        "deleted": 32,
        "admin_disabled": 16,
        "free": 8,
        "payments_disabled": 4,
        "unpaid": 1,
        "active": 1,
        "grace": 1,
        "expired": 1,
    }
    assert allowed_count == 10


def test_exact_deadlines():
    at = datetime(2026, 10, 5, tzinfo=UTC)
    common = {
        "deleted": False,
        "admin_active": True,
        "is_free": False,
        "payments_enabled": True,
    }
    assert (
        subscription_state(
            **common, paid_until=at, grace_until=at + timedelta(days=3), now=at
        )
        == "grace"
    )
    assert (
        subscription_state(
            **common, paid_until=at - timedelta(days=3), grace_until=at, now=at
        )
        == "expired"
    )
