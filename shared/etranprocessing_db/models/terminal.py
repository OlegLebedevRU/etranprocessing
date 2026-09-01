from __future__ import annotations

from datetime import datetime
from typing import TYPE_CHECKING

from sqlalchemy import (
    BigInteger,
    Boolean,
    CheckConstraint,
    DateTime,
    ForeignKey,
    Index,
    Integer,
    SmallInteger,
    String,
    func,
)
from sqlalchemy.orm import Mapped, mapped_column, relationship

from etranprocessing_db.base import Base

if TYPE_CHECKING:
    from etranprocessing_db.models.billing import CertificatePin


class TerminalType(Base):
    __tablename__ = "terminal_types"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    name: Mapped[str] = mapped_column(String(100), nullable=False)
    description: Mapped[str | None] = mapped_column(String(255), nullable=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )


class Terminal(Base):
    __tablename__ = "terminals"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    device_id: Mapped[int] = mapped_column(Integer, unique=True, nullable=False)
    sn: Mapped[str] = mapped_column(String(100), unique=True, nullable=False)
    cert_serial: Mapped[str | None] = mapped_column(String(100), nullable=True)
    cert_not_valid_after: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
    org_id: Mapped[int] = mapped_column(Integer, nullable=False)
    is_active: Mapped[bool] = mapped_column(Boolean, default=True)
    timezone: Mapped[str | None] = mapped_column(
        String(64), nullable=True, default=None, server_default=None
    )
    address: Mapped[str | None] = mapped_column(String(500), nullable=True)
    note: Mapped[str | None] = mapped_column(String(500), nullable=True)
    terminal_type_id: Mapped[int] = mapped_column(
        Integer,
        ForeignKey("terminal_types.id"),
        default=0,
        server_default="0",
        nullable=False,
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )
    show_in_monitoring: Mapped[bool] = mapped_column(
        Boolean, default=True, server_default="true", nullable=False
    )
    iot_provisioned: Mapped[bool] = mapped_column(
        Boolean, default=False, server_default="false", nullable=False
    )
    iot_provisioned_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
    iot_last_sync_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
    iot_is_online: Mapped[bool] = mapped_column(
        Boolean, default=False, server_default="false", nullable=False
    )
    iot_last_connected_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )

    terminal_type: Mapped[TerminalType | None] = relationship(
        "TerminalType", lazy="joined"
    )
    licenses: Mapped[list[License]] = relationship(
        back_populates="terminal", cascade="all, delete-orphan"
    )

    __table_args__ = (
        Index("idx_terminals_sn", "sn"),
        Index("idx_terminals_cert_serial", "cert_serial"),
        Index("idx_terminals_org_id", "org_id"),
        Index("idx_terminals_terminal_type_id", "terminal_type_id"),
    )


class License(Base):
    __tablename__ = "licenses"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    terminal_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("terminals.id", ondelete="CASCADE"), nullable=False
    )
    org_id: Mapped[int] = mapped_column(Integer, nullable=False)
    license_type: Mapped[str] = mapped_column(
        String(50), nullable=False, default="standard"
    )
    expires_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False
    )
    balance: Mapped[int] = mapped_column(Integer, default=0)
    billing_period_months: Mapped[int] = mapped_column(
        SmallInteger, nullable=False, server_default="1", default=1
    )
    monthly_price_override_minor: Mapped[int | None] = mapped_column(
        BigInteger, nullable=True
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )

    terminal: Mapped[Terminal] = relationship(back_populates="licenses")

    __table_args__ = (
        Index("idx_licenses_terminal_id", "terminal_id"),
        Index("idx_licenses_org_id", "org_id"),
        Index("idx_licenses_expires_at", "expires_at"),
        CheckConstraint(
            "monthly_price_override_minor >= 0", name="ck_license_price_non_negative"
        ),
        CheckConstraint(
            "billing_period_months > 0", name="ck_billing_period_months_positive"
        ),
    )


class TerminalCertHistory(Base):
    """Operational history of certificates issued for a terminal (append-only)."""

    __tablename__ = "terminal_cert_history"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    terminal_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("terminals.id"), nullable=False
    )
    cert_serial: Mapped[str] = mapped_column(String(100), nullable=False)
    not_valid_after: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
    pin_id: Mapped[int | None] = mapped_column(
        Integer, ForeignKey("certificate_pins.id"), nullable=True
    )
    source: Mapped[str] = mapped_column(String(20), nullable=False, default="setup")
    issued_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    terminal: Mapped[Terminal] = relationship()
    pin: Mapped[CertificatePin | None] = relationship("CertificatePin")

    __table_args__ = (Index("idx_terminal_cert_history_terminal", "terminal_id"),)


class TerminalCertDiscovery(Base):
    """Accumulator for unique terminal certificate data observed at ingress / mirror.

    Tracks unique (sn, cert_serial) combinations with request counts, validation status,
    and timestamps, preventing log spam while preserving complete history for diagnostics
    and troubleshooting validation discrepancies.
    """

    __tablename__ = "terminal_cert_discovery"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    sn: Mapped[str | None] = mapped_column(String(100), nullable=True, index=True)
    cert_serial: Mapped[str | None] = mapped_column(
        String(100), nullable=True, index=True
    )
    cert_dn: Mapped[str | None] = mapped_column(String(500), nullable=True)
    ou: Mapped[str | None] = mapped_column(String(50), nullable=True)
    o: Mapped[str | None] = mapped_column(String(50), nullable=True)
    is_valid: Mapped[bool] = mapped_column(Boolean, nullable=False, default=False)
    validation_status: Mapped[str] = mapped_column(
        String(50), nullable=False, default="unknown"
    )
    terminal_id: Mapped[int | None] = mapped_column(
        Integer,
        ForeignKey("terminals.id", ondelete="SET NULL"),
        nullable=True,
        index=True,
    )
    db_cert_serial: Mapped[str | None] = mapped_column(String(100), nullable=True)
    request_count: Mapped[int] = mapped_column(Integer, nullable=False, default=1)
    last_endpoint: Mapped[str | None] = mapped_column(String(100), nullable=True)
    client_ip: Mapped[str | None] = mapped_column(String(50), nullable=True)
    first_seen_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), nullable=False
    )
    last_seen_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True),
        server_default=func.now(),
        onupdate=func.now(),
        nullable=False,
    )

    terminal: Mapped[Terminal | None] = relationship()

    __table_args__ = (
        Index("idx_terminal_cert_discovery_sn_serial", "sn", "cert_serial"),
        Index("idx_terminal_cert_discovery_status", "validation_status"),
        Index("idx_terminal_cert_discovery_last_seen", "last_seen_at"),
    )
