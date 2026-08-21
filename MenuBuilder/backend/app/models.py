import uuid
from datetime import datetime

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
    Text,
    UniqueConstraint,
    func,
)
from sqlalchemy.dialects.postgresql import JSONB
from sqlalchemy.dialects.postgresql import UUID as PG_UUID
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.database import Base


class Org(Base):
    __tablename__ = "orgs"

    org_id: Mapped[int] = mapped_column(Integer, primary_key=True)
    org_name: Mapped[str] = mapped_column(String(150), nullable=False)
    name: Mapped[str] = mapped_column(String(150), nullable=False)
    status: Mapped[int] = mapped_column(Integer, default=1)
    is_active: Mapped[bool] = mapped_column(Boolean, default=True)
    email: Mapped[str | None] = mapped_column(String(255), nullable=True)
    phone: Mapped[str | None] = mapped_column(String(50), nullable=True)
    notify_by_email: Mapped[bool] = mapped_column(
        Boolean, default=True, server_default="true"
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )


class OrgBillingSettings(Base):
    __tablename__ = "org_billing_settings"

    org_id: Mapped[int] = mapped_column(Integer, primary_key=True)
    monthly_price_minor: Mapped[int] = mapped_column(
        BigInteger, nullable=False, default=100_000, server_default="100000"
    )
    currency: Mapped[str] = mapped_column(
        String(3), nullable=False, default="RUB", server_default="RUB"
    )
    billing_mode: Mapped[str] = mapped_column(
        String(30), nullable=False, default="standard", server_default="standard"
    )
    min_billing_periods: Mapped[int] = mapped_column(
        Integer, nullable=False, default=1, server_default="1"
    )
    allowed_billing_periods: Mapped[str | None] = mapped_column(
        String(50), nullable=True
    )
    default_selection_mode: Mapped[str] = mapped_column(
        String(30), nullable=False, default="all_due", server_default="all_due"
    )
    cert_billing_mode: Mapped[str] = mapped_column(
        String(20), nullable=False, default="none", server_default="none"
    )
    cert_price_minor: Mapped[int | None] = mapped_column(BigInteger, nullable=True)
    tenant_pin_creation_enabled: Mapped[bool] = mapped_column(
        Boolean, nullable=False, default=False, server_default="false"
    )
    cert_charge_primary_issue: Mapped[bool] = mapped_column(
        Boolean, nullable=False, default=True, server_default="true"
    )
    cert_charge_reissue: Mapped[bool] = mapped_column(
        Boolean, nullable=False, default=True, server_default="true"
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )


class OrgStatus(Base):
    __tablename__ = "org_statuses"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    org_id: Mapped[int] = mapped_column(Integer, unique=True, nullable=False)
    status: Mapped[str] = mapped_column(String(20), nullable=False, default="active")
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )


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

    terminal_type: Mapped[TerminalType | None] = relationship(
        "TerminalType", lazy="joined"
    )
    licenses: Mapped[list[License]] = relationship(
        back_populates="terminal", cascade="all, delete-orphan"
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
    is_active: Mapped[bool] = mapped_column(Boolean, default=True)
    billing_period_months: Mapped[int] = mapped_column(
        SmallInteger, nullable=False, server_default="1", default=1
    )
    monthly_price_override_minor: Mapped[int | None] = mapped_column(
        BigInteger, nullable=True
    )
    renewal_enabled: Mapped[bool] = mapped_column(
        Boolean, nullable=False, server_default="true", default=True
    )
    deactivation_requested_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )

    terminal: Mapped[Terminal] = relationship(back_populates="licenses")


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
    cert_policy_snapshot: Mapped[dict | None] = mapped_column(JSONB, nullable=True)

    order: Mapped[BillingOrder] = relationship(back_populates="items")
    terminal: Mapped[Terminal] = relationship()

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
        String(20), nullable=False, default="global_admin"
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

    terminal: Mapped[Terminal] = relationship()
    order_item: Mapped[BillingOrderItem | None] = relationship()


class MenuVariant(Base):
    __tablename__ = "menu_variants"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    org_id: Mapped[int] = mapped_column(Integer, nullable=False, index=True)
    name: Mapped[str] = mapped_column(String(255), nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )

    __table_args__ = (
        UniqueConstraint("org_id", "name", name="uq_menu_variants_org_name"),
    )

    groups: Mapped[list[Group]] = relationship(
        back_populates="menu_variant", cascade="all, delete-orphan"
    )
    bindings: Mapped[list[TerminalMenuBinding]] = relationship(
        back_populates="menu_variant", cascade="all, delete-orphan"
    )


class Group(Base):
    __tablename__ = "groups"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    menu_variant_id: Mapped[int] = mapped_column(
        Integer,
        ForeignKey("menu_variants.id", ondelete="CASCADE"),
        nullable=False,
        index=True,
    )
    org_id: Mapped[int] = mapped_column(Integer, nullable=False, index=True)
    number: Mapped[int] = mapped_column(Integer, nullable=False)
    name: Mapped[str] = mapped_column(String(255), nullable=False)
    parent_id: Mapped[int | None] = mapped_column(
        Integer, ForeignKey("groups.id", ondelete="SET NULL"), nullable=True, index=True
    )

    menu_variant: Mapped[MenuVariant] = relationship(back_populates="groups")
    parent: Mapped[Group | None] = relationship(
        "Group", remote_side="Group.id", back_populates="children"
    )
    children: Mapped[list[Group]] = relationship("Group", back_populates="parent")
    services: Mapped[list[Service]] = relationship(
        "Service", back_populates="group", cascade="all, delete-orphan"
    )


class Service(Base):
    __tablename__ = "services"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    menu_variant_id: Mapped[int] = mapped_column(
        Integer,
        ForeignKey("menu_variants.id", ondelete="CASCADE"),
        nullable=False,
        index=True,
    )
    group_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("groups.id", ondelete="CASCADE"), nullable=False, index=True
    )
    tsp_code: Mapped[int] = mapped_column(Integer, nullable=False)
    name: Mapped[str] = mapped_column(String(255), nullable=False)
    printname: Mapped[str | None] = mapped_column(String(255), nullable=True)
    price: Mapped[int] = mapped_column(Integer, default=0)
    protypenumber: Mapped[int] = mapped_column(Integer, default=0)

    group: Mapped[Group] = relationship("Group", back_populates="services")

    __table_args__ = (
        UniqueConstraint("menu_variant_id", "tsp_code", name="uq_service_variant_tsp"),
    )


class TerminalMenuBinding(Base):
    __tablename__ = "terminal_menu_bindings"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    device_id: Mapped[int] = mapped_column(
        Integer, unique=True, nullable=False, index=True
    )
    menu_variant_id: Mapped[int] = mapped_column(
        Integer,
        ForeignKey("menu_variants.id", ondelete="CASCADE"),
        nullable=False,
        index=True,
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    menu_variant: Mapped[MenuVariant] = relationship(back_populates="bindings")
