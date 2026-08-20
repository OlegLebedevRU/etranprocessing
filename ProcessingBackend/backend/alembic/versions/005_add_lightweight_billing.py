"""Add lightweight billing tables and extend licenses.

Revision ID: 005
Revises: 004
Create Date: 2026-08-18
"""

import os
from collections.abc import Sequence

import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import UUID as PG_UUID

from alembic import op

revision: str = "005"
down_revision: str | None = "004"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    # --- org_billing_settings ---
    op.create_table(
        "org_billing_settings",
        sa.Column("org_id", sa.Integer(), nullable=False),
        sa.Column("monthly_price_minor", sa.BigInteger(), nullable=False),
        sa.Column("currency", sa.String(3), server_default="RUB", nullable=False),
        sa.Column(
            "created_at",
            sa.DateTime(timezone=True),
            server_default=sa.text("now()"),
            nullable=True,
        ),
        sa.Column(
            "updated_at",
            sa.DateTime(timezone=True),
            server_default=sa.text("now()"),
            nullable=True,
        ),
        sa.PrimaryKeyConstraint("org_id"),
        sa.CheckConstraint(
            "monthly_price_minor >= 0", name="ck_org_billing_price_non_negative"
        ),
    )

    # --- billing_orders ---
    op.create_table(
        "billing_orders",
        sa.Column("id", PG_UUID(as_uuid=True), nullable=False),
        sa.Column("org_id", sa.Integer(), nullable=False),
        sa.Column("status", sa.String(20), server_default="pending", nullable=False),
        sa.Column("currency", sa.String(3), server_default="RUB", nullable=False),
        sa.Column("amount_minor", sa.BigInteger(), nullable=False),
        sa.Column("provider", sa.String(50), nullable=True),
        sa.Column("provider_order_id", sa.String(200), nullable=True),
        sa.Column("payment_url", sa.Text(), nullable=True),
        sa.Column(
            "created_at",
            sa.DateTime(timezone=True),
            server_default=sa.text("now()"),
            nullable=True,
        ),
        sa.Column("paid_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("expires_at", sa.DateTime(timezone=True), nullable=True),
        sa.PrimaryKeyConstraint("id"),
    )
    op.create_index("idx_billing_orders_org_id", "billing_orders", ["org_id"])
    op.create_index("idx_billing_orders_status", "billing_orders", ["status"])

    # --- billing_order_items ---
    op.create_table(
        "billing_order_items",
        sa.Column("id", sa.Integer(), autoincrement=True, nullable=False),
        sa.Column("order_id", PG_UUID(as_uuid=True), nullable=False),
        sa.Column("terminal_id", sa.Integer(), nullable=False),
        sa.Column("operation", sa.String(20), nullable=False),
        sa.Column("periods_due", sa.Integer(), nullable=False),
        sa.Column("advance_periods", sa.Integer(), nullable=False),
        sa.Column("billing_period_months", sa.Integer(), nullable=False),
        sa.Column("monthly_price_minor", sa.BigInteger(), nullable=False),
        sa.Column("amount_minor", sa.BigInteger(), nullable=False),
        sa.Column("old_expires_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("new_expires_at", sa.DateTime(timezone=True), nullable=False),
        sa.PrimaryKeyConstraint("id"),
        sa.ForeignKeyConstraint(["order_id"], ["billing_orders.id"]),
        sa.ForeignKeyConstraint(["terminal_id"], ["terminals.id"]),
    )
    op.create_index(
        "idx_billing_order_items_order_id", "billing_order_items", ["order_id"]
    )
    op.create_index(
        "idx_billing_order_items_terminal_id", "billing_order_items", ["terminal_id"]
    )

    # --- extend licenses table ---
    # Use server_default for existing rows, then remove default
    op.add_column(
        "licenses",
        sa.Column(
            "billing_period_months",
            sa.SmallInteger(),
            server_default="1",
            nullable=False,
        ),
    )
    op.add_column(
        "licenses",
        sa.Column("monthly_price_override_minor", sa.BigInteger(), nullable=True),
    )
    op.add_column(
        "licenses",
        sa.Column(
            "renewal_enabled", sa.Boolean(), server_default="true", nullable=False
        ),
    )
    op.add_column(
        "licenses",
        sa.Column(
            "deactivation_requested_at", sa.DateTime(timezone=True), nullable=True
        ),
    )

    # --- integrity constraints ---
    op.create_check_constraint(
        "ck_license_price_non_negative",
        "licenses",
        "monthly_price_override_minor >= 0",
    )
    # H4 fix: prevent billing_period_months <= 0 (infinite loop DoS)
    op.create_check_constraint(
        "ck_billing_period_months_positive",
        "licenses",
        "billing_period_months > 0",
    )

    # --- partial unique index: at most one active license per terminal ---
    op.execute(
        "CREATE UNIQUE INDEX uq_active_license ON licenses (terminal_id) WHERE is_active = true"
    )

    # --- backfill org_billing_settings for existing organizations ---
    # Default price from env var, or 3000 RUB (300000 kopecks) if not set
    default_price = os.environ.get("BILLING_DEFAULT_MONTHLY_PRICE_MINOR", "300000")
    op.execute(
        f"INSERT INTO org_billing_settings (org_id, monthly_price_minor, currency) "
        f"SELECT DISTINCT org_id, {default_price}, 'RUB' FROM terminals "
        f"WHERE org_id NOT IN (SELECT org_id FROM org_billing_settings) "
        f"ON CONFLICT (org_id) DO NOTHING"
    )


def downgrade() -> None:
    op.execute("DROP INDEX IF EXISTS uq_active_license")
    op.drop_constraint("ck_billing_period_months_positive", "licenses", type_="check")
    op.drop_constraint("ck_license_price_non_negative", "licenses", type_="check")
    op.drop_column("licenses", "deactivation_requested_at")
    op.drop_column("licenses", "renewal_enabled")
    op.drop_column("licenses", "monthly_price_override_minor")
    op.drop_column("licenses", "billing_period_months")

    op.drop_table("billing_order_items")
    op.drop_table("billing_orders")
    op.drop_table("org_billing_settings")
