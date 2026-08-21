"""Add email, phone, notify_by_email to orgs and billing options to org_billing_settings.

Revision ID: 012
Revises: 011
Create Date: 2026-08-21
"""

from collections.abc import Sequence

from alembic import op
import sqlalchemy as sa

revision: str = "012"
down_revision: str | None = "011"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    # 1. Add contact columns to orgs
    op.add_column("orgs", sa.Column("email", sa.String(255), nullable=True))
    op.add_column("orgs", sa.Column("phone", sa.String(50), nullable=True))
    op.add_column(
        "orgs",
        sa.Column(
            "notify_by_email",
            sa.Boolean(),
            nullable=False,
            server_default=sa.text("true"),
        ),
    )

    # 2. Add billing mode & calculation settings to org_billing_settings
    op.add_column(
        "org_billing_settings",
        sa.Column(
            "billing_mode",
            sa.String(30),
            nullable=False,
            server_default=sa.text("'standard'"),
        ),
    )
    op.add_column(
        "org_billing_settings",
        sa.Column(
            "min_billing_periods",
            sa.Integer(),
            nullable=False,
            server_default=sa.text("1"),
        ),
    )
    op.add_column(
        "org_billing_settings",
        sa.Column("allowed_billing_periods", sa.String(50), nullable=True),
    )
    op.add_column(
        "org_billing_settings",
        sa.Column(
            "default_selection_mode",
            sa.String(30),
            nullable=False,
            server_default=sa.text("'all_due'"),
        ),
    )


def downgrade() -> None:
    op.drop_column("org_billing_settings", "default_selection_mode")
    op.drop_column("org_billing_settings", "allowed_billing_periods")
    op.drop_column("org_billing_settings", "min_billing_periods")
    op.drop_column("org_billing_settings", "billing_mode")
    op.drop_column("orgs", "notify_by_email")
    op.drop_column("orgs", "phone")
    op.drop_column("orgs", "email")
