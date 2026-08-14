from contextlib import asynccontextmanager

from fastapi import FastAPI, Query
from fastapi.middleware.cors import CORSMiddleware
from sqlalchemy import select
from sqlalchemy.orm import selectinload

from app.config import settings
from app.database import Base, engine, async_session
from app.models import Group, MenuVariant, Service, TerminalMenuBinding
from app.routers import groups, menu_variants, services, terminal_bindings


@asynccontextmanager
async def lifespan(app: FastAPI):
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
    yield


app = FastAPI(title="MenuBuilder API", version="0.2.0", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.cors_origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(groups.router, prefix="/api/groups", tags=["groups"])
app.include_router(services.router, prefix="/api/services", tags=["services"])
app.include_router(menu_variants.router, prefix="/api/menu-variants", tags=["menu-variants"])
app.include_router(terminal_bindings.router, prefix="/api", tags=["terminals"])


def _build_menu_tree(groups_list, services_list):
    groups_by_parent = {}
    for g in groups_list:
        groups_by_parent.setdefault(g.parent_id, []).append(g)

    services_by_group = {}
    for s in services_list:
        services_by_group.setdefault(s.group_id, []).append(s)

    def build(parent_id):
        items = []
        for g in sorted(groups_by_parent.get(parent_id, []), key=lambda x: x.number):
            node = {"name": g.name}
            child_items = build(g.id)
            for s in sorted(services_by_group.get(g.id, []), key=lambda x: x.tsp_code):
                svc = {"name": s.name, "code": s.tsp_code, "prototypeid": s.protypenumber}
                if s.printname:
                    svc["printname"] = s.printname
                if s.price:
                    svc["price"] = str(s.price)
                child_items.append(svc)
            if child_items:
                node["items"] = child_items
            else:
                node["items"] = []
            items.append(node)
        return items

    return {"name": "root", "items": build(None)}


@app.get("/api/ListMenuFile")
async def list_menu_file(
    variant_id: int | None = Query(None, description="Menu variant ID. If omitted, uses terminal binding or first variant."),
    device_id: int | None = Query(None, description="Terminal device_id. Used to look up assigned variant."),
):
    async with async_session() as session:
        # Resolve variant
        variant = None
        if variant_id:
            variant = await session.get(MenuVariant, variant_id)
        elif device_id:
            binding = await session.scalar(
                select(TerminalMenuBinding).where(TerminalMenuBinding.device_id == device_id)
            )
            if binding:
                variant = await session.get(MenuVariant, binding.menu_variant_id)

        if not variant:
            # Fallback: first variant
            variant = await session.scalar(select(MenuVariant).order_by(MenuVariant.id).limit(1))

        if not variant:
            return {"name": "root", "items": []}

        groups_result = await session.execute(
            select(Group).where(Group.menu_variant_id == variant.id).order_by(Group.number)
        )
        groups_list = groups_result.scalars().all()

        group_ids = [g.id for g in groups_list]
        services_result = await session.execute(
            select(Service).where(Service.group_id.in_(group_ids)).order_by(Service.tsp_code)
        )
        services_list = services_result.scalars().all()

    return _build_menu_tree(groups_list, services_list)


@app.get("/api/stats")
async def get_stats(variant_id: int | None = None):
    from sqlalchemy import func

    async with async_session() as session:
        groups_q = select(func.count(Group.id))
        services_q = select(func.count(Service.id))
        tsp_q = select(func.count(func.distinct(Service.tsp_code)))
        avg_q = select(func.avg(Service.price))

        if variant_id:
            groups_q = groups_q.where(Group.menu_variant_id == variant_id)
            services_q = services_q.join(Group).where(Group.menu_variant_id == variant_id)
            tsp_q = tsp_q.join(Group).where(Group.menu_variant_id == variant_id)
            avg_q = avg_q.join(Group).where(Group.menu_variant_id == variant_id)

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


@app.get("/api/monitoring")
async def get_monitoring():
    """Terminal monitoring: connection history from GateGauge records.
    Returns last 2 hours in 10-min intervals (12 slots per terminal)."""
    from datetime import datetime, timedelta, timezone
    from sqlalchemy import text

    now = datetime.now(timezone.utc)
    start = now - timedelta(hours=2)

    async with async_session() as session:
        # Get all terminals
        terminals = (await session.execute(
            text("SELECT id, device_id, sn, org_id, is_active FROM terminals ORDER BY device_id")
        )).fetchall()

        # Get GateGauge records for last 2 hours
        records = (await session.execute(
            text("""
                SELECT device_id, created_at
                FROM gate_gauge_records
                WHERE created_at >= :start
                ORDER BY device_id, created_at
            """),
            {"start": start},
        )).fetchall()

    # Build interval map: for each device, which 10-min slots have data
    slot_duration = timedelta(minutes=10)
    device_slots: dict[int, list[bool]] = {}

    for r in records:
        dev_id = r[0]
        ts = r[1]
        if dev_id not in device_slots:
            device_slots[dev_id] = [False] * 12
        # Which slot? 0 = oldest (2h ago), 11 = most recent
        delta = now - ts
        slot_index = 11 - int(delta.total_seconds() // 600)
        if 0 <= slot_index < 12:
            device_slots[dev_id][slot_index] = True

    items = []
    for t in terminals:
        dev_id = t[1]
        slots = device_slots.get(dev_id, [False] * 12)
        items.append({
            "terminal_id": t[0],
            "device_id": dev_id,
            "sn": t[2],
            "org_id": t[3],
            "is_active": t[4],
            "slots": slots,
        })

    return {"start": start.isoformat(), "now": now.isoformat(), "items": items}
