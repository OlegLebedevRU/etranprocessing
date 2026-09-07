"""Add permissions JSONB column, GIN index, and role 4 tenant constraint to users.

Revision ID: 025
Revises: 024
Create Date: 2026-09-07
"""

from collections.abc import Sequence

import sqlalchemy as sa
from sqlalchemy.dialects import postgresql

from alembic import op

revision: str = "025"
down_revision: str | None = "024"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    # 1. Add permissions column
    op.add_column(
        "users",
        sa.Column(
            "permissions",
            postgresql.JSONB(astext_type=sa.Text()),
            nullable=False,
            server_default=sa.text("'[]'::jsonb"),
            comment="Список строковых разрешений пользователя (актуально для role_id=4)",
        ),
    )

    # 2. Create GIN index on permissions
    op.create_index(
        "idx_users_permissions",
        "users",
        ["permissions"],
        unique=False,
        postgresql_using="gin",
    )

    # 3. Create check constraint for role 4
    op.create_check_constraint(
        "chk_users_role4_tenant",
        "users",
        "role_id != 4 OR (org_id IS NOT NULL AND org_id > 0)",
    )


def downgrade() -> None:
    op.drop_constraint("chk_users_role4_tenant", "users", type_="check")
    op.drop_index("idx_users_permissions", table_name="users", postgresql_using="gin")
    op.drop_column("users", "permissions")
