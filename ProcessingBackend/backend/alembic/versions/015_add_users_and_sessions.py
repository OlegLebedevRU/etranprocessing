"""Add users and user_sessions tables, seed initial users from scratch.txt.

Revision ID: 015
Revises: 014
Create Date: 2026-08-26 23:30:00.000000
"""

from collections.abc import Sequence

import sqlalchemy as sa

from alembic import op

# revision identifiers, used by Alembic.
revision: str = "015"
down_revision: str | None = "014"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None

INITIAL_USERS = [
    # (id, org_id, username, md5_password, role_id, role, is_superuser)
    (532, 402, "ukrublevski", "5b033587d9ea87cbacf10addf76cce55", 3, "user", False),
    (548, 424, "Snoxin", "33c513816eac4acc7036abfe5d3b8d83", 3, "user", False),
    (555, 434, "admin434", "f57a79ab18470d77a34179583f359937", 3, "user", False),
    (556, 429, "adminag", "2a0f0fe8a02fd6df4afc878a28302fe8", 3, "user", False),
    (558, 435, "admin435", "2a0f0fe8a02fd6df4afc878a28302fe8", 3, "user", False),
    (562, 339, "spback339", "202cb962ac59075b964b07152d234b70", 3, "user", False),
    (563, 339, "violanta", "18d7c74fbbd34e40e1b530b987aaa870", 3, "user", False),
    (564, 442, "Admin", "8d86f13b96fa5ec689f192a8a4e20551", 3, "user", False),
    (567, 445, "admin445", "fde8d29e34ca732edc108a02766e8d69", 3, "user", False),
    (571, 450, "admin450", "2a0f0fe8a02fd6df4afc878a28302fe8", 3, "user", False),
    (574, 453, "admin453", "ec7f66b91c1f7f03e7542353536e1bc5", 3, "user", False),
    (581, 457, "admin457", "2a0f0fe8a02fd6df4afc878a28302fe8", 3, "user", False),
    (586, 457, "export457", "22bd39cf4f0cb73d33ab426715a627f0", 3, "user", False),
    (588, 462, "admin462", "2a0f0fe8a02fd6df4afc878a28302fe8", 3, "user", False),
    (589, 463, "admin463", "2a0f0fe8a02fd6df4afc878a28302fe8", 3, "user", False),
    (598, 472, "admin472", "b9c1dc658a2ac422d818040e088b5e68", 3, "user", False),
    (599, 475, "admin475", "2a0f0fe8a02fd6df4afc878a28302fe8", 3, "user", False),
    (600, 476, "admin476", "2a0f0fe8a02fd6df4afc878a28302fe8", 3, "user", False),
    (601, 477, "admin477", "2a0f0fe8a02fd6df4afc878a28302fe8", 3, "user", False),
    (607, 486, "admin486", "37e0201102cd253f90c621810c776e4b", 3, "user", False),
    (610, 488, "migron", "2a0f0fe8a02fd6df4afc878a28302fe8", 3, "user", False),
    (611, 491, "admin491", "2a0f0fe8a02fd6df4afc878a28302fe8", 3, "user", False),
    (613, 492, "admin492", "2a0f0fe8a02fd6df4afc878a28302fe8", 3, "user", False),
    (614, 493, "uraltest", "2a0f0fe8a02fd6df4afc878a28302fe8", 3, "user", False),
    (616, 495, "admin495", "21c9ba65e7ba06e4560ca00cc45ec40d", 3, "user", False),
    (617, 496, "admin496", "e9f01d6f7116433f0bf90276bf69447f", 3, "user", False),
    (618, 498, "rekmaster", "2a0f0fe8a02fd6df4afc878a28302fe8", 3, "user", False),
    (620, 500, "admin500", "2a0f0fe8a02fd6df4afc878a28302fe8", 3, "user", False),
    (621, 501, "admin501", "2a0f0fe8a02fd6df4afc878a28302fe8", 3, "user", False),
    (632, 355, "report355", "59bd41b103b2828f4d976433f3bb5bc1", 3, "user", False),
    (637, 519, "admin519", "2a0f0fe8a02fd6df4afc878a28302fe8", 3, "user", False),
    (638, 521, "admin521", "2a0f0fe8a02fd6df4afc878a28302fe8", 3, "user", False),
    (639, 522, "admin522", "557a17f5bc24d4bbab2cfd7b016a8317", 3, "user", False),
    (641, 498, "rekmasterexport", "088a276f7a45fcaa960dcc05832654d7", 3, "user", False),
    (643, 302, "admin302", "4dfa04db43189aab8903b186681da815", 3, "user", False),
    (1, 1, "o.lebedev", "eaf21fcabcffeb1f97f01a4fc02ece63", 1, "superuser", True),
]


def upgrade() -> None:
    # 1. Create users table
    op.create_table(
        "users",
        sa.Column("id", sa.Integer(), autoincrement=True, nullable=False),
        sa.Column("username", sa.String(length=100), nullable=False),
        sa.Column("md5_password", sa.String(length=64), nullable=False),
        sa.Column("org_id", sa.Integer(), nullable=True),
        sa.Column("role_id", sa.Integer(), nullable=False, server_default=sa.text("3")),
        sa.Column("role", sa.String(length=30), nullable=False, server_default="user"),
        sa.Column(
            "is_active",
            sa.Boolean(),
            nullable=False,
            server_default=sa.text("true"),
        ),
        sa.Column(
            "is_superuser",
            sa.Boolean(),
            nullable=False,
            server_default=sa.text("false"),
        ),
        sa.Column("full_name", sa.String(length=255), nullable=True),
        sa.Column(
            "created_at",
            sa.DateTime(timezone=True),
            server_default=sa.func.now(),
            nullable=False,
        ),
        sa.Column(
            "updated_at",
            sa.DateTime(timezone=True),
            server_default=sa.func.now(),
            nullable=False,
        ),
        sa.ForeignKeyConstraint(["org_id"], ["orgs.org_id"], ondelete="SET NULL"),
        sa.PrimaryKeyConstraint("id"),
    )
    op.create_index("idx_users_username", "users", ["username"], unique=True)
    op.create_index("idx_users_org_id", "users", ["org_id"], unique=False)

    # 2. Create user_sessions table
    op.create_table(
        "user_sessions",
        sa.Column("id", sa.Integer(), autoincrement=True, nullable=False),
        sa.Column("user_id", sa.Integer(), nullable=False),
        sa.Column("refresh_token", sa.String(length=255), nullable=False),
        sa.Column("refresh_token_hash", sa.String(length=64), nullable=False),
        sa.Column("ip_address", sa.String(length=45), nullable=True),
        sa.Column("user_agent", sa.String(length=500), nullable=True),
        sa.Column("expires_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column(
            "created_at",
            sa.DateTime(timezone=True),
            server_default=sa.func.now(),
            nullable=False,
        ),
        sa.Column(
            "last_used_at",
            sa.DateTime(timezone=True),
            server_default=sa.func.now(),
            nullable=False,
        ),
        sa.Column(
            "is_revoked",
            sa.Boolean(),
            nullable=False,
            server_default=sa.text("false"),
        ),
        sa.ForeignKeyConstraint(["user_id"], ["users.id"], ondelete="CASCADE"),
        sa.PrimaryKeyConstraint("id"),
    )
    op.create_index(
        "idx_user_sessions_user_id", "user_sessions", ["user_id"], unique=False
    )
    op.create_index(
        "idx_user_sessions_refresh_hash",
        "user_sessions",
        ["refresh_token_hash"],
        unique=True,
    )

    # 3. Seed placeholder orgs and initial users
    conn = op.get_bind()
    unique_org_ids = sorted({u[1] for u in INITIAL_USERS if u[1] is not None})
    for org_id in unique_org_ids:
        conn.execute(
            sa.text(
                "INSERT INTO orgs (org_id, org_name, name, status, is_active) "
                "VALUES (:org_id, :name, :name, 1, TRUE) "
                "ON CONFLICT (org_id) DO NOTHING"
            ),
            {"org_id": org_id, "name": f"Организация {org_id}"},
        )

    for user_id, org_id, username, md5_pwd, role_id, role, is_su in INITIAL_USERS:
        conn.execute(
            sa.text(
                "INSERT INTO users (id, org_id, username, md5_password, role_id, role, is_superuser, is_active) "
                "VALUES (:id, :org_id, :username, :md5_pwd, :role_id, :role, :is_su, TRUE) "
                "ON CONFLICT (username) DO NOTHING"
            ),
            {
                "id": user_id,
                "org_id": org_id,
                "username": username,
                "md5_pwd": md5_pwd,
                "role_id": role_id,
                "role": role,
                "is_su": is_su,
            },
        )

    # Reset sequence to max(id)
    if conn.dialect.name == "postgresql":
        conn.execute(
            sa.text(
                "SELECT setval(pg_get_serial_sequence('users', 'id'), COALESCE(max(id), 1)) FROM users;"
            )
        )


def downgrade() -> None:
    op.drop_index("idx_user_sessions_refresh_hash", table_name="user_sessions")
    op.drop_index("idx_user_sessions_user_id", table_name="user_sessions")
    op.drop_table("user_sessions")
    op.drop_index("idx_users_org_id", table_name="users")
    op.drop_index("idx_users_username", table_name="users")
    op.drop_table("users")
