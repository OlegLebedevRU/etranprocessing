from __future__ import annotations

from datetime import UTC, datetime
from typing import Any

from sqlalchemy import JSON, BigInteger, Boolean, DateTime, Integer, String, Text
from sqlalchemy.orm import Mapped, mapped_column

from etranprocessing_db.base import Base


class IotConsumerCheckpoint(Base):
    __tablename__ = "iot_consumer_checkpoints"

    consumer_id: Mapped[str] = mapped_column(String(64), primary_key=True)
    feed_name: Mapped[str] = mapped_column(
        String(64), nullable=False, default="remote_session_events"
    )
    last_cursor: Mapped[int] = mapped_column(BigInteger, nullable=False, default=0)
    last_event_id: Mapped[str | None] = mapped_column(String(128), nullable=True)
    last_event_occurred_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True),
        nullable=False,
        default=lambda: datetime.now(UTC),
        onupdate=lambda: datetime.now(UTC),
    )


class IotEventInbox(Base):
    __tablename__ = "iot_event_inbox"

    event_id: Mapped[str] = mapped_column(String(128), primary_key=True)
    cursor: Mapped[int] = mapped_column(BigInteger, nullable=False, index=True)
    event_type: Mapped[str] = mapped_column(String(64), nullable=False, index=True)
    event_version: Mapped[str] = mapped_column(
        String(32), nullable=False, default="1.0.0"
    )
    occurred_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False
    )
    tenant_id: Mapped[int | None] = mapped_column(Integer, nullable=True, index=True)
    terminal_id: Mapped[str | None] = mapped_column(String(128), nullable=True)
    device_id: Mapped[int | None] = mapped_column(Integer, nullable=True)
    sn: Mapped[str] = mapped_column(String(128), nullable=False, index=True)
    session_id: Mapped[str | None] = mapped_column(
        String(128), nullable=True, index=True
    )
    session_type: Mapped[str | None] = mapped_column(String(32), nullable=True)
    lifecycle_state: Mapped[str | None] = mapped_column(String(32), nullable=True)
    reason: Mapped[str | None] = mapped_column(String(128), nullable=True)
    operation_id: Mapped[str | None] = mapped_column(
        String(128), nullable=True, index=True
    )
    correlation_id: Mapped[str | None] = mapped_column(String(128), nullable=True)
    payload: Mapped[dict[str, Any] | None] = mapped_column(JSON, nullable=True)
    received_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False, default=lambda: datetime.now(UTC)
    )
    processed_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
    status: Mapped[str] = mapped_column(String(32), nullable=False, default="processed")


class IotEventQuarantine(Base):
    __tablename__ = "iot_event_quarantine"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    event_id: Mapped[str] = mapped_column(String(128), nullable=False, index=True)
    cursor: Mapped[int] = mapped_column(BigInteger, nullable=False)
    error_code: Mapped[str] = mapped_column(String(64), nullable=False)
    error_detail: Mapped[str] = mapped_column(Text, nullable=False)
    raw_event: Mapped[dict[str, Any]] = mapped_column(JSON, nullable=False)
    quarantined_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False, default=lambda: datetime.now(UTC)
    )
    retry_count: Mapped[int] = mapped_column(Integer, nullable=False, default=0)
    resolved: Mapped[bool] = mapped_column(Boolean, nullable=False, default=False)
