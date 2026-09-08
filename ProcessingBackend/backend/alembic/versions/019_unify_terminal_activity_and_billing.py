"""Unify terminal activity and billing status model.

Migrate terminal active state based on license renewal and expiration, and drop
deprecated columns (is_active, renewal_enabled, deactivation_requested_at) from licenses.

Revision ID: 019
Revises: 018
Create Date: 2026-08-31
"""

from collections.abc import Sequence

import sqlalchemy as sa

from alembic import op

# revision identifiers, used by Alembic.
revision: str = "019"
down_revision: str | None = "018"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    conn = op.get_bind()

    # 1. Ensure any terminal with renewal_enabled = false and expired license is marked is_active = false
    conn.execute(
        sa.text("""
            UPDATE terminals
            SET is_active = false
            WHERE id IN (
                SELECT terminal_id
                FROM licenses
                WHERE renewal_enabled = false AND expires_at <= NOW()
            )
        """)
    )

    # 2. Drop deprecated columns from licenses table
    op.drop_column("licenses", "deactivation_requested_at")
    op.drop_column("licenses", "renewal_enabled")
    op.drop_column("licenses", "is_active")


def downgrade() -> None:
    op.add_column(
        "licenses",
        sa.Column("is_active", sa.Boolean(), server_default="true", nullable=False),
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
            "deactivation_requested_at",
            sa.DateTime(timezone=True),
            nullable=True,
        ),
    )
