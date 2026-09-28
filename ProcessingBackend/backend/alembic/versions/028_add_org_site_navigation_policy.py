"""Add tenant navigation and license page visibility policy.

Revision ID: 028
Revises: 027
"""

import sqlalchemy as sa

from alembic import op

revision: str = "028"
down_revision: str | None = "027"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.add_column(
        "orgs",
        sa.Column(
            "site_mode", sa.String(length=16), server_default="both", nullable=False
        ),
    )
    op.add_column(
        "orgs", sa.Column("default_site", sa.String(length=16), nullable=True)
    )
    op.add_column(
        "orgs",
        sa.Column(
            "classic_licenses_enabled",
            sa.Boolean(),
            server_default=sa.true(),
            nullable=False,
        ),
    )
    op.add_column(
        "orgs",
        sa.Column(
            "l4desk_licenses_enabled",
            sa.Boolean(),
            server_default=sa.true(),
            nullable=False,
        ),
    )
    op.create_check_constraint(
        "ck_orgs_site_mode", "orgs", "site_mode IN ('classic', 'l4desk', 'both')"
    )
    op.create_check_constraint(
        "ck_orgs_default_site",
        "orgs",
        "default_site IS NULL OR default_site IN ('classic', 'l4desk')",
    )
    op.create_check_constraint(
        "ck_orgs_site_default_allowed",
        "orgs",
        "site_mode = 'both' OR default_site IS NULL OR default_site = site_mode",
    )


def downgrade() -> None:
    op.drop_constraint("ck_orgs_site_default_allowed", "orgs", type_="check")
    op.drop_constraint("ck_orgs_default_site", "orgs", type_="check")
    op.drop_constraint("ck_orgs_site_mode", "orgs", type_="check")
    op.drop_column("orgs", "l4desk_licenses_enabled")
    op.drop_column("orgs", "classic_licenses_enabled")
    op.drop_column("orgs", "default_site")
    op.drop_column("orgs", "site_mode")
