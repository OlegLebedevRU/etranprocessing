"""Add menu versioning, snapshots, terminal binding versions, and payment snapshot references.

Revision ID: 014
Revises: 013
Create Date: 2026-08-24
"""

from collections.abc import Sequence

from alembic import op

revision: str = "014"
down_revision: str | None = "013"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    # 1. Add version column to menu_variants
    op.execute("""
        DO $$
        BEGIN
            IF NOT EXISTS (
                SELECT 1 FROM information_schema.columns
                WHERE table_name = 'menu_variants' AND column_name = 'version'
            ) THEN
                ALTER TABLE menu_variants ADD COLUMN version INTEGER NOT NULL DEFAULT 1;
            END IF;
        END
        $$;
    """)

    # 2. Create menu_variant_snapshots table
    op.execute("""
        CREATE TABLE IF NOT EXISTS menu_variant_snapshots (
            id SERIAL PRIMARY KEY,
            menu_variant_id INTEGER NOT NULL REFERENCES menu_variants(id) ON DELETE CASCADE,
            version INTEGER NOT NULL,
            snapshot_data JSONB NOT NULL,
            created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
        );
    """)
    op.execute("""
        DO $$
        BEGIN
            IF NOT EXISTS (
                SELECT 1 FROM pg_constraint WHERE conname = 'uq_snapshot_variant_version'
            ) THEN
                ALTER TABLE menu_variant_snapshots ADD CONSTRAINT uq_snapshot_variant_version
                    UNIQUE (menu_variant_id, version);
            END IF;
        END
        $$;
    """)
    op.execute(
        "CREATE INDEX IF NOT EXISTS idx_snapshot_variant_version ON menu_variant_snapshots(menu_variant_id, version);"
    )

    # 3. Add loaded_version and loaded_at to terminal_menu_bindings
    op.execute("""
        DO $$
        BEGIN
            IF NOT EXISTS (
                SELECT 1 FROM information_schema.columns
                WHERE table_name = 'terminal_menu_bindings' AND column_name = 'loaded_version'
            ) THEN
                ALTER TABLE terminal_menu_bindings ADD COLUMN loaded_version INTEGER;
            END IF;
            IF NOT EXISTS (
                SELECT 1 FROM information_schema.columns
                WHERE table_name = 'terminal_menu_bindings' AND column_name = 'loaded_at'
            ) THEN
                ALTER TABLE terminal_menu_bindings ADD COLUMN loaded_at TIMESTAMP WITH TIME ZONE;
            END IF;
        END
        $$;
    """)

    # 4. Add menu_snapshot_id to payments
    op.execute("""
        DO $$
        BEGIN
            IF NOT EXISTS (
                SELECT 1 FROM information_schema.columns
                WHERE table_name = 'payments' AND column_name = 'menu_snapshot_id'
            ) THEN
                ALTER TABLE payments ADD COLUMN menu_snapshot_id INTEGER
                    REFERENCES menu_variant_snapshots(id) ON DELETE SET NULL;
            END IF;
        END
        $$;
    """)
    op.execute(
        "CREATE INDEX IF NOT EXISTS idx_payments_menu_snapshot_id ON payments(menu_snapshot_id);"
    )

    # 5. Add menu_snapshot_id to balance_terminal_tsp
    op.execute("""
        DO $$
        BEGIN
            IF NOT EXISTS (
                SELECT 1 FROM information_schema.columns
                WHERE table_name = 'balance_terminal_tsp' AND column_name = 'menu_snapshot_id'
            ) THEN
                ALTER TABLE balance_terminal_tsp ADD COLUMN menu_snapshot_id INTEGER
                    REFERENCES menu_variant_snapshots(id) ON DELETE SET NULL;
            END IF;
        END
        $$;
    """)
    op.execute(
        "CREATE INDEX IF NOT EXISTS idx_balance_menu_snapshot_id ON balance_terminal_tsp(menu_snapshot_id);"
    )
    op.execute("""
        DO $$
        BEGIN
            IF EXISTS (
                SELECT 1 FROM pg_constraint WHERE conname = 'uq_balance_day_terminal_tsp'
            ) THEN
                ALTER TABLE balance_terminal_tsp DROP CONSTRAINT uq_balance_day_terminal_tsp;
            END IF;
            IF NOT EXISTS (
                SELECT 1 FROM pg_constraint WHERE conname = 'uq_balance_day_terminal_tsp_snap'
            ) THEN
                ALTER TABLE balance_terminal_tsp ADD CONSTRAINT uq_balance_day_terminal_tsp_snap
                    UNIQUE (int_day, terminal_id, tsp_id, menu_snapshot_id);
            END IF;
        END
        $$;
    """)


def downgrade() -> None:
    op.execute("""
        DO $$
        BEGIN
            IF EXISTS (
                SELECT 1 FROM pg_constraint WHERE conname = 'uq_balance_day_terminal_tsp_snap'
            ) THEN
                ALTER TABLE balance_terminal_tsp DROP CONSTRAINT uq_balance_day_terminal_tsp_snap;
            END IF;
            IF NOT EXISTS (
                SELECT 1 FROM pg_constraint WHERE conname = 'uq_balance_day_terminal_tsp'
            ) THEN
                ALTER TABLE balance_terminal_tsp ADD CONSTRAINT uq_balance_day_terminal_tsp
                    UNIQUE (int_day, terminal_id, tsp_id);
            END IF;
        END
        $$;
    """)
    op.execute("DROP INDEX IF EXISTS idx_balance_menu_snapshot_id;")
    op.execute(
        "ALTER TABLE balance_terminal_tsp DROP COLUMN IF EXISTS menu_snapshot_id;"
    )
    op.execute("DROP INDEX IF EXISTS idx_payments_menu_snapshot_id;")
    op.execute("ALTER TABLE payments DROP COLUMN IF EXISTS menu_snapshot_id;")
    op.execute("ALTER TABLE terminal_menu_bindings DROP COLUMN IF EXISTS loaded_at;")
    op.execute(
        "ALTER TABLE terminal_menu_bindings DROP COLUMN IF EXISTS loaded_version;"
    )
    op.execute("DROP TABLE IF EXISTS menu_variant_snapshots;")
    op.execute("ALTER TABLE menu_variants DROP COLUMN IF EXISTS version;")
