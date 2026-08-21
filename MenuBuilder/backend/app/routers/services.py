from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import get_current_user
from app.database import get_db
from app.models import Group, MenuVariant, Service
from app.schemas import ServiceCreate, ServiceRead, ServiceUpdate

router = APIRouter()


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
    start_code = 1000301
    count_needed = 100
    all_codes = set(
        range(start_code, start_code + max(10000, len(used) + count_needed))
    )
    free = sorted(all_codes - used)
    return [{"tsp_code": code} for code in free[:count_needed]]


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

    tsp_code = data.tsp_code
    if not tsp_code or tsp_code == 0:
        used_codes = await db.execute(
            select(Service.tsp_code).where(
                Service.menu_variant_id == group.menu_variant_id
            )
        )
        used = {row[0] for row in used_codes.fetchall()}
        code = 1000301
        while code in used:
            code += 1
        tsp_code = code
    else:
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

    service_dict = data.model_dump()
    service_dict["tsp_code"] = tsp_code
    service = Service(**service_dict, menu_variant_id=group.menu_variant_id)
    db.add(service)
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

    await db.delete(service)
    await db.commit()
    return {"ok": True}
