from __future__ import annotations

from datetime import UTC, date, datetime
from types import SimpleNamespace
from typing import Any, cast
from unittest.mock import AsyncMock, MagicMock

import pytest
from sqlalchemy.ext.asyncio import AsyncSession

from app.services.financial_core.metering import FinMeteringService
from app.services.financial_core.metering_close_worker import FinMeteringCloseWorker


@pytest.mark.anyio
async def test_daily_close_requires_explicit_tenant_allowlist() -> None:
    db = MagicMock()
    worker = FinMeteringCloseWorker(tenant_ids=[])

    result = await worker.run_single_tick(cast(AsyncSession, db))

    assert result == {"days_closed": 0, "rows_posted": 0}
    db.execute.assert_not_called()
    db.commit.assert_not_called()


@pytest.mark.anyio
async def test_daily_close_waits_for_moscow_midnight_and_grace(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    row = SimpleNamespace(
        tenant_id=3,
        local_date=date(2026, 9, 26),
        timezone="Europe/Moscow",
        ledger_transaction_id=None,
    )
    result = MagicMock()
    result.scalars.return_value.all.return_value = [row]
    db = MagicMock()
    db.execute = AsyncMock(return_value=result)
    db.commit = AsyncMock()
    post = AsyncMock(return_value=[SimpleNamespace(ledger_transaction_id=9)])
    monkeypatch.setattr(FinMeteringService, "close_and_post_daily_usage", post)
    worker = FinMeteringCloseWorker(tenant_ids=[3], grace_sec=300)

    before = await worker.run_single_tick(
        cast(AsyncSession, db), datetime(2026, 9, 26, 21, 4, tzinfo=UTC)
    )
    assert before == {"days_closed": 0, "rows_posted": 0}
    post.assert_not_awaited()
    db.commit.assert_not_awaited()

    after = await worker.run_single_tick(
        cast(AsyncSession, db), datetime(2026, 9, 26, 21, 6, tzinfo=UTC)
    )
    assert after == {"days_closed": 1, "rows_posted": 1}
    post.assert_awaited_once_with(
        cast(Any, db), tenant_id=3, local_date=date(2026, 9, 26)
    )
    db.commit.assert_awaited_once()


@pytest.mark.anyio
async def test_daily_close_waits_for_every_row_in_tenant_day(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    rows = [
        SimpleNamespace(
            tenant_id=3, local_date=date(2026, 9, 26), timezone="Europe/Moscow"
        ),
        SimpleNamespace(tenant_id=3, local_date=date(2026, 9, 26), timezone="UTC"),
    ]
    result = MagicMock()
    result.scalars.return_value.all.return_value = rows
    db = MagicMock()
    db.execute = AsyncMock(return_value=result)
    db.commit = AsyncMock()
    post = AsyncMock()
    monkeypatch.setattr(FinMeteringService, "close_and_post_daily_usage", post)
    worker = FinMeteringCloseWorker(tenant_ids=[3], grace_sec=300)

    outcome = await worker.run_single_tick(
        cast(AsyncSession, db), datetime(2026, 9, 26, 21, 6, tzinfo=UTC)
    )

    assert outcome == {"days_closed": 0, "rows_posted": 0}
    post.assert_not_awaited()
    db.commit.assert_not_awaited()
