from fastapi import APIRouter, Depends, HTTPException, Query
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import get_current_user
from app.database import get_db
from app.models import MenuVariant, TerminalMenuBinding
from app.schemas import TerminalBindingCreate, TerminalBindingRead, TerminalInfo

router = APIRouter()


@router.get("/terminals")
async def list_terminals(
    page: int = Query(1, ge=1),
    page_size: int = Query(20, ge=1, le=100),
    search: str | None = Query(None),
    user: dict = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    """List active licensed terminals for the current user's organization."""
    from datetime import UTC, datetime

    from sqlalchemy import text

    now = datetime.now(UTC)
    org_id = user.get("org_id")
    if org_id is not None:
        try:
            org_id = int(org_id)
        except ValueError, TypeError:
            org_id = None

    conditions = [
        "t.is_active = true",
        "EXISTS (SELECT 1 FROM licenses l WHERE l.terminal_id = t.id AND l.is_active = true AND (l.renewal_enabled = true OR l.expires_at > :now))",
    ]
    params: dict = {"now": now}

    if org_id is not None:
        conditions.append("t.org_id = :org_id")
        params["org_id"] = org_id

    if search:
        conditions.append(
            "(CAST(t.device_id AS TEXT) ILIKE :search OR t.sn ILIKE :search OR t.address ILIKE :search)"
        )
        params["search"] = f"%{search.strip()}%"

    where_clause = " AND ".join(conditions)

    # Count total for this org and active licenses
    total = await db.scalar(
        text(f"SELECT count(*) FROM terminals t WHERE {where_clause}"),
        params,
    )

    # Get page of terminals with bindings
    offset = (page - 1) * page_size
    params["limit"] = page_size
    params["offset"] = offset

    rows = (
        await db.execute(
            text(f"""
                SELECT t.id, t.device_id, t.sn, t.org_id, t.is_active,
                       tmb.id as binding_id, tmb.menu_variant_id, mv.name as variant_name,
                       t.address, t.note, t.terminal_type_id, tt.name as terminal_type_name,
                       t.created_at
                FROM terminals t
                LEFT JOIN terminal_types tt ON tt.id = t.terminal_type_id
                LEFT JOIN terminal_menu_bindings tmb ON tmb.device_id = t.device_id
                LEFT JOIN menu_variants mv ON mv.id = tmb.menu_variant_id
                WHERE {where_clause}
                ORDER BY t.device_id
                LIMIT :limit OFFSET :offset
            """),
            params,
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
                address=r[8],
                note=r[9],
                terminal_type_id=r[10] if r[10] is not None else 0,
                terminal_type_name=r[11]
                if r[11] is not None
                else (
                    "Стандартный" if (r[10] == 0 or r[10] is None) else f"Тип {r[10]}"
                ),
                created_at=r[12],
            )
        )

    return {"total": total or 0, "page": page, "page_size": page_size, "items": items}


@router.post("/bindings", response_model=TerminalBindingRead, status_code=201)
async def create_or_update_binding(
    data: TerminalBindingCreate, db: AsyncSession = Depends(get_db)
):
    variant = await db.get(MenuVariant, data.menu_variant_id)
    if not variant:
        raise HTTPException(status_code=404, detail="Menu variant not found")

    existing = await db.scalar(
        select(TerminalMenuBinding).where(
            TerminalMenuBinding.device_id == data.device_id
        )
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

    binding = TerminalMenuBinding(
        device_id=data.device_id, menu_variant_id=data.menu_variant_id
    )
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
