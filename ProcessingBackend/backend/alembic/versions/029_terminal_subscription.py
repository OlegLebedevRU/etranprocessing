"""Add the durable terminal subscription expiry; preserve historical financial tables."""

import sqlalchemy as sa

from alembic import op

revision = "029"
down_revision = "028"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.add_column(
        "l4desk_terminals",
        sa.Column("paid_until", sa.DateTime(timezone=True), nullable=True),
    )


def downgrade() -> None:
    raise RuntimeError(
        "Subscription rights must be preserved; use a compatible application rollback"
    )
