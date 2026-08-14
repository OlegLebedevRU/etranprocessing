from fastapi import APIRouter, Depends, HTTPException, Query
from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import joinedload

from app.database import get_db
from app.models import MenuVariant, TerminalMenuBinding
from app.schemas import TerminalBindingCreate, TerminalBindingRead, TerminalInfo

router = APIRouter()


@router.get("/terminals")
async def list_terminals(
    page: int = Query(1, ge=1),
    page_size: int = Query(20, ge=1, le=100),
    db: AsyncSession = Depends(get_db),
):
    """List all terminals with their menu binding and activity status."""
    # Query terminals table directly (shared DB with ProcessingBackend)
    from sqlalchemy import text

    # Count total
    total = await db.scalar(text("SELECT count(*) FROM terminals"))

    # Get page of terminals with bindings
    offset = (page - 1) * page_size
    rows = (
        await db.execute(
            text("""
                SELECT t.id, t.device_id, t.sn, t.org_id, t.is_active,
                       tmb.id as binding_id, tmb.menu_variant_id, mv.name as variant_name
                FROM terminals t
                LEFT JOIN terminal_menu_bindings tmb ON tmb.device_id = t.device_id
                LEFT JOIN menu_variants mv ON mv.id = tmb.menu_variant_id
                ORDER BY t.device_id
                LIMIT :limit OFFSET :offset
            """),
            {"limit": page_size, "offset": offset},
        )
    ).fetchall()

    items = []
    for r in rows:
        items.append(
            TerminalInfo(
                terminal_id=r[0],
                device_id=r[1],
                sn=r[2],
                org_id=r[3],
                is_active=r[4],
                binding_id=r[5],
                menu_variant_id=r[6],
                menu_variant_name=r[7],
            )
        )

    return {"total": total or 0, "page": page, "page_size": page_size, "items": items}


@router.post("/bindings", response_model=TerminalBindingRead, status_code=201)
async def create_or_update_binding(data: TerminalBindingCreate, db: AsyncSession = Depends(get_db)):
    variant = await db.get(MenuVariant, data.menu_variant_id)
    if not variant:
        raise HTTPException(status_code=404, detail="Menu variant not found")

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


@router.delete("/bindings/{binding_id}")
async def delete_binding(binding_id: int, db: AsyncSession = Depends(get_db)):
    binding = await db.get(TerminalMenuBinding, binding_id)
    if not binding:
        raise HTTPException(status_code=404, detail="Binding not found")
    await db.delete(binding)
    await db.commit()
    return {"ok": True}
