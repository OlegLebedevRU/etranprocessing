from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy import func, select

from app.auth import require_tenant_context, resolve_org_id
from app.database import async_session
from app.models import Group, MenuVariant, Service

router = APIRouter(prefix="/api", tags=["dashboard"])


@router.get("/stats")
async def get_stats(
    variant_id: int | None = None,
    user: dict = Depends(require_tenant_context),
):
    org_id = resolve_org_id(user)

    async with async_session() as session:
        if variant_id is not None:
            variant = await session.get(MenuVariant, variant_id)
            if not variant or variant.org_id != org_id:
                raise HTTPException(status_code=404, detail="Variant not found")

        groups_q = select(func.count(Group.id))
        services_q = select(func.count(Service.id)).join(
            Group, Group.id == Service.group_id
        )
        tsp_q = select(func.count(func.distinct(Service.tsp_code))).join(
            Group, Group.id == Service.group_id
        )
        avg_q = select(func.avg(Service.price)).join(
            Group, Group.id == Service.group_id
        )

        groups_q = groups_q.where(Group.org_id == org_id)
        services_q = services_q.where(Group.org_id == org_id)
        tsp_q = tsp_q.where(Group.org_id == org_id)
        avg_q = avg_q.where(Group.org_id == org_id)

        if variant_id:
            groups_q = groups_q.where(Group.menu_variant_id == variant_id)
            services_q = services_q.where(Service.menu_variant_id == variant_id)
            tsp_q = tsp_q.where(Service.menu_variant_id == variant_id)
            avg_q = avg_q.where(Service.menu_variant_id == variant_id)

        groups_count = await session.scalar(groups_q)
        services_count = await session.scalar(services_q)
        tsp_count = await session.scalar(tsp_q)
        avg_price = await session.scalar(avg_q)

    return {
        "groups": groups_count or 0,
        "services": services_count or 0,
        "tsp_codes": tsp_count or 0,
        "avg_price": round(float(avg_price or 0), 2),
    }
