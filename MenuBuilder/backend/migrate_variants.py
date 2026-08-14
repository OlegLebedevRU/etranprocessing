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
                name VARCHAR(255) UNIQUE NOT NULL,
                created_at TIMESTAMPTZ DEFAULT now(),
                updated_at TIMESTAMPTZ DEFAULT now()
            )
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
