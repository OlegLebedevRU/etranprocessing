import json

from pin_server.db import Database


def _parse_int_day(date_str: str) -> int:
    """Convert YYYY-MM-DD to YYYYMMDD integer."""
    return int(date_str.replace("-", ""))


def _format_amount(kopecks: int) -> str:
    """Format kopecks as rubles string."""
    return f"{kopecks / 100:.2f} ₽"


PAYMENT_STATES = {
    0: "Новый",
    1: "В обработке",
    2: "Оплачен",
    3: "Не оплачен",
    4: "Остановлен",
    5: "Перезапуск",
    6: "Карантин",
}

PAY_TYPES = {
    0: "—",
    1: "Наличные",
    2: "Карта",
    3: "СБП",
    4: "Комбо",
}


async def report_payments(
    db: Database,
    date_from: str | None = None,
    date_to: str | None = None,
    device_ids: str | None = None,
    tsp_code: int | None = None,
    paym_state: int | None = None,
    top: int = 100,
    org_id: int | None = None,
) -> dict:
    """Query payments report."""
    conditions = ["1=1"]
    params = []
    idx = 1

    if org_id is not None:
        conditions.append(f"t.org_id = ${idx}")
        params.append(org_id)
        idx += 1
    if date_from:
        conditions.append(f"p.paym_datetime >= ${idx}")
        params.append(date_from)
        idx += 1
    if date_to:
        conditions.append(f"p.paym_datetime < (${idx}::date + interval '1 day')")
        params.append(date_to)
        idx += 1
    if device_ids:
        ids = [int(x.strip()) for x in device_ids.split(",")]
        placeholders = ", ".join(f"${idx + i}" for i in range(len(ids)))
        conditions.append(f"t.device_id IN ({placeholders})")
        params.extend(ids)
        idx += len(ids)
    if tsp_code is not None:
        conditions.append(f"p.paym_tsp_code = ${idx}")
        params.append(tsp_code)
        idx += 1
    if paym_state is not None:
        conditions.append(f"p.paym_state = ${idx}")
        params.append(paym_state)
        idx += 1

    where = " AND ".join(conditions)
    params_with_limit = params + [top]

    query = f"""
        SELECT p.paym_id, p.paym_datetime, p.paym_amount, p.paym_ext_id,
               p.paym_tsp_code, p.paym_state, p.pay_type_id,
               t.device_id, t.sn
        FROM payments p
        JOIN terminals t ON t.id = p.terminal_id
        WHERE {where}
        ORDER BY p.paym_datetime DESC
        LIMIT ${idx}
    """
    rows = await db.fetch(query, *params_with_limit)

    # Batch load params
    paym_ids = [r["paym_id"] for r in rows]
    params_map = {}
    if paym_ids:
        placeholders = ", ".join(f"${i + 1}" for i in range(len(paym_ids)))
        param_rows = await db.fetch(
            f"""SELECT pp.paym_id, tpc.parameter_code, tpc.code_description, pp.param_value
                FROM payment_params pp
                JOIN tsp_parameter_codes tpc ON tpc.param_id = pp.param_id
                WHERE pp.paym_id IN ({placeholders})
                ORDER BY pp.paym_id, tpc.parameter_code""",
            *paym_ids,
        )
        for pr in param_rows:
            params_map.setdefault(pr["paym_id"], []).append(
                {
                    "code": pr["parameter_code"],
                    "description": pr["code_description"],
                    "value": pr["param_value"],
                }
            )

    items = []
    for r in rows:
        items.append(
            {
                "paym_id": r["paym_id"],
                "paym_datetime": str(r["paym_datetime"]),
                "paym_amount": r["paym_amount"],
                "paym_amount_rub": _format_amount(r["paym_amount"]),
                "paym_ext_id": r["paym_ext_id"],
                "paym_tsp_code": r["paym_tsp_code"],
                "paym_state": r["paym_state"],
                "paym_state_label": PAYMENT_STATES.get(
                    r["paym_state"], str(r["paym_state"])
                ),
                "pay_type_id": r["pay_type_id"],
                "pay_type_label": PAY_TYPES.get(
                    r["pay_type_id"], str(r["pay_type_id"])
                ),
                "device_id": r["device_id"],
                "sn": r["sn"],
                "params": params_map.get(r["paym_id"], []),
            }
        )

    return {"items": items, "count": len(items)}


async def report_balance_by_terminal(
    db: Database,
    date_from: str | None = None,
    date_to: str | None = None,
    device_ids: str | None = None,
    tsp_code: int | None = None,
    org_id: int | None = None,
) -> dict:
    """Balance aggregated by terminal."""
    conditions = ["1=1"]
    params = []
    idx = 1

    if org_id is not None:
        conditions.append(f"b.org_id = ${idx}")
        params.append(org_id)
        idx += 1
    if date_from:
        conditions.append(f"b.int_day >= ${idx}")
        params.append(_parse_int_day(date_from))
        idx += 1
    if date_to:
        conditions.append(f"b.int_day <= ${idx}")
        params.append(_parse_int_day(date_to))
        idx += 1
    if device_ids:
        ids = [int(x.strip()) for x in device_ids.split(",")]
        placeholders = ", ".join(f"${idx + i}" for i in range(len(ids)))
        conditions.append(f"t.device_id IN ({placeholders})")
        params.extend(ids)
        idx += len(ids)
    if tsp_code is not None:
        conditions.append(f"ts.tsp_code = ${idx}")
        params.append(tsp_code)
        idx += 1

    where = " AND ".join(conditions)

    query = f"""
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
    """
    rows = await db.fetch(query, *params)

    items = [
        {
            "device_id": r["device_id"],
            "sn": r["sn"],
            "terminal_id": r["terminal_id"],
            "tsp_count": r["tsp_count"],
            "total_count": r["total_count"],
            "total_amount": r["total_amount"],
            "total_amount_rub": _format_amount(r["total_amount"]),
        }
        for r in rows
    ]

    return {"items": items, "count": len(items)}


async def report_balance_by_tsp(
    db: Database,
    date_from: str | None = None,
    date_to: str | None = None,
    device_ids: str | None = None,
    org_id: int | None = None,
) -> dict:
    """Balance aggregated by TSP."""
    conditions = ["1=1"]
    params = []
    idx = 1

    if org_id is not None:
        conditions.append(f"b.org_id = ${idx}")
        params.append(org_id)
        idx += 1
    if date_from:
        conditions.append(f"b.int_day >= ${idx}")
        params.append(_parse_int_day(date_from))
        idx += 1
    if date_to:
        conditions.append(f"b.int_day <= ${idx}")
        params.append(_parse_int_day(date_to))
        idx += 1
    if device_ids:
        ids = [int(x.strip()) for x in device_ids.split(",")]
        placeholders = ", ".join(f"${idx + i}" for i in range(len(ids)))
        conditions.append(f"t.device_id IN ({placeholders})")
        params.extend(ids)
        idx += len(ids)

    where = " AND ".join(conditions)

    query = f"""
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
    """
    rows = await db.fetch(query, *params)

    items = [
        {
            "tsp_code": r["tsp_code"],
            "tsp_name": r["tsp_name"],
            "terminal_count": r["terminal_count"],
            "total_count": r["total_count"],
            "total_amount": r["total_amount"],
            "total_amount_rub": _format_amount(r["total_amount"]),
        }
        for r in rows
    ]

    return {"items": items, "count": len(items)}


async def report_inkass(
    db: Database,
    date_from: str | None = None,
    date_to: str | None = None,
    device_ids: str | None = None,
    page: int = 1,
    size: int = 100,
    org_id: int | None = None,
) -> dict:
    """Inkassation report."""
    conditions = ["r.function_name = 'inkass'"]
    params = []
    idx = 1

    if org_id is not None:
        conditions.append(f"t.org_id = ${idx}")
        params.append(org_id)
        idx += 1
    if date_from:
        conditions.append(f"r.created_at >= ${idx}")
        params.append(date_from)
        idx += 1
    if date_to:
        conditions.append(f"r.created_at < (${idx}::date + interval '1 day')")
        params.append(date_to)
        idx += 1
    if device_ids:
        ids = [int(x.strip()) for x in device_ids.split(",")]
        placeholders = ", ".join(f"${idx + i}" for i in range(len(ids)))
        conditions.append(f"r.device_id IN ({placeholders})")
        params.extend(ids)
        idx += len(ids)

    where = " AND ".join(conditions)
    offset = (page - 1) * size
    params_with_limit = params + [size, offset]

    query = f"""
        SELECT r.id, r.device_id, r.sn, r.created_at, r.request_data, t.org_id
        FROM tech_gate_records r
        JOIN terminals t ON t.device_id = r.device_id
        WHERE {where}
        ORDER BY r.created_at DESC
        LIMIT ${idx} OFFSET ${idx + 1}
    """
    rows = await db.fetch(query, *params_with_limit)

    items = []
    for r in rows:
        data = r["request_data"] or {}
        if isinstance(data, str):
            data = json.loads(data)

        notes = [int(data.get(f"Note{i}", 0)) for i in range(10)]
        coins = [int(data.get(f"Coin{i}", 0)) for i in range(10)]

        items.append(
            {
                "id": r["id"],
                "device_id": r["device_id"],
                "sn": r["sn"],
                "org_id": r["org_id"],
                "server_datetime": str(r["created_at"]),
                "inkass_datetime": data.get("InkassDateTime"),
                "total_sum": int(data.get("TotalSum", 0)),
                "total_sum_rub": _format_amount(int(data.get("TotalSum", 0))),
                "total_count": int(data.get("TotalCount", 0)),
                "total_note_sum": int(data.get("TotalNoteSum", 0)),
                "total_note_count": int(data.get("TotalNoteCount", 0)),
                "total_coin_sum": int(data.get("TotalCoinSum", 0)),
                "total_coin_count": int(data.get("TotalCoinCount", 0)),
                "notes": notes,
                "coins": coins,
                "inkassator": data.get("Inkassator"),
                "cassette_num": data.get("CassetteNum"),
            }
        )

    return {"items": items, "count": len(items), "page": page, "size": size}
