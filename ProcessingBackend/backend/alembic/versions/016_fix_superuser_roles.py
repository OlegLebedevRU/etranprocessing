"""Fix user roles so only o.lebedev is superuser.

Revision ID: 016
Revises: 015
Create Date: 2026-08-27 01:40:00.000000
"""

from collections.abc import Sequence

import sqlalchemy as sa

from alembic import op

# revision identifiers, used by Alembic.
revision: str = "016"
down_revision: str | None = "015"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    conn = op.get_bind()
    conn.execute(
        sa.text(
            "UPDATE users "
            "SET is_superuser = FALSE, role = 'user', role_id = 3 "
            "WHERE username != 'o.lebedev'"
        )
    )
    conn.execute(
        sa.text(
            "UPDATE users "
            "SET is_superuser = TRUE, role = 'superuser', role_id = 1 "
            "WHERE username = 'o.lebedev'"
        )
    )


def downgrade() -> None:
    pass
