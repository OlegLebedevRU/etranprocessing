"""Add certificate_pins table

Revision ID: 002
Revises: 001
Create Date: 2026-08-17

"""

from collections.abc import Sequence

import sqlalchemy as sa

from alembic import op

# revision identifiers, used by Alembic.
revision: str = "002"
down_revision: str | None = "001"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    op.create_table(
        "certificate_pins",
        sa.Column("id", sa.Integer(), autoincrement=True, nullable=False),
        sa.Column("pin", sa.String(10), nullable=False),
        sa.Column("terminal_id", sa.Integer(), nullable=False),
        sa.Column("status", sa.String(20), nullable=False, server_default="pending"),
        sa.Column(
            "created_at", sa.DateTime(timezone=True), server_default=sa.text("now()")
        ),
        sa.Column("used_at", sa.DateTime(timezone=True), nullable=True),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("pin"),
        sa.ForeignKeyConstraint(["terminal_id"], ["terminals.id"]),
    )
    op.create_index("idx_cert_pins_terminal", "certificate_pins", ["terminal_id"])
    op.create_index("idx_cert_pins_status", "certificate_pins", ["status"])


def downgrade() -> None:
    op.drop_table("certificate_pins")
