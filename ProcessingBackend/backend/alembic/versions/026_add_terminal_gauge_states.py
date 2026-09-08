"""Add terminal_gauge_states table.

Revision ID: 026
Revises: 025
Create Date: 2026-09-08
"""

from collections.abc import Sequence

import sqlalchemy as sa
from sqlalchemy.dialects import postgresql

from alembic import op

revision: str = "026"
down_revision: str | None = "025"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    op.create_table(
        "terminal_gauge_states",
        sa.Column("device_id", sa.Integer(), nullable=False),
        sa.Column("sn", sa.String(length=100), nullable=True),
        sa.Column(
            "updated_at",
            sa.DateTime(timezone=True),
            nullable=False,
        ),
        sa.Column(
            "last_tick_epoch",
            sa.Integer(),
            nullable=False,
            server_default=sa.text("0"),
        ),
        sa.Column(
            "slots_bitmask",
            sa.Integer(),
            nullable=False,
            server_default=sa.text("0"),
        ),
        sa.Column(
            "gauge_data",
            postgresql.JSONB(astext_type=sa.Text()),
            nullable=False,
            server_default=sa.text("'{}'::jsonb"),
        ),
        sa.Column("last_payment_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("last_inkass_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("license_expires_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column(
            "internal_enrichment",
            postgresql.JSONB(astext_type=sa.Text()),
            nullable=False,
            server_default=sa.text("'{}'::jsonb"),
        ),
        sa.Column(
            "created_at",
            sa.DateTime(timezone=True),
            nullable=False,
            server_default=sa.func.now(),
        ),
        sa.PrimaryKeyConstraint("device_id", name="pk_terminal_gauge_states"),
        sa.ForeignKeyConstraint(
            ["device_id"],
            ["terminals.device_id"],
            name="fk_terminal_gauge_states_device_id_terminals",
            ondelete="CASCADE",
        ),
    )
    op.create_index(
        "ix_terminal_gauge_states_sn",
        "terminal_gauge_states",
        ["sn"],
        unique=False,
    )
    op.create_index(
        "ix_terminal_gauge_states_updated_at",
        "terminal_gauge_states",
        ["updated_at"],
    )


def downgrade() -> None:
    op.drop_index(
        "ix_terminal_gauge_states_updated_at", table_name="terminal_gauge_states"
    )
    op.drop_index("ix_terminal_gauge_states_sn", table_name="terminal_gauge_states")
    op.drop_table("terminal_gauge_states")
