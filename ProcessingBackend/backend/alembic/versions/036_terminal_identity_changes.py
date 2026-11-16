"""Identity change operations and superuser incidents and tenant-scoped daily aggregates."""

import sqlalchemy as sa

from alembic import op

revision = "036"
down_revision = "035"
branch_labels = None
depends_on = None


def upgrade():
    # Revision022 installs an expression INDEX, not an ordinary constraint.
    # Preserve its NULL/0 version semantics while adding tenant ownership.
    op.drop_index("uq_balance_day_terminal_tsp_ver", table_name="balance_terminal_tsp")
    op.create_index(
        "uq_balance_day_terminal_tsp_ver",
        "balance_terminal_tsp",
        [
            "int_day",
            "org_id",
            "terminal_id",
            "tsp_id",
            sa.text("COALESCE(menu_version, 0)"),
        ],
        unique=True,
    )
    op.create_table(
        "terminal_identity_changes",
        sa.Column(
            "terminal_id", sa.Integer(), sa.ForeignKey("terminals.id"), nullable=False
        ),
        sa.Column("operation_id", sa.Uuid(), primary_key=True),
        sa.Column("source_org_id", sa.Integer(), nullable=False),
        sa.Column(
            "target_org_id", sa.Integer(), sa.ForeignKey("orgs.org_id"), nullable=False
        ),
        sa.Column(
            "archived_technical_terminal_id",
            sa.Integer(),
            sa.ForeignKey("l4desk_terminals.terminal_id"),
        ),
        sa.Column("old_serial", sa.String(100)),
        sa.Column("new_serial", sa.String(100)),
        sa.Column("status", sa.String(20), nullable=False),
        sa.Column("requested_by", sa.String(100), nullable=False),
        sa.Column("pin_id", sa.Integer(), sa.ForeignKey("certificate_pins.id")),
        sa.Column("csr_sha256", sa.String(64)),
        sa.Column("certificate_response", sa.Text()),
        sa.Column(
            "created_at",
            sa.DateTime(timezone=True),
            server_default=sa.func.now(),
            nullable=False,
        ),
        sa.Column("issued_at", sa.DateTime(timezone=True)),
        sa.Column("completed_at", sa.DateTime(timezone=True)),
        sa.CheckConstraint(
            "status IN ('pending', 'sync_pending', 'completed', 'cancelled')",
            name="ck_identity_change_status",
        ),
        sa.CheckConstraint(
            "source_org_id <> target_org_id", name="ck_identity_change_different_orgs"
        ),
    )
    op.create_index(
        "ix_terminal_identity_changes_terminal_id",
        "terminal_identity_changes",
        ["terminal_id"],
    )
    op.create_index(
        "idx_identity_change_old_serial",
        "terminal_identity_changes",
        ["terminal_id", "old_serial"],
    )
    op.create_index(
        "uq_identity_change_active_terminal",
        "terminal_identity_changes",
        ["terminal_id"],
        unique=True,
        postgresql_where=sa.text("status IN ('pending', 'sync_pending')"),
        sqlite_where=sa.text("status IN ('pending', 'sync_pending')"),
    )
    op.create_table(
        "device_admin_events",
        sa.Column("id", sa.Integer(), primary_key=True),
        sa.Column("event_key", sa.String(160), unique=True, nullable=False),
        sa.Column(
            "terminal_id", sa.Integer(), sa.ForeignKey("terminals.id"), nullable=False
        ),
        sa.Column("operation_id", sa.Uuid(), nullable=False),
        sa.Column("event_type", sa.String(50), nullable=False),
        sa.Column("status", sa.String(20), nullable=False, server_default="new"),
        sa.Column("details", sa.JSON(), nullable=False),
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
        sa.Column("updated_by", sa.String(100)),
        sa.Column("comment", sa.Text()),
        sa.CheckConstraint(
            "status IN ('new', 'in_progress', 'closed')",
            name="ck_device_admin_event_status",
        ),
    )
    op.create_index(
        "idx_device_admin_events_status_created",
        "device_admin_events",
        ["status", "created_at"],
    )


def downgrade():
    # Restoring the old global aggregate key after real cross-tenant payments
    # would lose or merge history. Refuse rather than rewrite monetary records.
    if not op.get_context().as_sql:
        conflict = (
            op.get_bind()
            .execute(
                sa.text(
                    "SELECT 1 FROM balance_terminal_tsp GROUP BY int_day, terminal_id, tsp_id, COALESCE(menu_version, 0) HAVING count(*) > 1 LIMIT 1"
                )
            )
            .first()
        )
        if conflict:
            raise RuntimeError(
                "Cannot downgrade036: cross-tenant daily balances exist; keep the tenant-scoped reader and roll forward"
            )
        used = (
            op.get_bind()
            .execute(
                sa.text(
                    "SELECT 1 FROM terminal_identity_changes WHERE status != 'cancelled' LIMIT 1"
                )
            )
            .first()
        )
        if used:
            raise RuntimeError(
                "Cannot downgrade036: identity operations exist; cancel unused intents or roll forward"
            )
    op.drop_index("uq_balance_day_terminal_tsp_ver", table_name="balance_terminal_tsp")
    op.create_index(
        "uq_balance_day_terminal_tsp_ver",
        "balance_terminal_tsp",
        ["int_day", "terminal_id", "tsp_id", sa.text("COALESCE(menu_version, 0)")],
        unique=True,
    )
    op.drop_table("device_admin_events")
    op.drop_table("terminal_identity_changes")
