from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import joinedload

from app.database import get_db
from app.models import MenuVariant, TerminalMenuBinding
from app.schemas import TerminalBindingCreate, TerminalBindingRead

router = APIRouter()


@router.get("", response_model=list[TerminalBindingRead])
async def list_bindings(db: AsyncSession = Depends(get_db)):
    result = await db.execute(
        select(TerminalMenuBinding)
        .options(joinedload(TerminalMenuBinding.menu_variant))
        .order_by(TerminalMenuBinding.device_id)
    )
    bindings = result.scalars().unique().all()
    return [
        TerminalBindingRead(
            id=b.id,
            device_id=b.device_id,
            menu_variant_id=b.menu_variant_id,
            menu_variant_name=b.menu_variant.name if b.menu_variant else None,
            created_at=b.created_at,
        )
        for b in bindings
    ]


@router.post("", response_model=TerminalBindingRead, status_code=201)
async def create_or_update_binding(data: TerminalBindingCreate, db: AsyncSession = Depends(get_db)):
    # Validate variant exists
    variant = await db.get(MenuVariant, data.menu_variant_id)
    if not variant:
        raise HTTPException(status_code=404, detail="Menu variant not found")

    # Upsert: update if exists, create otherwise
    existing = await db.scalar(
        select(TerminalMenuBinding).where(TerminalMenuBinding.device_id == data.device_id)
    )
    if existing:
        existing.menu_variant_id = data.menu_variant_id
        await db.commit()
        await db.refresh(existing)
        return TerminalBindingRead(
            id=existing.id,
            device_id=existing.device_id,
            menu_variant_id=existing.menu_variant_id,
            menu_variant_name=variant.name,
            created_at=existing.created_at,
        )

    binding = TerminalMenuBinding(device_id=data.device_id, menu_variant_id=data.menu_variant_id)
    db.add(binding)
    await db.commit()
    await db.refresh(binding)
    return TerminalBindingRead(
        id=binding.id,
        device_id=binding.device_id,
        menu_variant_id=binding.menu_variant_id,
        menu_variant_name=variant.name,
        created_at=binding.created_at,
    )


@router.delete("/{binding_id}")
async def delete_binding(binding_id: int, db: AsyncSession = Depends(get_db)):
    binding = await db.get(TerminalMenuBinding, binding_id)
    if not binding:
        raise HTTPException(status_code=404, detail="Binding not found")
    await db.delete(binding)
    await db.commit()
    return {"ok": True}
