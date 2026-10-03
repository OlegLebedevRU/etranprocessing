from datetime import UTC, datetime, timedelta
from unittest.mock import AsyncMock

import pytest

from app.models import Terminal
from app.models_l4desk import L4DeskTerminal
from app.services.subscriptions import add_months, subscription_status


def state(*, free=False, paid=None, enabled=True, active=True, at=None):
    terminal = L4DeskTerminal(terminal_id=2, tenant_id=1, ordinal=2, paid_until=paid)
    runtime = Terminal(
        id=2, device_id=1000009, org_id=1, sn="subscription-test", is_active=active
    )
    return subscription_status(
        terminal,
        runtime,
        free_id=2 if free else 1,
        timezone="Europe/Moscow",
        enabled=enabled,
        now=at or datetime(2026, 10, 3, tzinfo=UTC),
    )


def test_free_is_unlimited_even_without_payment_provider():
    assert state(free=True).device_id == 1000009
    assert state(free=True).terminal_id == 2
    assert state(free=True, enabled=False).allowed
    assert state(free=True, enabled=False).state == "free"


def test_provider_gate_never_grants_a_subscription():
    assert not state(enabled=True).allowed
    assert state(enabled=True).state == "unpaid"
    assert not state(enabled=False, paid=datetime(2027, 1, 1, tzinfo=UTC)).allowed


def test_administrative_disable_is_not_cancelled_by_payment_or_free_privilege():
    assert state(free=True, active=False).state == "admin_disabled"
    assert not state(active=False, paid=datetime(2027, 1, 1, tzinfo=UTC)).allowed


def test_exact_expiry_and_grace_boundaries():
    paid = datetime(2026, 10, 1, tzinfo=UTC)
    assert state(paid=paid, at=paid - timedelta(seconds=1)).state == "active"
    assert state(paid=paid, at=paid).state == "grace"
    assert state(paid=paid, at=paid + timedelta(days=3) - timedelta(seconds=1)).allowed
    assert not state(paid=paid, at=paid + timedelta(days=3)).allowed


def test_calendar_month_packages_do_not_iterate_clipped_months():
    at = datetime(2026, 1, 31, 9, tzinfo=UTC)
    assert add_months(at, 1, "UTC") == datetime(2026, 2, 28, 9, tzinfo=UTC)
    assert add_months(at, 3, "UTC") == datetime(2026, 4, 30, 9, tzinfo=UTC)


@pytest.mark.anyio
async def test_policy_uses_server_tenant_scope_for_every_role(monkeypatch):
    from app.services.remote_session_policy import get_remote_session_policy

    monkeypatch.setattr(
        "app.services.remote_session_policy.check_terminal",
        AsyncMock(return_value=state()),
    )
    for role in (1, 3, 4, 5):
        decision = await get_remote_session_policy(
            {"role_id": role}
        ).evaluate_session_request(
            1, Terminal(id=2), "video", {"role_id": role}, AsyncMock()
        )
        assert not decision.allowed
        assert decision.error_code == "subscription_unpaid"


def test_resource_duration_split_survives_dst_fold_and_rounds_once():
    from app.services.resource_time import split_interval_by_local_days

    start = datetime(2026, 10, 25, 0, 30, tzinfo=UTC)
    end = datetime(2026, 10, 25, 1, 30, tzinfo=UTC)
    assert (
        sum(
            seconds
            for _, seconds in split_interval_by_local_days(start, end, "Europe/Berlin")
        )
        == 3600
    )
    start = datetime(2026, 10, 2, 23, 59, 59, 400000, tzinfo=UTC)
    end = datetime(2026, 10, 3, 0, 0, 0, 600000, tzinfo=UTC)
    assert (
        sum(seconds for _, seconds in split_interval_by_local_days(start, end, "UTC"))
        == 1
    )
