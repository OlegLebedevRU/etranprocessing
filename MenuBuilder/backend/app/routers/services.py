from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.models import Group, Service
from app.schemas import ServiceCreate, ServiceRead, ServiceUpdate

router = APIRouter()


@router.get("", response_model=list[ServiceRead])
async def list_services(group_id: int | None = None, db: AsyncSession = Depends(get_db)):
    stmt = select(Service)
    if group_id is not None:
        stmt = stmt.where(Service.group_id == group_id)
    stmt = stmt.order_by(Service.tsp_code)
    result = await db.execute(stmt)
    return result.scalars().all()


@router.get("/free-tsp")
async def get_free_tsp(group_id: int, db: AsyncSession = Depends(get_db)):
    group = await db.get(Group, group_id)
    if not group:
        raise HTTPException(status_code=404, detail="Group not found")
    used_codes = await db.execute(
        select(Service.tsp_code).where(Service.group_id == group_id)
    )
    used = {row[0] for row in used_codes.fetchall()}
    all_codes = set(range(1, 10001))
    free = sorted(all_codes - used)
    return [{"tsp_code": code} for code in free[:100]]


@router.get("/{service_id}", response_model=ServiceRead)
async def get_service(service_id: int, db: AsyncSession = Depends(get_db)):
    service = await db.get(Service, service_id)
    if not service:
        raise HTTPException(status_code=404, detail="Service not found")
    return service


@router.post("", response_model=ServiceRead, status_code=201)
async def create_service(data: ServiceCreate, db: AsyncSession = Depends(get_db)):
    group = await db.get(Group, data.group_id)
    if not group:
        raise HTTPException(status_code=404, detail="Group not found")
    existing = await db.scalar(
        select(Service).where(
            Service.tsp_code == data.tsp_code,
            Service.menu_variant_id == group.menu_variant_id,
        )
    )
    if existing:
        raise HTTPException(status_code=409, detail=f"tsp_code {data.tsp_code} already in use in this menu variant")
    service = Service(**data.model_dump(), menu_variant_id=group.menu_variant_id)
    db.add(service)
    await db.commit()
    await db.refresh(service)
    return service


@router.put("/{service_id}", response_model=ServiceRead)
async def update_service(service_id: int, data: ServiceUpdate, db: AsyncSession = Depends(get_db)):
    service = await db.get(Service, service_id)
    if not service:
        raise HTTPException(status_code=404, detail="Service not found")
    for key, value in data.model_dump(exclude_unset=True).items():
        setattr(service, key, value)
    await db.commit()
    await db.refresh(service)
    return service


@router.delete("/{service_id}")
async def delete_service(service_id: int, db: AsyncSession = Depends(get_db)):
    service = await db.get(Service, service_id)
    if not service:
        raise HTTPException(status_code=404, detail="Service not found")
    await db.delete(service)
    await db.commit()
    return {"ok": True}
