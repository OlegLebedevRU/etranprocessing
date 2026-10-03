from datetime import UTC, datetime, timedelta
from types import SimpleNamespace
from unittest.mock import AsyncMock

import pytest

from app.models_l4desk import L4DeskRemoteSession
from app.repositories.l4desk_repository import L4DeskRepository
from app.routers import video_control
from app.services.remote_session_metering import (
    checkpoint_due,
    checkpoint_remote_session,
    close_remote_session,
    initial_metering_cursor,
)
from app.services.usage import UsageService


def _session(at: datetime, *, cursor: int | None = None) -> L4DeskRemoteSession:
    session = L4DeskRemoteSession(
        id=42,
        tenant_id=1000,
        terminal_id=1000005,
        operation_id="op-42",
        correlation_id="corr-42",
        provider_session_id="stream-42",
        session_type="video",
        state="active",
        active_at=at,
    )
    session.last_cursor = initial_metering_cursor(at) if cursor is None else cursor
    return session


@pytest.fixture
def metering_fakes(monkeypatch):
    db = SimpleNamespace(commit=AsyncMock())
    get_active = AsyncMock()
    audit = AsyncMock(return_value=SimpleNamespace(id=11))
    record_usage = AsyncMock(return_value=[])
    monkeypatch.setattr(
        L4DeskRepository, "get_active_session_by_terminal_id", get_active
    )
    monkeypatch.setattr(L4DeskRepository, "record_audit_event", audit)
    monkeypatch.setattr(UsageService, "record_session_usage", record_usage)
    return db, get_active, audit, record_usage


@pytest.mark.anyio
async def test_minute_checkpoint_is_atomic_and_repeat_does_not_double_count(
    metering_fakes,
):
    db, get_active, audit, record_usage = metering_fakes
    start = datetime(2026, 9, 27, 0, 0, 0, 900000, tzinfo=UTC)
    session = _session(start)
    get_active.return_value = session

    assert session.last_cursor == int(start.timestamp()) + 1
    assert not checkpoint_due(session, start + timedelta(seconds=59))
    at = start + timedelta(seconds=60, milliseconds=200)
    assert checkpoint_due(session, at)

    assert (
        await checkpoint_remote_session(
            db,
            terminal_id=1000005,
            session_id=42,
            provider_session_id="stream-42",
            at=at,
        )
        == 60
    )
    kwargs = record_usage.await_args.kwargs
    assert (kwargs["end_utc"] - kwargs["start_utc"]).total_seconds() == 60
    assert session.last_cursor == int(at.timestamp())
    assert session.source_events_hash and len(session.source_events_hash) == 64
    assert audit.await_count == 1
    db.commit.assert_awaited_once()

    assert (
        await checkpoint_remote_session(
            db,
            terminal_id=1000005,
            session_id=42,
            provider_session_id="stream-42",
            at=at,
        )
        == 0
    )
    record_usage.assert_awaited_once()


@pytest.mark.anyio
async def test_stop_bills_only_tail_after_last_checkpoint(metering_fakes):
    db, get_active, _, record_usage = metering_fakes
    start = datetime(2026, 9, 27, 0, 0, 0, 900000, tzinfo=UTC)
    session = _session(start, cursor=int(start.timestamp()) + 61)
    get_active.return_value = session

    closed = await close_remote_session(
        db,
        terminal_id=1000005,
        session_type="video",
        reason="stream_stopped",
        at=start + timedelta(seconds=70, milliseconds=200),
        confirmed_end=True,
    )
    assert closed is session
    assert session.state == "closed"
    kwargs = record_usage.await_args.kwargs
    assert (kwargs["end_utc"] - kwargs["start_utc"]).total_seconds() == 10
    assert session.last_cursor == int(kwargs["end_utc"].timestamp())
    db.commit.assert_awaited_once()


@pytest.mark.anyio
async def test_multiple_periods_round_down_once_for_whole_session(metering_fakes):
    db, get_active, _, record_usage = metering_fakes
    start = datetime(2026, 9, 27, 0, 0, 0, 900000, tzinfo=UTC)
    session = _session(start)
    get_active.return_value = session
    for seconds in (60, 120):
        await checkpoint_remote_session(
            db,
            terminal_id=1000005,
            session_id=42,
            provider_session_id="stream-42",
            at=start + timedelta(seconds=seconds, milliseconds=200),
        )
    stop_at = start + timedelta(seconds=150, milliseconds=200)
    await close_remote_session(
        db,
        terminal_id=1000005,
        session_type="video",
        reason="stream_stopped",
        at=stop_at,
        confirmed_end=True,
    )
    intervals = [
        (
            call.kwargs["start_utc"],
            call.kwargs["end_utc"],
        )
        for call in record_usage.await_args_list
    ]
    assert [int((end - begin).total_seconds()) for begin, end in intervals] == [
        60,
        60,
        30,
    ]
    assert intervals[0][0] == datetime.fromtimestamp(
        initial_metering_cursor(start), UTC
    )
    assert intervals[0][1] == intervals[1][0]
    assert intervals[1][1] == intervals[2][0]
    assert (
        sum((end - begin).total_seconds() for begin, end in intervals)
        <= (stop_at - start).total_seconds()
    )


@pytest.mark.anyio
async def test_uncertain_end_waives_tail_but_preserves_completed_periods(
    metering_fakes,
):
    db, get_active, audit, record_usage = metering_fakes
    start = datetime(2026, 9, 27, 0, 0, tzinfo=UTC)
    session = _session(start, cursor=int(start.timestamp()) + 120)
    get_active.return_value = session

    await close_remote_session(
        db,
        terminal_id=1000005,
        session_type="video",
        reason="provider_end_unconfirmed",
        at=start + timedelta(minutes=30),
        confirmed_end=False,
        expected_provider_session_id="stream-42",
    )
    record_usage.assert_not_awaited()
    assert session.state == "failed"
    assert session.last_cursor == int(start.timestamp()) + 120
    assert audit.await_args.kwargs["details"]["tail_seconds"] == 0
    assert session.source_events_hash and len(session.source_events_hash) == 64
    db.commit.assert_awaited_once()


@pytest.mark.anyio
async def test_legacy_session_starts_at_new_confirmed_anchor(metering_fakes):
    db, get_active, audit, record_usage = metering_fakes
    start = datetime(2026, 9, 27, 0, 0, tzinfo=UTC)
    session = _session(start, cursor=0)
    get_active.return_value = session
    at = start + timedelta(minutes=20, microseconds=100)

    assert checkpoint_due(session, at)
    assert (
        await checkpoint_remote_session(
            db,
            terminal_id=1000005,
            session_id=42,
            provider_session_id="stream-42",
            at=at,
        )
        == 0
    )
    record_usage.assert_not_awaited()
    assert session.last_cursor == int(at.timestamp()) + 1
    assert audit.await_args.kwargs["details"]["prior_interval"] == "waived"


@pytest.mark.anyio
async def test_new_provider_epoch_waives_only_unconfirmed_old_tail(monkeypatch):
    start = datetime(2026, 9, 27, 0, 0, tzinfo=UTC)
    session = _session(start, cursor=int(start.timestamp()) + 120)
    status = AsyncMock(
        return_value={
            "lease": {"active": False},
            "agent": {"stream": {"state": "failed", "stream_instance_id": "stream-42"}},
        }
    )
    close = AsyncMock(return_value=session)
    monkeypatch.setattr(video_control.iot_client, "remote_input_status", status)
    monkeypatch.setattr(video_control, "close_remote_session", close)

    result = await video_control._reconcile_stale_video_session(
        SimpleNamespace(),
        terminal_id=1000005,
        sn="test-sn",
        org_id=1000,
        user={"id": 1},
        active_session=session,
    )
    assert result is None
    assert close.await_args.kwargs["confirmed_end"] is False
    assert close.await_args.kwargs["expected_provider_session_id"] == "stream-42"


@pytest.mark.anyio
async def test_current_provider_epoch_is_kept(monkeypatch):
    session = _session(datetime(2026, 9, 27, 0, 0, tzinfo=UTC))
    status = AsyncMock(
        return_value={
            "lease": {"active": True},
            "agent": {
                "stream": {"state": "running", "stream_instance_id": "stream-42"}
            },
        }
    )
    close = AsyncMock()
    monkeypatch.setattr(video_control.iot_client, "remote_input_status", status)
    monkeypatch.setattr(video_control, "close_remote_session", close)

    result = await video_control._reconcile_stale_video_session(
        SimpleNamespace(),
        terminal_id=1000005,
        sn="test-sn",
        org_id=1000,
        user={"id": 1},
        active_session=session,
    )
    assert result is session
    close.assert_not_awaited()


@pytest.mark.anyio
async def test_due_video_keepalive_waits_for_agent_ack_before_checkpoint(monkeypatch):
    now = datetime.now(UTC)
    session = _session(now - timedelta(minutes=2))
    verify = AsyncMock(
        return_value=SimpleNamespace(id=1000005, org_id=1000, sn="test-sn")
    )
    get_active = AsyncMock(return_value=session)
    keepalive = AsyncMock(
        return_value={
            "stream_instance_id": "stream-42",
            "stream_state": "running",
            "terminal_healthy": True,
        }
    )
    renew = AsyncMock(return_value={"status": "ok"})
    checkpoint = AsyncMock(return_value=60)
    monkeypatch.setattr(video_control, "_verify_device_access", verify)
    monkeypatch.setattr(
        L4DeskRepository, "get_active_session_by_terminal_id", get_active
    )
    monkeypatch.setattr(video_control.iot_client, "remote_input_keepalive", keepalive)
    monkeypatch.setattr(video_control.media_orchestrator_client, "renew_session", renew)
    monkeypatch.setattr(video_control, "checkpoint_remote_session", checkpoint)

    await video_control.keepalive_device_control_lease(
        1000005,
        video_control.KeepaliveRequest(lease_id="lease-42"),
        user={"is_superuser": True},
        db=SimpleNamespace(),
    )
    assert keepalive.await_args.kwargs["wait_ack"] is True
    renew.assert_awaited_once_with(sn="test-sn")
    assert checkpoint.await_args.kwargs["provider_session_id"] == "stream-42"


@pytest.mark.anyio
async def test_unconfirmed_keepalive_does_not_charge(monkeypatch):
    now = datetime.now(UTC)
    session = _session(now - timedelta(minutes=2))
    monkeypatch.setattr(
        video_control,
        "_verify_device_access",
        AsyncMock(return_value=SimpleNamespace(id=1000005, org_id=1000, sn="test-sn")),
    )
    monkeypatch.setattr(
        L4DeskRepository,
        "get_active_session_by_terminal_id",
        AsyncMock(return_value=session),
    )
    keepalive = AsyncMock(
        return_value={
            "stream_instance_id": "stream-42",
            "stream_state": "running",
            "terminal_healthy": False,
        }
    )
    checkpoint = AsyncMock()
    monkeypatch.setattr(video_control.iot_client, "remote_input_keepalive", keepalive)
    monkeypatch.setattr(
        video_control.media_orchestrator_client,
        "renew_session",
        AsyncMock(return_value={"status": "ok"}),
    )
    monkeypatch.setattr(video_control, "checkpoint_remote_session", checkpoint)

    await video_control.keepalive_device_control_lease(
        1000005,
        video_control.KeepaliveRequest(lease_id="lease-42"),
        user={"is_superuser": True},
        db=SimpleNamespace(),
    )
    assert keepalive.await_args.kwargs["wait_ack"] is True
    checkpoint.assert_not_awaited()
