from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import get_current_user
from app.database import get_db
from app.models import CatalogCategory, CatalogItem, Group, MenuVariant, Service
from app.schemas import ServiceCreate, ServiceRead, ServiceUpdate

router = APIRouter()

CATALOG_TSP_START = 1000301
CATALOG_TSP_END = 1000999

CUSTOM_TSP_START = 1001301
CUSTOM_TSP_END = 1001999


@router.get("", response_model=list[ServiceRead])
async def list_services(
    group_id: int | None = None,
    menu_variant_id: int | None = None,
    user: dict = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    org_id = user.get("org_id")
    stmt = select(Service)
    if group_id is not None:
        group = await db.get(Group, group_id)
        if not group:
            raise HTTPException(status_code=404, detail="Group not found")
        if (
            org_id
            and org_id > 0
            and group.org_id != org_id
            and not user.get("is_superuser")
        ):
            raise HTTPException(status_code=404, detail="Group not found")
        stmt = stmt.where(Service.group_id == group_id)
    elif menu_variant_id is not None:
        variant = await db.get(MenuVariant, menu_variant_id)
        if not variant:
            raise HTTPException(status_code=404, detail="Menu variant not found")
        if (
            org_id
            and org_id > 0
            and variant.org_id != org_id
            and not user.get("is_superuser")
        ):
            raise HTTPException(status_code=404, detail="Menu variant not found")
        stmt = stmt.where(Service.menu_variant_id == menu_variant_id)
    else:
        stmt = stmt.join(Group, Group.id == Service.group_id)
        if org_id and org_id > 0:
            stmt = stmt.where(Group.org_id == org_id)
        elif not user.get("is_superuser"):
            return []

    stmt = stmt.order_by(Service.tsp_code)
    result = await db.execute(stmt)
    return result.scalars().all()


@router.get("/free-tsp")
async def get_free_tsp(
    group_id: int,
    from_catalog: bool = False,
    user: dict = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    group = await db.get(Group, group_id)
    if not group:
        raise HTTPException(status_code=404, detail="Group not found")
    org_id = user.get("org_id")
    if (
        org_id
        and org_id > 0
        and group.org_id != org_id
        and not user.get("is_superuser")
    ):
        raise HTTPException(status_code=404, detail="Group not found")
    if not org_id and not user.get("is_superuser"):
        raise HTTPException(status_code=403, detail="Forbidden")

    used_codes = await db.execute(
        select(Service.tsp_code).where(Service.menu_variant_id == group.menu_variant_id)
    )
    used = {row[0] for row in used_codes.fetchall()}

    if from_catalog:
        catalog_used_res = await db.execute(
            select(CatalogItem.tsp_code).where(CatalogItem.org_id == org_id)
        )
        used.update(row[0] for row in catalog_used_res.fetchall())
        all_codes = set(range(CATALOG_TSP_START, CATALOG_TSP_END + 1))
    else:
        all_codes = set(range(CUSTOM_TSP_START, CUSTOM_TSP_END + 1))

    free = sorted(all_codes - used)
    return [{"tsp_code": code} for code in free[:100]]


@router.get("/{service_id}", response_model=ServiceRead)
async def get_service(
    service_id: int,
    user: dict = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    service = await db.get(Service, service_id)
    if not service:
        raise HTTPException(status_code=404, detail="Service not found")
    org_id = user.get("org_id")
    if org_id and org_id > 0:
        group = await db.get(Group, service.group_id)
        if not group or (group.org_id != org_id and not user.get("is_superuser")):
            raise HTTPException(status_code=404, detail="Service not found")
    elif not user.get("is_superuser"):
        raise HTTPException(status_code=403, detail="Forbidden")
    return service


@router.post("", response_model=ServiceRead, status_code=201)
async def create_service(
    data: ServiceCreate,
    user: dict = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    group = await db.get(Group, data.group_id)
    if not group:
        raise HTTPException(status_code=404, detail="Group not found")
    org_id = user.get("org_id")
    if (
        org_id
        and org_id > 0
        and group.org_id != org_id
        and not user.get("is_superuser")
    ):
        raise HTTPException(status_code=404, detail="Group not found")
    if not org_id and not user.get("is_superuser"):
        raise HTTPException(status_code=403, detail="Forbidden")

    catalog_item_id = data.catalog_item_id
    tsp_code = data.tsp_code

    # If linked from catalog
    if catalog_item_id:
        cat_item = await db.get(CatalogItem, catalog_item_id)
        if not cat_item or (
            org_id and cat_item.org_id != org_id and not user.get("is_superuser")
        ):
            raise HTTPException(status_code=404, detail="Catalog item not found")
        tsp_code = cat_item.tsp_code

    # If creating a new service marked "Добавить в каталог"
    elif data.add_to_catalog:
        category_id = data.catalog_category_id
        if not category_id:
            # Fallback to first category of this org
            first_cat = await db.scalar(
                select(CatalogCategory)
                .where(CatalogCategory.org_id == org_id)
                .order_by(CatalogCategory.id)
            )
            if not first_cat:
                first_cat = CatalogCategory(
                    org_id=org_id, name="Общие услуги", sort_order=0
                )
                db.add(first_cat)
                await db.flush()
            category_id = first_cat.id
        else:
            cat = await db.get(CatalogCategory, category_id)
            if not cat or (
                org_id and cat.org_id != org_id and not user.get("is_superuser")
            ):
                raise HTTPException(
                    status_code=404, detail="Catalog category not found"
                )

        # Allocate catalog TSP code
        if not tsp_code or not (CATALOG_TSP_START <= tsp_code <= CATALOG_TSP_END):
            used_res = await db.execute(
                select(CatalogItem.tsp_code).where(CatalogItem.org_id == org_id)
            )
            used_cat = {r[0] for r in used_res.fetchall()}
            code = CATALOG_TSP_START
            while code in used_cat and code <= CATALOG_TSP_END:
                code += 1
            if code > CATALOG_TSP_END:
                raise HTTPException(
                    status_code=400, detail="Catalog TSP codes exhausted"
                )
            tsp_code = code

        # Create CatalogItem
        new_cat_item = CatalogItem(
            org_id=org_id,
            category_id=category_id,
            tsp_code=tsp_code,
            name=data.name.strip(),
            printname=data.printname.strip() if data.printname else None,
            price=data.price,
            protypenumber=data.protypenumber,
        )
        db.add(new_cat_item)
        await db.flush()
        catalog_item_id = new_cat_item.id

    # Custom local service
    else:
        if not tsp_code or tsp_code == 0:
            used_codes_res = await db.execute(
                select(Service.tsp_code).where(
                    Service.menu_variant_id == group.menu_variant_id
                )
            )
            used = {row[0] for row in used_codes_res.fetchall()}
            code = CUSTOM_TSP_START
            while code in used and code <= CUSTOM_TSP_END:
                code += 1
            if code > CUSTOM_TSP_END:
                raise HTTPException(
                    status_code=400, detail="Custom TSP codes exhausted"
                )
            tsp_code = code

    # Check for collision in current menu variant
    existing = await db.scalar(
        select(Service).where(
            Service.tsp_code == tsp_code,
            Service.menu_variant_id == group.menu_variant_id,
        )
    )
    if existing:
        raise HTTPException(
            status_code=409,
            detail=f"tsp_code {tsp_code} already in use in this menu variant",
        )

    service = Service(
        menu_variant_id=group.menu_variant_id,
        group_id=data.group_id,
        tsp_code=tsp_code,
        name=data.name.strip(),
        printname=data.printname.strip() if data.printname else None,
        price=data.price,
        protypenumber=data.protypenumber,
        catalog_item_id=catalog_item_id,
    )
    db.add(service)
    variant = await db.get(MenuVariant, group.menu_variant_id)
    if variant:
        variant.version = (variant.version or 1) + 1
    await db.commit()
    await db.refresh(service)
    return service


@router.put("/{service_id}", response_model=ServiceRead)
async def update_service(
    service_id: int,
    data: ServiceUpdate,
    user: dict = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    service = await db.get(Service, service_id)
    if not service:
        raise HTTPException(status_code=404, detail="Service not found")
    org_id = user.get("org_id")
    if org_id and org_id > 0:
        group = await db.get(Group, service.group_id)
        if not group or (group.org_id != org_id and not user.get("is_superuser")):
            raise HTTPException(status_code=404, detail="Service not found")
    elif not user.get("is_superuser"):
        raise HTTPException(status_code=403, detail="Forbidden")

    for key, value in data.model_dump(exclude_unset=True).items():
        setattr(service, key, value)
    variant = await db.get(MenuVariant, service.menu_variant_id)
    if variant:
        variant.version = (variant.version or 1) + 1
    await db.commit()
    await db.refresh(service)
    return service


@router.delete("/{service_id}")
async def delete_service(
    service_id: int,
    user: dict = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    service = await db.get(Service, service_id)
    if not service:
        raise HTTPException(status_code=404, detail="Service not found")
    org_id = user.get("org_id")
    if org_id and org_id > 0:
        group = await db.get(Group, service.group_id)
        if not group or (group.org_id != org_id and not user.get("is_superuser")):
            raise HTTPException(status_code=404, detail="Service not found")
    elif not user.get("is_superuser"):
        raise HTTPException(status_code=403, detail="Forbidden")

    variant = await db.get(MenuVariant, service.menu_variant_id)
    if variant:
        variant.version = (variant.version or 1) + 1
    await db.delete(service)
    await db.commit()
    return {"ok": True}
