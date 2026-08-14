from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.database import get_db
from app.models import Group
from app.schemas import GroupCreate, GroupRead, GroupUpdate

router = APIRouter()


@router.get("", response_model=list[GroupRead])
async def list_groups(org_id: int | None = None, db: AsyncSession = Depends(get_db)):
    stmt = select(Group).options(selectinload(Group.children))
    if org_id is not None:
        stmt = stmt.where(Group.org_id == org_id)
    stmt = stmt.order_by(Group.number)
    result = await db.execute(stmt)
    return result.scalars().all()


@router.get("/{group_id}", response_model=GroupRead)
async def get_group(group_id: int, db: AsyncSession = Depends(get_db)):
    group = await db.get(Group, group_id)
    if not group:
        raise HTTPException(status_code=404, detail="Group not found")
    return group


@router.post("", response_model=GroupRead, status_code=201)
async def create_group(data: GroupCreate, db: AsyncSession = Depends(get_db)):
    if data.number == 0:
        max_num = await db.scalar(
            select(Group.number).where(Group.org_id == data.org_id).order_by(Group.number.desc()).limit(1)
        )
        data.number = (max_num or 0) + 1
    group = Group(**data.model_dump())
    db.add(group)
    await db.commit()
    await db.refresh(group)
    return group


@router.put("/{group_id}", response_model=GroupRead)
async def update_group(group_id: int, data: GroupUpdate, db: AsyncSession = Depends(get_db)):
    group = await db.get(Group, group_id)
    if not group:
        raise HTTPException(status_code=404, detail="Group not found")
    for key, value in data.model_dump(exclude_unset=True).items():
        setattr(group, key, value)
    await db.commit()
    await db.refresh(group)
    return group


@router.delete("/{group_id}")
async def delete_group(group_id: int, db: AsyncSession = Depends(get_db)):
    group = await db.get(Group, group_id, options=[selectinload(Group.services), selectinload(Group.children)])
    if not group:
        raise HTTPException(status_code=404, detail="Group not found")
    if group.services:
        raise HTTPException(status_code=409, detail="Cannot delete group with services. Remove services first.")
    if group.children:
        raise HTTPException(status_code=409, detail="Cannot delete group with subgroups. Remove subgroups first.")
    await db.delete(group)
    await db.commit()
    return {"ok": True}
