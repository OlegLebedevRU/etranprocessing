"""Add menu_version to payments and balances, create v1 snapshots, backfill and recreate unique index.

Revision ID: 022
Revises: 021
Create Date: 2026-09-03
"""

import json
from collections.abc import Sequence

import sqlalchemy as sa

from alembic import op

# revision identifiers, used by Alembic.
revision: str = "022"
down_revision: str | None = "021"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def _build_snapshot_data(conn, variant_id: int, version: int) -> dict:
    groups = conn.execute(
        sa.text("""
            SELECT id, name, parent_id, number
            FROM groups
            WHERE menu_variant_id = :v_id
            ORDER BY number, id
        """),
        {"v_id": variant_id},
    ).fetchall()

    group_ids = [g[0] for g in groups]
    services = []
    if group_ids:
        services = conn.execute(
            sa.text("""
                SELECT group_id, tsp_code, name, printname, price, protypenumber
                FROM services
                WHERE group_id = ANY(:g_ids)
                ORDER BY tsp_code
            """),
            {"g_ids": group_ids},
        ).fetchall()

    groups_by_parent: dict[int | None, list] = {}
    for g in groups:
        groups_by_parent.setdefault(g[2], []).append(g)

    services_by_group: dict[int, list] = {}
    for s in services:
        services_by_group.setdefault(s[0], []).append(s)

    def build_tree(parent_id: int | None):
        items = []
        for g in sorted(groups_by_parent.get(parent_id, []), key=lambda x: x[3]):
            node = {"name": g[1]}
            child_items = build_tree(g[0])
            for s in sorted(services_by_group.get(g[0], []), key=lambda x: x[1]):
                svc = {
                    "name": s[2],
                    "code": s[1],
                    "prototypeid": s[5],
                }
                if s[3]:
                    svc["printname"] = s[3]
                if s[4]:
                    svc["price"] = str(s[4])
                child_items.append(svc)
            node["items"] = child_items if child_items else []
            items.append(node)
        return items

    tree = {"name": "root", "items": build_tree(None)}
    services_by_tsp = {
        str(s[1]): {
            "name": s[2],
            "printname": s[3],
            "price": s[4],
            "protypenumber": s[5],
        }
        for s in services
    }

    return {
        "menu_variant_id": variant_id,
        "version": version,
        "tree": tree,
        "services_by_tsp": services_by_tsp,
    }


def upgrade() -> None:
    # 1. Add menu_version to payments
    op.execute("""
        DO $$
        BEGIN
            IF NOT EXISTS (
                SELECT 1 FROM information_schema.columns
                WHERE table_name = 'payments' AND column_name = 'menu_version'
            ) THEN
                ALTER TABLE payments ADD COLUMN menu_version INTEGER;
            END IF;
        END
        $$;
    """)
    op.execute(
        "CREATE INDEX IF NOT EXISTS idx_payments_menu_version ON payments(menu_version);"
    )
    op.execute(
        "CREATE INDEX IF NOT EXISTS idx_payments_tsp_code_menu_version ON payments(paym_tsp_code, menu_version);"
    )

    # 2. Add menu_version to balance_terminal_tsp
    op.execute("""
        DO $$
        BEGIN
            IF NOT EXISTS (
                SELECT 1 FROM information_schema.columns
                WHERE table_name = 'balance_terminal_tsp' AND column_name = 'menu_version'
            ) THEN
                ALTER TABLE balance_terminal_tsp ADD COLUMN menu_version INTEGER;
            END IF;
        END
        $$;
    """)
    op.execute(
        "CREATE INDEX IF NOT EXISTS idx_balance_menu_version ON balance_terminal_tsp(menu_version);"
    )

    conn = op.get_bind()

    # 3. Create snapshots for all menu variants that do not have a snapshot for version 1
    variants = conn.execute(
        sa.text("SELECT id, version FROM menu_variants ORDER BY id")
    ).fetchall()
    for v_id, v_ver in variants:
        target_version = v_ver or 1
        existing_snap = conn.execute(
            sa.text("""
                SELECT id FROM menu_variant_snapshots
                WHERE menu_variant_id = :v_id AND version = :version
            """),
            {"v_id": v_id, "version": target_version},
        ).fetchone()

        if not existing_snap:
            snap_data = _build_snapshot_data(conn, v_id, target_version)
            conn.execute(
                sa.text("""
                    INSERT INTO menu_variant_snapshots (menu_variant_id, version, snapshot_data, created_at)
                    VALUES (:v_id, :version, :data, NOW())
                """),
                {
                    "v_id": v_id,
                    "version": target_version,
                    "data": json.dumps(snap_data),
                },
            )

    # 4. Create bindings for unbound active terminals
    op.execute("""
        INSERT INTO terminal_menu_bindings (device_id, menu_variant_id, loaded_version, loaded_at)
        SELECT t.device_id, mv.first_variant_id, 1, NOW()
        FROM terminals t
        JOIN LATERAL (
            SELECT id AS first_variant_id
            FROM menu_variants
            WHERE org_id = t.org_id
            ORDER BY id
            LIMIT 1
        ) mv ON true
        WHERE NOT EXISTS (
            SELECT 1 FROM terminal_menu_bindings bnd
            WHERE bnd.device_id = t.device_id
        );
    """)

    # 5. Backfill menu_version and menu_snapshot_id in balance_terminal_tsp and payments
    op.execute("""
        UPDATE balance_terminal_tsp b
        SET menu_version = 1,
            menu_snapshot_id = s.id
        FROM terminals t
        JOIN terminal_menu_bindings bnd ON bnd.device_id = t.device_id
        JOIN menu_variant_snapshots s ON s.menu_variant_id = bnd.menu_variant_id AND s.version = 1
        WHERE b.terminal_id = t.id AND (b.menu_version IS NULL OR b.menu_snapshot_id IS NULL);
    """)

    op.execute("""
        UPDATE payments p
        SET menu_version = 1,
            menu_snapshot_id = s.id
        FROM terminals t
        JOIN terminal_menu_bindings bnd ON bnd.device_id = t.device_id
        JOIN menu_variant_snapshots s ON s.menu_variant_id = bnd.menu_variant_id AND s.version = 1
        WHERE p.terminal_id = t.id AND (p.menu_version IS NULL OR p.menu_snapshot_id IS NULL);
    """)

    op.execute("""
        UPDATE balance_terminal_tsp SET menu_version = 1 WHERE menu_version IS NULL;
    """)
    op.execute("""
        UPDATE payments SET menu_version = 1 WHERE menu_version IS NULL;
    """)

    # 6. Deduplicate balance_terminal_tsp if any duplicate rows exist
    op.execute("""
        DO $$
        DECLARE
            r RECORD;
        BEGIN
            FOR r IN
                SELECT int_day, terminal_id, tsp_id, COALESCE(menu_version, 0) as m_ver,
                       COUNT(*) as cnt,
                       SUM(amount) as sum_amount,
                       SUM(count) as sum_count,
                       MAX(rec_id) as keep_id,
                       MAX(menu_snapshot_id) as snap_id
                FROM balance_terminal_tsp
                GROUP BY int_day, terminal_id, tsp_id, COALESCE(menu_version, 0)
                HAVING COUNT(*) > 1
            LOOP
                UPDATE balance_terminal_tsp
                SET amount = r.sum_amount,
                    count = r.sum_count,
                    menu_snapshot_id = COALESCE(menu_snapshot_id, r.snap_id)
                WHERE rec_id = r.keep_id;

                DELETE FROM balance_terminal_tsp
                WHERE int_day = r.int_day
                  AND terminal_id = r.terminal_id
                  AND tsp_id = r.tsp_id
                  AND COALESCE(menu_version, 0) = r.m_ver
                  AND rec_id <> r.keep_id;
            END LOOP;
        END $$;
    """)

    # 7. Recreate unique index on (int_day, terminal_id, tsp_id, COALESCE(menu_version, 0))
    op.execute("""
        DO $$
        BEGIN
            IF EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'uq_balance_day_terminal_tsp_snap') THEN
                ALTER TABLE balance_terminal_tsp DROP CONSTRAINT uq_balance_day_terminal_tsp_snap;
            END IF;
            IF EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'uq_balance_day_terminal_tsp') THEN
                ALTER TABLE balance_terminal_tsp DROP CONSTRAINT uq_balance_day_terminal_tsp;
            END IF;
            IF EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'uq_balance_day_terminal_tsp_ver') THEN
                ALTER TABLE balance_terminal_tsp DROP CONSTRAINT uq_balance_day_terminal_tsp_ver;
            END IF;
        END $$;
    """)
    op.execute("DROP INDEX IF EXISTS uq_balance_day_terminal_tsp_ver;")
    op.execute("DROP INDEX IF EXISTS uq_balance_day_terminal_tsp_snap;")
    op.execute("""
        CREATE UNIQUE INDEX IF NOT EXISTS uq_balance_day_terminal_tsp_ver
        ON balance_terminal_tsp (int_day, terminal_id, tsp_id, COALESCE(menu_version, 0));
    """)


def downgrade() -> None:
    op.execute("DROP INDEX IF EXISTS uq_balance_day_terminal_tsp_ver;")
    op.execute("""
        DO $$
        BEGIN
            IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'uq_balance_day_terminal_tsp_snap') THEN
                ALTER TABLE balance_terminal_tsp ADD CONSTRAINT uq_balance_day_terminal_tsp_snap
                    UNIQUE (int_day, terminal_id, tsp_id, menu_snapshot_id);
            END IF;
        END $$;
    """)
    op.execute("DROP INDEX IF EXISTS idx_balance_menu_version;")
    op.execute("ALTER TABLE balance_terminal_tsp DROP COLUMN IF EXISTS menu_version;")
    op.execute("DROP INDEX IF EXISTS idx_payments_tsp_code_menu_version;")
    op.execute("DROP INDEX IF EXISTS idx_payments_menu_version;")
    op.execute("ALTER TABLE payments DROP COLUMN IF EXISTS menu_version;")
