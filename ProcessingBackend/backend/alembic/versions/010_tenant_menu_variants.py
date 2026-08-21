"""Add org_id to menu_variants and scope uniqueness by (org_id, name).

Revision ID: 010
Revises: 009
Create Date: 2026-08-21
"""

from collections.abc import Sequence

from alembic import op

revision: str = "010"
down_revision: str | None = "009"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    # Add org_id column to menu_variants
    op.execute("""
        DO $$
        BEGIN
            IF NOT EXISTS (
                SELECT 1 FROM information_schema.columns
                WHERE table_name = 'menu_variants' AND column_name = 'org_id'
            ) THEN
                ALTER TABLE menu_variants ADD COLUMN org_id INTEGER NOT NULL DEFAULT 1;
            END IF;
        END
        $$;
    """)

    # Drop old global unique constraint on name if it exists
    op.execute("""
        DO $$
        BEGIN
            IF EXISTS (
                SELECT 1 FROM pg_constraint WHERE conname = 'menu_variants_name_key'
            ) THEN
                ALTER TABLE menu_variants DROP CONSTRAINT menu_variants_name_key;
            END IF;
        END
        $$;
    """)

    # Add tenant-scoped unique constraint (org_id, name)
    op.execute("""
        DO $$
        BEGIN
            IF NOT EXISTS (
                SELECT 1 FROM pg_constraint WHERE conname = 'uq_menu_variants_org_name'
            ) THEN
                ALTER TABLE menu_variants ADD CONSTRAINT uq_menu_variants_org_name
                    UNIQUE (org_id, name);
            END IF;
        END
        $$;
    """)

    # Index on org_id
    op.execute(
        "CREATE INDEX IF NOT EXISTS idx_menu_variants_org_id ON menu_variants(org_id)"
    )


def downgrade() -> None:
    op.execute("DROP INDEX IF EXISTS idx_menu_variants_org_id")
    op.execute(
        "ALTER TABLE menu_variants DROP CONSTRAINT IF EXISTS uq_menu_variants_org_name"
    )
    op.execute("""
        DO $$
        BEGIN
            IF NOT EXISTS (
                SELECT 1 FROM pg_constraint WHERE conname = 'menu_variants_name_key'
            ) THEN
                ALTER TABLE menu_variants ADD CONSTRAINT menu_variants_name_key UNIQUE (name);
            END IF;
        END
        $$;
    """)
    op.execute("ALTER TABLE menu_variants DROP COLUMN IF EXISTS org_id")
