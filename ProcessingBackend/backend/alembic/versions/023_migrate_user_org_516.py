"""Migrate user for org=516 from legacy MSSQL to PostgreSQL.

Revision ID: 023
Revises: 022
Create Date: 2026-09-04 19:30:00.000000
"""

from collections.abc import Sequence

import sqlalchemy as sa

from alembic import op

# revision identifiers, used by Alembic.
revision: str = "023"
down_revision: str | None = "022"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None

USER_516 = {
    "id": 633,
    "org_id": 516,
    "username": "admin516",
    "md5_password": "2a0f0fe8a02fd6df4afc878a28302fe8",
    "role_id": 3,
    "role": "user",
    "is_superuser": False,
    "is_active": True,
    "full_name": "admin516",
    "last_org_id": 516,
}


def upgrade() -> None:
    conn = op.get_bind()

    # 1. Ensure target organization 516 exists in orgs table
    conn.execute(
        sa.text(
            "INSERT INTO orgs (org_id, org_name, name, status, is_active) "
            "VALUES (:org_id, :name, :name, 1, TRUE) "
            "ON CONFLICT (org_id) DO NOTHING"
        ),
        {"org_id": USER_516["org_id"], "name": f"Организация {USER_516['org_id']}"},
    )

    # 2. Insert or update user from legacy MSSQL
    conn.execute(
        sa.text(
            "INSERT INTO users (id, org_id, username, md5_password, role_id, role, is_superuser, is_active, full_name, last_org_id) "
            "VALUES (:id, :org_id, :username, :md5_password, :role_id, :role, :is_superuser, :is_active, :full_name, :last_org_id) "
            "ON CONFLICT (id) DO UPDATE SET "
            "  username = EXCLUDED.username, "
            "  md5_password = EXCLUDED.md5_password, "
            "  org_id = EXCLUDED.org_id, "
            "  role_id = EXCLUDED.role_id, "
            "  role = EXCLUDED.role, "
            "  is_superuser = EXCLUDED.is_superuser, "
            "  is_active = EXCLUDED.is_active, "
            "  full_name = EXCLUDED.full_name, "
            "  last_org_id = EXCLUDED.last_org_id"
        ),
        USER_516,
    )

    # 3. Synchronize serial sequence to max(id)
    if conn.dialect.name == "postgresql":
        conn.execute(
            sa.text(
                "SELECT setval(pg_get_serial_sequence('users', 'id'), COALESCE((SELECT max(id) FROM users), 1));"
            )
        )


def downgrade() -> None:
    conn = op.get_bind()
    conn.execute(
        sa.text("DELETE FROM users WHERE id = :id AND username = :username"),
        {"id": USER_516["id"], "username": USER_516["username"]},
    )
