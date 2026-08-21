"""Migration: add menu_variants, terminal_menu_bindings, and menu_variant_id to groups/services."""
import asyncio
from sqlalchemy import text
from app.database import engine


async def migrate():
    async with engine.begin() as conn:
        # Create menu_variants table
        await conn.execute(text("""
            CREATE TABLE IF NOT EXISTS menu_variants (
                id SERIAL PRIMARY KEY,
                org_id INTEGER NOT NULL DEFAULT 1,
                name VARCHAR(255) NOT NULL,
                created_at TIMESTAMPTZ DEFAULT now(),
                updated_at TIMESTAMPTZ DEFAULT now()
            )
        """))
        await conn.execute(text("""
            CREATE INDEX IF NOT EXISTS idx_menu_variants_org_id ON menu_variants(org_id)
        """))
        await conn.execute(text("""
            DO $$
            BEGIN
                IF NOT EXISTS (
                    SELECT 1 FROM information_schema.columns
                    WHERE table_name = 'menu_variants' AND column_name = 'org_id'
                ) THEN
                    ALTER TABLE menu_variants ADD COLUMN org_id INTEGER NOT NULL DEFAULT 1;
                END IF;
                IF EXISTS (
                    SELECT 1 FROM pg_constraint WHERE conname = 'menu_variants_name_key'
                ) THEN
                    ALTER TABLE menu_variants DROP CONSTRAINT menu_variants_name_key;
                END IF;
                IF NOT EXISTS (
                    SELECT 1 FROM pg_constraint WHERE conname = 'uq_menu_variants_org_name'
                ) THEN
                    ALTER TABLE menu_variants ADD CONSTRAINT uq_menu_variants_org_name UNIQUE (org_id, name);
                END IF;
            END
            $$;
        """))

        # Create terminal_menu_bindings table
        await conn.execute(text("""
            CREATE TABLE IF NOT EXISTS terminal_menu_bindings (
                id SERIAL PRIMARY KEY,
                device_id INTEGER UNIQUE NOT NULL,
                menu_variant_id INTEGER NOT NULL REFERENCES menu_variants(id) ON DELETE CASCADE,
                created_at TIMESTAMPTZ DEFAULT now()
            )
        """))
        await conn.execute(text("""
            CREATE INDEX IF NOT EXISTS idx_tmb_device_id ON terminal_menu_bindings(device_id)
        """))
        await conn.execute(text("""
            CREATE INDEX IF NOT EXISTS idx_tmb_menu_variant_id ON terminal_menu_bindings(menu_variant_id)
        """))

        # Add menu_variant_id to groups (if not exists)
        res = await conn.execute(text("""
            SELECT column_name FROM information_schema.columns
            WHERE table_name='groups' AND column_name='menu_variant_id'
        """))
        if not res.fetchone():
            # Create default variant
            await conn.execute(text("""
                INSERT INTO menu_variants (name) VALUES ('Основное')
                ON CONFLICT (name) DO NOTHING
            """))
            default_id = await conn.scalar(text("SELECT id FROM menu_variants LIMIT 1"))

            await conn.execute(text(f"""
                ALTER TABLE groups ADD COLUMN menu_variant_id INTEGER
                REFERENCES menu_variants(id) ON DELETE CASCADE DEFAULT {default_id}
            """))
            await conn.execute(text("""
                CREATE INDEX IF NOT EXISTS idx_groups_menu_variant_id ON groups(menu_variant_id)
            """))

        # Add menu_variant_id to services (if not exists)
        res = await conn.execute(text("""
            SELECT column_name FROM information_schema.columns
            WHERE table_name='services' AND column_name='menu_variant_id'
        """))
        if not res.fetchone():
            default_id = await conn.scalar(text("SELECT id FROM menu_variants LIMIT 1"))
            await conn.execute(text(f"""
                ALTER TABLE services ADD COLUMN menu_variant_id INTEGER
                REFERENCES menu_variants(id) ON DELETE CASCADE DEFAULT {default_id}
            """))
            await conn.execute(text("""
                CREATE INDEX IF NOT EXISTS idx_services_menu_variant_id ON services(menu_variant_id)
            """))
            # Add unique constraint
            await conn.execute(text("""
                ALTER TABLE services DROP CONSTRAINT IF EXISTS services_tsp_code_key
            """))
            await conn.execute(text("""
                ALTER TABLE services ADD CONSTRAINT uq_service_variant_tsp
                UNIQUE (menu_variant_id, tsp_code)
            """))

        # Make groups.menu_variant_id NOT NULL
        await conn.execute(text("""
            ALTER TABLE groups ALTER COLUMN menu_variant_id SET NOT NULL
        """))
        await conn.execute(text("""
            ALTER TABLE services ALTER COLUMN menu_variant_id SET NOT NULL
        """))

    print("Migration complete!")


if __name__ == "__main__":
    asyncio.run(migrate())
