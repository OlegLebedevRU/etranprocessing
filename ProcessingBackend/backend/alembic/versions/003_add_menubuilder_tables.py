"""Add MenuBuilder tables (menu_variants, terminal_menu_bindings) and menu_variant_id to groups/services.

This migration is idempotent — uses IF NOT EXISTS for tables and columns.
Applied to existing database where groups/services already exist.

Revision ID: 003
Revises: 002
Create Date: 2026-08-17
"""

from collections.abc import Sequence

from alembic import op

revision: str = "003"
down_revision: str | None = "002"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    # --- menu_variants ---
    op.execute("""
        CREATE TABLE IF NOT EXISTS menu_variants (
            id SERIAL PRIMARY KEY,
            name VARCHAR(255) UNIQUE NOT NULL,
            created_at TIMESTAMPTZ DEFAULT now(),
            updated_at TIMESTAMPTZ DEFAULT now()
        )
    """)

    # --- terminal_menu_bindings ---
    op.execute("""
        CREATE TABLE IF NOT EXISTS terminal_menu_bindings (
            id SERIAL PRIMARY KEY,
            device_id INTEGER UNIQUE NOT NULL,
            menu_variant_id INTEGER NOT NULL REFERENCES menu_variants(id) ON DELETE CASCADE,
            created_at TIMESTAMPTZ DEFAULT now()
        )
    """)
    op.execute(
        "CREATE INDEX IF NOT EXISTS idx_tmb_device_id ON terminal_menu_bindings(device_id)"
    )
    op.execute(
        "CREATE INDEX IF NOT EXISTS idx_tmb_menu_variant_id ON terminal_menu_bindings(menu_variant_id)"
    )

    # --- groups.menu_variant_id ---
    # Ensure default variant exists
    op.execute("""
        INSERT INTO menu_variants (name) VALUES ('Основное')
        ON CONFLICT (name) DO NOTHING
    """)

    # Add column if not exists
    op.execute("""
        DO $$
        BEGIN
            IF NOT EXISTS (
                SELECT 1 FROM information_schema.columns
                WHERE table_name = 'groups' AND column_name = 'menu_variant_id'
            ) THEN
                ALTER TABLE groups ADD COLUMN menu_variant_id INTEGER
                    REFERENCES menu_variants(id) ON DELETE CASCADE
                    DEFAULT 1;
                ALTER TABLE groups ALTER COLUMN menu_variant_id SET NOT NULL;
            END IF;
        END
        $$;
    """)
    op.execute(
        "CREATE INDEX IF NOT EXISTS idx_groups_menu_variant_id ON groups(menu_variant_id)"
    )

    # --- services.menu_variant_id ---
    op.execute("""
        DO $$
        BEGIN
            IF NOT EXISTS (
                SELECT 1 FROM information_schema.columns
                WHERE table_name = 'services' AND column_name = 'menu_variant_id'
            ) THEN
                ALTER TABLE services ADD COLUMN menu_variant_id INTEGER
                    REFERENCES menu_variants(id) ON DELETE CASCADE
                    DEFAULT 1;
                ALTER TABLE services ALTER COLUMN menu_variant_id SET NOT NULL;
            END IF;
        END
        $$;
    """)
    op.execute(
        "CREATE INDEX IF NOT EXISTS idx_services_menu_variant_id ON services(menu_variant_id)"
    )

    # Unique constraint on (menu_variant_id, tsp_code) — replace old single-column unique
    op.execute("ALTER TABLE services DROP CONSTRAINT IF EXISTS services_tsp_code_key")
    op.execute("""
        DO $$
        BEGIN
            IF NOT EXISTS (
                SELECT 1 FROM pg_constraint WHERE conname = 'uq_service_variant_tsp'
            ) THEN
                ALTER TABLE services ADD CONSTRAINT uq_service_variant_tsp
                    UNIQUE (menu_variant_id, tsp_code);
            END IF;
        END
        $$;
    """)


def downgrade() -> None:
    op.drop_constraint("uq_service_variant_tsp", "services", type_="unique")
    op.drop_column("services", "menu_variant_id")
    op.drop_column("groups", "menu_variant_id")
    op.drop_table("terminal_menu_bindings")
    op.drop_table("menu_variants")
