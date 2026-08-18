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
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )

    licenses: Mapped[list[License]] = relationship(
        back_populates="terminal", cascade="all, delete-orphan"
    )

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
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
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
        SmallInteger, nullable=False, server_default="1"
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


class Org(Base):
    __tablename__ = "orgs"

    org_id: Mapped[int] = mapped_column(Integer, primary_key=True)
    org_name: Mapped[str] = mapped_column(String(150), nullable=False)
    name: Mapped[str] = mapped_column(String(150), nullable=False)
    status: Mapped[int] = mapped_column(Integer, default=1)
    is_active: Mapped[bool] = mapped_column(Boolean, default=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )

    __table_args__ = (Index("idx_orgs_status", "status"),)


class Tsp(Base):
    __tablename__ = "tsp"

    tsp_id: Mapped[int] = mapped_column(Integer, primary_key=True)
    tsp_code: Mapped[int] = mapped_column(Integer, unique=True, nullable=False)
    tsp_name: Mapped[str] = mapped_column(String(255), nullable=False)

    __table_args__ = (Index("idx_tsp_code", "tsp_code"),)


class ServiceMenu(Base):
    """Services table from MenuBuilder - used for prototypenumber lookup."""

    __tablename__ = "services"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    menu_variant_id: Mapped[int] = mapped_column(Integer, nullable=False)
    group_id: Mapped[int] = mapped_column(Integer, nullable=False)
    tsp_code: Mapped[int] = mapped_column(Integer, nullable=False)
    name: Mapped[str] = mapped_column(String(255), nullable=False)
    printname: Mapped[str | None] = mapped_column(String(255), nullable=True)
    price: Mapped[int] = mapped_column(Integer, default=0)
    protypenumber: Mapped[int] = mapped_column(Integer, default=0)

    __table_args__ = (
        UniqueConstraint("menu_variant_id", "tsp_code", name="uq_service_variant_tsp"),
    )


class TspParameterCode(Base):
    __tablename__ = "tsp_parameter_codes"

    param_id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    prototypenumber: Mapped[int] = mapped_column(Integer, nullable=False)
    parameter_code: Mapped[int] = mapped_column(Integer, nullable=False)
    code_description: Mapped[str | None] = mapped_column(String(255), nullable=True)

    __table_args__ = (
        UniqueConstraint(
            "prototypenumber", "parameter_code", name="uq_prototype_param_code"
        ),
        Index("idx_tsp_param_prototype", "prototypenumber"),
    )


class Payment(Base):
    __tablename__ = "payments"

    paym_id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    paym_datetime: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    paym_amount: Mapped[int] = mapped_column(BigInteger, nullable=False)
    paym_ext_id: Mapped[str] = mapped_column(String(20), nullable=False)
    paym_tsp_code: Mapped[int] = mapped_column(Integer, nullable=False)
    terminal_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("terminals.id"), nullable=False
    )
    org_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("orgs.org_id"), nullable=False
    )
    paym_state: Mapped[int] = mapped_column(Integer, default=0)
    pay_type_id: Mapped[int] = mapped_column(Integer, default=0)

    terminal: Mapped[Terminal] = relationship()
    org: Mapped[Org] = relationship()
    params: Mapped[list[PaymentParam]] = relationship(
        back_populates="payment", cascade="all, delete-orphan"
    )

    __table_args__ = (
        Index("idx_payments_terminal_id", "terminal_id"),
        Index("idx_payments_org_id", "org_id"),
        Index("idx_payments_ext_id", "paym_ext_id"),
        Index("idx_payments_tsp_code", "paym_tsp_code"),
        Index("idx_payments_datetime", "paym_datetime"),
    )


class PaymentParam(Base):
    __tablename__ = "payment_params"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    paym_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("payments.paym_id", ondelete="CASCADE"), nullable=False
    )
    param_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("tsp_parameter_codes.param_id"), nullable=False
    )
    param_value: Mapped[str | None] = mapped_column(String(2000), nullable=True)

    payment: Mapped[Payment] = relationship(back_populates="params")
    param: Mapped[TspParameterCode] = relationship()

    __table_args__ = (
        Index("idx_payment_params_paym_id", "paym_id"),
        Index("idx_payment_params_param_id", "param_id"),
    )


class BalanceTerminalTsp(Base):
    __tablename__ = "balance_terminal_tsp"

    rec_id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    int_day: Mapped[int] = mapped_column(Integer, nullable=False)
    org_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("orgs.org_id"), nullable=False
    )
    terminal_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("terminals.id"), nullable=False
    )
    tsp_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("tsp.tsp_id"), nullable=False
    )
    amount: Mapped[int] = mapped_column(BigInteger, default=0)
    count: Mapped[int] = mapped_column(Integer, default=0)
    update_datetime: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )

    __table_args__ = (
        UniqueConstraint(
            "int_day", "terminal_id", "tsp_id", name="uq_balance_day_terminal_tsp"
        ),
        Index("idx_balance_int_day", "int_day"),
        Index("idx_balance_terminal_id", "terminal_id"),
        Index("idx_balance_tsp_id", "tsp_id"),
        Index("idx_balance_org_id", "org_id"),
    )


class ApiToken(Base):
    __tablename__ = "api_tokens"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    jti: Mapped[str] = mapped_column(String(36), unique=True, nullable=False)
    user_id: Mapped[str] = mapped_column(String(100), nullable=False)
    name: Mapped[str | None] = mapped_column(String(200), nullable=True)
    expires_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    last_used_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
    revoked_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )

    __table_args__ = (
        Index("idx_api_tokens_user", "user_id"),
        Index("idx_api_tokens_jti", "jti"),
    )


class OrgBillingSettings(Base):
    __tablename__ = "org_billing_settings"

    org_id: Mapped[int] = mapped_column(Integer, primary_key=True)
    monthly_price_minor: Mapped[int] = mapped_column(BigInteger, nullable=False)
    currency: Mapped[str] = mapped_column(
        String(3), nullable=False, server_default="RUB"
    )
    # Organizational certificate tariff policy (source of truth — no per-terminal override).
    cert_billing_mode: Mapped[str] = mapped_column(
        String(20), nullable=False, default="none"
    )
    cert_price_minor: Mapped[int | None] = mapped_column(BigInteger, nullable=True)
    tenant_pin_creation_enabled: Mapped[bool] = mapped_column(Boolean, default=False)
    cert_charge_primary_issue: Mapped[bool] = mapped_column(Boolean, default=True)
    cert_charge_reissue: Mapped[bool] = mapped_column(Boolean, default=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )

    __table_args__ = (
        CheckConstraint(
            "monthly_price_minor >= 0", name="ck_org_billing_price_non_negative"
        ),
        CheckConstraint(
            "cert_price_minor IS NULL OR cert_price_minor >= 0",
            name="ck_org_cert_price_non_negative",
        ),
        CheckConstraint(
            "cert_billing_mode IN ('none', 'per_operation')",
            name="ck_org_cert_mode",
        ),
    )


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
    # Prevents a later org-tariff change from retroactively altering an already-created order.
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

    terminal: Mapped[Terminal] = relationship()
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
    pin: Mapped[CertificatePin | None] = relationship()

    __table_args__ = (Index("idx_terminal_cert_history_terminal", "terminal_id"),)
