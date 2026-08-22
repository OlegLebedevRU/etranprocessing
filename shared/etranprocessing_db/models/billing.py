from __future__ import annotations

import uuid
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
    String,
    Text,
    func,
)
from sqlalchemy.dialects.postgresql import JSONB
from sqlalchemy.dialects.postgresql import UUID as PG_UUID
from sqlalchemy.orm import Mapped, mapped_column, relationship

from etranprocessing_db.base import Base

if TYPE_CHECKING:
    from etranprocessing_db.models.terminal import Terminal


class BillingOrder(Base):
    __tablename__ = "billing_orders"

    id: Mapped[uuid.UUID] = mapped_column(
        PG_UUID(as_uuid=True), primary_key=True, default=uuid.uuid4
    )
    org_id: Mapped[int] = mapped_column(Integer, nullable=False)
    status: Mapped[str] = mapped_column(
        String(20), nullable=False, server_default="pending"
    )
    currency: Mapped[str] = mapped_column(
        String(3), nullable=False, server_default="RUB"
    )
    amount_minor: Mapped[int] = mapped_column(BigInteger, nullable=False)
    provider: Mapped[str | None] = mapped_column(String(50), nullable=True)
    provider_order_id: Mapped[str | None] = mapped_column(String(200), nullable=True)
    payment_url: Mapped[str | None] = mapped_column(Text, nullable=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    paid_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
    expires_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )

    items: Mapped[list[BillingOrderItem]] = relationship(
        back_populates="order", cascade="all, delete-orphan"
    )

    __table_args__ = (
        Index("idx_billing_orders_org_id", "org_id"),
        Index("idx_billing_orders_status", "status"),
    )


class BillingOrderItem(Base):
    __tablename__ = "billing_order_items"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    order_id: Mapped[uuid.UUID] = mapped_column(
        PG_UUID(as_uuid=True),
        ForeignKey("billing_orders.id"),
        nullable=False,
    )
    terminal_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("terminals.id"), nullable=False
    )
    operation: Mapped[str] = mapped_column(String(20), nullable=False)
    periods_due: Mapped[int] = mapped_column(Integer, nullable=False)
    advance_periods: Mapped[int] = mapped_column(Integer, nullable=False)
    billing_period_months: Mapped[int] = mapped_column(Integer, nullable=False)
    monthly_price_minor: Mapped[int] = mapped_column(BigInteger, nullable=False)
    amount_minor: Mapped[int] = mapped_column(BigInteger, nullable=False)
    old_expires_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False
    )
    new_expires_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False
    )
    # Snapshot of the org cert tariff policy at request time (operation="cert_pin" only).
    # Prevents a later org-tariff change from retroacting on an already-created order.
    cert_policy_snapshot: Mapped[dict | None] = mapped_column(JSONB, nullable=True)

    order: Mapped[BillingOrder] = relationship(back_populates="items")
    terminal: Mapped[Terminal] = relationship("Terminal")

    __table_args__ = (
        Index("idx_billing_order_items_order_id", "order_id"),
        Index("idx_billing_order_items_terminal_id", "terminal_id"),
        CheckConstraint(
            "operation IN ('renewal', 'reactivation', 'cert_pin')",
            name="ck_billing_order_items_operation",
        ),
    )


class CertificatePin(Base):
    __tablename__ = "certificate_pins"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    pin: Mapped[str] = mapped_column(String(10), unique=True, nullable=False)
    terminal_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("terminals.id"), nullable=False
    )
    org_id: Mapped[int] = mapped_column(Integer, nullable=False)
    order_item_id: Mapped[int | None] = mapped_column(
        Integer, ForeignKey("billing_order_items.id"), nullable=True
    )
    created_by: Mapped[str | None] = mapped_column(String(100), nullable=True)
    creation_source: Mapped[str] = mapped_column(
        String(20), nullable=False, default="system"
    )
    payment_required: Mapped[bool] = mapped_column(Boolean, default=False)
    status: Mapped[str] = mapped_column(String(20), nullable=False, default="pending")
    expires_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    used_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )

    terminal: Mapped[Terminal] = relationship("Terminal")
    order_item: Mapped[BillingOrderItem | None] = relationship()

    __table_args__ = (
        Index("idx_cert_pins_terminal", "terminal_id"),
        Index("idx_cert_pins_status", "status"),
        Index("idx_cert_pins_org", "org_id"),
        Index("idx_cert_pins_order_item", "order_item_id"),
        CheckConstraint(
            "status IN ('pending', 'used', 'expired', 'cancelled')",
            name="ck_certificate_pins_status",
        ),
        CheckConstraint(
            "creation_source IN ('tenant', 'global_admin', 'system')",
            name="ck_certificate_pins_creation_source",
        ),
    )
