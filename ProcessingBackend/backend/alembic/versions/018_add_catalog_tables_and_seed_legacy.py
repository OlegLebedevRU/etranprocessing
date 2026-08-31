"""Add catalog_categories and catalog_items tables, add catalog_item_id to services, and copy legacy menus into Catalog.

Revision ID: 018
Revises: 017
Create Date: 2026-08-31
"""

from collections.abc import Sequence

import sqlalchemy as sa

from alembic import op

# revision identifiers, used by Alembic.
revision: str = "018"
down_revision: str | None = "017"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    # 1. Create catalog_categories table
    op.create_table(
        "catalog_categories",
        sa.Column("id", sa.Integer(), primary_key=True, autoincrement=True),
        sa.Column("org_id", sa.Integer(), nullable=False, index=True),
        sa.Column("name", sa.String(255), nullable=False),
        sa.Column(
            "parent_id",
            sa.Integer(),
            sa.ForeignKey("catalog_categories.id", ondelete="CASCADE"),
            nullable=True,
            index=True,
        ),
        sa.Column("sort_order", sa.Integer(), nullable=False, server_default="0"),
        sa.Column(
            "created_at", sa.DateTime(timezone=True), server_default=sa.func.now()
        ),
        sa.Column(
            "updated_at", sa.DateTime(timezone=True), server_default=sa.func.now()
        ),
    )

    # 2. Create catalog_items table
    op.create_table(
        "catalog_items",
        sa.Column("id", sa.Integer(), primary_key=True, autoincrement=True),
        sa.Column("org_id", sa.Integer(), nullable=False, index=True),
        sa.Column(
            "category_id",
            sa.Integer(),
            sa.ForeignKey("catalog_categories.id", ondelete="CASCADE"),
            nullable=False,
            index=True,
        ),
        sa.Column("tsp_code", sa.Integer(), nullable=False),
        sa.Column("name", sa.String(255), nullable=False),
        sa.Column("printname", sa.String(255), nullable=True),
        sa.Column("price", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("protypenumber", sa.Integer(), nullable=False, server_default="0"),
        sa.Column(
            "created_at", sa.DateTime(timezone=True), server_default=sa.func.now()
        ),
        sa.Column(
            "updated_at", sa.DateTime(timezone=True), server_default=sa.func.now()
        ),
        sa.UniqueConstraint("org_id", "tsp_code", name="uq_catalog_org_tsp"),
    )

    # 3. Add catalog_item_id column to services table
    op.add_column(
        "services",
        sa.Column(
            "catalog_item_id",
            sa.Integer(),
            sa.ForeignKey("catalog_items.id", ondelete="SET NULL"),
            nullable=True,
            index=True,
        ),
    )

    # 4. Migrate existing Legacy menus into Catalog per organization
    conn = op.get_bind()

    # Find legacy menu variants (or first variant for org if named "Легаси меню")
    variants = conn.execute(
        sa.text("""
            SELECT id, org_id, name
            FROM menu_variants
            WHERE name = 'Легаси меню'
            ORDER BY org_id, id
        """)
    ).fetchall()

    for v_id, org_id, _ in variants:
        # Load all groups for this variant
        groups = conn.execute(
            sa.text("""
                SELECT id, name, parent_id, number
                FROM groups
                WHERE menu_variant_id = :v_id
                ORDER BY id
            """),
            {"v_id": v_id},
        ).fetchall()

        if not groups:
            continue

        # Map group_id -> created catalog_category_id
        group_to_cat_id: dict[int, int] = {}

        # Identify root groups
        root_groups = [g for g in groups if g[2] is None]
        other_groups = [g for g in groups if g[2] is not None]

        # Check if root group has services directly attached
        for rg in root_groups:
            rg_id, rg_name, _, _ = rg
            # Create a catalog category for root group or check child groups
            cat_res = conn.execute(
                sa.text("""
                    INSERT INTO catalog_categories (org_id, name, parent_id, sort_order)
                    VALUES (:org_id, :name, NULL, 0)
                    RETURNING id
                """),
                {"org_id": org_id, "name": rg_name or "Главное меню"},
            ).fetchone()
            if cat_res:
                group_to_cat_id[rg_id] = cat_res[0]

        # Iteratively create child categories up to depth
        remaining = list(other_groups)
        while remaining:
            progress = False
            next_remaining = []
            for g in remaining:
                g_id, g_name, g_parent_id, _ = g
                if g_parent_id in group_to_cat_id:
                    parent_cat_id = group_to_cat_id[g_parent_id]
                    cat_res = conn.execute(
                        sa.text("""
                            INSERT INTO catalog_categories (org_id, name, parent_id, sort_order)
                            VALUES (:org_id, :name, :parent_id, 0)
                            RETURNING id
                        """),
                        {"org_id": org_id, "name": g_name, "parent_id": parent_cat_id},
                    ).fetchone()
                    if cat_res:
                        group_to_cat_id[g_id] = cat_res[0]
                    progress = True
                else:
                    next_remaining.append(g)
            if not progress:
                # Break cycle or unreachable parents: assign to first root category
                for g in next_remaining:
                    g_id, g_name, _, _ = g
                    fallback_parent = (
                        next(iter(group_to_cat_id.values()))
                        if group_to_cat_id
                        else None
                    )
                    cat_res = conn.execute(
                        sa.text("""
                            INSERT INTO catalog_categories (org_id, name, parent_id, sort_order)
                            VALUES (:org_id, :name, :parent_id, 0)
                            RETURNING id
                        """),
                        {
                            "org_id": org_id,
                            "name": g_name,
                            "parent_id": fallback_parent,
                        },
                    ).fetchone()
                    if cat_res:
                        group_to_cat_id[g_id] = cat_res[0]
                break
            remaining = next_remaining

        # Now migrate services for this variant into catalog_items
        services = conn.execute(
            sa.text("""
                SELECT id, group_id, tsp_code, name, printname, price, protypenumber
                FROM services
                WHERE menu_variant_id = :v_id
                ORDER BY id
            """),
            {"v_id": v_id},
        ).fetchall()

        for (
            s_id,
            s_group_id,
            s_tsp,
            s_name,
            s_printname,
            s_price,
            s_protype,
        ) in services:
            cat_id = group_to_cat_id.get(s_group_id)
            if not cat_id and group_to_cat_id:
                cat_id = next(iter(group_to_cat_id.values()))

            if cat_id:
                # Check if item with this tsp_code already exists for org
                existing_item = conn.execute(
                    sa.text("""
                        SELECT id FROM catalog_items WHERE org_id = :org_id AND tsp_code = :tsp_code
                    """),
                    {"org_id": org_id, "tsp_code": s_tsp},
                ).fetchone()

                if existing_item:
                    item_id = existing_item[0]
                else:
                    item_res = conn.execute(
                        sa.text("""
                            INSERT INTO catalog_items (org_id, category_id, tsp_code, name, printname, price, protypenumber)
                            VALUES (:org_id, :cat_id, :tsp_code, :name, :printname, :price, :protypenumber)
                            RETURNING id
                        """),
                        {
                            "org_id": org_id,
                            "cat_id": cat_id,
                            "tsp_code": s_tsp,
                            "name": s_name,
                            "printname": s_printname,
                            "price": s_price or 0,
                            "protypenumber": s_protype or 0,
                        },
                    ).fetchone()
                    item_id = item_res[0] if item_res else None

                if item_id:
                    # Link service to catalog item
                    conn.execute(
                        sa.text("""
                            UPDATE services
                            SET catalog_item_id = :item_id
                            WHERE id = :s_id
                        """),
                        {"item_id": item_id, "s_id": s_id},
                    )


def downgrade() -> None:
    op.drop_column("services", "catalog_item_id")
    op.drop_table("catalog_items")
    op.drop_table("catalog_categories")
