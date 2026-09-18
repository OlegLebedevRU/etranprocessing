from __future__ import annotations

from datetime import date, datetime
from typing import Any

from sqlalchemy import (
    JSON,
    BigInteger,
    Boolean,
    CheckConstraint,
    Date,
    DateTime,
    ForeignKey,
    ForeignKeyConstraint,
    Index,
    Integer,
    PrimaryKeyConstraint,
    String,
    Text,
    UniqueConstraint,
    func,
    text,
)
from sqlalchemy.orm import Mapped, mapped_column

from etranprocessing_db.base import Base


class FinTariffVersion(Base):
    __tablename__ = "fin_tariff_versions"

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True)
    version: Mapped[str] = mapped_column(String(64), nullable=False)
    effective_from: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False
    )
    terminal_month_kopecks: Mapped[int] = mapped_column(BigInteger, nullable=False)
    hourly_rate_kopecks: Mapped[int] = mapped_column(BigInteger, nullable=False)
    free_daily_seconds: Mapped[int] = mapped_column(Integer, nullable=False)
    actor: Mapped[str] = mapped_column(String(128), nullable=False)
    correlation_id: Mapped[str] = mapped_column(String(128), nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    __table_args__ = (
        PrimaryKeyConstraint("id", name="fin_tariff_pk"),
        UniqueConstraint("version", name="fin_tariff_version_uq"),
        UniqueConstraint("effective_from", name="fin_tariff_effective_uq"),
        CheckConstraint(
            "terminal_month_kopecks >= 0 AND hourly_rate_kopecks >= 0 AND free_daily_seconds >= 0",
            name="fin_tariff_values_ck",
        ),
    )


class FinAccount(Base):
    __tablename__ = "fin_accounts"

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True)
    tenant_id: Mapped[int | None] = mapped_column(
        ForeignKey("orgs.org_id", ondelete="RESTRICT", name="fin_account_tenant_fk")
    )
    kind: Mapped[str] = mapped_column(String(32), nullable=False)
    currency: Mapped[str] = mapped_column(String(3), server_default="RUB")
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    __table_args__ = (
        PrimaryKeyConstraint("id", name="fin_account_pk"),
        CheckConstraint("currency = 'RUB'", name="fin_account_currency_ck"),
        CheckConstraint(
            "(kind = 'tenant_settlement' AND tenant_id IS NOT NULL) OR (kind IN ('payment_clearing', 'usage_revenue') AND tenant_id IS NULL)",
            name="fin_account_scope_ck",
        ),
        UniqueConstraint("tenant_id", "kind", name="fin_account_tenant_kind_uq"),
        Index(
            "fin_account_global_kind_uq",
            "kind",
            unique=True,
            postgresql_where=text("tenant_id IS NULL"),
            sqlite_where=text("tenant_id IS NULL"),
        ),
    )


class FinLedgerTransaction(Base):
    __tablename__ = "fin_ledger_transactions"

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True)
    tenant_id: Mapped[int] = mapped_column(
        ForeignKey("orgs.org_id", ondelete="RESTRICT", name="fin_transaction_tenant_fk")
    )
    operation_id: Mapped[str] = mapped_column(String(128), nullable=False)
    kind: Mapped[str] = mapped_column(String(32), nullable=False)
    status: Mapped[str] = mapped_column(String(16), server_default="draft")
    corrects_transaction_id: Mapped[int | None] = mapped_column(BigInteger)
    debit_kopecks: Mapped[int] = mapped_column(BigInteger, nullable=False)
    credit_kopecks: Mapped[int] = mapped_column(BigInteger, nullable=False)
    source_project: Mapped[str] = mapped_column(String(64), nullable=False)
    source_type: Mapped[str] = mapped_column(String(64), nullable=False)
    source_id: Mapped[str] = mapped_column(String(128), nullable=False)
    source_event_id: Mapped[str | None] = mapped_column(String(128))
    source_events_hash: Mapped[str] = mapped_column(String(64), nullable=False)
    archive_batch_id: Mapped[str | None] = mapped_column(String(128))
    calculation_snapshot: Mapped[dict[str, Any] | None] = mapped_column(JSON)
    actor: Mapped[str] = mapped_column(String(128), nullable=False)
    correlation_id: Mapped[str] = mapped_column(String(128), nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    posted_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))

    __table_args__ = (
        PrimaryKeyConstraint("id", name="fin_transaction_pk"),
        UniqueConstraint("id", "tenant_id", name="fin_transaction_tenant_uq"),
        UniqueConstraint(
            "tenant_id", "operation_id", name="fin_transaction_operation_uq"
        ),
        UniqueConstraint(
            "tenant_id",
            "source_project",
            "source_type",
            "source_id",
            name="fin_transaction_source_uq",
        ),
        ForeignKeyConstraint(
            ["corrects_transaction_id", "tenant_id"],
            ["fin_ledger_transactions.id", "fin_ledger_transactions.tenant_id"],
            name="fin_transaction_corrects_fk",
            ondelete="RESTRICT",
        ),
        CheckConstraint(
            "kind IN ('payment', 'usage', 'terminal_month', 'adjustment', 'reversal')",
            name="fin_transaction_kind_ck",
        ),
        CheckConstraint(
            "status IN ('draft', 'posted')", name="fin_transaction_status_ck"
        ),
        CheckConstraint(
            "(status = 'posted') = (posted_at IS NOT NULL)",
            name="fin_transaction_posted_ck",
        ),
        CheckConstraint(
            "debit_kopecks >= 0 AND debit_kopecks = credit_kopecks AND debit_kopecks % 100 = 0",
            name="fin_transaction_balance_ck",
        ),
        CheckConstraint(
            "corrects_transaction_id IS NULL OR corrects_transaction_id != id",
            name="fin_transaction_self_ck",
        ),
        CheckConstraint(
            "(kind IN ('adjustment', 'reversal')) = (corrects_transaction_id IS NOT NULL)",
            name="fin_transaction_correction_ck",
        ),
        Index("fin_transaction_tenant_time_ix", "tenant_id", "created_at"),
        Index("fin_transaction_correlation_ix", "correlation_id"),
    )


class FinLedgerEntry(Base):
    __tablename__ = "fin_ledger_entries"

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True)
    transaction_id: Mapped[int] = mapped_column(BigInteger, nullable=False)
    tenant_id: Mapped[int] = mapped_column(Integer, nullable=False)
    line_number: Mapped[int] = mapped_column(Integer, nullable=False)
    account_id: Mapped[int] = mapped_column(
        ForeignKey("fin_accounts.id", ondelete="RESTRICT", name="fin_entry_account_fk")
    )
    debit_kopecks: Mapped[int] = mapped_column(BigInteger, server_default="0")
    credit_kopecks: Mapped[int] = mapped_column(BigInteger, server_default="0")

    __table_args__ = (
        PrimaryKeyConstraint("id", name="fin_entry_pk"),
        ForeignKeyConstraint(
            ["transaction_id", "tenant_id"],
            ["fin_ledger_transactions.id", "fin_ledger_transactions.tenant_id"],
            ondelete="RESTRICT",
            name="fin_entry_transaction_fk",
        ),
        UniqueConstraint("transaction_id", "line_number", name="fin_entry_line_uq"),
        CheckConstraint("line_number > 0", name="fin_entry_line_ck"),
        CheckConstraint(
            "(debit_kopecks > 0 AND credit_kopecks = 0) OR (credit_kopecks > 0 AND debit_kopecks = 0)",
            name="fin_entry_side_ck",
        ),
        CheckConstraint(
            "debit_kopecks % 100 = 0 AND credit_kopecks % 100 = 0",
            name="fin_entry_rubles_ck",
        ),
        Index("fin_entry_account_transaction_ix", "account_id", "transaction_id"),
    )


class FinBillingProfile(Base):
    __tablename__ = "fin_billing_profiles"

    tenant_id: Mapped[int] = mapped_column(
        ForeignKey("orgs.org_id", ondelete="RESTRICT", name="fin_profile_tenant_fk"),
        primary_key=True,
    )
    anchor_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    anchor_day: Mapped[int | None] = mapped_column(Integer)
    anchor_timezone: Mapped[str | None] = mapped_column(String(64))
    first_payment_transaction_id: Mapped[int | None] = mapped_column(BigInteger)
    entitlement: Mapped[str] = mapped_column(String(16), server_default="free")
    entitlement_changed_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    __table_args__ = (
        PrimaryKeyConstraint("tenant_id", name="fin_profile_pk"),
        ForeignKeyConstraint(
            ["first_payment_transaction_id", "tenant_id"],
            ["fin_ledger_transactions.id", "fin_ledger_transactions.tenant_id"],
            ondelete="RESTRICT",
            name="fin_profile_first_payment_fk",
        ),
        CheckConstraint(
            "anchor_day BETWEEN 1 AND 31", name="fin_profile_anchor_day_ck"
        ),
        CheckConstraint(
            "(anchor_at IS NULL AND anchor_day IS NULL AND anchor_timezone IS NULL AND first_payment_transaction_id IS NULL) OR (anchor_at IS NOT NULL AND anchor_day IS NOT NULL AND anchor_timezone IS NOT NULL AND first_payment_transaction_id IS NOT NULL)",
            name="fin_profile_anchor_ck",
        ),
        CheckConstraint(
            "entitlement IN ('free', 'active', 'grace', 'blocked')",
            name="fin_profile_entitlement_ck",
        ),
        CheckConstraint(
            "entitlement NOT IN ('active', 'grace') OR anchor_at IS NOT NULL",
            name="fin_profile_paid_ck",
        ),
    )


class FinBillingCycle(Base):
    __tablename__ = "fin_billing_cycles"

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True)
    tenant_id: Mapped[int] = mapped_column(
        ForeignKey(
            "fin_billing_profiles.tenant_id",
            ondelete="RESTRICT",
            name="fin_cycle_profile_fk",
        )
    )
    sequence: Mapped[int] = mapped_column(Integer, nullable=False)
    starts_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False)
    ends_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False)
    grace_deadline: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False
    )
    timezone: Mapped[str] = mapped_column(String(64), nullable=False)
    closed_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))

    __table_args__ = (
        PrimaryKeyConstraint("id", name="fin_cycle_pk"),
        UniqueConstraint("id", "tenant_id", name="fin_cycle_tenant_uq"),
        UniqueConstraint("tenant_id", "sequence", name="fin_cycle_sequence_uq"),
        UniqueConstraint("tenant_id", "starts_at", name="fin_cycle_start_uq"),
        CheckConstraint("sequence >= 0", name="fin_cycle_sequence_ck"),
        CheckConstraint(
            "starts_at < grace_deadline AND grace_deadline < ends_at",
            name="fin_cycle_interval_ck",
        ),
        Index("fin_cycle_end_ix", "ends_at"),
    )


class FinUsageDaily(Base):
    __tablename__ = "fin_usage_daily"

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True)
    tenant_id: Mapped[int] = mapped_column(Integer, nullable=False)
    terminal_id: Mapped[int] = mapped_column(Integer, nullable=False)
    local_date: Mapped[date] = mapped_column(Date, nullable=False)
    timezone: Mapped[str] = mapped_column(String(64), nullable=False)
    tariff_version_id: Mapped[int] = mapped_column(
        ForeignKey(
            "fin_tariff_versions.id", ondelete="RESTRICT", name="fin_usage_tariff_fk"
        )
    )
    source_seconds: Mapped[int] = mapped_column(BigInteger, nullable=False)
    video_seconds: Mapped[int] = mapped_column(BigInteger, nullable=False)
    console_seconds: Mapped[int] = mapped_column(BigInteger, nullable=False)
    free_seconds: Mapped[int] = mapped_column(BigInteger, nullable=False)
    billable_seconds: Mapped[int] = mapped_column(BigInteger, nullable=False)
    rounded_billable_hours: Mapped[int] = mapped_column(Integer, nullable=False)
    rate_kopecks: Mapped[int] = mapped_column(BigInteger, nullable=False)
    calculated_kopecks: Mapped[int] = mapped_column(BigInteger, nullable=False)
    posted_kopecks: Mapped[int] = mapped_column(BigInteger, nullable=False)
    discarded_kopecks: Mapped[int] = mapped_column(BigInteger, nullable=False)
    source_project: Mapped[str] = mapped_column(String(64), nullable=False)
    source_event_id: Mapped[str | None] = mapped_column(String(128))
    source_events_hash: Mapped[str] = mapped_column(String(64), nullable=False)
    archive_batch_id: Mapped[str | None] = mapped_column(String(128))
    ledger_transaction_id: Mapped[int | None] = mapped_column(BigInteger)
    actor: Mapped[str] = mapped_column(String(128), nullable=False)
    correlation_id: Mapped[str] = mapped_column(String(128), nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    posted_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))

    __table_args__ = (
        PrimaryKeyConstraint("id", name="fin_usage_pk"),
        ForeignKeyConstraint(
            ["terminal_id", "tenant_id"],
            ["l4desk_terminals.terminal_id", "l4desk_terminals.tenant_id"],
            name="fin_usage_terminal_fk",
            ondelete="RESTRICT",
        ),
        ForeignKeyConstraint(
            ["ledger_transaction_id", "tenant_id"],
            ["fin_ledger_transactions.id", "fin_ledger_transactions.tenant_id"],
            name="fin_usage_transaction_fk",
            ondelete="RESTRICT",
        ),
        UniqueConstraint("terminal_id", "local_date", name="fin_usage_day_uq"),
        UniqueConstraint("ledger_transaction_id", name="fin_usage_transaction_uq"),
        CheckConstraint(
            "video_seconds >= 0 AND console_seconds >= 0 AND free_seconds >= 0 AND billable_seconds >= 0 AND rounded_billable_hours >= 0 AND rate_kopecks >= 0",
            name="fin_usage_values_ck",
        ),
        CheckConstraint(
            "source_seconds = video_seconds + console_seconds AND source_seconds = free_seconds + billable_seconds",
            name="fin_usage_seconds_ck",
        ),
        CheckConstraint(
            "posted_kopecks >= 0 AND calculated_kopecks = posted_kopecks + discarded_kopecks AND discarded_kopecks >= 0 AND discarded_kopecks < 100 AND posted_kopecks % 100 = 0",
            name="fin_usage_rounding_ck",
        ),
        CheckConstraint(
            "(posted_at IS NULL) = (ledger_transaction_id IS NULL)",
            name="fin_usage_posted_ck",
        ),
        Index("fin_usage_tenant_date_ix", "tenant_id", "local_date"),
        Index(
            "fin_usage_pending_ix",
            "local_date",
            postgresql_where=text("posted_at IS NULL"),
            sqlite_where=text("posted_at IS NULL"),
        ),
    )


class FinTerminalMonthlyCharge(Base):
    __tablename__ = "fin_terminal_monthly_charges"

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True)
    tenant_id: Mapped[int] = mapped_column(Integer, nullable=False)
    terminal_id: Mapped[int] = mapped_column(Integer, nullable=False)
    billing_cycle_id: Mapped[int] = mapped_column(BigInteger, nullable=False)
    tariff_version_id: Mapped[int] = mapped_column(
        ForeignKey(
            "fin_tariff_versions.id", ondelete="RESTRICT", name="fin_monthly_tariff_fk"
        )
    )
    is_free: Mapped[bool] = mapped_column(Boolean, nullable=False)
    first_online_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False
    )
    calculated_kopecks: Mapped[int] = mapped_column(BigInteger, nullable=False)
    posted_kopecks: Mapped[int] = mapped_column(BigInteger, nullable=False)
    discarded_kopecks: Mapped[int] = mapped_column(BigInteger, nullable=False)
    source_project: Mapped[str] = mapped_column(String(64), nullable=False)
    source_event_id: Mapped[str] = mapped_column(String(128), nullable=False)
    source_events_hash: Mapped[str] = mapped_column(String(64), nullable=False)
    archive_batch_id: Mapped[str | None] = mapped_column(String(128))
    ledger_transaction_id: Mapped[int | None] = mapped_column(BigInteger)
    actor: Mapped[str] = mapped_column(String(128), nullable=False)
    correlation_id: Mapped[str] = mapped_column(String(128), nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    posted_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))

    __table_args__ = (
        PrimaryKeyConstraint("id", name="fin_monthly_pk"),
        ForeignKeyConstraint(
            ["terminal_id", "tenant_id"],
            ["l4desk_terminals.terminal_id", "l4desk_terminals.tenant_id"],
            name="fin_monthly_terminal_fk",
            ondelete="RESTRICT",
        ),
        ForeignKeyConstraint(
            ["billing_cycle_id", "tenant_id"],
            ["fin_billing_cycles.id", "fin_billing_cycles.tenant_id"],
            name="fin_monthly_cycle_fk",
            ondelete="RESTRICT",
        ),
        ForeignKeyConstraint(
            ["ledger_transaction_id", "tenant_id"],
            ["fin_ledger_transactions.id", "fin_ledger_transactions.tenant_id"],
            name="fin_monthly_transaction_fk",
            ondelete="RESTRICT",
        ),
        UniqueConstraint(
            "terminal_id", "billing_cycle_id", name="fin_monthly_terminal_cycle_uq"
        ),
        UniqueConstraint("ledger_transaction_id", name="fin_monthly_transaction_uq"),
        CheckConstraint(
            "posted_kopecks >= 0 AND calculated_kopecks = posted_kopecks + discarded_kopecks AND discarded_kopecks >= 0 AND discarded_kopecks < 100 AND posted_kopecks % 100 = 0",
            name="fin_monthly_rounding_ck",
        ),
        CheckConstraint(
            "NOT is_free OR calculated_kopecks = 0", name="fin_monthly_free_ck"
        ),
        CheckConstraint(
            "(posted_at IS NULL) = (ledger_transaction_id IS NULL)",
            name="fin_monthly_posted_ck",
        ),
        Index("fin_monthly_tenant_cycle_ix", "tenant_id", "billing_cycle_id"),
    )


class FinPayment(Base):
    __tablename__ = "fin_payments"

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True)
    tenant_id: Mapped[int] = mapped_column(
        ForeignKey("orgs.org_id", ondelete="RESTRICT", name="fin_payment_tenant_fk")
    )
    operation_id: Mapped[str] = mapped_column(String(128), nullable=False)
    provider: Mapped[str] = mapped_column(String(32), server_default="yookassa")
    provider_payment_id: Mapped[str | None] = mapped_column(String(128))
    status: Mapped[str] = mapped_column(String(32), server_default="pending")
    amount_kopecks: Mapped[int] = mapped_column(BigInteger, nullable=False)
    currency: Mapped[str] = mapped_column(String(3), server_default="RUB")
    confirmation_url: Mapped[str | None] = mapped_column(Text)
    provider_receipt_id: Mapped[str | None] = mapped_column(String(128))
    receipt_status: Mapped[str | None] = mapped_column(String(32))
    receipt_snapshot: Mapped[dict[str, Any] | None] = mapped_column(JSON)
    verified_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    succeeded_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    ledger_transaction_id: Mapped[int | None] = mapped_column(BigInteger)
    actor: Mapped[str] = mapped_column(String(128), nullable=False)
    correlation_id: Mapped[str] = mapped_column(String(128), nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    __table_args__ = (
        PrimaryKeyConstraint("id", name="fin_payment_pk"),
        UniqueConstraint("tenant_id", "operation_id", name="fin_payment_operation_uq"),
        UniqueConstraint(
            "provider", "provider_payment_id", name="fin_payment_provider_uq"
        ),
        UniqueConstraint("ledger_transaction_id", name="fin_payment_transaction_uq"),
        ForeignKeyConstraint(
            ["ledger_transaction_id", "tenant_id"],
            ["fin_ledger_transactions.id", "fin_ledger_transactions.tenant_id"],
            name="fin_payment_transaction_fk",
            ondelete="RESTRICT",
        ),
        CheckConstraint(
            "amount_kopecks > 0 AND amount_kopecks % 100 = 0 AND currency = 'RUB'",
            name="fin_payment_amount_ck",
        ),
        CheckConstraint(
            "status IN ('pending', 'waiting_for_capture', 'succeeded', 'canceled')",
            name="fin_payment_status_ck",
        ),
        CheckConstraint(
            "status != 'succeeded' OR (verified_at IS NOT NULL AND succeeded_at IS NOT NULL AND provider_payment_id IS NOT NULL)",
            name="fin_payment_verified_ck",
        ),
        CheckConstraint(
            "ledger_transaction_id IS NULL OR status = 'succeeded'",
            name="fin_payment_posting_ck",
        ),
        Index("fin_payment_tenant_time_ix", "tenant_id", "created_at"),
        Index("fin_payment_status_time_ix", "status", "created_at"),
    )


class FinManualPayment(Base):
    __tablename__ = "fin_manual_payments"

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True)
    tenant_id: Mapped[int] = mapped_column(Integer, nullable=False)
    operation_id: Mapped[str] = mapped_column(String(128), nullable=False)
    amount_kopecks: Mapped[int] = mapped_column(BigInteger, nullable=False)
    received_on: Mapped[date] = mapped_column(Date, nullable=False)
    document_number: Mapped[str] = mapped_column(String(128), nullable=False)
    purpose: Mapped[str] = mapped_column(Text, nullable=False)
    payer: Mapped[str] = mapped_column(String(500), nullable=False)
    comment: Mapped[str | None] = mapped_column(Text)
    evidence_reference: Mapped[str | None] = mapped_column(Text)
    created_by_user_id: Mapped[int] = mapped_column(
        ForeignKey("users.id", ondelete="RESTRICT", name="fin_manual_creator_fk")
    )
    ledger_transaction_id: Mapped[int] = mapped_column(BigInteger, nullable=False)
    correlation_id: Mapped[str] = mapped_column(String(128), nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    __table_args__ = (
        PrimaryKeyConstraint("id", name="fin_manual_pk"),
        UniqueConstraint("tenant_id", "operation_id", name="fin_manual_operation_uq"),
        UniqueConstraint("ledger_transaction_id", name="fin_manual_transaction_uq"),
        ForeignKeyConstraint(
            ["ledger_transaction_id", "tenant_id"],
            ["fin_ledger_transactions.id", "fin_ledger_transactions.tenant_id"],
            name="fin_manual_transaction_fk",
            ondelete="RESTRICT",
        ),
        CheckConstraint(
            "amount_kopecks > 0 AND amount_kopecks % 100 = 0",
            name="fin_manual_amount_ck",
        ),
        Index("fin_manual_tenant_date_ix", "tenant_id", "received_on"),
    )


class FinBalanceProjection(Base):
    __tablename__ = "fin_balance_projections"

    tenant_id: Mapped[int] = mapped_column(
        ForeignKey("orgs.org_id", ondelete="RESTRICT", name="fin_balance_tenant_fk"),
        primary_key=True,
    )
    account_id: Mapped[int] = mapped_column(
        ForeignKey(
            "fin_accounts.id", ondelete="RESTRICT", name="fin_balance_account_fk"
        )
    )
    balance_kopecks: Mapped[int] = mapped_column(BigInteger, server_default="0")
    version: Mapped[int] = mapped_column(BigInteger, server_default="0")
    last_transaction_id: Mapped[int | None] = mapped_column(BigInteger)
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    __table_args__ = (
        PrimaryKeyConstraint("tenant_id", name="fin_balance_pk"),
        UniqueConstraint("account_id", name="fin_balance_account_uq"),
        ForeignKeyConstraint(
            ["last_transaction_id", "tenant_id"],
            ["fin_ledger_transactions.id", "fin_ledger_transactions.tenant_id"],
            name="fin_balance_transaction_fk",
            ondelete="RESTRICT",
        ),
        CheckConstraint("version >= 0", name="fin_balance_version_ck"),
        CheckConstraint("balance_kopecks % 100 = 0", name="fin_balance_rubles_ck"),
    )


class FinNotificationDelivery(Base):
    __tablename__ = "fin_notification_deliveries"

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True)
    tenant_id: Mapped[int] = mapped_column(Integer, nullable=False)
    billing_cycle_id: Mapped[int] = mapped_column(BigInteger, nullable=False)
    notification_type: Mapped[str] = mapped_column(String(32), nullable=False)
    scheduled_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False
    )
    status: Mapped[str] = mapped_column(String(16), server_default="pending")
    attempts: Mapped[int] = mapped_column(Integer, server_default="0")
    sent_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    provider_message_id: Mapped[str | None] = mapped_column(String(128))
    last_error: Mapped[str | None] = mapped_column(String(500))
    correlation_id: Mapped[str] = mapped_column(String(128), nullable=False)

    __table_args__ = (
        PrimaryKeyConstraint("id", name="fin_notification_pk"),
        ForeignKeyConstraint(
            ["billing_cycle_id", "tenant_id"],
            ["fin_billing_cycles.id", "fin_billing_cycles.tenant_id"],
            name="fin_notification_cycle_fk",
            ondelete="RESTRICT",
        ),
        UniqueConstraint(
            "tenant_id",
            "billing_cycle_id",
            "notification_type",
            name="fin_notification_cycle_type_uq",
        ),
        CheckConstraint(
            "notification_type IN ('cycle_minus_7', 'cycle_minus_3', 'cycle_minus_1', 'grace', 'blocked')",
            name="fin_notification_type_ck",
        ),
        CheckConstraint(
            "status IN ('pending', 'sending', 'sent', 'failed') AND attempts >= 0",
            name="fin_notification_status_ck",
        ),
        CheckConstraint(
            "status != 'sent' OR sent_at IS NOT NULL", name="fin_notification_sent_ck"
        ),
        Index("fin_notification_due_ix", "status", "scheduled_at"),
    )


class FinReconciliationRun(Base):
    __tablename__ = "fin_reconciliation_runs"

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True)
    tenant_id: Mapped[int | None] = mapped_column(
        ForeignKey(
            "orgs.org_id", ondelete="RESTRICT", name="fin_reconciliation_tenant_fk"
        )
    )
    operation_id: Mapped[str] = mapped_column(String(128), nullable=False)
    period_start: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False
    )
    period_end: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False
    )
    status: Mapped[str] = mapped_column(String(16), server_default="pending")
    calculated_kopecks: Mapped[int | None] = mapped_column(BigInteger)
    posted_kopecks: Mapped[int | None] = mapped_column(BigInteger)
    discarded_kopecks: Mapped[int | None] = mapped_column(BigInteger)
    debit_kopecks: Mapped[int | None] = mapped_column(BigInteger)
    credit_kopecks: Mapped[int | None] = mapped_column(BigInteger)
    balance_difference_kopecks: Mapped[int | None] = mapped_column(BigInteger)
    mismatch_count: Mapped[int | None] = mapped_column(BigInteger)
    source_events_hash: Mapped[str | None] = mapped_column(String(64))
    details: Mapped[dict[str, Any] | None] = mapped_column(JSON)
    actor: Mapped[str] = mapped_column(String(128), nullable=False)
    correlation_id: Mapped[str] = mapped_column(String(128), nullable=False)
    started_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    finished_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))

    __table_args__ = (
        PrimaryKeyConstraint("id", name="fin_reconciliation_pk"),
        UniqueConstraint("operation_id", name="fin_reconciliation_operation_uq"),
        CheckConstraint(
            "period_end > period_start", name="fin_reconciliation_period_ck"
        ),
        CheckConstraint(
            "status IN ('pending', 'matched', 'mismatch', 'failed')",
            name="fin_reconciliation_status_ck",
        ),
        CheckConstraint("mismatch_count >= 0", name="fin_reconciliation_count_ck"),
        CheckConstraint(
            "status != 'matched' OR (finished_at IS NOT NULL AND mismatch_count IS NOT NULL AND mismatch_count = 0 AND calculated_kopecks IS NOT NULL AND posted_kopecks IS NOT NULL AND discarded_kopecks IS NOT NULL AND calculated_kopecks = posted_kopecks + discarded_kopecks AND debit_kopecks IS NOT NULL AND credit_kopecks IS NOT NULL AND debit_kopecks = credit_kopecks AND balance_difference_kopecks IS NOT NULL AND balance_difference_kopecks = 0)",
            name="fin_reconciliation_matched_ck",
        ),
        Index("fin_reconciliation_tenant_time_ix", "tenant_id", "started_at"),
    )


class FinArchiveBatch(Base):
    __tablename__ = "fin_archive_batches"

    id: Mapped[str] = mapped_column(String(128), primary_key=True)
    source_project: Mapped[str] = mapped_column(String(64), primary_key=True)
    schema_version: Mapped[str] = mapped_column(String(64), nullable=False)
    archive_month: Mapped[date] = mapped_column(Date, nullable=False)
    source_types: Mapped[list[str]] = mapped_column(JSON, nullable=False)
    row_count: Mapped[int] = mapped_column(BigInteger, nullable=False)
    min_occurred_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    max_occurred_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    through_cursor: Mapped[int | None] = mapped_column(BigInteger)
    consumers_passed_cursor: Mapped[int | None] = mapped_column(BigInteger)
    storage_reference: Mapped[str] = mapped_column(Text, nullable=False)
    checksum_sha256: Mapped[str] = mapped_column(String(64), nullable=False)
    manifest: Mapped[dict[str, Any]] = mapped_column(JSON, nullable=False)
    status: Mapped[str] = mapped_column(String(16), server_default="pending")
    verified_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    purged_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    retain_until: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False
    )
    actor: Mapped[str] = mapped_column(String(128), nullable=False)
    correlation_id: Mapped[str] = mapped_column(String(128), nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    __table_args__ = (
        PrimaryKeyConstraint("id", "source_project", name="fin_archive_pk"),
        CheckConstraint(
            "row_count >= 0 AND length(checksum_sha256) = 64",
            name="fin_archive_values_ck",
        ),
        CheckConstraint(
            "max_occurred_at >= min_occurred_at", name="fin_archive_interval_ck"
        ),
        CheckConstraint(
            "through_cursor >= 0 AND consumers_passed_cursor >= 0",
            name="fin_archive_cursor_ck",
        ),
        CheckConstraint(
            "status IN ('pending', 'verified', 'failed')", name="fin_archive_status_ck"
        ),
        CheckConstraint(
            "status != 'verified' OR verified_at IS NOT NULL",
            name="fin_archive_verified_ck",
        ),
        CheckConstraint(
            "purged_at IS NULL OR (status = 'verified' AND verified_at IS NOT NULL AND (through_cursor IS NULL OR (consumers_passed_cursor IS NOT NULL AND consumers_passed_cursor >= through_cursor)))",
            name="fin_archive_purge_ck",
        ),
        Index("fin_archive_project_month_ix", "source_project", "archive_month"),
    )
