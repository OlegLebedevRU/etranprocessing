from datetime import datetime

from sqlalchemy import Boolean, DateTime, ForeignKey, Index, Integer, String, Text, func
from sqlalchemy.dialects.postgresql import JSONB
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.database import Base


class Terminal(Base):
    __tablename__ = "terminals"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    device_id: Mapped[int] = mapped_column(Integer, unique=True, nullable=False)
    sn: Mapped[str] = mapped_column(String(100), unique=True, nullable=False)
    cert_serial: Mapped[str | None] = mapped_column(String(100), nullable=True)
    org_id: Mapped[int] = mapped_column(Integer, nullable=False)
    is_active: Mapped[bool] = mapped_column(Boolean, default=True)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())
    updated_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now(), onupdate=func.now())

    licenses: Mapped[list["License"]] = relationship(back_populates="terminal", cascade="all, delete-orphan")

    __table_args__ = (
        Index("idx_terminals_sn", "sn"),
        Index("idx_terminals_cert_serial", "cert_serial"),
        Index("idx_terminals_org_id", "org_id"),
    )


class OrgStatus(Base):
    __tablename__ = "org_statuses"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    org_id: Mapped[int] = mapped_column(Integer, unique=True, nullable=False)
    status: Mapped[str] = mapped_column(String(20), nullable=False, default="active")
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())
    updated_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now(), onupdate=func.now())


class License(Base):
    __tablename__ = "licenses"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    terminal_id: Mapped[int] = mapped_column(Integer, ForeignKey("terminals.id", ondelete="CASCADE"), nullable=False)
    org_id: Mapped[int] = mapped_column(Integer, nullable=False)
    license_type: Mapped[str] = mapped_column(String(50), nullable=False, default="standard")
    expires_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False)
    balance: Mapped[int] = mapped_column(Integer, default=0)
    is_active: Mapped[bool] = mapped_column(Boolean, default=True)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())
    updated_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now(), onupdate=func.now())

    terminal: Mapped["Terminal"] = relationship(back_populates="licenses")

    __table_args__ = (
        Index("idx_licenses_terminal_id", "terminal_id"),
        Index("idx_licenses_org_id", "org_id"),
        Index("idx_licenses_expires_at", "expires_at"),
    )


class GateGaugeRecord(Base):
    __tablename__ = "gate_gauge_records"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    device_id: Mapped[int] = mapped_column(Integer, nullable=False)
    sn: Mapped[str | None] = mapped_column(String(100), nullable=True)
    gauge_data: Mapped[dict] = mapped_column(JSONB, nullable=False)
    raw_data: Mapped[str | None] = mapped_column(Text, nullable=True)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())

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
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())

    __table_args__ = (
        Index("idx_techgate_device_id", "device_id"),
        Index("idx_techgate_function", "function_name"),
        Index("idx_techgate_created_at", "created_at"),
    )
