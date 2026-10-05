import asyncio
from types import SimpleNamespace
from unittest.mock import AsyncMock, MagicMock

import pytest

from app.models_l4desk import L4DeskRemoteSession
from app.services import remote_session_stop as stop
from app.services import subscription_worker as worker


@pytest.mark.anyio
@pytest.mark.parametrize("active", [False, True])
@pytest.mark.parametrize("commercial", [None, False, True])
@pytest.mark.parametrize("retry", [False, True])
async def test_common_admin_deny_survives_commercial_recovery(
    monkeypatch, active, commercial, retry
):
    session = L4DeskRemoteSession(
        id=1,
        tenant_id=1,
        terminal_id=1,
        state="stop_requested" if retry else "active",
        reason="subscription_blocked",
        operation_id="fixture",
        correlation_id="fixture",
    )
    repo = SimpleNamespace(
        get_active_sessions_for_tenant=AsyncMock(return_value=[session]),
        get_stop_requested_sessions=AsyncMock(return_value=[session]),
    )
    monkeypatch.setattr(stop, "L4DeskRepository", lambda db: repo)
    monkeypatch.setattr(
        "app.services.subscriptions.check_terminal",
        AsyncMock(
            return_value=None
            if commercial is None
            else SimpleNamespace(allowed=commercial)
        ),
    )
    provider = AsyncMock(return_value=True)
    monkeypatch.setattr(
        stop.RemoteSessionStopService, "_stop_provider_session", provider
    )
    monkeypatch.setattr(stop.RemoteSessionStopService, "_close_after_stop", AsyncMock())
    db = SimpleNamespace(
        get=AsyncMock(return_value=SimpleNamespace(is_active=active)),
        flush=AsyncMock(),
        add=MagicMock(),
    )
    if retry:
        await stop.RemoteSessionStopService.process_stop_outbox(db, tenant_id=1)
    else:
        await stop.RemoteSessionStopService.stop_sessions_for_blocked_tenant(
            db, 1, terminal_id=1
        )
    assert provider.await_count == int(not active or commercial is False)


@pytest.mark.anyio
async def test_slow_payment_loop_cannot_starve_session_checks(monkeypatch):
    payment_started = asyncio.Event()
    sessions_checked = asyncio.Event()
    never = asyncio.Event()

    async def payments():
        payment_started.set()
        await never.wait()

    async def sessions():
        await payment_started.wait()
        sessions_checked.set()
        await never.wait()

    monkeypatch.setattr(worker, "reconcile_payments", payments)
    monkeypatch.setattr(worker, "reconcile_sessions", sessions)
    task = asyncio.create_task(worker.run_worker())
    try:
        await asyncio.wait_for(sessions_checked.wait(), 1)
    finally:
        task.cancel()
        with pytest.raises(asyncio.CancelledError):
            await task
