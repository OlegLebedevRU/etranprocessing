"""Bounded demand-driven FM readiness; additive and backward compatible."""

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects import postgresql

revision = "035"
down_revision = "034"
branch_labels = None
depends_on = None


def upgrade():
    op.create_table(
        "fm_readiness_probes",
        sa.Column("terminal_id", sa.Integer(), sa.ForeignKey("terminals.id", ondelete="CASCADE"), primary_key=True),
        sa.Column("request_id", sa.Uuid(), nullable=False, unique=True),
        sa.Column("tenant_id", sa.Integer(), nullable=False),
        sa.Column("actor", postgresql.JSONB(), nullable=False),
        sa.Column("cert_serial", sa.String(100), nullable=False),
        sa.Column("expires_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("proof_until", sa.DateTime(timezone=True)),
        sa.Column("agent_instance_id", sa.Uuid()),
        sa.Column("response_hash", sa.String(64)),
        sa.Column("result", postgresql.JSONB()),
        sa.Column("consumed_by", sa.Uuid()),
    )


def downgrade():
    op.drop_table("fm_readiness_probes")
