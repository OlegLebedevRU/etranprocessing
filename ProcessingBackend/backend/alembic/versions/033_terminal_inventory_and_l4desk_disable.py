"""Persistent event75 inventory and independent L4Desk user disable.

Revision ID: 033
Revises: 032
"""

import sqlalchemy as sa
from sqlalchemy.dialects import postgresql

from alembic import op

revision = "033"
down_revision = "032"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.execute("SET LOCAL lock_timeout = '5s'")
    op.add_column(
        "l4desk_terminals", sa.Column("user_disabled_at", sa.DateTime(timezone=True))
    )
    op.create_table(
        "terminal_tools_inventory",
        sa.Column(
            "terminal_id",
            sa.Integer(),
            sa.ForeignKey("terminals.id", ondelete="CASCADE"),
            primary_key=True,
        ),
        sa.Column("tenant_id", sa.Integer(), nullable=False),
        sa.Column("sn", sa.String(128), nullable=False),
        sa.Column("cert_thumbprint", sa.String(40), nullable=False),
        sa.Column("cert_serial", sa.String(40), nullable=False),
        sa.Column("cert_expires_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("payload_sha256", sa.String(64), nullable=False),
        sa.Column("suite_version", sa.String(64), nullable=False),
        sa.Column("components", postgresql.JSONB(), nullable=False),
        sa.Column("event_uuid", sa.Uuid(), nullable=False),
        sa.Column("source_revision", sa.BigInteger(), nullable=False),
        sa.Column("published_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("received_at", sa.DateTime(timezone=True), nullable=False),
    )
    op.create_index(
        "ix_terminal_tools_inventory_tenant_id",
        "terminal_tools_inventory",
        ["tenant_id"],
    )
    op.create_table(
        "terminal_tools_inventory_inbox",
        sa.Column("event_uuid", sa.Uuid(), primary_key=True),
        sa.Column("tenant_id", sa.Integer(), nullable=False),
        sa.Column(
            "terminal_id",
            sa.Integer(),
            sa.ForeignKey("terminals.id", ondelete="CASCADE"),
            nullable=False,
        ),
        sa.Column("payload_sha256", sa.String(64), nullable=False),
        sa.Column("source_revision", sa.BigInteger(), nullable=False),
        sa.Column("outcome", sa.String(24), nullable=False),
        sa.Column("received_at", sa.DateTime(timezone=True), nullable=False),
    )
    op.create_index(
        "ix_terminal_tools_inventory_inbox_tenant_id",
        "terminal_tools_inventory_inbox",
        ["tenant_id"],
    )
    op.create_index(
        "ix_terminal_tools_inventory_inbox_received_at",
        "terminal_tools_inventory_inbox",
        ["received_at"],
    )


def downgrade() -> None:
    op.drop_table("terminal_tools_inventory_inbox")
    op.drop_table("terminal_tools_inventory")
    op.drop_column("l4desk_terminals", "user_disabled_at")
