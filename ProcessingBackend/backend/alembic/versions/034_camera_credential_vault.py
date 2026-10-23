"""Verified recipient and encrypted camera vault, additive from033."""

import sqlalchemy as sa

from alembic import op

revision = "034"
down_revision = "033"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.execute("SET LOCAL lock_timeout = '5s'")
    op.add_column(
        "terminals", sa.Column("credential_recipient_pem", sa.Text(), nullable=True)
    )
    op.create_table(
        "camera_credential_profiles",
        sa.Column("id", sa.Uuid(), primary_key=True),
        sa.Column("tenant_id", sa.Integer(), nullable=False),
        sa.Column(
            "terminal_id", sa.Integer(), sa.ForeignKey("terminals.id"), nullable=False
        ),
        sa.Column("device_id", sa.Integer(), nullable=False),
        sa.Column("sn", sa.String(64), nullable=False),
        sa.Column("camera_id", sa.String(12), nullable=False),
        sa.Column("version", sa.Integer(), nullable=False),
        sa.Column("active_operation_id", sa.Uuid()),
        sa.Column("pending_operation_id", sa.Uuid()),
        sa.UniqueConstraint(
            "terminal_id", "camera_id", name="uq_camera_credential_profile"
        ),
        sa.CheckConstraint("version >= 0", name="ck_camera_profile_version"),
    )
    op.create_table(
        "camera_credential_receipts",
        sa.Column("operation_id", sa.Uuid(), primary_key=True),
        sa.Column(
            "profile_id",
            sa.Uuid(),
            sa.ForeignKey("camera_credential_profiles.id"),
            nullable=False,
        ),
        sa.Column("version", sa.Integer(), nullable=False),
        sa.Column("kind", sa.String(8), nullable=False),
        sa.Column("state", sa.String(16), nullable=False),
        sa.Column("key_id", sa.String(64), nullable=False),
        sa.Column("algorithm", sa.String(16), nullable=False),
        sa.Column("nonce", sa.LargeBinary(), nullable=False),
        sa.Column("ciphertext", sa.LargeBinary(), nullable=False),
        sa.Column("wrap_nonce", sa.LargeBinary(), nullable=False),
        sa.Column("wrapped_dek", sa.LargeBinary(), nullable=False),
        sa.Column("expires_at_ms", sa.BigInteger(), nullable=False),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.UniqueConstraint(
            "profile_id", "version", name="uq_camera_credential_version"
        ),
        sa.CheckConstraint("version > 0", name="ck_camera_receipt_version"),
        sa.CheckConstraint(
            "kind IN ('replace', 'clear')", name="ck_camera_receipt_kind"
        ),
        sa.CheckConstraint(
            "state IN ('pending', 'active', 'superseded', 'failed', 'cleared')",
            name="ck_camera_receipt_state",
        ),
        sa.CheckConstraint(
            "algorithm = 'A256GCM-v1'", name="ck_camera_receipt_algorithm"
        ),
    )


def downgrade() -> None:
    op.drop_table("camera_credential_receipts")
    op.drop_table("camera_credential_profiles")
    op.drop_column("terminals", "credential_recipient_pem")
