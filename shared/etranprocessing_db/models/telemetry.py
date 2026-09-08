from __future__ import annotations

from datetime import datetime

from sqlalchemy import (
    DateTime,
    ForeignKey,
    Index,
    Integer,
    String,
    Text,
    func,
)
from sqlalchemy.dialects.postgresql import JSONB
from sqlalchemy.orm import Mapped, mapped_column

from etranprocessing_db.base import Base


class GateGaugeRecord(Base):
    __tablename__ = "gate_gauge_records"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    device_id: Mapped[int] = mapped_column(Integer, nullable=False)
    sn: Mapped[str | None] = mapped_column(String(100), nullable=True)
    gauge_data: Mapped[dict] = mapped_column(JSONB, nullable=False)
    raw_data: Mapped[str | None] = mapped_column(Text, nullable=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    __table_args__ = (
        Index("idx_gauge_device_id", "device_id"),
        Index("idx_gauge_created_at", "created_at"),
    )


class TechGateRecord(Base):
    __tablename__ = "tech_gate_records"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    device_id: Mapped[int] = mapped_column(Integer, nullable=False)
    sn: Mapped[str | None] = mapped_column(String(100), nullable=True)
    function_name: Mapped[str] = mapped_column(String(50), nullable=False)
    request_data: Mapped[dict] = mapped_column(JSONB, nullable=False)
    raw_params: Mapped[str | None] = mapped_column(Text, nullable=True)
    response_status: Mapped[str | None] = mapped_column(String(20), nullable=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    __table_args__ = (
        Index("idx_techgate_device_id", "device_id"),
        Index("idx_techgate_function", "function_name"),
        Index("idx_techgate_created_at", "created_at"),
    )


class TerminalGaugeState(Base):
    __tablename__ = "terminal_gauge_states"

    device_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("terminals.device_id", ondelete="CASCADE"), primary_key=True
    )
    sn: Mapped[str | None] = mapped_column(String(100), nullable=True, index=True)
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False
    )
    last_tick_epoch: Mapped[int] = mapped_column(Integer, nullable=False, default=0)
    slots_bitmask: Mapped[int] = mapped_column(Integer, nullable=False, default=0)
    gauge_data: Mapped[dict] = mapped_column(JSONB, nullable=False, default=dict)
    last_payment_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
    last_inkass_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
    license_expires_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
    internal_enrichment: Mapped[dict] = mapped_column(
        JSONB, nullable=False, default=dict
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
