"""Add L4Desk, IoT inbox, and financial subledger tables.

Revision ID: 027
Revises: 026
Create Date: 2026-09-18
"""

import contextlib
from collections.abc import Sequence

import sqlalchemy as sa

from alembic import op

revision: str = "027"
down_revision: str | None = "026"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    bind = op.get_bind()
    is_offline = bool(getattr(op.get_context(), "as_sql", False))
    inspector = sa.inspect(bind) if not is_offline else None
    existing_tables = (
        set(inspector.get_table_names()) if inspector is not None else set()
    )

    def table_missing(name: str) -> bool:
        return is_offline or name not in existing_tables

    def index_missing(tname: str, iname: str) -> bool:
        if is_offline or inspector is None:
            return True
        if tname not in existing_tables:
            return True
        with contextlib.suppress(Exception):
            return iname not in {idx["name"] for idx in inspector.get_indexes(tname)}
        return True

    # --- fin_archive_batches ---
    if table_missing("fin_archive_batches"):
        op.create_table(
            "fin_archive_batches",
            sa.Column("id", sa.String(length=128), nullable=False),
            sa.Column("source_project", sa.String(length=64), nullable=False),
            sa.Column("schema_version", sa.String(length=64), nullable=False),
            sa.Column("archive_month", sa.Date(), nullable=False),
            sa.Column("source_types", sa.JSON(), nullable=False),
            sa.Column("row_count", sa.BigInteger(), nullable=False),
            sa.Column("min_occurred_at", sa.DateTime(timezone=True), nullable=True),
            sa.Column("max_occurred_at", sa.DateTime(timezone=True), nullable=True),
            sa.Column("through_cursor", sa.BigInteger(), nullable=True),
            sa.Column("consumers_passed_cursor", sa.BigInteger(), nullable=True),
            sa.Column("storage_reference", sa.Text(), nullable=False),
            sa.Column("checksum_sha256", sa.String(length=64), nullable=False),
            sa.Column("manifest", sa.JSON(), nullable=False),
            sa.Column(
                "status", sa.String(length=16), server_default="pending", nullable=False
            ),
            sa.Column("verified_at", sa.DateTime(timezone=True), nullable=True),
            sa.Column("purged_at", sa.DateTime(timezone=True), nullable=True),
            sa.Column("retain_until", sa.DateTime(timezone=True), nullable=False),
            sa.Column("actor", sa.String(length=128), nullable=False),
            sa.Column("correlation_id", sa.String(length=128), nullable=False),
            sa.Column(
                "created_at",
                sa.DateTime(timezone=True),
                server_default=sa.text("now()"),
                nullable=False,
            ),
            sa.CheckConstraint(
                "purged_at IS NULL OR (status = 'verified' AND verified_at IS NOT NULL AND (through_cursor IS NULL OR (consumers_passed_cursor IS NOT NULL AND consumers_passed_cursor >= through_cursor)))",
                name="fin_archive_purge_ck",
            ),
            sa.CheckConstraint(
                "status != 'verified' OR verified_at IS NOT NULL",
                name="fin_archive_verified_ck",
            ),
            sa.CheckConstraint(
                "status IN ('pending', 'verified', 'failed')",
                name="fin_archive_status_ck",
            ),
            sa.CheckConstraint(
                "max_occurred_at >= min_occurred_at", name="fin_archive_interval_ck"
            ),
            sa.CheckConstraint(
                "row_count >= 0 AND length(checksum_sha256) = 64",
                name="fin_archive_values_ck",
            ),
            sa.CheckConstraint(
                "through_cursor >= 0 AND consumers_passed_cursor >= 0",
                name="fin_archive_cursor_ck",
            ),
            sa.PrimaryKeyConstraint("id", "source_project", name="fin_archive_pk"),
        )

    if index_missing("fin_archive_batches", "fin_archive_project_month_ix"):
        op.create_index(
            "fin_archive_project_month_ix",
            "fin_archive_batches",
            ["source_project", "archive_month"],
            unique=False,
        )

    # --- fin_tariff_versions ---
    if table_missing("fin_tariff_versions"):
        op.create_table(
            "fin_tariff_versions",
            sa.Column("id", sa.BigInteger(), nullable=False),
            sa.Column("version", sa.String(length=64), nullable=False),
            sa.Column("effective_from", sa.DateTime(timezone=True), nullable=False),
            sa.Column("terminal_month_kopecks", sa.BigInteger(), nullable=False),
            sa.Column("hourly_rate_kopecks", sa.BigInteger(), nullable=False),
            sa.Column("free_daily_seconds", sa.Integer(), nullable=False),
            sa.Column("actor", sa.String(length=128), nullable=False),
            sa.Column("correlation_id", sa.String(length=128), nullable=False),
            sa.Column(
                "created_at",
                sa.DateTime(timezone=True),
                server_default=sa.text("now()"),
                nullable=False,
            ),
            sa.CheckConstraint(
                "terminal_month_kopecks >= 0 AND hourly_rate_kopecks >= 0 AND free_daily_seconds >= 0",
                name="fin_tariff_values_ck",
            ),
            sa.PrimaryKeyConstraint("id", name="fin_tariff_pk"),
            sa.UniqueConstraint("effective_from", name="fin_tariff_effective_uq"),
            sa.UniqueConstraint("version", name="fin_tariff_version_uq"),
        )

    # --- iot_consumer_checkpoints ---
    if table_missing("iot_consumer_checkpoints"):
        op.create_table(
            "iot_consumer_checkpoints",
            sa.Column("consumer_id", sa.String(length=64), nullable=False),
            sa.Column("feed_name", sa.String(length=64), nullable=False),
            sa.Column("last_cursor", sa.BigInteger(), nullable=False),
            sa.Column("last_event_id", sa.String(length=128), nullable=True),
            sa.Column(
                "last_event_occurred_at", sa.DateTime(timezone=True), nullable=True
            ),
            sa.Column("updated_at", sa.DateTime(timezone=True), nullable=False),
            sa.PrimaryKeyConstraint("consumer_id"),
        )

    # --- iot_event_inbox ---
    if table_missing("iot_event_inbox"):
        op.create_table(
            "iot_event_inbox",
            sa.Column("event_id", sa.String(length=128), nullable=False),
            sa.Column("cursor", sa.BigInteger(), nullable=False),
            sa.Column("event_type", sa.String(length=64), nullable=False),
            sa.Column("event_version", sa.String(length=32), nullable=False),
            sa.Column("occurred_at", sa.DateTime(timezone=True), nullable=False),
            sa.Column("tenant_id", sa.Integer(), nullable=True),
            sa.Column("terminal_id", sa.String(length=128), nullable=True),
            sa.Column("device_id", sa.Integer(), nullable=True),
            sa.Column("sn", sa.String(length=128), nullable=False),
            sa.Column("session_id", sa.String(length=128), nullable=True),
            sa.Column("session_type", sa.String(length=32), nullable=True),
            sa.Column("lifecycle_state", sa.String(length=32), nullable=True),
            sa.Column("reason", sa.String(length=128), nullable=True),
            sa.Column("operation_id", sa.String(length=128), nullable=True),
            sa.Column("correlation_id", sa.String(length=128), nullable=True),
            sa.Column("payload", sa.JSON(), nullable=True),
            sa.Column("received_at", sa.DateTime(timezone=True), nullable=False),
            sa.Column("processed_at", sa.DateTime(timezone=True), nullable=True),
            sa.Column("status", sa.String(length=32), nullable=False),
            sa.PrimaryKeyConstraint("event_id"),
        )

    if index_missing("iot_event_inbox", "ix_iot_event_inbox_event_type"):
        op.create_index(
            op.f("ix_iot_event_inbox_event_type"),
            "iot_event_inbox",
            ["event_type"],
            unique=False,
        )

    if index_missing("iot_event_inbox", "ix_iot_event_inbox_tenant_id"):
        op.create_index(
            op.f("ix_iot_event_inbox_tenant_id"),
            "iot_event_inbox",
            ["tenant_id"],
            unique=False,
        )

    if index_missing("iot_event_inbox", "ix_iot_event_inbox_cursor"):
        op.create_index(
            op.f("ix_iot_event_inbox_cursor"),
            "iot_event_inbox",
            ["cursor"],
            unique=False,
        )

    if index_missing("iot_event_inbox", "ix_iot_event_inbox_operation_id"):
        op.create_index(
            op.f("ix_iot_event_inbox_operation_id"),
            "iot_event_inbox",
            ["operation_id"],
            unique=False,
        )

    if index_missing("iot_event_inbox", "ix_iot_event_inbox_sn"):
        op.create_index(
            op.f("ix_iot_event_inbox_sn"), "iot_event_inbox", ["sn"], unique=False
        )

    if index_missing("iot_event_inbox", "ix_iot_event_inbox_session_id"):
        op.create_index(
            op.f("ix_iot_event_inbox_session_id"),
            "iot_event_inbox",
            ["session_id"],
            unique=False,
        )

    # --- iot_event_quarantine ---
    if table_missing("iot_event_quarantine"):
        op.create_table(
            "iot_event_quarantine",
            sa.Column("id", sa.Integer(), autoincrement=True, nullable=False),
            sa.Column("event_id", sa.String(length=128), nullable=False),
            sa.Column("cursor", sa.BigInteger(), nullable=False),
            sa.Column("error_code", sa.String(length=64), nullable=False),
            sa.Column("error_detail", sa.Text(), nullable=False),
            sa.Column("raw_event", sa.JSON(), nullable=False),
            sa.Column("quarantined_at", sa.DateTime(timezone=True), nullable=False),
            sa.Column("retry_count", sa.Integer(), nullable=False),
            sa.Column("resolved", sa.Boolean(), nullable=False),
            sa.PrimaryKeyConstraint("id"),
        )

    if index_missing("iot_event_quarantine", "ix_iot_event_quarantine_event_id"):
        op.create_index(
            op.f("ix_iot_event_quarantine_event_id"),
            "iot_event_quarantine",
            ["event_id"],
            unique=False,
        )

    # --- fin_accounts ---
    if table_missing("fin_accounts"):
        op.create_table(
            "fin_accounts",
            sa.Column("id", sa.BigInteger(), nullable=False),
            sa.Column("tenant_id", sa.Integer(), nullable=True),
            sa.Column("kind", sa.String(length=32), nullable=False),
            sa.Column(
                "currency", sa.String(length=3), server_default="RUB", nullable=False
            ),
            sa.Column(
                "created_at",
                sa.DateTime(timezone=True),
                server_default=sa.text("now()"),
                nullable=False,
            ),
            sa.CheckConstraint(
                "(kind = 'tenant_settlement' AND tenant_id IS NOT NULL) OR (kind IN ('payment_clearing', 'usage_revenue') AND tenant_id IS NULL)",
                name="fin_account_scope_ck",
            ),
            sa.CheckConstraint("currency = 'RUB'", name="fin_account_currency_ck"),
            sa.ForeignKeyConstraint(
                ["tenant_id"],
                ["orgs.org_id"],
                name="fin_account_tenant_fk",
                ondelete="RESTRICT",
            ),
            sa.PrimaryKeyConstraint("id", name="fin_account_pk"),
            sa.UniqueConstraint("tenant_id", "kind", name="fin_account_tenant_kind_uq"),
        )

    if index_missing("fin_accounts", "fin_account_global_kind_uq"):
        op.create_index(
            "fin_account_global_kind_uq",
            "fin_accounts",
            ["kind"],
            unique=True,
            postgresql_where=sa.text("tenant_id IS NULL"),
            sqlite_where=sa.text("tenant_id IS NULL"),
        )

    # --- fin_ledger_transactions ---
    if table_missing("fin_ledger_transactions"):
        op.create_table(
            "fin_ledger_transactions",
            sa.Column("id", sa.BigInteger(), nullable=False),
            sa.Column("tenant_id", sa.Integer(), nullable=False),
            sa.Column("operation_id", sa.String(length=128), nullable=False),
            sa.Column("kind", sa.String(length=32), nullable=False),
            sa.Column(
                "status", sa.String(length=16), server_default="draft", nullable=False
            ),
            sa.Column("corrects_transaction_id", sa.BigInteger(), nullable=True),
            sa.Column("debit_kopecks", sa.BigInteger(), nullable=False),
            sa.Column("credit_kopecks", sa.BigInteger(), nullable=False),
            sa.Column("source_project", sa.String(length=64), nullable=False),
            sa.Column("source_type", sa.String(length=64), nullable=False),
            sa.Column("source_id", sa.String(length=128), nullable=False),
            sa.Column("source_event_id", sa.String(length=128), nullable=True),
            sa.Column("source_events_hash", sa.String(length=64), nullable=False),
            sa.Column("archive_batch_id", sa.String(length=128), nullable=True),
            sa.Column("calculation_snapshot", sa.JSON(), nullable=True),
            sa.Column("actor", sa.String(length=128), nullable=False),
            sa.Column("correlation_id", sa.String(length=128), nullable=False),
            sa.Column(
                "created_at",
                sa.DateTime(timezone=True),
                server_default=sa.text("now()"),
                nullable=False,
            ),
            sa.Column("posted_at", sa.DateTime(timezone=True), nullable=True),
            sa.CheckConstraint(
                "(kind IN ('adjustment', 'reversal')) = (corrects_transaction_id IS NOT NULL)",
                name="fin_transaction_correction_ck",
            ),
            sa.CheckConstraint(
                "(status = 'posted') = (posted_at IS NOT NULL)",
                name="fin_transaction_posted_ck",
            ),
            sa.CheckConstraint(
                "kind IN ('payment', 'usage', 'terminal_month', 'adjustment', 'reversal')",
                name="fin_transaction_kind_ck",
            ),
            sa.CheckConstraint(
                "status IN ('draft', 'posted')", name="fin_transaction_status_ck"
            ),
            sa.CheckConstraint(
                "corrects_transaction_id IS NULL OR corrects_transaction_id != id",
                name="fin_transaction_self_ck",
            ),
            sa.CheckConstraint(
                "debit_kopecks >= 0 AND debit_kopecks = credit_kopecks AND debit_kopecks % 100 = 0",
                name="fin_transaction_balance_ck",
            ),
            sa.ForeignKeyConstraint(
                ["corrects_transaction_id", "tenant_id"],
                ["fin_ledger_transactions.id", "fin_ledger_transactions.tenant_id"],
                name="fin_transaction_corrects_fk",
                ondelete="RESTRICT",
            ),
            sa.ForeignKeyConstraint(
                ["tenant_id"],
                ["orgs.org_id"],
                name="fin_transaction_tenant_fk",
                ondelete="RESTRICT",
            ),
            sa.PrimaryKeyConstraint("id", name="fin_transaction_pk"),
            sa.UniqueConstraint("id", "tenant_id", name="fin_transaction_tenant_uq"),
            sa.UniqueConstraint(
                "tenant_id", "operation_id", name="fin_transaction_operation_uq"
            ),
            sa.UniqueConstraint(
                "tenant_id",
                "source_project",
                "source_type",
                "source_id",
                name="fin_transaction_source_uq",
            ),
        )

    if index_missing("fin_ledger_transactions", "fin_transaction_tenant_time_ix"):
        op.create_index(
            "fin_transaction_tenant_time_ix",
            "fin_ledger_transactions",
            ["tenant_id", "created_at"],
            unique=False,
        )

    if index_missing("fin_ledger_transactions", "fin_transaction_correlation_ix"):
        op.create_index(
            "fin_transaction_correlation_ix",
            "fin_ledger_transactions",
            ["correlation_id"],
            unique=False,
        )

    # --- fin_reconciliation_runs ---
    if table_missing("fin_reconciliation_runs"):
        op.create_table(
            "fin_reconciliation_runs",
            sa.Column("id", sa.BigInteger(), nullable=False),
            sa.Column("tenant_id", sa.Integer(), nullable=True),
            sa.Column("operation_id", sa.String(length=128), nullable=False),
            sa.Column("period_start", sa.DateTime(timezone=True), nullable=False),
            sa.Column("period_end", sa.DateTime(timezone=True), nullable=False),
            sa.Column(
                "status", sa.String(length=16), server_default="pending", nullable=False
            ),
            sa.Column("calculated_kopecks", sa.BigInteger(), nullable=True),
            sa.Column("posted_kopecks", sa.BigInteger(), nullable=True),
            sa.Column("discarded_kopecks", sa.BigInteger(), nullable=True),
            sa.Column("debit_kopecks", sa.BigInteger(), nullable=True),
            sa.Column("credit_kopecks", sa.BigInteger(), nullable=True),
            sa.Column("balance_difference_kopecks", sa.BigInteger(), nullable=True),
            sa.Column("mismatch_count", sa.BigInteger(), nullable=True),
            sa.Column("source_events_hash", sa.String(length=64), nullable=True),
            sa.Column("details", sa.JSON(), nullable=True),
            sa.Column("actor", sa.String(length=128), nullable=False),
            sa.Column("correlation_id", sa.String(length=128), nullable=False),
            sa.Column(
                "started_at",
                sa.DateTime(timezone=True),
                server_default=sa.text("now()"),
                nullable=False,
            ),
            sa.Column("finished_at", sa.DateTime(timezone=True), nullable=True),
            sa.CheckConstraint(
                "status != 'matched' OR (finished_at IS NOT NULL AND mismatch_count IS NOT NULL AND mismatch_count = 0 AND calculated_kopecks IS NOT NULL AND posted_kopecks IS NOT NULL AND discarded_kopecks IS NOT NULL AND calculated_kopecks = posted_kopecks + discarded_kopecks AND debit_kopecks IS NOT NULL AND credit_kopecks IS NOT NULL AND debit_kopecks = credit_kopecks AND balance_difference_kopecks IS NOT NULL AND balance_difference_kopecks = 0)",
                name="fin_reconciliation_matched_ck",
            ),
            sa.CheckConstraint(
                "status IN ('pending', 'matched', 'mismatch', 'failed')",
                name="fin_reconciliation_status_ck",
            ),
            sa.CheckConstraint(
                "mismatch_count >= 0", name="fin_reconciliation_count_ck"
            ),
            sa.CheckConstraint(
                "period_end > period_start", name="fin_reconciliation_period_ck"
            ),
            sa.ForeignKeyConstraint(
                ["tenant_id"],
                ["orgs.org_id"],
                name="fin_reconciliation_tenant_fk",
                ondelete="RESTRICT",
            ),
            sa.PrimaryKeyConstraint("id", name="fin_reconciliation_pk"),
            sa.UniqueConstraint("operation_id", name="fin_reconciliation_operation_uq"),
        )

    if index_missing("fin_reconciliation_runs", "fin_reconciliation_tenant_time_ix"):
        op.create_index(
            "fin_reconciliation_tenant_time_ix",
            "fin_reconciliation_runs",
            ["tenant_id", "started_at"],
            unique=False,
        )

    # --- l4desk_audit_events ---
    if table_missing("l4desk_audit_events"):
        op.create_table(
            "l4desk_audit_events",
            sa.Column("id", sa.BigInteger(), nullable=False),
            sa.Column("tenant_id", sa.Integer(), nullable=True),
            sa.Column("actor", sa.String(length=128), nullable=False),
            sa.Column("event_type", sa.String(length=64), nullable=False),
            sa.Column("subject_type", sa.String(length=64), nullable=False),
            sa.Column("subject_id", sa.String(length=128), nullable=False),
            sa.Column("operation_id", sa.String(length=128), nullable=True),
            sa.Column("correlation_id", sa.String(length=128), nullable=False),
            sa.Column("outcome", sa.String(length=32), nullable=False),
            sa.Column("details", sa.JSON(), nullable=True),
            sa.Column(
                "occurred_at",
                sa.DateTime(timezone=True),
                server_default=sa.text("now()"),
                nullable=False,
            ),
            sa.ForeignKeyConstraint(
                ["tenant_id"], ["orgs.org_id"], ondelete="SET NULL"
            ),
            sa.PrimaryKeyConstraint("id"),
        )

    if index_missing("l4desk_audit_events", "l4desk_audit_tenant_time_ix"):
        op.create_index(
            "l4desk_audit_tenant_time_ix",
            "l4desk_audit_events",
            ["tenant_id", "occurred_at"],
            unique=False,
        )

    if index_missing("l4desk_audit_events", "l4desk_audit_correlation_ix"):
        op.create_index(
            "l4desk_audit_correlation_ix",
            "l4desk_audit_events",
            ["correlation_id"],
            unique=False,
        )

    # --- l4desk_tenant_profiles ---
    if table_missing("l4desk_tenant_profiles"):
        op.create_table(
            "l4desk_tenant_profiles",
            sa.Column("tenant_id", sa.Integer(), nullable=False),
            sa.Column("timezone", sa.String(length=64), nullable=False),
            sa.Column("pending_timezone", sa.String(length=64), nullable=True),
            sa.Column(
                "timezone_effective_at", sa.DateTime(timezone=True), nullable=True
            ),
            sa.Column(
                "created_at",
                sa.DateTime(timezone=True),
                server_default=sa.text("now()"),
                nullable=False,
            ),
            sa.CheckConstraint(
                "(pending_timezone IS NULL) = (timezone_effective_at IS NULL)",
                name="l4desk_timezone_pending_ck",
            ),
            sa.ForeignKeyConstraint(
                ["tenant_id"], ["orgs.org_id"], ondelete="RESTRICT"
            ),
            sa.PrimaryKeyConstraint("tenant_id"),
        )

    # --- fin_balance_projections ---
    if table_missing("fin_balance_projections"):
        op.create_table(
            "fin_balance_projections",
            sa.Column("tenant_id", sa.Integer(), nullable=False),
            sa.Column("account_id", sa.BigInteger(), nullable=False),
            sa.Column(
                "balance_kopecks", sa.BigInteger(), server_default="0", nullable=False
            ),
            sa.Column("version", sa.BigInteger(), server_default="0", nullable=False),
            sa.Column("last_transaction_id", sa.BigInteger(), nullable=True),
            sa.Column(
                "updated_at",
                sa.DateTime(timezone=True),
                server_default=sa.text("now()"),
                nullable=False,
            ),
            sa.CheckConstraint(
                "balance_kopecks % 100 = 0", name="fin_balance_rubles_ck"
            ),
            sa.CheckConstraint("version >= 0", name="fin_balance_version_ck"),
            sa.ForeignKeyConstraint(
                ["account_id"],
                ["fin_accounts.id"],
                name="fin_balance_account_fk",
                ondelete="RESTRICT",
            ),
            sa.ForeignKeyConstraint(
                ["last_transaction_id", "tenant_id"],
                ["fin_ledger_transactions.id", "fin_ledger_transactions.tenant_id"],
                name="fin_balance_transaction_fk",
                ondelete="RESTRICT",
            ),
            sa.ForeignKeyConstraint(
                ["tenant_id"],
                ["orgs.org_id"],
                name="fin_balance_tenant_fk",
                ondelete="RESTRICT",
            ),
            sa.PrimaryKeyConstraint("tenant_id", name="fin_balance_pk"),
            sa.UniqueConstraint("account_id", name="fin_balance_account_uq"),
        )

    # --- fin_billing_profiles ---
    if table_missing("fin_billing_profiles"):
        op.create_table(
            "fin_billing_profiles",
            sa.Column("tenant_id", sa.Integer(), nullable=False),
            sa.Column("anchor_at", sa.DateTime(timezone=True), nullable=True),
            sa.Column("anchor_day", sa.Integer(), nullable=True),
            sa.Column("anchor_timezone", sa.String(length=64), nullable=True),
            sa.Column("first_payment_transaction_id", sa.BigInteger(), nullable=True),
            sa.Column(
                "entitlement",
                sa.String(length=16),
                server_default="free",
                nullable=False,
            ),
            sa.Column(
                "entitlement_changed_at",
                sa.DateTime(timezone=True),
                server_default=sa.text("now()"),
                nullable=False,
            ),
            sa.CheckConstraint(
                "entitlement IN ('free', 'active', 'grace', 'blocked')",
                name="fin_profile_entitlement_ck",
            ),
            sa.CheckConstraint(
                "entitlement NOT IN ('active', 'grace') OR anchor_at IS NOT NULL",
                name="fin_profile_paid_ck",
            ),
            sa.CheckConstraint(
                "(anchor_at IS NULL AND anchor_day IS NULL AND anchor_timezone IS NULL AND first_payment_transaction_id IS NULL) OR (anchor_at IS NOT NULL AND anchor_day IS NOT NULL AND anchor_timezone IS NOT NULL AND first_payment_transaction_id IS NOT NULL)",
                name="fin_profile_anchor_ck",
            ),
            sa.CheckConstraint(
                "anchor_day BETWEEN 1 AND 31", name="fin_profile_anchor_day_ck"
            ),
            sa.ForeignKeyConstraint(
                ["first_payment_transaction_id", "tenant_id"],
                ["fin_ledger_transactions.id", "fin_ledger_transactions.tenant_id"],
                name="fin_profile_first_payment_fk",
                ondelete="RESTRICT",
            ),
            sa.ForeignKeyConstraint(
                ["tenant_id"],
                ["orgs.org_id"],
                name="fin_profile_tenant_fk",
                ondelete="RESTRICT",
            ),
            sa.PrimaryKeyConstraint("tenant_id", name="fin_profile_pk"),
        )

    # --- fin_ledger_entries ---
    if table_missing("fin_ledger_entries"):
        op.create_table(
            "fin_ledger_entries",
            sa.Column("id", sa.BigInteger(), nullable=False),
            sa.Column("transaction_id", sa.BigInteger(), nullable=False),
            sa.Column("tenant_id", sa.Integer(), nullable=False),
            sa.Column("line_number", sa.Integer(), nullable=False),
            sa.Column("account_id", sa.BigInteger(), nullable=False),
            sa.Column(
                "debit_kopecks", sa.BigInteger(), server_default="0", nullable=False
            ),
            sa.Column(
                "credit_kopecks", sa.BigInteger(), server_default="0", nullable=False
            ),
            sa.CheckConstraint(
                "(debit_kopecks > 0 AND credit_kopecks = 0) OR (credit_kopecks > 0 AND debit_kopecks = 0)",
                name="fin_entry_side_ck",
            ),
            sa.CheckConstraint(
                "debit_kopecks % 100 = 0 AND credit_kopecks % 100 = 0",
                name="fin_entry_rubles_ck",
            ),
            sa.CheckConstraint("line_number > 0", name="fin_entry_line_ck"),
            sa.ForeignKeyConstraint(
                ["account_id"],
                ["fin_accounts.id"],
                name="fin_entry_account_fk",
                ondelete="RESTRICT",
            ),
            sa.ForeignKeyConstraint(
                ["transaction_id", "tenant_id"],
                ["fin_ledger_transactions.id", "fin_ledger_transactions.tenant_id"],
                name="fin_entry_transaction_fk",
                ondelete="RESTRICT",
            ),
            sa.PrimaryKeyConstraint("id", name="fin_entry_pk"),
            sa.UniqueConstraint(
                "transaction_id", "line_number", name="fin_entry_line_uq"
            ),
        )

    if index_missing("fin_ledger_entries", "fin_entry_account_transaction_ix"):
        op.create_index(
            "fin_entry_account_transaction_ix",
            "fin_ledger_entries",
            ["account_id", "transaction_id"],
            unique=False,
        )

    # --- fin_manual_payments ---
    if table_missing("fin_manual_payments"):
        op.create_table(
            "fin_manual_payments",
            sa.Column("id", sa.BigInteger(), nullable=False),
            sa.Column("tenant_id", sa.Integer(), nullable=False),
            sa.Column("operation_id", sa.String(length=128), nullable=False),
            sa.Column("amount_kopecks", sa.BigInteger(), nullable=False),
            sa.Column("received_on", sa.Date(), nullable=False),
            sa.Column("document_number", sa.String(length=128), nullable=False),
            sa.Column("purpose", sa.Text(), nullable=False),
            sa.Column("payer", sa.String(length=500), nullable=False),
            sa.Column("comment", sa.Text(), nullable=True),
            sa.Column("evidence_reference", sa.Text(), nullable=True),
            sa.Column("created_by_user_id", sa.Integer(), nullable=False),
            sa.Column("ledger_transaction_id", sa.BigInteger(), nullable=False),
            sa.Column("correlation_id", sa.String(length=128), nullable=False),
            sa.Column(
                "created_at",
                sa.DateTime(timezone=True),
                server_default=sa.text("now()"),
                nullable=False,
            ),
            sa.CheckConstraint(
                "amount_kopecks > 0 AND amount_kopecks % 100 = 0",
                name="fin_manual_amount_ck",
            ),
            sa.ForeignKeyConstraint(
                ["created_by_user_id"],
                ["users.id"],
                name="fin_manual_creator_fk",
                ondelete="RESTRICT",
            ),
            sa.ForeignKeyConstraint(
                ["ledger_transaction_id", "tenant_id"],
                ["fin_ledger_transactions.id", "fin_ledger_transactions.tenant_id"],
                name="fin_manual_transaction_fk",
                ondelete="RESTRICT",
            ),
            sa.PrimaryKeyConstraint("id", name="fin_manual_pk"),
            sa.UniqueConstraint(
                "ledger_transaction_id", name="fin_manual_transaction_uq"
            ),
            sa.UniqueConstraint(
                "tenant_id", "operation_id", name="fin_manual_operation_uq"
            ),
        )

    if index_missing("fin_manual_payments", "fin_manual_tenant_date_ix"):
        op.create_index(
            "fin_manual_tenant_date_ix",
            "fin_manual_payments",
            ["tenant_id", "received_on"],
            unique=False,
        )

    # --- fin_payments ---
    if table_missing("fin_payments"):
        op.create_table(
            "fin_payments",
            sa.Column("id", sa.BigInteger(), nullable=False),
            sa.Column("tenant_id", sa.Integer(), nullable=False),
            sa.Column("operation_id", sa.String(length=128), nullable=False),
            sa.Column(
                "provider",
                sa.String(length=32),
                server_default="yookassa",
                nullable=False,
            ),
            sa.Column("provider_payment_id", sa.String(length=128), nullable=True),
            sa.Column(
                "status", sa.String(length=32), server_default="pending", nullable=False
            ),
            sa.Column("amount_kopecks", sa.BigInteger(), nullable=False),
            sa.Column(
                "currency", sa.String(length=3), server_default="RUB", nullable=False
            ),
            sa.Column("confirmation_url", sa.Text(), nullable=True),
            sa.Column("provider_receipt_id", sa.String(length=128), nullable=True),
            sa.Column("receipt_status", sa.String(length=32), nullable=True),
            sa.Column("receipt_snapshot", sa.JSON(), nullable=True),
            sa.Column("verified_at", sa.DateTime(timezone=True), nullable=True),
            sa.Column("succeeded_at", sa.DateTime(timezone=True), nullable=True),
            sa.Column("ledger_transaction_id", sa.BigInteger(), nullable=True),
            sa.Column("actor", sa.String(length=128), nullable=False),
            sa.Column("correlation_id", sa.String(length=128), nullable=False),
            sa.Column(
                "created_at",
                sa.DateTime(timezone=True),
                server_default=sa.text("now()"),
                nullable=False,
            ),
            sa.CheckConstraint(
                "amount_kopecks > 0 AND amount_kopecks % 100 = 0 AND currency = 'RUB'",
                name="fin_payment_amount_ck",
            ),
            sa.CheckConstraint(
                "ledger_transaction_id IS NULL OR status = 'succeeded'",
                name="fin_payment_posting_ck",
            ),
            sa.CheckConstraint(
                "status != 'succeeded' OR (verified_at IS NOT NULL AND succeeded_at IS NOT NULL AND provider_payment_id IS NOT NULL)",
                name="fin_payment_verified_ck",
            ),
            sa.CheckConstraint(
                "status IN ('pending', 'waiting_for_capture', 'succeeded', 'canceled')",
                name="fin_payment_status_ck",
            ),
            sa.ForeignKeyConstraint(
                ["ledger_transaction_id", "tenant_id"],
                ["fin_ledger_transactions.id", "fin_ledger_transactions.tenant_id"],
                name="fin_payment_transaction_fk",
                ondelete="RESTRICT",
            ),
            sa.ForeignKeyConstraint(
                ["tenant_id"],
                ["orgs.org_id"],
                name="fin_payment_tenant_fk",
                ondelete="RESTRICT",
            ),
            sa.PrimaryKeyConstraint("id", name="fin_payment_pk"),
            sa.UniqueConstraint(
                "ledger_transaction_id", name="fin_payment_transaction_uq"
            ),
            sa.UniqueConstraint(
                "provider", "provider_payment_id", name="fin_payment_provider_uq"
            ),
            sa.UniqueConstraint(
                "tenant_id", "operation_id", name="fin_payment_operation_uq"
            ),
        )

    if index_missing("fin_payments", "fin_payment_status_time_ix"):
        op.create_index(
            "fin_payment_status_time_ix",
            "fin_payments",
            ["status", "created_at"],
            unique=False,
        )

    if index_missing("fin_payments", "fin_payment_tenant_time_ix"):
        op.create_index(
            "fin_payment_tenant_time_ix",
            "fin_payments",
            ["tenant_id", "created_at"],
            unique=False,
        )

    # --- l4desk_memberships ---
    if table_missing("l4desk_memberships"):
        op.create_table(
            "l4desk_memberships",
            sa.Column("tenant_id", sa.Integer(), nullable=False),
            sa.Column("user_id", sa.Integer(), nullable=False),
            sa.Column("role_id", sa.Integer(), server_default="5", nullable=False),
            sa.Column("is_owner", sa.Boolean(), server_default="false", nullable=False),
            sa.Column(
                "created_at",
                sa.DateTime(timezone=True),
                server_default=sa.text("now()"),
                nullable=False,
            ),
            sa.CheckConstraint("role_id = 5", name="l4desk_membership_role_ck"),
            sa.ForeignKeyConstraint(
                ["tenant_id"], ["orgs.org_id"], ondelete="RESTRICT"
            ),
            sa.ForeignKeyConstraint(["user_id"], ["users.id"], ondelete="RESTRICT"),
            sa.PrimaryKeyConstraint("tenant_id", "user_id"),
        )

    if index_missing("l4desk_memberships", "l4desk_membership_user_ix"):
        op.create_index(
            "l4desk_membership_user_ix", "l4desk_memberships", ["user_id"], unique=False
        )

    if index_missing("l4desk_memberships", "l4desk_membership_owner_uq"):
        op.create_index(
            "l4desk_membership_owner_uq",
            "l4desk_memberships",
            ["tenant_id"],
            unique=True,
            postgresql_where=sa.text("is_owner"),
            sqlite_where=sa.text("is_owner"),
        )

    # --- l4desk_registrations ---
    if table_missing("l4desk_registrations"):
        op.create_table(
            "l4desk_registrations",
            sa.Column("id", sa.BigInteger(), nullable=False),
            sa.Column("email_normalized", sa.String(length=255), nullable=False),
            sa.Column("password_hash", sa.String(length=255), nullable=False),
            sa.Column("token_hash", sa.String(length=64), nullable=False),
            sa.Column("terms_version", sa.String(length=64), nullable=False),
            sa.Column("timezone", sa.String(length=64), nullable=False),
            sa.Column("source", sa.String(length=128), nullable=True),
            sa.Column("correlation_id", sa.String(length=128), nullable=False),
            sa.Column(
                "created_at",
                sa.DateTime(timezone=True),
                server_default=sa.text("now()"),
                nullable=False,
            ),
            sa.Column("expires_at", sa.DateTime(timezone=True), nullable=False),
            sa.Column("consumed_at", sa.DateTime(timezone=True), nullable=True),
            sa.Column("user_id", sa.Integer(), nullable=True),
            sa.Column("tenant_id", sa.Integer(), nullable=True),
            sa.CheckConstraint(
                "consumed_at IS NULL OR consumed_at >= created_at",
                name="l4desk_registration_consumed_ck",
            ),
            sa.CheckConstraint(
                "expires_at > created_at", name="l4desk_registration_expiry_ck"
            ),
            sa.ForeignKeyConstraint(
                ["tenant_id"], ["orgs.org_id"], ondelete="SET NULL"
            ),
            sa.ForeignKeyConstraint(["user_id"], ["users.id"], ondelete="SET NULL"),
            sa.PrimaryKeyConstraint("id"),
            sa.UniqueConstraint("email_normalized"),
            sa.UniqueConstraint("token_hash"),
        )

    if index_missing("l4desk_registrations", "l4desk_registration_expiry_ix"):
        op.create_index(
            "l4desk_registration_expiry_ix",
            "l4desk_registrations",
            ["expires_at"],
            unique=False,
        )

    # --- l4desk_terminals ---
    if table_missing("l4desk_terminals"):
        op.create_table(
            "l4desk_terminals",
            sa.Column("terminal_id", sa.Integer(), autoincrement=False, nullable=False),
            sa.Column("tenant_id", sa.Integer(), nullable=False),
            sa.Column("runtime_terminal_id", sa.Integer(), nullable=True),
            sa.Column("ordinal", sa.BigInteger(), nullable=False),
            sa.Column("sn", sa.String(length=128), nullable=False),
            sa.Column("external_terminal_id", sa.String(length=128), nullable=False),
            sa.Column("device_id", sa.Integer(), nullable=True),
            sa.Column("operation_id", sa.String(length=128), nullable=False),
            sa.Column("correlation_id", sa.String(length=128), nullable=False),
            sa.Column(
                "provisioning_state",
                sa.String(length=32),
                server_default="pending",
                nullable=False,
            ),
            sa.Column(
                "pin_state",
                sa.String(length=32),
                server_default="pending",
                nullable=False,
            ),
            sa.Column("certificate_reference", sa.String(length=128), nullable=True),
            sa.Column("last_error", sa.String(length=500), nullable=True),
            sa.Column("first_online_at", sa.DateTime(timezone=True), nullable=True),
            sa.Column("last_online_at", sa.DateTime(timezone=True), nullable=True),
            sa.Column(
                "created_at",
                sa.DateTime(timezone=True),
                server_default=sa.text("now()"),
                nullable=False,
            ),
            sa.Column("deleted_at", sa.DateTime(timezone=True), nullable=True),
            sa.CheckConstraint(
                "pin_state IN ('pending', 'issued', 'consumed', 'expired', 'failed')",
                name="l4desk_terminal_pin_ck",
            ),
            sa.CheckConstraint(
                "provisioning_state IN ('pending', 'ready', 'failed')",
                name="l4desk_terminal_provisioning_ck",
            ),
            sa.CheckConstraint("ordinal > 0", name="l4desk_terminal_ordinal_ck"),
            sa.ForeignKeyConstraint(
                ["runtime_terminal_id"], ["terminals.id"], ondelete="SET NULL"
            ),
            sa.ForeignKeyConstraint(
                ["tenant_id"], ["orgs.org_id"], ondelete="RESTRICT"
            ),
            sa.PrimaryKeyConstraint("terminal_id"),
            sa.UniqueConstraint("operation_id"),
            sa.UniqueConstraint("runtime_terminal_id"),
            sa.UniqueConstraint(
                "tenant_id", "external_terminal_id", name="l4desk_terminal_external_uq"
            ),
            sa.UniqueConstraint(
                "tenant_id", "ordinal", name="l4desk_terminal_ordinal_uq"
            ),
            sa.UniqueConstraint(
                "terminal_id", "tenant_id", name="l4desk_terminal_tenant_uq"
            ),
        )

    if index_missing("l4desk_terminals", "l4desk_terminal_correlation_ix"):
        op.create_index(
            "l4desk_terminal_correlation_ix",
            "l4desk_terminals",
            ["correlation_id"],
            unique=False,
        )

    # --- fin_billing_cycles ---
    if table_missing("fin_billing_cycles"):
        op.create_table(
            "fin_billing_cycles",
            sa.Column("id", sa.BigInteger(), nullable=False),
            sa.Column("tenant_id", sa.Integer(), nullable=False),
            sa.Column("sequence", sa.Integer(), nullable=False),
            sa.Column("starts_at", sa.DateTime(timezone=True), nullable=False),
            sa.Column("ends_at", sa.DateTime(timezone=True), nullable=False),
            sa.Column("grace_deadline", sa.DateTime(timezone=True), nullable=False),
            sa.Column("timezone", sa.String(length=64), nullable=False),
            sa.Column("closed_at", sa.DateTime(timezone=True), nullable=True),
            sa.CheckConstraint("sequence >= 0", name="fin_cycle_sequence_ck"),
            sa.CheckConstraint(
                "starts_at < grace_deadline AND grace_deadline < ends_at",
                name="fin_cycle_interval_ck",
            ),
            sa.ForeignKeyConstraint(
                ["tenant_id"],
                ["fin_billing_profiles.tenant_id"],
                name="fin_cycle_profile_fk",
                ondelete="RESTRICT",
            ),
            sa.PrimaryKeyConstraint("id", name="fin_cycle_pk"),
            sa.UniqueConstraint("id", "tenant_id", name="fin_cycle_tenant_uq"),
            sa.UniqueConstraint("tenant_id", "sequence", name="fin_cycle_sequence_uq"),
            sa.UniqueConstraint("tenant_id", "starts_at", name="fin_cycle_start_uq"),
        )

    if index_missing("fin_billing_cycles", "fin_cycle_end_ix"):
        op.create_index(
            "fin_cycle_end_ix", "fin_billing_cycles", ["ends_at"], unique=False
        )

    # --- fin_usage_daily ---
    if table_missing("fin_usage_daily"):
        op.create_table(
            "fin_usage_daily",
            sa.Column("id", sa.BigInteger(), nullable=False),
            sa.Column("tenant_id", sa.Integer(), nullable=False),
            sa.Column("terminal_id", sa.Integer(), nullable=False),
            sa.Column("local_date", sa.Date(), nullable=False),
            sa.Column("timezone", sa.String(length=64), nullable=False),
            sa.Column("tariff_version_id", sa.BigInteger(), nullable=False),
            sa.Column("source_seconds", sa.BigInteger(), nullable=False),
            sa.Column("video_seconds", sa.BigInteger(), nullable=False),
            sa.Column("console_seconds", sa.BigInteger(), nullable=False),
            sa.Column("free_seconds", sa.BigInteger(), nullable=False),
            sa.Column("billable_seconds", sa.BigInteger(), nullable=False),
            sa.Column("rounded_billable_hours", sa.Integer(), nullable=False),
            sa.Column("rate_kopecks", sa.BigInteger(), nullable=False),
            sa.Column("calculated_kopecks", sa.BigInteger(), nullable=False),
            sa.Column("posted_kopecks", sa.BigInteger(), nullable=False),
            sa.Column("discarded_kopecks", sa.BigInteger(), nullable=False),
            sa.Column("source_project", sa.String(length=64), nullable=False),
            sa.Column("source_event_id", sa.String(length=128), nullable=True),
            sa.Column("source_events_hash", sa.String(length=64), nullable=False),
            sa.Column("archive_batch_id", sa.String(length=128), nullable=True),
            sa.Column("ledger_transaction_id", sa.BigInteger(), nullable=True),
            sa.Column("actor", sa.String(length=128), nullable=False),
            sa.Column("correlation_id", sa.String(length=128), nullable=False),
            sa.Column(
                "created_at",
                sa.DateTime(timezone=True),
                server_default=sa.text("now()"),
                nullable=False,
            ),
            sa.Column("posted_at", sa.DateTime(timezone=True), nullable=True),
            sa.CheckConstraint(
                "(posted_at IS NULL) = (ledger_transaction_id IS NULL)",
                name="fin_usage_posted_ck",
            ),
            sa.CheckConstraint(
                "posted_kopecks >= 0 AND calculated_kopecks = posted_kopecks + discarded_kopecks AND discarded_kopecks >= 0 AND discarded_kopecks < 100 AND posted_kopecks % 100 = 0",
                name="fin_usage_rounding_ck",
            ),
            sa.CheckConstraint(
                "source_seconds = video_seconds + console_seconds AND source_seconds = free_seconds + billable_seconds",
                name="fin_usage_seconds_ck",
            ),
            sa.CheckConstraint(
                "video_seconds >= 0 AND console_seconds >= 0 AND free_seconds >= 0 AND billable_seconds >= 0 AND rounded_billable_hours >= 0 AND rate_kopecks >= 0",
                name="fin_usage_values_ck",
            ),
            sa.ForeignKeyConstraint(
                ["ledger_transaction_id", "tenant_id"],
                ["fin_ledger_transactions.id", "fin_ledger_transactions.tenant_id"],
                name="fin_usage_transaction_fk",
                ondelete="RESTRICT",
            ),
            sa.ForeignKeyConstraint(
                ["tariff_version_id"],
                ["fin_tariff_versions.id"],
                name="fin_usage_tariff_fk",
                ondelete="RESTRICT",
            ),
            sa.ForeignKeyConstraint(
                ["terminal_id", "tenant_id"],
                ["l4desk_terminals.terminal_id", "l4desk_terminals.tenant_id"],
                name="fin_usage_terminal_fk",
                ondelete="RESTRICT",
            ),
            sa.PrimaryKeyConstraint("id", name="fin_usage_pk"),
            sa.UniqueConstraint(
                "ledger_transaction_id", name="fin_usage_transaction_uq"
            ),
            sa.UniqueConstraint("terminal_id", "local_date", name="fin_usage_day_uq"),
        )

    if index_missing("fin_usage_daily", "fin_usage_pending_ix"):
        op.create_index(
            "fin_usage_pending_ix",
            "fin_usage_daily",
            ["local_date"],
            unique=False,
            postgresql_where=sa.text("posted_at IS NULL"),
            sqlite_where=sa.text("posted_at IS NULL"),
        )

    if index_missing("fin_usage_daily", "fin_usage_tenant_date_ix"):
        op.create_index(
            "fin_usage_tenant_date_ix",
            "fin_usage_daily",
            ["tenant_id", "local_date"],
            unique=False,
        )

    # --- l4desk_remote_sessions ---
    if table_missing("l4desk_remote_sessions"):
        op.create_table(
            "l4desk_remote_sessions",
            sa.Column("id", sa.BigInteger(), nullable=False),
            sa.Column("tenant_id", sa.Integer(), nullable=False),
            sa.Column("terminal_id", sa.Integer(), nullable=False),
            sa.Column("operation_id", sa.String(length=128), nullable=False),
            sa.Column("correlation_id", sa.String(length=128), nullable=False),
            sa.Column("provider_session_id", sa.String(length=128), nullable=True),
            sa.Column("requested_by_user_id", sa.Integer(), nullable=True),
            sa.Column("session_type", sa.String(length=32), nullable=False),
            sa.Column(
                "state", sa.String(length=32), server_default="reserved", nullable=False
            ),
            sa.Column(
                "requested_at",
                sa.DateTime(timezone=True),
                server_default=sa.text("now()"),
                nullable=False,
            ),
            sa.Column("active_at", sa.DateTime(timezone=True), nullable=True),
            sa.Column("closed_at", sa.DateTime(timezone=True), nullable=True),
            sa.Column("reason", sa.String(length=128), nullable=True),
            sa.Column("last_event_id", sa.String(length=128), nullable=True),
            sa.Column(
                "last_cursor", sa.BigInteger(), server_default="0", nullable=False
            ),
            sa.Column("source_events_hash", sa.String(length=64), nullable=True),
            sa.CheckConstraint(
                "session_type IN ('console', 'video')", name="l4desk_session_type_ck"
            ),
            sa.CheckConstraint(
                "state != 'active' OR active_at IS NOT NULL",
                name="l4desk_session_active_ck",
            ),
            sa.CheckConstraint(
                "state IN ('reserved', 'start_requested', 'active', 'stop_requested', 'closed', 'failed')",
                name="l4desk_session_state_ck",
            ),
            sa.CheckConstraint(
                "state NOT IN ('closed', 'failed') OR closed_at IS NOT NULL",
                name="l4desk_session_closed_ck",
            ),
            sa.CheckConstraint(
                "closed_at IS NULL OR active_at IS NULL OR closed_at >= active_at",
                name="l4desk_session_interval_ck",
            ),
            sa.CheckConstraint("last_cursor >= 0", name="l4desk_session_cursor_ck"),
            sa.ForeignKeyConstraint(
                ["requested_by_user_id"], ["users.id"], ondelete="SET NULL"
            ),
            sa.ForeignKeyConstraint(
                ["terminal_id", "tenant_id"],
                ["l4desk_terminals.terminal_id", "l4desk_terminals.tenant_id"],
                name="l4desk_session_terminal_fk",
                ondelete="RESTRICT",
            ),
            sa.PrimaryKeyConstraint("id"),
            sa.UniqueConstraint("provider_session_id"),
            sa.UniqueConstraint(
                "tenant_id", "operation_id", name="l4desk_session_operation_uq"
            ),
        )

    if index_missing("l4desk_remote_sessions", "l4desk_session_tenant_time_ix"):
        op.create_index(
            "l4desk_session_tenant_time_ix",
            "l4desk_remote_sessions",
            ["tenant_id", "requested_at"],
            unique=False,
        )

    if index_missing("l4desk_remote_sessions", "l4desk_session_correlation_ix"):
        op.create_index(
            "l4desk_session_correlation_ix",
            "l4desk_remote_sessions",
            ["correlation_id"],
            unique=False,
        )

    if index_missing("l4desk_remote_sessions", "l4desk_session_reservation_uq"):
        op.create_index(
            "l4desk_session_reservation_uq",
            "l4desk_remote_sessions",
            ["terminal_id"],
            unique=True,
            postgresql_where=sa.text(
                "state IN ('reserved', 'start_requested', 'active', 'stop_requested')"
            ),
            sqlite_where=sa.text(
                "state IN ('reserved', 'start_requested', 'active', 'stop_requested')"
            ),
        )

    if index_missing("l4desk_remote_sessions", "l4desk_session_state_ix"):
        op.create_index(
            "l4desk_session_state_ix", "l4desk_remote_sessions", ["state"], unique=False
        )

    # --- fin_notification_deliveries ---
    if table_missing("fin_notification_deliveries"):
        op.create_table(
            "fin_notification_deliveries",
            sa.Column("id", sa.BigInteger(), nullable=False),
            sa.Column("tenant_id", sa.Integer(), nullable=False),
            sa.Column("billing_cycle_id", sa.BigInteger(), nullable=False),
            sa.Column("notification_type", sa.String(length=32), nullable=False),
            sa.Column("scheduled_at", sa.DateTime(timezone=True), nullable=False),
            sa.Column(
                "status", sa.String(length=16), server_default="pending", nullable=False
            ),
            sa.Column("attempts", sa.Integer(), server_default="0", nullable=False),
            sa.Column("sent_at", sa.DateTime(timezone=True), nullable=True),
            sa.Column("provider_message_id", sa.String(length=128), nullable=True),
            sa.Column("last_error", sa.String(length=500), nullable=True),
            sa.Column("correlation_id", sa.String(length=128), nullable=False),
            sa.CheckConstraint(
                "notification_type IN ('cycle_minus_7', 'cycle_minus_3', 'cycle_minus_1', 'grace', 'blocked')",
                name="fin_notification_type_ck",
            ),
            sa.CheckConstraint(
                "status != 'sent' OR sent_at IS NOT NULL",
                name="fin_notification_sent_ck",
            ),
            sa.CheckConstraint(
                "status IN ('pending', 'sending', 'sent', 'failed') AND attempts >= 0",
                name="fin_notification_status_ck",
            ),
            sa.ForeignKeyConstraint(
                ["billing_cycle_id", "tenant_id"],
                ["fin_billing_cycles.id", "fin_billing_cycles.tenant_id"],
                name="fin_notification_cycle_fk",
                ondelete="RESTRICT",
            ),
            sa.PrimaryKeyConstraint("id", name="fin_notification_pk"),
            sa.UniqueConstraint(
                "tenant_id",
                "billing_cycle_id",
                "notification_type",
                name="fin_notification_cycle_type_uq",
            ),
        )

    if index_missing("fin_notification_deliveries", "fin_notification_due_ix"):
        op.create_index(
            "fin_notification_due_ix",
            "fin_notification_deliveries",
            ["status", "scheduled_at"],
            unique=False,
        )

    # --- fin_terminal_monthly_charges ---
    if table_missing("fin_terminal_monthly_charges"):
        op.create_table(
            "fin_terminal_monthly_charges",
            sa.Column("id", sa.BigInteger(), nullable=False),
            sa.Column("tenant_id", sa.Integer(), nullable=False),
            sa.Column("terminal_id", sa.Integer(), nullable=False),
            sa.Column("billing_cycle_id", sa.BigInteger(), nullable=False),
            sa.Column("tariff_version_id", sa.BigInteger(), nullable=False),
            sa.Column("is_free", sa.Boolean(), nullable=False),
            sa.Column("first_online_at", sa.DateTime(timezone=True), nullable=False),
            sa.Column("calculated_kopecks", sa.BigInteger(), nullable=False),
            sa.Column("posted_kopecks", sa.BigInteger(), nullable=False),
            sa.Column("discarded_kopecks", sa.BigInteger(), nullable=False),
            sa.Column("source_project", sa.String(length=64), nullable=False),
            sa.Column("source_event_id", sa.String(length=128), nullable=False),
            sa.Column("source_events_hash", sa.String(length=64), nullable=False),
            sa.Column("archive_batch_id", sa.String(length=128), nullable=True),
            sa.Column("ledger_transaction_id", sa.BigInteger(), nullable=True),
            sa.Column("actor", sa.String(length=128), nullable=False),
            sa.Column("correlation_id", sa.String(length=128), nullable=False),
            sa.Column(
                "created_at",
                sa.DateTime(timezone=True),
                server_default=sa.text("now()"),
                nullable=False,
            ),
            sa.Column("posted_at", sa.DateTime(timezone=True), nullable=True),
            sa.CheckConstraint(
                "(posted_at IS NULL) = (ledger_transaction_id IS NULL)",
                name="fin_monthly_posted_ck",
            ),
            sa.CheckConstraint(
                "NOT is_free OR calculated_kopecks = 0", name="fin_monthly_free_ck"
            ),
            sa.CheckConstraint(
                "posted_kopecks >= 0 AND calculated_kopecks = posted_kopecks + discarded_kopecks AND discarded_kopecks >= 0 AND discarded_kopecks < 100 AND posted_kopecks % 100 = 0",
                name="fin_monthly_rounding_ck",
            ),
            sa.ForeignKeyConstraint(
                ["billing_cycle_id", "tenant_id"],
                ["fin_billing_cycles.id", "fin_billing_cycles.tenant_id"],
                name="fin_monthly_cycle_fk",
                ondelete="RESTRICT",
            ),
            sa.ForeignKeyConstraint(
                ["ledger_transaction_id", "tenant_id"],
                ["fin_ledger_transactions.id", "fin_ledger_transactions.tenant_id"],
                name="fin_monthly_transaction_fk",
                ondelete="RESTRICT",
            ),
            sa.ForeignKeyConstraint(
                ["tariff_version_id"],
                ["fin_tariff_versions.id"],
                name="fin_monthly_tariff_fk",
                ondelete="RESTRICT",
            ),
            sa.ForeignKeyConstraint(
                ["terminal_id", "tenant_id"],
                ["l4desk_terminals.terminal_id", "l4desk_terminals.tenant_id"],
                name="fin_monthly_terminal_fk",
                ondelete="RESTRICT",
            ),
            sa.PrimaryKeyConstraint("id", name="fin_monthly_pk"),
            sa.UniqueConstraint(
                "ledger_transaction_id", name="fin_monthly_transaction_uq"
            ),
            sa.UniqueConstraint(
                "terminal_id", "billing_cycle_id", name="fin_monthly_terminal_cycle_uq"
            ),
        )

    if index_missing("fin_terminal_monthly_charges", "fin_monthly_tenant_cycle_ix"):
        op.create_index(
            "fin_monthly_tenant_cycle_ix",
            "fin_terminal_monthly_charges",
            ["tenant_id", "billing_cycle_id"],
            unique=False,
        )


def downgrade() -> None:
    bind = op.get_bind()
    is_offline = bool(getattr(op.get_context(), "as_sql", False))
    inspector = sa.inspect(bind) if not is_offline else None
    existing_tables = (
        set(inspector.get_table_names()) if inspector is not None else set()
    )

    def index_present(tname: str, iname: str) -> bool:
        if is_offline:
            return True
        if inspector is None or tname not in existing_tables:
            return False
        with contextlib.suppress(Exception):
            return iname in {idx["name"] for idx in inspector.get_indexes(tname)}
        return False

    def table_present(name: str) -> bool:
        return is_offline or name in existing_tables

    # --- fin_terminal_monthly_charges ---
    if index_present("fin_terminal_monthly_charges", "fin_monthly_tenant_cycle_ix"):
        op.drop_index(
            "fin_monthly_tenant_cycle_ix", table_name="fin_terminal_monthly_charges"
        )
    if table_present("fin_terminal_monthly_charges"):
        op.drop_table("fin_terminal_monthly_charges")

    # --- fin_notification_deliveries ---
    if index_present("fin_notification_deliveries", "fin_notification_due_ix"):
        op.drop_index(
            "fin_notification_due_ix", table_name="fin_notification_deliveries"
        )
    if table_present("fin_notification_deliveries"):
        op.drop_table("fin_notification_deliveries")

    # --- l4desk_remote_sessions ---
    if index_present("l4desk_remote_sessions", "l4desk_session_state_ix"):
        op.drop_index("l4desk_session_state_ix", table_name="l4desk_remote_sessions")
    if index_present("l4desk_remote_sessions", "l4desk_session_reservation_uq"):
        op.drop_index(
            "l4desk_session_reservation_uq", table_name="l4desk_remote_sessions"
        )
    if index_present("l4desk_remote_sessions", "l4desk_session_correlation_ix"):
        op.drop_index(
            "l4desk_session_correlation_ix", table_name="l4desk_remote_sessions"
        )
    if index_present("l4desk_remote_sessions", "l4desk_session_tenant_time_ix"):
        op.drop_index(
            "l4desk_session_tenant_time_ix", table_name="l4desk_remote_sessions"
        )
    if table_present("l4desk_remote_sessions"):
        op.drop_table("l4desk_remote_sessions")

    # --- fin_usage_daily ---
    if index_present("fin_usage_daily", "fin_usage_tenant_date_ix"):
        op.drop_index("fin_usage_tenant_date_ix", table_name="fin_usage_daily")
    if index_present("fin_usage_daily", "fin_usage_pending_ix"):
        op.drop_index("fin_usage_pending_ix", table_name="fin_usage_daily")
    if table_present("fin_usage_daily"):
        op.drop_table("fin_usage_daily")

    # --- fin_billing_cycles ---
    if index_present("fin_billing_cycles", "fin_cycle_end_ix"):
        op.drop_index("fin_cycle_end_ix", table_name="fin_billing_cycles")
    if table_present("fin_billing_cycles"):
        op.drop_table("fin_billing_cycles")

    # --- l4desk_terminals ---
    if index_present("l4desk_terminals", "l4desk_terminal_correlation_ix"):
        op.drop_index("l4desk_terminal_correlation_ix", table_name="l4desk_terminals")
    if table_present("l4desk_terminals"):
        op.drop_table("l4desk_terminals")

    # --- l4desk_registrations ---
    if index_present("l4desk_registrations", "l4desk_registration_expiry_ix"):
        op.drop_index(
            "l4desk_registration_expiry_ix", table_name="l4desk_registrations"
        )
    if table_present("l4desk_registrations"):
        op.drop_table("l4desk_registrations")

    # --- l4desk_memberships ---
    if index_present("l4desk_memberships", "l4desk_membership_owner_uq"):
        op.drop_index("l4desk_membership_owner_uq", table_name="l4desk_memberships")
    if index_present("l4desk_memberships", "l4desk_membership_user_ix"):
        op.drop_index("l4desk_membership_user_ix", table_name="l4desk_memberships")
    if table_present("l4desk_memberships"):
        op.drop_table("l4desk_memberships")

    # --- fin_payments ---
    if index_present("fin_payments", "fin_payment_tenant_time_ix"):
        op.drop_index("fin_payment_tenant_time_ix", table_name="fin_payments")
    if index_present("fin_payments", "fin_payment_status_time_ix"):
        op.drop_index("fin_payment_status_time_ix", table_name="fin_payments")
    if table_present("fin_payments"):
        op.drop_table("fin_payments")

    # --- fin_manual_payments ---
    if index_present("fin_manual_payments", "fin_manual_tenant_date_ix"):
        op.drop_index("fin_manual_tenant_date_ix", table_name="fin_manual_payments")
    if table_present("fin_manual_payments"):
        op.drop_table("fin_manual_payments")

    # --- fin_ledger_entries ---
    if index_present("fin_ledger_entries", "fin_entry_account_transaction_ix"):
        op.drop_index(
            "fin_entry_account_transaction_ix", table_name="fin_ledger_entries"
        )
    if table_present("fin_ledger_entries"):
        op.drop_table("fin_ledger_entries")

    # --- fin_billing_profiles ---
    if table_present("fin_billing_profiles"):
        op.drop_table("fin_billing_profiles")

    # --- fin_balance_projections ---
    if table_present("fin_balance_projections"):
        op.drop_table("fin_balance_projections")

    # --- l4desk_tenant_profiles ---
    if table_present("l4desk_tenant_profiles"):
        op.drop_table("l4desk_tenant_profiles")

    # --- l4desk_audit_events ---
    if index_present("l4desk_audit_events", "l4desk_audit_correlation_ix"):
        op.drop_index("l4desk_audit_correlation_ix", table_name="l4desk_audit_events")
    if index_present("l4desk_audit_events", "l4desk_audit_tenant_time_ix"):
        op.drop_index("l4desk_audit_tenant_time_ix", table_name="l4desk_audit_events")
    if table_present("l4desk_audit_events"):
        op.drop_table("l4desk_audit_events")

    # --- fin_reconciliation_runs ---
    if index_present("fin_reconciliation_runs", "fin_reconciliation_tenant_time_ix"):
        op.drop_index(
            "fin_reconciliation_tenant_time_ix", table_name="fin_reconciliation_runs"
        )
    if table_present("fin_reconciliation_runs"):
        op.drop_table("fin_reconciliation_runs")

    # --- fin_ledger_transactions ---
    if index_present("fin_ledger_transactions", "fin_transaction_correlation_ix"):
        op.drop_index(
            "fin_transaction_correlation_ix", table_name="fin_ledger_transactions"
        )
    if index_present("fin_ledger_transactions", "fin_transaction_tenant_time_ix"):
        op.drop_index(
            "fin_transaction_tenant_time_ix", table_name="fin_ledger_transactions"
        )
    if table_present("fin_ledger_transactions"):
        op.drop_table("fin_ledger_transactions")

    # --- fin_accounts ---
    if index_present("fin_accounts", "fin_account_global_kind_uq"):
        op.drop_index("fin_account_global_kind_uq", table_name="fin_accounts")
    if table_present("fin_accounts"):
        op.drop_table("fin_accounts")

    # --- iot_event_quarantine ---
    if index_present("iot_event_quarantine", "ix_iot_event_quarantine_event_id"):
        op.drop_index(
            "ix_iot_event_quarantine_event_id", table_name="iot_event_quarantine"
        )
    if table_present("iot_event_quarantine"):
        op.drop_table("iot_event_quarantine")

    # --- iot_event_inbox ---
    if index_present("iot_event_inbox", "ix_iot_event_inbox_session_id"):
        op.drop_index("ix_iot_event_inbox_session_id", table_name="iot_event_inbox")
    if index_present("iot_event_inbox", "ix_iot_event_inbox_sn"):
        op.drop_index("ix_iot_event_inbox_sn", table_name="iot_event_inbox")
    if index_present("iot_event_inbox", "ix_iot_event_inbox_operation_id"):
        op.drop_index("ix_iot_event_inbox_operation_id", table_name="iot_event_inbox")
    if index_present("iot_event_inbox", "ix_iot_event_inbox_cursor"):
        op.drop_index("ix_iot_event_inbox_cursor", table_name="iot_event_inbox")
    if index_present("iot_event_inbox", "ix_iot_event_inbox_tenant_id"):
        op.drop_index("ix_iot_event_inbox_tenant_id", table_name="iot_event_inbox")
    if index_present("iot_event_inbox", "ix_iot_event_inbox_event_type"):
        op.drop_index("ix_iot_event_inbox_event_type", table_name="iot_event_inbox")
    if table_present("iot_event_inbox"):
        op.drop_table("iot_event_inbox")

    # --- iot_consumer_checkpoints ---
    if table_present("iot_consumer_checkpoints"):
        op.drop_table("iot_consumer_checkpoints")

    # --- fin_tariff_versions ---
    if table_present("fin_tariff_versions"):
        op.drop_table("fin_tariff_versions")

    # --- fin_archive_batches ---
    if index_present("fin_archive_batches", "fin_archive_project_month_ix"):
        op.drop_index("fin_archive_project_month_ix", table_name="fin_archive_batches")
    if table_present("fin_archive_batches"):
        op.drop_table("fin_archive_batches")
