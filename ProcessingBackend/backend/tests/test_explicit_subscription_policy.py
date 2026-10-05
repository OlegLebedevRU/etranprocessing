from datetime import UTC, datetime, timedelta
from itertools import product
from types import SimpleNamespace
from unittest.mock import AsyncMock

import pytest
from etranprocessing_access import subscription_allowed, subscription_state

from app.config import settings
from app.models import Terminal
from app.services.leo4proxy_policy import get_leo4proxy_policy, subscription_allowance


@pytest.mark.anyio
async def test_complete_transport_matrix_matches_shared_decision(monkeypatch):
    monkeypatch.setattr(settings, "product_scope_split_enabled", True)
    at = datetime(2026, 10, 5, tzinfo=UTC)
    deadlines = [
        None,
        at + timedelta(days=1),
        at - timedelta(days=1),
        at - timedelta(days=4),
    ]
    count = 0
    for deleted, active, free, enabled, paid in product(
        [False, True], [False, True], [False, True], [False, True], deadlines
    ):
        monkeypatch.setattr(settings, "yookassa_enabled", enabled)
        runtime = Terminal(
            id=773,
            org_id=1,
            sn="fixture",
            is_active=active,
            l4desk_subscription_enabled=True,
            cert_not_valid_after=at + timedelta(days=365),
        )
        db = AsyncMock()
        db.get.return_value = SimpleNamespace(timezone="UTC")
        db.scalar.side_effect = [
            SimpleNamespace(deleted_at=at if deleted else None, paid_until=paid),
            773 if free else 774,
            None,
        ]
        allowance = await subscription_allowance(db, runtime, now=at)
        final = get_leo4proxy_policy(runtime, allowance, now=at)
        expected = subscription_state(
            deleted=deleted,
            admin_active=active,
            is_free=free,
            payments_enabled=enabled,
            paid_until=paid,
            grace_until=paid + timedelta(days=3) if paid else None,
            now=at,
        )
        assert final.mqtt_rtp_allowed == subscription_allowed(expected)
        assert final.outgoing_https_allowed
        count += 1
    assert count == 64


@pytest.mark.anyio
async def test_classic_profile_never_enrolls_other_terminals(monkeypatch):
    monkeypatch.setattr(settings, "product_scope_split_enabled", True)
    db = AsyncMock()
    assert await subscription_allowance(db, Terminal(l4desk_subscription_enabled=False))
    db.get.assert_not_called()
    db.scalar.assert_not_called()


@pytest.mark.anyio
async def test_explicit_enrollment_without_profile_is_denied(monkeypatch):
    monkeypatch.setattr(settings, "product_scope_split_enabled", True)
    db = AsyncMock()
    db.get.return_value = None
    assert not await subscription_allowance(
        db, Terminal(org_id=1, l4desk_subscription_enabled=True)
    )
