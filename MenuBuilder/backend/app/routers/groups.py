from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.auth import get_current_user
from app.database import get_db
from app.models import Group, MenuVariant
from app.schemas import GroupCreate, GroupRead, GroupUpdate

router = APIRouter()


@router.get("", response_model=list[GroupRead])
async def list_groups(
    menu_variant_id: int,
    user: dict = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    org_id = user.get("org_id")
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
    if not org_id and not user.get("is_superuser"):
        raise HTTPException(status_code=403, detail="Forbidden")

    stmt = (
        select(Group)
        .where(Group.menu_variant_id == menu_variant_id)
        .options(selectinload(Group.children))
        .order_by(Group.number)
    )
    result = await db.execute(stmt)
    return result.scalars().all()


@router.get("/{group_id}", response_model=GroupRead)
async def get_group(
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
    return group


@router.post("", response_model=GroupRead, status_code=201)
async def create_group(
    data: GroupCreate,
    user: dict = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    org_id = user.get("org_id")
    if not org_id or org_id <= 0:
        raise HTTPException(status_code=400, detail="Organization context required")

    variant = await db.get(MenuVariant, data.menu_variant_id)
    if not variant:
        raise HTTPException(status_code=404, detail="Menu variant not found")
    if variant.org_id != org_id and not user.get("is_superuser"):
        raise HTTPException(status_code=404, detail="Menu variant not found")

    if data.parent_id:
        parent = await db.get(Group, data.parent_id)
        if not parent or parent.menu_variant_id != data.menu_variant_id:
            raise HTTPException(
                status_code=400, detail="Parent group not found in this menu variant"
            )
        if parent.org_id != org_id and not user.get("is_superuser"):
            raise HTTPException(
                status_code=400, detail="Parent group not found in this organization"
            )

    number = data.number
    if not number or number == 0:
        max_num = await db.scalar(
            select(Group.number)
            .where(
                Group.menu_variant_id == data.menu_variant_id,
                Group.org_id == org_id,
            )
            .order_by(Group.number.desc())
            .limit(1)
        )
        number = max((max_num or 0) + 1, 801)

    group = Group(
        menu_variant_id=data.menu_variant_id,
        org_id=org_id,
        name=data.name,
        parent_id=data.parent_id,
        number=number,
    )
    db.add(group)
    variant.version = (variant.version or 1) + 1
    await db.commit()
    await db.refresh(group)
    return group


@router.put("/{group_id}", response_model=GroupRead)
async def update_group(
    group_id: int,
    data: GroupUpdate,
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

    update_dict = data.model_dump(exclude_unset=True)
    if "parent_id" in update_dict and update_dict["parent_id"] is not None:
        if update_dict["parent_id"] == group.id:
            raise HTTPException(
                status_code=400, detail="Group cannot be its own parent"
            )
        parent = await db.get(Group, update_dict["parent_id"])
        if not parent or parent.menu_variant_id != group.menu_variant_id:
            raise HTTPException(
                status_code=400, detail="Parent group not found in this menu variant"
            )
        if (
            org_id
            and org_id > 0
            and parent.org_id != org_id
            and not user.get("is_superuser")
        ):
            raise HTTPException(
                status_code=400, detail="Parent group not found in this organization"
            )

    for key, value in update_dict.items():
        setattr(group, key, value)
    variant = await db.get(MenuVariant, group.menu_variant_id)
    if variant:
        variant.version = (variant.version or 1) + 1
    await db.commit()
    await db.refresh(group)
    return group


@router.delete("/{group_id}")
async def delete_group(
    group_id: int,
    user: dict = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    group = await db.get(
        Group,
        group_id,
        options=[selectinload(Group.services), selectinload(Group.children)],
    )
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

    if group.services:
        raise HTTPException(
            status_code=409,
            detail="Cannot delete group with services. Remove services first.",
        )
    if group.children:
        raise HTTPException(
            status_code=409,
            detail="Cannot delete group with subgroups. Remove subgroups first.",
        )
    variant = await db.get(MenuVariant, group.menu_variant_id)
    if variant:
        variant.version = (variant.version or 1) + 1
    await db.delete(group)
    await db.commit()
    return {"ok": True}
