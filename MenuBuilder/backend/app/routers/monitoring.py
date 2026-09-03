from datetime import UTC, datetime, timedelta

from etranprocessing_gauge import decode_slots_bitmask
from fastapi import APIRouter, Depends, Query
from sqlalchemy import text

from app.auth import require_tenant_context, resolve_org_id
from app.database import async_session
from app.services.gauge_bus import gauge_store

router = APIRouter(prefix="/api", tags=["monitoring"])


@router.get("/monitoring")
async def get_monitoring(
    page: int = Query(1, ge=1),
    page_size: int = Query(20, ge=1, le=100),
    search: str | None = Query(None),
    user: dict = Depends(require_tenant_context),
):
    """Return terminal connection history and current gauge state."""
    now = datetime.now(UTC)
    start = now - timedelta(hours=2)

    org_id = resolve_org_id(user)

    conditions = [
        "t.is_active = true",
        "t.show_in_monitoring = true",
        "t.org_id = :org_id",
    ]
    params: dict = {"now": now, "org_id": org_id}

    if search:
        conditions.append(
            "(CAST(t.device_id AS TEXT) ILIKE :search OR t.sn ILIKE :search OR t.address ILIKE :search)"
        )
        params["search"] = f"%{search.strip()}%"

    where_clause = " AND ".join(conditions)

    async with async_session() as session:
        total = await session.scalar(
            text(f"SELECT count(*) FROM terminals t WHERE {where_clause}"), params
        )
        offset = (page - 1) * page_size
        page_params = {**params, "limit": page_size, "offset": offset}
        terminals = (
            await session.execute(
                text(
                    f"SELECT t.id, t.device_id, t.sn, t.org_id, t.is_active, "
                    f"t.cert_serial, t.cert_not_valid_after, t.address, t.note, "
                    f"t.terminal_type_id, tt.name AS terminal_type_name, t.created_at, "
                    f"t.iot_provisioned, t.iot_provisioned_at, t.iot_is_online, t.iot_last_connected_at "
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

        terminal_ids = [terminal[0] for terminal in terminals]
        device_ids = [terminal[1] for terminal in terminals]
        payment_rows = (
            await session.execute(
                text(
                    "SELECT terminal_id, MAX(paym_datetime) FROM payments "
                    "WHERE terminal_id = ANY(:t_ids) GROUP BY terminal_id"
                ),
                {"t_ids": terminal_ids},
            )
        ).fetchall()
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
        inkass_rows = (
            await session.execute(
                text("""
                SELECT device_id, MAX(created_at)
                FROM tech_gate_records
                WHERE function_name = 'inkass' AND device_id = ANY(:d_ids)
                GROUP BY device_id
            """),
                {"d_ids": device_ids},
            )
        ).fetchall()

    last_payment_map: dict[int, datetime] = {
        row[0]: row[1] for row in payment_rows if row[1]
    }
    license_map: dict[int, datetime] = {
        row[0]: row[1] for row in license_rows if row[1]
    }
    last_inkass_map: dict[int, datetime] = {
        row[0]: row[1] for row in inkass_rows if row[1]
    }

    def fmt_soft_version(raw: str) -> str:
        if not raw or raw == "0":
            return "—"
        if "." in raw:
            return raw
        if len(raw) > 2:
            return raw[:-2] + "." + raw[-2:]
        return "0." + raw.zfill(2)

    now_epoch_tick = int(now.timestamp() // 600)
    items = []
    for terminal in terminals:
        dev_id = terminal[1]
        sn = terminal[2]
        snapshot = gauge_store.get_by_device_id(dev_id) or (
            gauge_store.get_by_sn(sn) if sn else None
        )
        if snapshot:
            bitmask = int(snapshot.get("slots_bitmask", 0))
            last_epoch_tick = int(snapshot.get("last_tick_epoch", 0))
            updated_at = snapshot.get("updated_at")
            slots = decode_slots_bitmask(
                bitmask,
                last_epoch_tick,
                now_epoch_tick,
                updated_at=updated_at,
                now_dt=now,
            )
            gauge = snapshot.get("gauge", {})
        else:
            slots = [False] * 12
            gauge = {}

        last_payment = last_payment_map.get(terminal[0])
        license_expires = license_map.get(terminal[0])
        last_inkass = last_inkass_map.get(dev_id)
        cert_not_valid_after = terminal[6]
        lastnumconn = 0
        for slot in reversed(slots):
            if not slot:
                lastnumconn += 1
            else:
                break

        def g(key: str, _gauge: dict = gauge) -> str:
            return str(_gauge.get(key, ""))

        validator_type_raw = g("112")
        validator_type_map = {"WBA003": 1, "CCNET": 2, "ICT U70": 4}
        validator_type = validator_type_map.get(validator_type_raw, 0)
        try:
            validator_type = int(validator_type_raw)
        except ValueError, TypeError:
            pass

        items.append(
            {
                "terminal_id": terminal[0],
                "device_id": dev_id,
                "sn": terminal[2],
                "org_id": terminal[3],
                "is_active": terminal[4],
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
                "last_inkass_at": last_inkass.isoformat() if last_inkass else None,
                "license_expires_at": license_expires.isoformat()
                if license_expires
                else None,
                "cert_serial": terminal[5],
                "cert_not_valid_after": cert_not_valid_after.isoformat()
                if cert_not_valid_after
                else None,
                "address": terminal[7],
                "note": terminal[8],
                "terminal_type_id": terminal[9] if terminal[9] is not None else 0,
                "terminal_type_name": terminal[10]
                if terminal[10] is not None
                else (
                    "Стандартный"
                    if terminal[9] == 0 or terminal[9] is None
                    else f"Тип {terminal[9]}"
                ),
                "created_at": terminal[11].isoformat()
                if len(terminal) > 11 and terminal[11]
                else None,
                "iot_provisioned": bool(terminal[12])
                if len(terminal) > 12 and terminal[12] is not None
                else False,
                "iot_provisioned_at": terminal[13].isoformat()
                if len(terminal) > 13 and terminal[13]
                else None,
                "iot_is_online": bool(terminal[14])
                if len(terminal) > 14 and terminal[14] is not None
                else False,
                "iot_last_connected_at": terminal[15].isoformat()
                if len(terminal) > 15 and terminal[15]
                else None,
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
