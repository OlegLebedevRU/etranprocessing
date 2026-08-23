"""Add Leo4 IoT provisioning fields to terminals.

Revision ID: 013
Revises: 012
Create Date: 2026-08-23
"""

from collections.abc import Sequence

import sqlalchemy as sa

from alembic import op

revision: str = "013"
down_revision: str | None = "012"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    op.add_column(
        "terminals",
        sa.Column(
            "iot_provisioned",
            sa.Boolean(),
            nullable=False,
            server_default=sa.text("false"),
        ),
    )
    op.add_column(
        "terminals",
        sa.Column(
            "iot_provisioned_at",
            sa.DateTime(timezone=True),
            nullable=True,
        ),
    )
    op.add_column(
        "terminals",
        sa.Column(
            "iot_last_sync_at",
            sa.DateTime(timezone=True),
            nullable=True,
        ),
    )
    op.add_column(
        "terminals",
        sa.Column(
            "iot_is_online",
            sa.Boolean(),
            nullable=False,
            server_default=sa.text("false"),
        ),
    )
    op.add_column(
        "terminals",
        sa.Column(
            "iot_last_connected_at",
            sa.DateTime(timezone=True),
            nullable=True,
        ),
    )


def downgrade() -> None:
    op.drop_column("terminals", "iot_last_connected_at")
    op.drop_column("terminals", "iot_is_online")
    op.drop_column("terminals", "iot_last_sync_at")
    op.drop_column("terminals", "iot_provisioned_at")
    op.drop_column("terminals", "iot_provisioned")
