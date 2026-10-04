"""Separate renewal PINs and retain a bounded same-CSR response for recovery."""

import sqlalchemy as sa

from alembic import op

revision = "030"
down_revision = "029"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.add_column(
        "certificate_pins",
        sa.Column("purpose", sa.String(20), nullable=False, server_default="setup"),
    )
    op.add_column(
        "certificate_pins",
        sa.Column("renewal_auth_serial", sa.String(100), nullable=True),
    )
    op.add_column(
        "certificate_pins",
        sa.Column("renewal_csr_sha256", sa.String(64), nullable=True),
    )
    op.add_column(
        "certificate_pins", sa.Column("renewal_response", sa.Text(), nullable=True)
    )
    op.create_check_constraint(
        "ck_certificate_pins_purpose",
        "certificate_pins",
        "purpose IN ('setup', 'renew')",
    )


def downgrade() -> None:
    raise RuntimeError(
        "Preserve renewal recovery records; roll back applications with compatible readers"
    )
