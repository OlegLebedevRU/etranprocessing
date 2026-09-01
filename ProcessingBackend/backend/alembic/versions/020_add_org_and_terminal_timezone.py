"""Add timezone column to orgs and terminals tables.

Revision ID: 020
Revises: 019
Create Date: 2026-09-01
"""

from collections.abc import Sequence

import sqlalchemy as sa

from alembic import op

# revision identifiers, used by Alembic.
revision: str = "020"
down_revision: str | None = "019"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    # 1. Add timezone column to orgs with default 'Europe/Moscow'
    op.add_column(
        "orgs",
        sa.Column(
            "timezone",
            sa.String(64),
            server_default="Europe/Moscow",
            nullable=False,
        ),
    )

    # 2. Add optional timezone override column to terminals
    op.add_column(
        "terminals",
        sa.Column(
            "timezone",
            sa.String(64),
            nullable=True,
        ),
    )

    # 3. Set timezone for known existing organizations
    conn = op.get_bind()
    conn.execute(
        sa.text("UPDATE orgs SET timezone = 'Asia/Yekaterinburg' WHERE org_id = 424")
    )


def downgrade() -> None:
    op.drop_column("terminals", "timezone")
    op.drop_column("orgs", "timezone")
