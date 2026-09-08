"""Add active_org_id to user_sessions and last_org_id to users.

Revision ID: 021
Revises: 020
Create Date: 2026-09-03
"""

from collections.abc import Sequence

import sqlalchemy as sa

from alembic import op

# revision identifiers, used by Alembic.
revision: str = "021"
down_revision: str | None = "020"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    # 1. Add last_org_id column to users
    op.add_column(
        "users",
        sa.Column(
            "last_org_id",
            sa.Integer(),
            nullable=True,
        ),
    )

    # 2. Add active_org_id column to user_sessions
    op.add_column(
        "user_sessions",
        sa.Column(
            "active_org_id",
            sa.Integer(),
            nullable=True,
        ),
    )

    # 3. Add index on user_sessions.active_org_id
    op.create_index(
        "idx_user_sessions_active_org_id",
        "user_sessions",
        ["active_org_id"],
        unique=False,
    )


def downgrade() -> None:
    op.drop_index("idx_user_sessions_active_org_id", table_name="user_sessions")
    op.drop_column("user_sessions", "active_org_id")
    op.drop_column("users", "last_org_id")
