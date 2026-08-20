from contextlib import asynccontextmanager
from datetime import UTC

from fastapi import Depends, FastAPI, Query
from fastapi.middleware.cors import CORSMiddleware
from sqlalchemy import select

from app.auth import get_current_user
from app.config import settings
from app.database import Base, async_session, engine
from app.models import Group, MenuVariant, Service, TerminalMenuBinding
from app.routers import (
    admin_tenants,
    auth,
    groups,
    mcp_proxy,
    menu_variants,
    profile,
    services,
    terminal_bindings,
)


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

app.include_router(auth.router, prefix="/api", tags=["auth"])
app.include_router(admin_tenants.router, prefix="/api", tags=["admin-tenants"])
app.include_router(groups.router, prefix="/api/groups", tags=["groups"])
app.include_router(services.router, prefix="/api/services", tags=["services"])
app.include_router(
    menu_variants.router, prefix="/api/menu-variants", tags=["menu-variants"]
)
app.include_router(terminal_bindings.router, prefix="/api", tags=["terminals"])
app.include_router(profile.router, prefix="/api", tags=["profile"])
app.include_router(mcp_proxy.router, prefix="/api", tags=["mcp"])


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
                svc = {
                    "name": s.name,
                    "code": s.tsp_code,
                    "prototypeid": s.protypenumber,
                }
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
    variant_id: int | None = Query(
        None,
        description="Menu variant ID. If omitted, uses terminal binding or first variant.",
    ),
    device_id: int | None = Query(
        None, description="Terminal device_id. Used to look up assigned variant."
    ),
):
    async with async_session() as session:
        # Resolve variant
        variant = None
        if variant_id:
            variant = await session.get(MenuVariant, variant_id)
        elif device_id:
            binding = await session.scalar(
                select(TerminalMenuBinding).where(
                    TerminalMenuBinding.device_id == device_id
                )
            )
            if binding:
                variant = await session.get(MenuVariant, binding.menu_variant_id)

        if not variant:
            # Fallback: first variant
            variant = await session.scalar(
                select(MenuVariant).order_by(MenuVariant.id).limit(1)
            )

        if not variant:
            return {"name": "root", "items": []}

        groups_result = await session.execute(
            select(Group)
            .where(Group.menu_variant_id == variant.id)
            .order_by(Group.number)
        )
        groups_list = groups_result.scalars().all()

        group_ids = [g.id for g in groups_list]
        services_result = await session.execute(
            select(Service)
            .where(Service.group_id.in_(group_ids))
            .order_by(Service.tsp_code)
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
            services_q = services_q.join(Group).where(
                Group.menu_variant_id == variant_id
            )
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
async def get_monitoring(
    page: int = Query(1, ge=1),
    page_size: int = Query(20, ge=1, le=100),
    search: str | None = Query(None),
    user: dict = Depends(get_current_user),
):
    """Terminal monitoring: connection history + GateGauge data.
    Returns last 2 hours in 10-min intervals (12 slots per terminal)
    plus latest device state from gauge_data.
    Filters out terminals disabled in licenses and supports server pagination."""
    from datetime import datetime, timedelta

    from sqlalchemy import text

    now = datetime.now(UTC)
    start = now - timedelta(hours=2)

    org_id = user.get("org_id")
    if org_id is not None:
        try:
            org_id = int(org_id)
        except ValueError, TypeError:
            org_id = None

    conditions = [
        "t.is_active = true",
        "EXISTS (SELECT 1 FROM licenses l WHERE l.terminal_id = t.id AND l.is_active = true AND l.renewal_enabled = true AND l.expires_at > :now)",
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

    async with async_session() as session:
        # Total count
        total = await session.scalar(
            text(f"SELECT count(*) FROM terminals t WHERE {where_clause}"),
            params,
        )

        offset = (page - 1) * page_size
        page_params = {**params, "limit": page_size, "offset": offset}

        # Page of terminals
        terminals = (
            await session.execute(
                text(
                    f"SELECT t.id, t.device_id, t.sn, t.org_id, t.is_active, "
                    f"t.cert_serial, t.cert_not_valid_after, t.address, t.note, "
                    f"t.terminal_type_id, tt.name AS terminal_type_name, t.created_at "
                    f"FROM terminals t "
                    f"LEFT JOIN terminal_types tt ON tt.id = t.terminal_type_id "
                    f"WHERE {where_clause} "
                    f"ORDER BY t.device_id "
                    f"LIMIT :limit OFFSET :offset"
                ),
                page_params,
            )
        ).fetchall()

        if not terminals:
            return {
                "start": start.isoformat(),
                "now": now.isoformat(),
                "total": total or 0,
                "page": page,
                "page_size": page_size,
                "items": [],
            }

        terminal_ids = [t[0] for t in terminals]
        device_ids = [t[1] for t in terminals]

        # Latest payment per terminal (scoped to current page only)
        payment_rows = (
            await session.execute(
                text(
                    "SELECT terminal_id, MAX(paym_datetime) "
                    "FROM payments "
                    "WHERE terminal_id = ANY(:t_ids) "
                    "GROUP BY terminal_id"
                ),
                {"t_ids": terminal_ids},
            )
        ).fetchall()

        # Latest license expiration per terminal (scoped to current page only)
        license_rows = (
            await session.execute(
                text("""
                SELECT DISTINCT ON (terminal_id) terminal_id, expires_at
                FROM licenses
                WHERE terminal_id = ANY(:t_ids)
                ORDER BY terminal_id, expires_at DESC
            """),
                {"t_ids": terminal_ids},
            )
        ).fetchall()

        # Get GateGauge records for last 2 hours (scoped to current page only)
        records = (
            await session.execute(
                text("""
                SELECT device_id, created_at
                FROM gate_gauge_records
                WHERE device_id = ANY(:d_ids) AND created_at >= :start
                ORDER BY device_id, created_at
            """),
                {"d_ids": device_ids, "start": start},
            )
        ).fetchall()

        # Get latest gauge_data per terminal (scoped to current page only)
        latest = (
            await session.execute(
                text("""
                SELECT DISTINCT ON (device_id) device_id, gauge_data
                FROM gate_gauge_records
                WHERE device_id = ANY(:d_ids)
                ORDER BY device_id, created_at DESC
            """),
                {"d_ids": device_ids},
            )
        ).fetchall()

    # Build latest gauge map
    gauge_map: dict[int, dict] = {}
    for r in latest:
        gauge_map[r[0]] = r[1] if r[1] else {}

    last_payment_map: dict[int, datetime] = {r[0]: r[1] for r in payment_rows if r[1]}
    license_map: dict[int, datetime] = {r[0]: r[1] for r in license_rows if r[1]}

    # Build interval map with overlap buffer to avoid false red on boundary shift
    SLOT_DURATION = 600  # 10 minutes
    SLOT_OVERLAP = 60  # 1 minute buffer on each side of boundary
    device_slots: dict[int, list[bool]] = {}
    for r in records:
        dev_id = r[0]
        ts = r[1]
        if dev_id not in device_slots:
            device_slots[dev_id] = [False] * 12
        delta_sec = (now - ts).total_seconds()
        slot_index = 11 - int(delta_sec // SLOT_DURATION)
        if 0 <= slot_index < 12:
            device_slots[dev_id][slot_index] = True
            # If near boundary, also mark adjacent slot to prevent false red on recalculation
            offset_in_slot = delta_sec % SLOT_DURATION
            if offset_in_slot < SLOT_OVERLAP and slot_index > 0:
                device_slots[dev_id][slot_index - 1] = True
            elif offset_in_slot > SLOT_DURATION - SLOT_OVERLAP and slot_index < 11:
                device_slots[dev_id][slot_index + 1] = True

    def fmt_soft_version(raw: str) -> str:
        if not raw or raw == "0":
            return "—"
        if "." in raw:
            return raw
        if len(raw) > 2:
            return raw[:-2] + "." + raw[-2:]
        return "0." + raw.zfill(2)

    items = []
    for t in terminals:
        dev_id = t[1]
        slots = device_slots.get(dev_id, [False] * 12)
        gauge = gauge_map.get(dev_id, {})
        last_payment = last_payment_map.get(t[0])
        license_expires = license_map.get(t[0])
        cert_not_valid_after = t[6]

        # lastnumconn: count trailing false in slots
        lastnumconn = 0
        for s in reversed(slots):
            if not s:
                lastnumconn += 1
            else:
                break

        # Extract resource codes from gauge_data (keys are strings in JSONB)
        def g(key: str, _gauge: dict = gauge) -> str:
            return str(_gauge.get(key, ""))

        validator_type_raw = g("112")
        # Resource 112 can be a name like "CCNET" or a numeric code
        validator_type_map = {"WBA003": 1, "CCNET": 2, "ICT U70": 4}
        validator_type = validator_type_map.get(validator_type_raw, 0)
        try:
            validator_type = int(validator_type_raw)
        except ValueError, TypeError:
            pass  # keep mapped value

        items.append(
            {
                "terminal_id": t[0],
                "device_id": dev_id,
                "sn": t[2],
                "org_id": t[3],
                "is_active": t[4],
                "slots": slots,
                "lastnumconn": lastnumconn,
                "validator_state": g("102"),
                "validator_type": validator_type,
                "cash_amount": int(g("109") or "0"),
                "printer_state": g("121"),
                "printer_fr": int(g("124") or "0"),
                "printer_check_counter": int(g("120") or "0"),
                "soft_version": fmt_soft_version(g("130")),
                "last_payment_at": last_payment.isoformat() if last_payment else None,
                "license_expires_at": license_expires.isoformat()
                if license_expires
                else None,
                "cert_serial": t[5],
                "cert_not_valid_after": cert_not_valid_after.isoformat()
                if cert_not_valid_after
                else None,
                "address": t[7],
                "note": t[8],
                "terminal_type_id": t[9] if t[9] is not None else 0,
                "terminal_type_name": t[10]
                if t[10] is not None
                else ("Стандартный" if (t[9] == 0 or t[9] is None) else f"Тип {t[9]}"),
                "created_at": t[11].isoformat() if t[11] else None,
            }
        )

    return {
        "start": start.isoformat(),
        "now": now.isoformat(),
        "total": total or 0,
        "page": page,
        "page_size": page_size,
        "items": items,
    }


@app.get("/api/reports/inkass")
async def get_inkass_report(
    date_from: str | None = None,
    date_to: str | None = None,
    device_ids: str | None = None,
    page: int = Query(1, ge=1),
    size: int = Query(100, ge=10, le=500),
):
    """Inkassation report from TechGate records."""
    from datetime import datetime

    from sqlalchemy import text

    async with async_session() as session:
        # Build filters
        conditions = ["r.function_name = 'inkass'"]
        params: dict = {}

        if date_from:
            conditions.append("r.created_at >= :date_from")
            params["date_from"] = date_from
        if date_to:
            conditions.append("r.created_at < (:date_to::date + interval '1 day')")
            params["date_to"] = date_to
        if device_ids:
            ids = [int(x.strip()) for x in device_ids.split(",") if x.strip().isdigit()]
            if ids:
                placeholders = ",".join(f":did_{i}" for i in range(len(ids)))
                conditions.append(f"r.device_id IN ({placeholders})")
                for i, did in enumerate(ids):
                    params[f"did_{i}"] = did

        where = " AND ".join(conditions)

        # Count
        count_row = (
            await session.execute(
                text(f"SELECT count(*) FROM tech_gate_records r WHERE {where}"),
                params,
            )
        ).scalar()

        # Fetch page
        offset = (page - 1) * size
        rows = (
            await session.execute(
                text(f"""
                SELECT r.id, r.device_id, r.sn, r.created_at, r.request_data
                FROM tech_gate_records r
                WHERE {where}
                ORDER BY r.created_at DESC
                LIMIT :limit OFFSET :offset
            """),
                {**params, "limit": size, "offset": offset},
            )
        ).fetchall()

        # Get terminal org_ids
        dev_ids = list({r[1] for r in rows})
        org_map: dict[int, int] = {}
        if dev_ids:
            ph = ",".join(f":oid_{i}" for i in range(len(dev_ids)))
            org_rows = (
                await session.execute(
                    text(
                        f"SELECT device_id, org_id FROM terminals WHERE device_id IN ({ph})"
                    ),
                    {f"oid_{i}": did for i, did in enumerate(dev_ids)},
                )
            ).fetchall()
            org_map = {r[0]: r[1] for r in org_rows}

    def jint(d: dict, key: str) -> int:
        v = d.get(key, "0")
        try:
            return int(v)
        except ValueError, TypeError:
            return 0

    def parse_dt(s: str) -> str:
        """Parse DD.MM.YYYY HH:MM:SS to ISO-like string."""
        if not s:
            return ""
        try:
            dt = datetime.strptime(s, "%d.%m.%Y %H:%M:%S").replace(tzinfo=UTC)
            return dt.strftime("%Y-%m-%d %H:%M:%S")
        except ValueError:
            return s

    items = []
    for r in rows:
        d = r[4] if r[4] else {}
        notes = [jint(d, f"Note{i}") for i in range(10)]
        coins = [jint(d, f"Coin{i}") for i in range(10)]
        items.append(
            {
                "id": r[0],
                "device_id": r[1],
                "sn": r[2] or "",
                "org_id": org_map.get(r[1], 0),
                "inkass_datetime": parse_dt(d.get("InkassDateTime", "")),
                "server_datetime": r[3].strftime("%Y-%m-%d %H:%M:%S") if r[3] else "",
                "total_sum": jint(d, "TotalSum"),
                "total_count": jint(d, "TotalCount"),
                "total_note_sum": jint(d, "TotalNoteSum"),
                "total_note_count": jint(d, "TotalNoteCount"),
                "total_coin_sum": jint(d, "TotalCoinSum"),
                "total_coin_count": jint(d, "TotalCoinCount"),
                "notes": notes,
                "coins": coins,
                "inkassator": d.get("Inkassator", ""),
                "inkass_ext_id": d.get("InkassExtId", ""),
                "paym_ext_id": d.get("PaymExtId", ""),
                "inkass_id": d.get("InkassId", ""),
                "cassette_num": d.get("cassetteNum", ""),
                "cnt_inkass": jint(d, "cntInkass"),
                "cnt_inkass_sum": jint(d, "cntInkassSum"),
                "cnt_transact": jint(d, "cntTransact"),
                "cnt_total_sum": jint(d, "cntTotalSum"),
                "transact_count": jint(d, "TransactCount"),
                "last_sum_inkass": jint(d, "LastSumInkass"),
                "currency": jint(d, "Currency"),
            }
        )

    return {"items": items, "total": count_row or 0}


PAYM_STATE_LABELS = {
    0: "Новый",
    1: "В обработке",
    2: "Оплачен",
    3: "Не оплачен",
    4: "Остановлен",
    5: "Перезапуск",
    6: "Карантин",
}

PAY_TYPE_LABELS = {
    0: "—",
    1: "Наличные",
    2: "Карта",
    3: "СБП",
    4: "Комбо",
}


@app.get("/api/reports/payments")
async def get_payments_report(
    user: dict = Depends(get_current_user),
    date_from: str | None = None,
    date_to: str | None = None,
    device_ids: str | None = None,
    tsp_code: int | None = None,
    paym_state: int | None = None,
    top: int = Query(100, ge=10, le=1000),
):
    """Payments report with expandable params. Tenant-scoped by org_id."""
    from sqlalchemy import text

    org_id = user.get("org_id")
    if not org_id:
        return {"items": [], "total": 0}

    async with async_session() as session:
        conditions = ["p.org_id = :org_id"]
        params: dict = {"org_id": org_id}

        if date_from:
            conditions.append("p.paym_datetime >= :date_from")
            params["date_from"] = date_from
        if date_to:
            conditions.append("p.paym_datetime < (:date_to::date + interval '1 day')")
            params["date_to"] = date_to
        if device_ids:
            ids = [int(x.strip()) for x in device_ids.split(",") if x.strip().isdigit()]
            if ids:
                placeholders = ",".join(f":did_{i}" for i in range(len(ids)))
                conditions.append(f"t.device_id IN ({placeholders})")
                for i, did in enumerate(ids):
                    params[f"did_{i}"] = did
        if tsp_code is not None:
            conditions.append("p.paym_tsp_code = :tsp_code")
            params["tsp_code"] = tsp_code
        if paym_state is not None:
            conditions.append("p.paym_state = :paym_state")
            params["paym_state"] = paym_state

        where = " AND ".join(conditions)

        # Count
        count_row = (
            await session.execute(
                text(f"""
                SELECT count(*)
                FROM payments p
                JOIN terminals t ON t.id = p.terminal_id
                WHERE {where}
            """),
                params,
            )
        ).scalar()

        # Fetch payments
        rows = (
            await session.execute(
                text(f"""
                SELECT p.paym_id, p.paym_datetime, p.paym_amount, p.paym_ext_id,
                       p.paym_tsp_code, p.paym_state, p.pay_type_id,
                       t.device_id, t.sn
                FROM payments p
                JOIN terminals t ON t.id = p.terminal_id
                WHERE {where}
                ORDER BY p.paym_datetime DESC
                LIMIT :top
            """),
                {**params, "top": top},
            )
        ).fetchall()

        if not rows:
            return {"items": [], "total": count_row or 0}

        paym_ids = [r[0] for r in rows]

        # Fetch params for all payments in one query
        ph = ",".join(f":pid_{i}" for i in range(len(paym_ids)))
        param_rows = (
            await session.execute(
                text(f"""
                SELECT pp.paym_id, tpc.parameter_code, tpc.code_description, pp.param_value
                FROM payment_params pp
                JOIN tsp_parameter_codes tpc ON tpc.param_id = pp.param_id
                WHERE pp.paym_id IN ({ph})
                ORDER BY pp.paym_id, tpc.parameter_code
            """),
                {f"pid_{i}": pid for i, pid in enumerate(paym_ids)},
            )
        ).fetchall()

        # Group params by paym_id
        params_map: dict[int, list[dict]] = {}
        for pr in param_rows:
            params_map.setdefault(pr[0], []).append(
                {
                    "code": pr[1],
                    "description": pr[2] or "",
                    "value": pr[3] or "",
                }
            )

    items = []
    for r in rows:
        paym_id = r[0]
        items.append(
            {
                "paym_id": paym_id,
                "paym_datetime": r[1].strftime("%Y-%m-%d %H:%M:%S") if r[1] else "",
                "paym_amount": r[2],
                "paym_ext_id": (r[3] or "").strip(),
                "paym_tsp_code": r[4],
                "paym_state": r[5],
                "paym_state_label": PAYM_STATE_LABELS.get(r[5], str(r[5])),
                "pay_type_id": r[6],
                "pay_type_label": PAY_TYPE_LABELS.get(r[6], str(r[6])),
                "device_id": r[7],
                "sn": r[8] or "",
                "params": params_map.get(paym_id, []),
            }
        )

    return {"items": items, "total": count_row or 0}


def _parse_int_day(date_str: str) -> int | None:
    """Convert YYYY-MM-DD to yyyyMMdd integer for balance_terminal_tsp.int_day."""
    try:
        parts = date_str.split("-")
        if len(parts) == 3:
            return int(parts[0]) * 10000 + int(parts[1]) * 100 + int(parts[2])
    except ValueError, IndexError:
        pass
    return None


@app.get("/api/reports/balance-by-terminal")
async def get_balance_by_terminal(
    user: dict = Depends(get_current_user),
    date_from: str | None = None,
    date_to: str | None = None,
    device_ids: str | None = None,
    tsp_code: int | None = None,
):
    """Balance statistics grouped by terminal. Tenant-scoped."""
    from sqlalchemy import text

    org_id = user.get("org_id")
    if not org_id:
        return {"items": []}

    async with async_session() as session:
        conditions = ["b.org_id = :org_id"]
        params: dict = {"org_id": org_id}

        if date_from:
            day_from = _parse_int_day(date_from)
            if day_from is not None:
                conditions.append("b.int_day >= :day_from")
                params["day_from"] = day_from
        if date_to:
            day_to = _parse_int_day(date_to)
            if day_to is not None:
                conditions.append("b.int_day <= :day_to")
                params["day_to"] = day_to
        if device_ids:
            ids = [int(x.strip()) for x in device_ids.split(",") if x.strip().isdigit()]
            if ids:
                placeholders = ",".join(f":did_{i}" for i in range(len(ids)))
                conditions.append(f"t.device_id IN ({placeholders})")
                for i, did in enumerate(ids):
                    params[f"did_{i}"] = did
        if tsp_code is not None:
            conditions.append("ts.tsp_code = :tsp_code")
            params["tsp_code"] = tsp_code

        where = " AND ".join(conditions)

        rows = (
            await session.execute(
                text(f"""
                SELECT t.device_id, t.sn, t.id AS terminal_id,
                       COUNT(*) AS tsp_count,
                       SUM(b.count) AS total_count,
                       SUM(b.amount) AS total_amount
                FROM balance_terminal_tsp b
                JOIN terminals t ON t.id = b.terminal_id
                JOIN tsp ts ON ts.tsp_id = b.tsp_id
                WHERE {where}
                GROUP BY t.device_id, t.sn, t.id
                ORDER BY total_amount DESC
            """),
                params,
            )
        ).fetchall()

    items = []
    for r in rows:
        items.append(
            {
                "device_id": r[0],
                "sn": (r[1] or "").strip(),
                "terminal_id": r[2],
                "tsp_count": r[3],
                "total_count": r[4],
                "total_amount": r[5],
            }
        )

    return {"items": items}


@app.get("/api/reports/balance-by-tsp")
async def get_balance_by_tsp(
    user: dict = Depends(get_current_user),
    date_from: str | None = None,
    date_to: str | None = None,
    device_ids: str | None = None,
):
    """Balance statistics grouped by TSP. Tenant-scoped."""
    from sqlalchemy import text

    org_id = user.get("org_id")
    if not org_id:
        return {"items": []}

    async with async_session() as session:
        conditions = ["b.org_id = :org_id"]
        params: dict = {"org_id": org_id}

        if date_from:
            day_from = _parse_int_day(date_from)
            if day_from is not None:
                conditions.append("b.int_day >= :day_from")
                params["day_from"] = day_from
        if date_to:
            day_to = _parse_int_day(date_to)
            if day_to is not None:
                conditions.append("b.int_day <= :day_to")
                params["day_to"] = day_to
        if device_ids:
            ids = [int(x.strip()) for x in device_ids.split(",") if x.strip().isdigit()]
            if ids:
                placeholders = ",".join(f":did_{i}" for i in range(len(ids)))
                conditions.append(f"t.device_id IN ({placeholders})")
                for i, did in enumerate(ids):
                    params[f"did_{i}"] = did

        where = " AND ".join(conditions)

        rows = (
            await session.execute(
                text(f"""
                SELECT ts.tsp_code, ts.tsp_name,
                       COUNT(DISTINCT b.terminal_id) AS terminal_count,
                       SUM(b.count) AS total_count,
                       SUM(b.amount) AS total_amount
                FROM balance_terminal_tsp b
                JOIN terminals t ON t.id = b.terminal_id
                JOIN tsp ts ON ts.tsp_id = b.tsp_id
                WHERE {where}
                GROUP BY ts.tsp_code, ts.tsp_name
                ORDER BY total_amount DESC
            """),
                params,
            )
        ).fetchall()

    items = []
    for r in rows:
        items.append(
            {
                "tsp_code": r[0],
                "tsp_name": (r[1] or "").strip(),
                "terminal_count": r[2],
                "total_count": r[3],
                "total_amount": r[4],
            }
        )

    return {"items": items}
