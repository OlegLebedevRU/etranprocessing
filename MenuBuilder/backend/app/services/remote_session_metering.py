"""Durable, consumer-favouring metering checkpoints for remote sessions."""

import hashlib
import math
from datetime import UTC, datetime

from sqlalchemy.ext.asyncio import AsyncSession

from app.models_l4desk import L4DeskRemoteSession
from app.repositories.l4desk_repository import L4DeskRepository
from app.services.financial_core.metering import FinMeteringService

CHECKPOINT_SECONDS = 60


def initial_metering_cursor(active_at: datetime) -> int:
    """Start at the next whole UTC second so fractional time is never billed."""
    return math.ceil(active_at.timestamp())


def checkpoint_due(session: L4DeskRemoteSession | None, at: datetime) -> bool:
    if session is None or session.state != "active" or session.active_at is None:
        return False
    if not session.last_cursor or session.last_cursor < initial_metering_cursor(
        session.active_at
    ):
        return True  # Existing sessions need a fresh, unbilled starting point.
    return int(at.timestamp()) - session.last_cursor >= CHECKPOINT_SECONDS


async def _record_session_event(
    db: AsyncSession,
    session: L4DeskRemoteSession,
    *,
    event_type: str,
    event_id: str,
    at: datetime,
    details: dict[str, object],
) -> None:
    event = await L4DeskRepository(db).record_audit_event(
        actor="remote_session_metering",
        event_type=event_type,
        subject_type="remote_session",
        subject_id=str(session.id),
        correlation_id=session.correlation_id,
        operation_id=event_id,
        tenant_id=session.tenant_id,
        details=details,
        occurred_at=at,
    )
    session.last_event_id = event_id
    session.source_events_hash = hashlib.sha256(
        f"{session.source_events_hash or ''}:{event.id}:{event_id}".encode()
    ).hexdigest()


async def _record_period(
    db: AsyncSession,
    session: L4DeskRemoteSession,
    *,
    end_epoch: int,
    at: datetime,
) -> int:
    start_epoch = session.last_cursor
    if end_epoch <= start_epoch:
        return 0
    event_id = f"remote-session-{session.id}-through-{end_epoch}"
    await FinMeteringService.record_session_usage(
        db,
        tenant_id=session.tenant_id,
        terminal_id=session.terminal_id,
        session_type=session.session_type,
        start_utc=datetime.fromtimestamp(start_epoch, UTC),
        end_utc=datetime.fromtimestamp(end_epoch, UTC),
        event_id=event_id,
        actor="remote_session_metering",
        correlation_id=session.correlation_id,
    )
    session.last_cursor = end_epoch
    await _record_session_event(
        db,
        session,
        event_type="remote_session_usage_period",
        event_id=event_id,
        at=at,
        details={
            "start_epoch": start_epoch,
            "end_epoch": end_epoch,
            "seconds": end_epoch - start_epoch,
            "provider_session_id": session.provider_session_id,
        },
    )
    return end_epoch - start_epoch


async def checkpoint_remote_session(
    db: AsyncSession,
    *,
    terminal_id: int,
    session_id: int,
    provider_session_id: str,
    at: datetime,
) -> int:
    """Persist one period and its cursor atomically after provider confirmation."""
    session = await L4DeskRepository(db).get_active_session_by_terminal_id(
        terminal_id, lock=True
    )
    if (
        session is None
        or session.id != session_id
        or session.provider_session_id != provider_session_id
        or session.state != "active"
        or session.active_at is None
    ):
        return 0
    if not session.last_cursor or session.last_cursor < initial_metering_cursor(
        session.active_at
    ):
        # Sessions opened by the old code have no durable watermark. Their
        # earlier interval cannot be proved and is waived for the customer.
        session.last_cursor = max(
            initial_metering_cursor(session.active_at),
            initial_metering_cursor(at),
        )
        await _record_session_event(
            db,
            session,
            event_type="remote_session_metering_anchor",
            event_id=f"remote-session-{session.id}-anchor-{session.last_cursor}",
            at=at,
            details={"cursor_epoch": session.last_cursor, "prior_interval": "waived"},
        )
        await db.commit()
        return 0
    end_epoch = int(at.timestamp())
    if end_epoch - session.last_cursor < CHECKPOINT_SECONDS:
        return 0
    seconds = await _record_period(db, session, end_epoch=end_epoch, at=at)
    await db.commit()
    return seconds


async def close_remote_session(
    db: AsyncSession,
    *,
    terminal_id: int,
    session_type: str,
    reason: str,
    at: datetime,
    confirmed_end: bool,
    expected_provider_session_id: str | None = None,
) -> L4DeskRemoteSession | None:
    """Bill the confirmed tail, or waive an end time that cannot be proved."""
    session = await L4DeskRepository(db).get_active_session_by_terminal_id(
        terminal_id, lock=True
    )
    if (
        session is None
        or session.session_type != session_type
        or (
            expected_provider_session_id is not None
            and session.provider_session_id != expected_provider_session_id
        )
    ):
        return None

    seconds = 0
    if (
        confirmed_end
        and session.active_at is not None
        and int(session.last_cursor or 0) >= initial_metering_cursor(session.active_at)
    ):
        seconds = await _record_period(
            db, session, end_epoch=int(at.timestamp()), at=at
        )
    session.state = "closed" if confirmed_end else "failed"
    session.closed_at = at
    session.reason = reason
    await _record_session_event(
        db,
        session,
        event_type="remote_session_closed"
        if confirmed_end
        else "remote_session_waived",
        event_id=f"remote-session-{session.id}-end-{int(at.timestamp())}",
        at=at,
        details={
            "reason": reason,
            "confirmed_end": confirmed_end,
            "tail_seconds": seconds,
            "cursor_epoch": session.last_cursor,
            "provider_session_id": session.provider_session_id,
        },
    )
    await db.commit()
    return session
