from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.auth import require_tenant_context, resolve_org_id
from app.database import get_db
from app.models import (
    CatalogCategory,
    CatalogItem,
    MenuVariant,
    Service,
)
from app.schemas import (
    CatalogCategoryCreate,
    CatalogCategoryRead,
    CatalogCategoryUpdate,
    CatalogItemCreate,
    CatalogItemRead,
    CatalogItemUpdate,
    CatalogPropagateResponse,
)

router = APIRouter(prefix="/api/catalog", tags=["catalog"])

CATALOG_TSP_START = 1000301
CATALOG_TSP_END = 1000999


async def _get_category_depth(db: AsyncSession, category_id: int | None) -> int:
    """Calculate depth of category (root = 1, child = 2, subchild = 3)."""
    if category_id is None:
        return 0
    depth = 1
    curr_id = category_id
    while curr_id is not None:
        cat = await db.get(CatalogCategory, curr_id)
        if not cat or cat.parent_id is None:
            break
        depth += 1
        curr_id = cat.parent_id
        if depth > 10:  # Prevent infinite loop
            break
    return depth


@router.get("/categories", response_model=list[CatalogCategoryRead])
async def list_categories(
    user: dict = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
):
    org_id = resolve_org_id(user)

    stmt = select(CatalogCategory).where(CatalogCategory.org_id == org_id)
    stmt = stmt.order_by(CatalogCategory.sort_order, CatalogCategory.id)
    result = await db.execute(stmt)
    all_cats = list(result.scalars().all())

    # Get items count per category
    item_counts_stmt = (
        select(CatalogItem.category_id, func.count(CatalogItem.id))
        .where(CatalogItem.org_id == org_id)
        .group_by(CatalogItem.category_id)
    )
    count_rows = (await db.execute(item_counts_stmt)).fetchall()
    counts_map = {r[0]: r[1] for r in count_rows}

    # Build tree
    cat_map: dict[int, CatalogCategoryRead] = {}
    for c in all_cats:
        cat_map[c.id] = CatalogCategoryRead(
            id=c.id,
            org_id=c.org_id,
            name=c.name,
            parent_id=c.parent_id,
            sort_order=c.sort_order,
            depth=1,
            created_at=c.created_at,
            updated_at=c.updated_at,
            items_count=counts_map.get(c.id, 0),
            children=[],
        )

    roots: list[CatalogCategoryRead] = []
    for c in all_cats:
        read_obj = cat_map[c.id]
        if c.parent_id and c.parent_id in cat_map:
            parent_obj = cat_map[c.parent_id]
            read_obj.depth = parent_obj.depth + 1
            parent_obj.children.append(read_obj)
        else:
            read_obj.depth = 1
            roots.append(read_obj)

    return roots


@router.post(
    "/categories",
    response_model=CatalogCategoryRead,
    status_code=status.HTTP_201_CREATED,
)
async def create_category(
    data: CatalogCategoryCreate,
    user: dict = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
):
    org_id = resolve_org_id(user)

    parent_depth = 0
    if data.parent_id is not None:
        parent = await db.get(CatalogCategory, data.parent_id)
        if not parent or parent.org_id != org_id:
            raise HTTPException(status_code=404, detail="Parent category not found")
        parent_depth = await _get_category_depth(db, data.parent_id)
        if parent_depth >= 3:
            raise HTTPException(
                status_code=400,
                detail="Превышена максимальная глубина вложенности каталога (максимум 3 уровня)",
            )

    cat = CatalogCategory(
        org_id=org_id,
        name=data.name.strip(),
        parent_id=data.parent_id,
        sort_order=data.sort_order,
    )
    db.add(cat)
    await db.commit()
    await db.refresh(cat)

    return CatalogCategoryRead(
        id=cat.id,
        org_id=cat.org_id,
        name=cat.name,
        parent_id=cat.parent_id,
        sort_order=cat.sort_order,
        depth=parent_depth + 1,
        created_at=cat.created_at,
        updated_at=cat.updated_at,
        items_count=0,
        children=[],
    )


@router.put("/categories/{category_id}", response_model=CatalogCategoryRead)
async def update_category(
    category_id: int,
    data: CatalogCategoryUpdate,
    user: dict = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
):
    org_id = resolve_org_id(user)
    cat = await db.get(CatalogCategory, category_id)
    if not cat or cat.org_id != org_id:
        raise HTTPException(status_code=404, detail="Category not found")

    if data.parent_id is not None and data.parent_id != cat.parent_id:
        if data.parent_id == cat.id:
            raise HTTPException(
                status_code=400, detail="Cannot set category as its own parent"
            )
        parent = await db.get(CatalogCategory, data.parent_id)
        if not parent or parent.org_id != org_id:
            raise HTTPException(status_code=404, detail="Parent category not found")
        parent_depth = await _get_category_depth(db, data.parent_id)
        if parent_depth >= 3:
            raise HTTPException(
                status_code=400,
                detail="Превышена максимальная глубина вложенности каталога (максимум 3 уровня)",
            )
        cat.parent_id = data.parent_id

    if data.name is not None:
        cat.name = data.name.strip()
    if data.sort_order is not None:
        cat.sort_order = data.sort_order

    await db.commit()
    await db.refresh(cat)

    depth = await _get_category_depth(db, cat.id)
    count = await db.scalar(
        select(func.count(CatalogItem.id)).where(CatalogItem.category_id == cat.id)
    )

    return CatalogCategoryRead(
        id=cat.id,
        org_id=cat.org_id,
        name=cat.name,
        parent_id=cat.parent_id,
        sort_order=cat.sort_order,
        depth=depth,
        created_at=cat.created_at,
        updated_at=cat.updated_at,
        items_count=count or 0,
        children=[],
    )


@router.delete("/categories/{category_id}")
async def delete_category(
    category_id: int,
    user: dict = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
):
    org_id = resolve_org_id(user)
    cat = await db.get(CatalogCategory, category_id)
    if not cat or cat.org_id != org_id:
        raise HTTPException(status_code=404, detail="Category not found")

    await db.delete(cat)
    await db.commit()
    return {"ok": True}


@router.get("/free-tsp")
async def get_free_catalog_tsp(
    user: dict = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
):
    org_id = resolve_org_id(user)

    used_codes_res = await db.execute(
        select(CatalogItem.tsp_code).where(CatalogItem.org_id == org_id)
    )
    used = {row[0] for row in used_codes_res.fetchall()}

    all_codes = set(range(CATALOG_TSP_START, CATALOG_TSP_END + 1))
    free = sorted(all_codes - used)
    return [{"tsp_code": code} for code in free[:100]]


@router.get("/items", response_model=list[CatalogItemRead])
async def list_items(
    category_id: int | None = None,
    search: str | None = None,
    user: dict = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
):
    org_id = resolve_org_id(user)

    stmt = (
        select(CatalogItem)
        .options(selectinload(CatalogItem.category))
        .where(CatalogItem.org_id == org_id)
    )
    if category_id is not None:
        stmt = stmt.where(CatalogItem.category_id == category_id)
    if search:
        s = f"%{search.strip()}%"
        stmt = stmt.where(
            (CatalogItem.name.ilike(s)) | (CatalogItem.printname.ilike(s))
        )

    stmt = stmt.order_by(CatalogItem.tsp_code)
    result = await db.execute(stmt)
    items = result.scalars().all()

    return [
        CatalogItemRead(
            id=item.id,
            org_id=item.org_id,
            category_id=item.category_id,
            tsp_code=item.tsp_code,
            name=item.name,
            printname=item.printname,
            price=item.price,
            protypenumber=item.protypenumber,
            category_name=item.category.name if item.category else None,
            created_at=item.created_at,
            updated_at=item.updated_at,
        )
        for item in items
    ]


@router.post(
    "/items", response_model=CatalogItemRead, status_code=status.HTTP_201_CREATED
)
async def create_item(
    data: CatalogItemCreate,
    user: dict = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
):
    org_id = resolve_org_id(user)

    category = await db.get(CatalogCategory, data.category_id)
    if not category or category.org_id != org_id:
        raise HTTPException(status_code=404, detail="Category not found")

    tsp_code = data.tsp_code
    if not tsp_code or tsp_code == 0:
        used_codes_res = await db.execute(
            select(CatalogItem.tsp_code).where(CatalogItem.org_id == org_id)
        )
        used = {row[0] for row in used_codes_res.fetchall()}
        code = CATALOG_TSP_START
        while code in used and code <= CATALOG_TSP_END:
            code += 1
        if code > CATALOG_TSP_END:
            raise HTTPException(
                status_code=400,
                detail=f"Все коды ТСП каталога ({CATALOG_TSP_START}..{CATALOG_TSP_END}) исчерпаны",
            )
        tsp_code = code
    else:
        if not (CATALOG_TSP_START <= tsp_code <= CATALOG_TSP_END):
            raise HTTPException(
                status_code=400,
                detail=f"Код ТСП каталога должен быть в диапазоне {CATALOG_TSP_START}..{CATALOG_TSP_END}",
            )
        existing = await db.scalar(
            select(CatalogItem).where(
                CatalogItem.tsp_code == tsp_code, CatalogItem.org_id == org_id
            )
        )
        if existing:
            raise HTTPException(
                status_code=409,
                detail=f"Код ТСП {tsp_code} уже используется в каталоге организации",
            )

    item = CatalogItem(
        org_id=org_id,
        category_id=data.category_id,
        tsp_code=tsp_code,
        name=data.name.strip(),
        printname=data.printname.strip() if data.printname else None,
        price=data.price,
        protypenumber=data.protypenumber,
    )
    db.add(item)
    await db.commit()
    await db.refresh(item)

    return CatalogItemRead(
        id=item.id,
        org_id=item.org_id,
        category_id=item.category_id,
        tsp_code=item.tsp_code,
        name=item.name,
        printname=item.printname,
        price=item.price,
        protypenumber=item.protypenumber,
        category_name=category.name,
        created_at=item.created_at,
        updated_at=item.updated_at,
    )


@router.put("/items/{item_id}", response_model=CatalogItemRead)
async def update_item(
    item_id: int,
    data: CatalogItemUpdate,
    user: dict = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
):
    org_id = resolve_org_id(user)
    item = await db.get(CatalogItem, item_id)
    if not item or item.org_id != org_id:
        raise HTTPException(status_code=404, detail="Item not found")

    if data.category_id is not None and data.category_id != item.category_id:
        category = await db.get(CatalogCategory, data.category_id)
        if not category or category.org_id != org_id:
            raise HTTPException(status_code=404, detail="Category not found")
        item.category_id = data.category_id

    if data.name is not None:
        item.name = data.name.strip()
    if data.printname is not None:
        item.printname = data.printname.strip() if data.printname else None
    if data.price is not None:
        item.price = data.price
    if data.protypenumber is not None:
        item.protypenumber = data.protypenumber

    await db.commit()
    await db.refresh(item)

    cat = await db.get(CatalogCategory, item.category_id)
    return CatalogItemRead(
        id=item.id,
        org_id=item.org_id,
        category_id=item.category_id,
        tsp_code=item.tsp_code,
        name=item.name,
        printname=item.printname,
        price=item.price,
        protypenumber=item.protypenumber,
        category_name=cat.name if cat else None,
        created_at=item.created_at,
        updated_at=item.updated_at,
    )


@router.delete("/items/{item_id}")
async def delete_item(
    item_id: int,
    user: dict = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
):
    org_id = resolve_org_id(user)
    item = await db.get(CatalogItem, item_id)
    if not item or item.org_id != org_id:
        raise HTTPException(status_code=404, detail="Item not found")

    await db.delete(item)
    await db.commit()
    return {"ok": True}


@router.post("/propagate-to-menus", response_model=CatalogPropagateResponse)
async def propagate_catalog_to_menus(
    user: dict = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
):
    """
    Propagate catalog changes to all linked services across menu variants.
    Increments menu variant version for all affected variants and creates snapshots.
    """
    org_id = resolve_org_id(user)

    # 1. Fetch all catalog items for this org
    items_stmt = select(CatalogItem).where(CatalogItem.org_id == org_id)
    items_res = await db.execute(items_stmt)
    catalog_items = {item.id: item for item in items_res.scalars().all()}

    if not catalog_items:
        return CatalogPropagateResponse(
            updated_variants_count=0,
            updated_services_count=0,
            affected_variant_names=[],
        )

    # 2. Fetch all services linked to these catalog items
    services_stmt = (
        select(Service)
        .join(MenuVariant, MenuVariant.id == Service.menu_variant_id)
        .where(
            MenuVariant.org_id == org_id,
            Service.catalog_item_id.in_(list(catalog_items.keys())),
        )
    )
    services_res = await db.execute(services_stmt)
    linked_services = services_res.scalars().all()

    affected_variant_ids: set[int] = set()
    updated_services_count = 0

    for s in linked_services:
        if s.catalog_item_id is None:
            continue
        cat_item = catalog_items.get(s.catalog_item_id)
        if not cat_item:
            continue

        changed = False
        if s.name != cat_item.name:
            s.name = cat_item.name
            changed = True
        if s.printname != cat_item.printname:
            s.printname = cat_item.printname
            changed = True
        if s.price != cat_item.price:
            s.price = cat_item.price
            changed = True
        if s.protypenumber != cat_item.protypenumber:
            s.protypenumber = cat_item.protypenumber
            changed = True

        if changed:
            affected_variant_ids.add(s.menu_variant_id)
            updated_services_count += 1

    # 3. For all affected variants, increment version and create snapshot
    affected_variants_list: list[MenuVariant] = []
    if affected_variant_ids:
        var_stmt = select(MenuVariant).where(
            MenuVariant.id.in_(list(affected_variant_ids))
        )
        var_res = await db.execute(var_stmt)
        affected_variants_list = list(var_res.scalars().all())

        for variant in affected_variants_list:
            variant.version = (variant.version or 1) + 1

    await db.commit()

    return CatalogPropagateResponse(
        updated_variants_count=len(affected_variants_list),
        updated_services_count=updated_services_count,
        affected_variant_names=[v.name for v in affected_variants_list],
    )
