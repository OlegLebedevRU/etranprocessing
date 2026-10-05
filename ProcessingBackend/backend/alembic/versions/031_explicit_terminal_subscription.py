"""Explicit commercial enrollment; technical session rows do not enroll a terminal."""

import sqlalchemy as sa

from alembic import op

revision = "031"
down_revision = "030"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.add_column(
        "terminals",
        sa.Column("l4desk_subscription_enabled", sa.Boolean(), nullable=False, server_default=sa.false()),
    )


def downgrade() -> None:
    raise RuntimeError("Preserve explicit enrollments; roll back with compatible readers")
