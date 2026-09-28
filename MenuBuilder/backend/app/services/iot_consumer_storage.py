from __future__ import annotations

import logging
from abc import ABC, abstractmethod
from dataclasses import dataclass
from datetime import UTC, datetime
from typing import Any

from sqlalchemy import func, select

from app.config import settings
from app.database import async_session
from app.models_iot_consumer import (
    IotConsumerCheckpoint,
    IotEventInbox,
    IotEventQuarantine,
)
from app.services.iot_event_feed_client import RemoteSessionEventItem

logger = logging.getLogger(__name__)


@dataclass
class CheckpointData:
    consumer_id: str
    feed_name: str
    last_cursor: int
    last_event_id: str | None
    last_event_occurred_at: datetime | None
    updated_at: datetime


class IotConsumerStorage(ABC):
    """Abstract interface for consumer checkpoint and inbox persistence."""

    @abstractmethod
    async def get_checkpoint(self, consumer_id: str) -> CheckpointData:
        """Fetch current checkpoint for consumer_id, or create default cursor=0."""
        ...

    @abstractmethod
    async def update_checkpoint(
        self,
        consumer_id: str,
        last_cursor: int,
        last_event_id: str | None,
        last_event_occurred_at: datetime | None,
    ) -> None:
        """Atomically update checkpoint cursor and last event metadata."""
        ...

    @abstractmethod
    async def has_event(self, event_id: str) -> bool:
        """Check if event_id already exists in inbox (idempotent deduplication check)."""
        ...

    @abstractmethod
    async def save_inbox_event(
        self,
        event: RemoteSessionEventItem,
        status: str = "processed",
    ) -> bool:
        """Persist an event item to the idempotent inbox.

        Returns True if saved, False if duplicate event_id already existed.
        """
        ...

    @abstractmethod
    async def save_quarantine(
        self,
        event_id: str,
        cursor: int,
        error_code: str,
        error_detail: str,
        raw_event: dict[str, Any],
    ) -> None:
        """Persist an anomaly event to quarantine."""
        ...

    @abstractmethod
    async def get_counts(self) -> dict[str, int]:
        """Return total counts: {'inbox': int, 'quarantine': int}."""
        ...


class InMemoryIotConsumerStorage(IotConsumerStorage):
    """In-memory storage adapter for tests and isolated execution."""

    def __init__(self) -> None:
        self._checkpoints: dict[str, CheckpointData] = {}
        self._inbox: dict[str, dict[str, Any]] = {}
        self._quarantine: list[dict[str, Any]] = []

    async def get_checkpoint(self, consumer_id: str) -> CheckpointData:
        if consumer_id not in self._checkpoints:
            self._checkpoints[consumer_id] = CheckpointData(
                consumer_id=consumer_id,
                feed_name="remote_session_events",
                last_cursor=0,
                last_event_id=None,
                last_event_occurred_at=None,
                updated_at=datetime.now(UTC),
            )
        return self._checkpoints[consumer_id]

    async def update_checkpoint(
        self,
        consumer_id: str,
        last_cursor: int,
        last_event_id: str | None,
        last_event_occurred_at: datetime | None,
    ) -> None:
        self._checkpoints[consumer_id] = CheckpointData(
            consumer_id=consumer_id,
            feed_name="remote_session_events",
            last_cursor=last_cursor,
            last_event_id=last_event_id,
            last_event_occurred_at=last_event_occurred_at,
            updated_at=datetime.now(UTC),
        )

    async def has_event(self, event_id: str) -> bool:
        return event_id in self._inbox

    async def save_inbox_event(
        self,
        event: RemoteSessionEventItem,
        status: str = "processed",
    ) -> bool:
        if event.event_id in self._inbox:
            return False
        self._inbox[event.event_id] = {
            "event_id": event.event_id,
            "cursor": event.cursor,
            "event_type": event.event_type,
            "event_version": event.event_version,
            "occurred_at": event.occurred_at,
            "tenant_id": event.tenant_id,
            "terminal_id": event.terminal_id,
            "device_id": event.device_id,
            "sn": event.sn,
            "session_id": event.session_id,
            "session_type": event.session_type,
            "lifecycle_state": event.lifecycle_state,
            "reason": event.reason,
            "operation_id": event.operation_id,
            "correlation_id": event.correlation_id,
            "payload": event.payload,
            "received_at": datetime.now(UTC),
            "processed_at": datetime.now(UTC),
            "status": status,
        }
        return True

    async def save_quarantine(
        self,
        event_id: str,
        cursor: int,
        error_code: str,
        error_detail: str,
        raw_event: dict[str, Any],
    ) -> None:
        self._quarantine.append(
            {
                "event_id": event_id,
                "cursor": cursor,
                "error_code": error_code,
                "error_detail": error_detail,
                "raw_event": raw_event,
                "quarantined_at": datetime.now(UTC),
                "retry_count": 0,
                "resolved": False,
            }
        )

    async def get_counts(self) -> dict[str, int]:
        return {
            "inbox": len(self._inbox),
            "quarantine": len(self._quarantine),
        }


class DatabaseIotConsumerStorage(IotConsumerStorage):
    """Database storage adapter using SQLAlchemy 2.0 AsyncSession with automatic fallback."""

    def __init__(self) -> None:
        self._tables_ensured = False
        self._fallback = InMemoryIotConsumerStorage()

    async def _ensure_tables(self) -> None:
        if self._tables_ensured or not settings.database_url:
            return
        # Zero automatic DDL: schema is managed by Alembic migrations and verified at startup.
        self._tables_ensured = True

    async def get_checkpoint(self, consumer_id: str) -> CheckpointData:
        if not settings.database_url:
            return await self._fallback.get_checkpoint(consumer_id)
        await self._ensure_tables()
        try:
            async with async_session() as session:
                result = await session.execute(
                    select(IotConsumerCheckpoint).where(
                        IotConsumerCheckpoint.consumer_id == consumer_id
                    )
                )
                row = result.scalar_one_or_none()
                if row:
                    return CheckpointData(
                        consumer_id=row.consumer_id,
                        feed_name=row.feed_name,
                        last_cursor=row.last_cursor,
                        last_event_id=row.last_event_id,
                        last_event_occurred_at=row.last_event_occurred_at,
                        updated_at=row.updated_at,
                    )
                # Initialize default checkpoint
                new_cp = IotConsumerCheckpoint(
                    consumer_id=consumer_id,
                    feed_name="remote_session_events",
                    last_cursor=0,
                    last_event_id=None,
                    last_event_occurred_at=None,
                )
                session.add(new_cp)
                await session.commit()
                await session.refresh(new_cp)
                return CheckpointData(
                    consumer_id=new_cp.consumer_id,
                    feed_name=new_cp.feed_name,
                    last_cursor=new_cp.last_cursor,
                    last_event_id=new_cp.last_event_id,
                    last_event_occurred_at=new_cp.last_event_occurred_at,
                    updated_at=new_cp.updated_at,
                )
        except Exception as exc:  # noqa: BLE001
            logger.warning("Database error getting checkpoint: %s; using fallback", exc)
            return await self._fallback.get_checkpoint(consumer_id)

    async def update_checkpoint(
        self,
        consumer_id: str,
        last_cursor: int,
        last_event_id: str | None,
        last_event_occurred_at: datetime | None,
    ) -> None:
        if not settings.database_url:
            await self._fallback.update_checkpoint(
                consumer_id, last_cursor, last_event_id, last_event_occurred_at
            )
            return
        await self._ensure_tables()
        try:
            async with async_session() as session:
                result = await session.execute(
                    select(IotConsumerCheckpoint).where(
                        IotConsumerCheckpoint.consumer_id == consumer_id
                    )
                )
                cp = result.scalar_one_or_none()
                now_utc = datetime.now(UTC)
                if cp:
                    cp.last_cursor = last_cursor
                    cp.last_event_id = last_event_id
                    cp.last_event_occurred_at = last_event_occurred_at
                    cp.updated_at = now_utc
                else:
                    cp = IotConsumerCheckpoint(
                        consumer_id=consumer_id,
                        feed_name="remote_session_events",
                        last_cursor=last_cursor,
                        last_event_id=last_event_id,
                        last_event_occurred_at=last_event_occurred_at,
                        updated_at=now_utc,
                    )
                    session.add(cp)
                await session.commit()
        except Exception as exc:  # noqa: BLE001
            logger.warning(
                "Database error updating checkpoint: %s; saving to fallback", exc
            )
            await self._fallback.update_checkpoint(
                consumer_id, last_cursor, last_event_id, last_event_occurred_at
            )

    async def has_event(self, event_id: str) -> bool:
        if not settings.database_url:
            return await self._fallback.has_event(event_id)
        await self._ensure_tables()
        try:
            async with async_session() as session:
                result = await session.execute(
                    select(IotEventInbox.event_id).where(
                        IotEventInbox.event_id == event_id
                    )
                )
                return result.scalar_one_or_none() is not None
        except Exception as exc:  # noqa: BLE001
            logger.warning("Database error checking has_event: %s; using fallback", exc)
            return await self._fallback.has_event(event_id)

    async def save_inbox_event(
        self,
        event: RemoteSessionEventItem,
        status: str = "processed",
    ) -> bool:
        if not settings.database_url:
            return await self._fallback.save_inbox_event(event, status)
        await self._ensure_tables()
        try:
            async with async_session() as session:
                existing = await session.execute(
                    select(IotEventInbox.event_id).where(
                        IotEventInbox.event_id == event.event_id
                    )
                )
                if existing.scalar_one_or_none() is not None:
                    return False

                now_utc = datetime.now(UTC)
                item = IotEventInbox(
                    event_id=event.event_id,
                    cursor=event.cursor,
                    event_type=event.event_type,
                    event_version=event.event_version,
                    occurred_at=event.occurred_at,
                    tenant_id=event.tenant_id,
                    terminal_id=event.terminal_id,
                    device_id=event.device_id,
                    sn=event.sn,
                    session_id=event.session_id,
                    session_type=event.session_type,
                    lifecycle_state=event.lifecycle_state,
                    reason=event.reason,
                    operation_id=event.operation_id,
                    correlation_id=event.correlation_id,
                    payload=event.payload,
                    received_at=now_utc,
                    processed_at=now_utc,
                    status=status,
                )
                session.add(item)
                await session.commit()
                return True
        except Exception as exc:  # noqa: BLE001
            logger.warning(
                "Database error saving inbox event: %s; saving to fallback", exc
            )
            return await self._fallback.save_inbox_event(event, status)

    async def save_quarantine(
        self,
        event_id: str,
        cursor: int,
        error_code: str,
        error_detail: str,
        raw_event: dict[str, Any],
    ) -> None:
        if not settings.database_url:
            await self._fallback.save_quarantine(
                event_id, cursor, error_code, error_detail, raw_event
            )
            return
        await self._ensure_tables()
        try:
            async with async_session() as session:
                q = IotEventQuarantine(
                    event_id=event_id,
                    cursor=cursor,
                    error_code=error_code,
                    error_detail=error_detail,
                    raw_event=raw_event,
                    quarantined_at=datetime.now(UTC),
                )
                session.add(q)
                await session.commit()
        except Exception as exc:  # noqa: BLE001
            logger.warning(
                "Database error saving quarantine event: %s; saving to fallback", exc
            )
            await self._fallback.save_quarantine(
                event_id, cursor, error_code, error_detail, raw_event
            )

    async def get_counts(self) -> dict[str, int]:
        if not settings.database_url:
            return await self._fallback.get_counts()
        await self._ensure_tables()
        try:
            async with async_session() as session:
                inbox_res = await session.execute(
                    select(func.count(IotEventInbox.event_id))
                )
                quar_res = await session.execute(
                    select(func.count(IotEventQuarantine.id))
                )
                return {
                    "inbox": inbox_res.scalar_one() or 0,
                    "quarantine": quar_res.scalar_one() or 0,
                }
        except Exception as exc:  # noqa: BLE001
            logger.warning("Database error getting counts: %s; using fallback", exc)
            return await self._fallback.get_counts()


def get_iot_consumer_storage(in_memory: bool = False) -> IotConsumerStorage:
    if in_memory:
        return InMemoryIotConsumerStorage()
    return DatabaseIotConsumerStorage()
