import re

from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.database import get_db
from app.models import Group, MenuVariant, Service
from app.schemas import MenuVariantCreate, MenuVariantDuplicate, MenuVariantRead

router = APIRouter()


@router.get("", response_model=list[MenuVariantRead])
async def list_variants(db: AsyncSession = Depends(get_db)):
    result = await db.execute(select(MenuVariant).order_by(MenuVariant.name))
    return result.scalars().all()


@router.get("/{variant_id}", response_model=MenuVariantRead)
async def get_variant(variant_id: int, db: AsyncSession = Depends(get_db)):
    variant = await db.get(MenuVariant, variant_id)
    if not variant:
        raise HTTPException(status_code=404, detail="Menu variant not found")
    return variant


@router.post("", response_model=MenuVariantRead, status_code=201)
async def create_variant(data: MenuVariantCreate, db: AsyncSession = Depends(get_db)):
    existing = await db.scalar(select(MenuVariant).where(MenuVariant.name == data.name))
    if existing:
        raise HTTPException(
            status_code=409, detail=f"Variant '{data.name}' already exists"
        )
    variant = MenuVariant(name=data.name)
    db.add(variant)
    await db.commit()
    await db.refresh(variant)
    return variant


@router.delete("/{variant_id}")
async def delete_variant(variant_id: int, db: AsyncSession = Depends(get_db)):
    variant = await db.get(MenuVariant, variant_id)
    if not variant:
        raise HTTPException(status_code=404, detail="Menu variant not found")
    await db.delete(variant)
    await db.commit()
    return {"ok": True}


@router.post("/duplicate", response_model=MenuVariantRead, status_code=201)
async def duplicate_variant(
    data: MenuVariantDuplicate, db: AsyncSession = Depends(get_db)
):
    source = await db.get(
        MenuVariant,
        data.source_variant_id,
        options=[
            selectinload(MenuVariant.groups).selectinload(Group.services),
        ],
    )
    if not source:
        raise HTTPException(status_code=404, detail="Source variant not found")

    # Generate name with +1 suffix
    if data.new_name:
        new_name = data.new_name
    else:
        match = re.match(r"^(.+?)(\d+)$", source.name)
        if match:
            base, num = match.group(1), int(match.group(2))
            new_name = f"{base}{num + 1}"
        else:
            new_name = f"{source.name} (копия)"

    existing = await db.scalar(select(MenuVariant).where(MenuVariant.name == new_name))
    if existing:
        raise HTTPException(
            status_code=409, detail=f"Variant '{new_name}' already exists"
        )

    new_variant = MenuVariant(name=new_name)
    db.add(new_variant)
    await db.flush()  # get new_variant.id

    # Copy groups, mapping old IDs to new
    old_group_map: dict[int, Group] = {}
    for g in source.groups:
        new_group = Group(
            menu_variant_id=new_variant.id,
            org_id=g.org_id,
            number=g.number,
            name=g.name,
            parent_id=None,  # set after all groups created
        )
        old_group_map[g.id] = new_group
        db.add(new_group)

    await db.flush()

    # Fix parent_id references
    for g in source.groups:
        if g.parent_id and g.parent_id in old_group_map:
            old_group_map[g.id].parent_id = old_group_map[g.parent_id].id

    # Copy services
    for g in source.groups:
        for s in g.services:
            new_service = Service(
                menu_variant_id=new_variant.id,
                group_id=old_group_map[g.id].id,
                tsp_code=s.tsp_code,
                name=s.name,
                printname=s.printname,
                price=s.price,
                protypenumber=s.protypenumber,
            )
            db.add(new_service)

    await db.commit()
    await db.refresh(new_variant)
    return new_variant
