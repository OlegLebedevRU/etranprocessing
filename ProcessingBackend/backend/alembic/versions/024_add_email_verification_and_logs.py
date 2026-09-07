"""Add email verification, email logs, and org contact flags.

Revision ID: 024
Revises: 023
Create Date: 2026-09-07
"""

from collections.abc import Sequence

import sqlalchemy as sa

from alembic import op

revision: str = "024"
down_revision: str | None = "023"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    # 1. Add send_reports, is_email_verified, email_verified_at to orgs
    op.add_column(
        "orgs",
        sa.Column(
            "send_reports",
            sa.Boolean(),
            nullable=False,
            server_default=sa.text("true"),
        ),
    )
    op.add_column(
        "orgs",
        sa.Column(
            "is_email_verified",
            sa.Boolean(),
            nullable=False,
            server_default=sa.text("false"),
        ),
    )
    op.add_column(
        "orgs",
        sa.Column(
            "email_verified_at",
            sa.DateTime(timezone=True),
            nullable=True,
        ),
    )

    # 2. Create email_verifications table
    op.create_table(
        "email_verifications",
        sa.Column("id", sa.Integer(), autoincrement=True, nullable=False),
        sa.Column("org_id", sa.Integer(), nullable=False),
        sa.Column("user_id", sa.Integer(), nullable=True),
        sa.Column("email", sa.String(length=255), nullable=False),
        sa.Column("token_hash", sa.String(length=64), nullable=False),
        sa.Column("otp_code", sa.String(length=6), nullable=False),
        sa.Column(
            "attempts_left",
            sa.Integer(),
            nullable=False,
            server_default=sa.text("5"),
        ),
        sa.Column(
            "expires_at",
            sa.DateTime(timezone=True),
            nullable=False,
        ),
        sa.Column(
            "is_used",
            sa.Boolean(),
            nullable=False,
            server_default=sa.text("false"),
        ),
        sa.Column(
            "created_at",
            sa.DateTime(timezone=True),
            nullable=False,
            server_default=sa.text("now()"),
        ),
        sa.ForeignKeyConstraint(
            ["org_id"],
            ["orgs.org_id"],
            name="fk_email_verif_org",
            ondelete="CASCADE",
        ),
        sa.ForeignKeyConstraint(
            ["user_id"],
            ["users.id"],
            name="fk_email_verif_user",
            ondelete="SET NULL",
        ),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("token_hash"),
    )
    op.create_index("idx_email_verif_token", "email_verifications", ["token_hash"])
    op.create_index("idx_email_verif_org", "email_verifications", ["org_id"])

    # 3. Create email_logs table
    op.create_table(
        "email_logs",
        sa.Column("id", sa.Integer(), autoincrement=True, nullable=False),
        sa.Column("org_id", sa.Integer(), nullable=True),
        sa.Column("user_id", sa.Integer(), nullable=True),
        sa.Column("device_id", sa.String(length=128), nullable=False),
        sa.Column("recipients", sa.String(length=1000), nullable=False),
        sa.Column("subject", sa.String(length=500), nullable=True),
        sa.Column(
            "status",
            sa.String(length=50),
            nullable=False,
            server_default=sa.text("'sent'"),
        ),
        sa.Column("postbox_message_id", sa.String(length=255), nullable=True),
        sa.Column("storage_path", sa.String(length=1000), nullable=True),
        sa.Column("error_message", sa.Text(), nullable=True),
        sa.Column(
            "created_at",
            sa.DateTime(timezone=True),
            nullable=False,
            server_default=sa.text("now()"),
        ),
        sa.ForeignKeyConstraint(
            ["org_id"],
            ["orgs.org_id"],
            name="fk_email_logs_org",
            ondelete="SET NULL",
        ),
        sa.ForeignKeyConstraint(
            ["user_id"],
            ["users.id"],
            name="fk_email_logs_user",
            ondelete="SET NULL",
        ),
        sa.PrimaryKeyConstraint("id"),
    )
    op.create_index("idx_email_logs_org_id", "email_logs", ["org_id"])
    op.create_index("idx_email_logs_device_id", "email_logs", ["device_id"])
    op.create_index("idx_email_logs_created_at", "email_logs", ["created_at"])


def downgrade() -> None:
    op.drop_table("email_logs")
    op.drop_table("email_verifications")
    op.drop_column("orgs", "email_verified_at")
    op.drop_column("orgs", "is_email_verified")
    op.drop_column("orgs", "send_reports")
