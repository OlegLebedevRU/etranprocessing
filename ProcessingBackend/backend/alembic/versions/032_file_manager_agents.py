"""Register authenticated file-manager agent capabilities.

Revision ID: 032
Revises: 031
"""

import sqlalchemy as sa
from sqlalchemy.dialects import postgresql

from alembic import op

revision = "032"
down_revision = "031"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.create_table(
        "fm_agents",
        sa.Column("terminal_id", sa.Integer(), nullable=False),
        sa.Column("tenant_id", sa.Integer(), nullable=False),
        sa.Column("agent_instance_id", sa.Uuid(), nullable=False),
        sa.Column("agent_version", sa.String(64), nullable=False),
        sa.Column("protocol_version", sa.Integer(), nullable=False),
        sa.Column("capabilities", postgresql.JSONB(), nullable=False),
        sa.Column("filesystem_ready", sa.Boolean(), nullable=False),
        sa.Column("cert_serial", sa.String(100), nullable=False),
        sa.Column("last_seen_at", sa.DateTime(timezone=True), nullable=False),
        sa.ForeignKeyConstraint(["terminal_id"], ["terminals.id"], ondelete="CASCADE"),
        sa.PrimaryKeyConstraint("terminal_id"),
    )
    op.create_index("ix_fm_agents_tenant_id", "fm_agents", ["tenant_id"])
    op.create_table(
        "fm_operations",
        sa.Column("id", sa.Uuid(), primary_key=True),
        sa.Column(
            "terminal_id", sa.Integer(), sa.ForeignKey("terminals.id"), nullable=False
        ),
        sa.Column("tenant_id", sa.Integer(), nullable=False),
        sa.Column("lease_id", sa.Uuid(), nullable=False),
        sa.Column("owner_user_id", sa.String(128), nullable=False),
        sa.Column("owner_session_id", sa.String(128), nullable=False),
        sa.Column("owner_role", sa.String(32), nullable=False),
        sa.Column("kind", sa.String(16), nullable=False),
        sa.Column("state", sa.String(24), nullable=False),
        sa.Column("path", sa.Text(), nullable=False),
        sa.Column("size_bytes", sa.BigInteger()),
        sa.Column("sha256", sa.String(64)),
        sa.Column("data", postgresql.JSONB(), nullable=False),
        sa.Column("error_code", sa.String(64)),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("updated_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("expires_at", sa.DateTime(timezone=True), nullable=False),
        sa.CheckConstraint(
            "kind IN ('session', 'list', 'upload', 'download')",
            name="ck_fm_operation_kind",
        ),
        sa.CheckConstraint(
            "state IN ('created', 'active', 'running', 'verifying', 'committing', 'completed', 'failed', 'cancelled', 'cancelling')",
            name="ck_fm_operation_state",
        ),
        sa.CheckConstraint(
            "size_bytes IS NULL OR size_bytes BETWEEN 0 AND 67108864",
            name="ck_fm_operation_size",
        ),
    )
    op.create_index(
        "ix_fm_operation_tenant_device",
        "fm_operations",
        ["tenant_id", "terminal_id", "created_at"],
    )
    op.create_index(
        "uq_fm_active_transfer",
        "fm_operations",
        ["terminal_id"],
        unique=True,
        postgresql_where=sa.text(
            "kind IN ('upload', 'download') AND state IN ('created', 'running', 'verifying', 'committing', 'cancelling')"
        ),
    )


def downgrade() -> None:
    op.drop_table("fm_operations")
    op.drop_index("ix_fm_agents_tenant_id", table_name="fm_agents")
    op.drop_table("fm_agents")
