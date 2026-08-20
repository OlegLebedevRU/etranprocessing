"""Add terminal_cert_discovery accumulator table.

Revision ID: 009
Revises: 008
Create Date: 2026-08-20
"""

from collections.abc import Sequence

import sqlalchemy as sa

from alembic import op

revision: str = "009"
down_revision: str | None = "008"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    op.create_table(
        "terminal_cert_discovery",
        sa.Column("id", sa.Integer(), primary_key=True, autoincrement=True),
        sa.Column("sn", sa.String(100), nullable=True),
        sa.Column("cert_serial", sa.String(100), nullable=True),
        sa.Column("cert_dn", sa.String(500), nullable=True),
        sa.Column("ou", sa.String(50), nullable=True),
        sa.Column("o", sa.String(50), nullable=True),
        sa.Column(
            "is_valid",
            sa.Boolean(),
            nullable=False,
            server_default=sa.text("false"),
        ),
        sa.Column(
            "validation_status",
            sa.String(50),
            nullable=False,
            server_default="unknown",
        ),
        sa.Column(
            "terminal_id",
            sa.Integer(),
            sa.ForeignKey("terminals.id", ondelete="SET NULL"),
            nullable=True,
        ),
        sa.Column("db_cert_serial", sa.String(100), nullable=True),
        sa.Column(
            "request_count",
            sa.Integer(),
            nullable=False,
            server_default="1",
        ),
        sa.Column("last_endpoint", sa.String(100), nullable=True),
        sa.Column("client_ip", sa.String(50), nullable=True),
        sa.Column(
            "first_seen_at",
            sa.DateTime(timezone=True),
            server_default=sa.text("now()"),
            nullable=False,
        ),
        sa.Column(
            "last_seen_at",
            sa.DateTime(timezone=True),
            server_default=sa.text("now()"),
            nullable=False,
        ),
    )
    op.create_index(
        "idx_terminal_cert_discovery_sn",
        "terminal_cert_discovery",
        ["sn"],
    )
    op.create_index(
        "idx_terminal_cert_discovery_serial",
        "terminal_cert_discovery",
        ["cert_serial"],
    )
    op.create_index(
        "idx_terminal_cert_discovery_sn_serial",
        "terminal_cert_discovery",
        ["sn", "cert_serial"],
    )
    op.create_index(
        "idx_terminal_cert_discovery_status",
        "terminal_cert_discovery",
        ["validation_status"],
    )
    op.create_index(
        "idx_terminal_cert_discovery_last_seen",
        "terminal_cert_discovery",
        ["last_seen_at"],
    )


def downgrade() -> None:
    op.drop_table("terminal_cert_discovery")
