"""Controlled confirmation races and denied simulation without a real payment."""

import asyncio
from datetime import UTC, datetime, timedelta
from unittest.mock import AsyncMock, MagicMock
from uuid import uuid4

import pytest
from fastapi import HTTPException

from app.config import settings
from app.models import BillingOrder, BillingOrderItem, License, Terminal
from app.models_l4desk import L4DeskAuditEvent
from app.routers.billing import BillingUser, confirm_payment
from app.services.payment_provider import MockPaymentProvider


@pytest.fixture
def anyio_backend():
    return "asyncio"


def pending_order():
    return BillingOrder(
        id=uuid4(), org_id=1, status="pending", amount_minor=100, currency="RUB"
    )


@pytest.mark.anyio
@pytest.mark.parametrize("enabled", [False, True])
async def test_role3_cannot_request_simulation(monkeypatch, enabled):
    monkeypatch.setattr(settings, "yookassa_enabled", enabled)
    value = pending_order()
    db = AsyncMock()
    db.execute.return_value = MagicMock(scalar_one_or_none=lambda: value)
    db.add = MagicMock()
    with pytest.raises(HTTPException) as caught:
        await confirm_payment(
            str(value.id), BillingUser("tenant-user", 1), db, simulate=True
        )
    assert caught.value.status_code == 403
    db.commit.assert_not_awaited()
    db.add.assert_not_called()


@pytest.mark.anyio
async def test_concurrent_simulation_applies_once_and_preserves_newer_license(
    monkeypatch,
):
    """Both verifications finish before the locked re-read; second observes paid.

    This fake models row locking and checks the SQL lock/populate_existing flags.
    It proves the application schedule, not PostgreSQL deployment correctness.
    """
    monkeypatch.setattr(settings, "yookassa_enabled", False)
    value = pending_order()
    at = datetime.now(UTC)
    license_ = License(
        id=1, terminal_id=1, org_id=1, expires_at=at + timedelta(days=90)
    )
    original_expiry = license_.expires_at
    item = BillingOrderItem(
        id=1,
        order_id=value.id,
        terminal_id=1,
        operation="renewal",
        new_expires_at=at + timedelta(days=30),
        billing_period_months=1,
    )
    row_lock = asyncio.Lock()
    verified = asyncio.Event()
    verification_count = 0
    audit = []

    async def verify(self, _order_id):
        nonlocal verification_count
        verification_count += 1
        if verification_count == 2:
            verified.set()
        await verified.wait()
        return True

    monkeypatch.setattr(MockPaymentProvider, "verify_payment", verify)

    class Session:
        locked = False

        async def execute(self, stmt):
            entity = stmt.column_descriptions[0]["entity"]
            if entity is BillingOrder:
                if stmt._for_update_arg is not None:
                    assert stmt.get_execution_options()["populate_existing"] is True
                    await row_lock.acquire()
                    self.locked = True
                return MagicMock(scalar_one_or_none=lambda: value)
            if entity is BillingOrderItem:
                return MagicMock(scalars=lambda: MagicMock(all=lambda: [item]))
            assert entity is License
            return MagicMock(scalar_one_or_none=lambda: license_)

        async def scalar(self, stmt):
            assert stmt.column_descriptions[0]["entity"] is Terminal
            assert stmt._for_update_arg is not None
            return Terminal(id=1, org_id=1)

        def add(self, entry):
            assert isinstance(entry, L4DeskAuditEvent)
            audit.append(entry)

        async def commit(self):
            self.release()

        def release(self):
            if self.locked:
                self.locked = False
                row_lock.release()

    async def request():
        db = Session()
        try:
            return await confirm_payment(
                str(value.id), BillingUser("superuser", 1, True), db
            )
        finally:
            db.release()

    async with asyncio.timeout(3):
        replies = await asyncio.gather(request(), request())
    assert sorted(reply.items_updated for reply in replies) == [0, 1]
    assert value.status == "paid"
    assert value.provider == "simulation"
    assert license_.expires_at == original_expiry
    assert len(audit) == 1
    assert audit[0].actor == "superuser"
    assert audit[0].outcome == "simulated"
    assert audit[0].details["amount_minor"] == 100
