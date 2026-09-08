from datetime import UTC, datetime, timedelta

from etranprocessing_gauge import decode_slots_bitmask
from fastapi import APIRouter, Depends, Query
from sqlalchemy import text

from app.auth import resolve_org_id
from app.database import async_session
from app.security.permissions import PERMISSION_MONITORING_VIEW, require_permission

router = APIRouter(prefix="/api", tags=["monitoring"])


@router.get("/monitoring")
async def get_monitoring(
    page: int = Query(1, ge=1),
    page_size: int = Query(20, ge=1, le=100),
    search: str | None = Query(None),
    user: dict = Depends(require_permission(PERMISSION_MONITORING_VIEW)),
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
                    f"t.iot_provisioned, t.iot_provisioned_at, t.iot_is_online, t.iot_last_connected_at, "
                    f"tgs.slots_bitmask, tgs.last_tick_epoch, tgs.updated_at AS tgs_updated_at, "
                    f"tgs.gauge_data, tgs.last_payment_at, tgs.last_inkass_at, "
                    f"tgs.license_expires_at AS tgs_license_expires_at, "
                    f"lic.expires_at AS license_expires_at "
                    f"FROM terminals t "
                    f"LEFT JOIN terminal_types tt ON tt.id = t.terminal_type_id "
                    f"LEFT JOIN terminal_gauge_states tgs ON tgs.device_id = t.device_id "
                    f"LEFT JOIN LATERAL ("
                    f"    SELECT l.expires_at "
                    f"    FROM licenses l "
                    f"    WHERE l.terminal_id = t.id "
                    f"    ORDER BY l.expires_at DESC "
                    f"    LIMIT 1"
                    f") lic ON true "
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

        stale_records = [
            {"device_id": t[1], "sn": t[2], "exp": t[23], "now": now}
            for t in terminals
            if t[1] is not None and len(t) > 23 and t[22] != t[23]
        ]
        if stale_records:
            await session.execute(
                text(
                    "INSERT INTO terminal_gauge_states (device_id, sn, updated_at, license_expires_at) "
                    "VALUES (:device_id, :sn, :now, :exp) "
                    "ON CONFLICT (device_id) DO UPDATE "
                    "SET license_expires_at = EXCLUDED.license_expires_at"
                ),
                stale_records,
            )
            await session.commit()

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

        tgs_slots_bitmask = terminal[16] if len(terminal) > 16 else None
        tgs_last_tick_epoch = terminal[17] if len(terminal) > 17 else None
        tgs_updated_at = terminal[18] if len(terminal) > 18 else None
        tgs_gauge_data = terminal[19] if len(terminal) > 19 else None
        last_payment = terminal[20] if len(terminal) > 20 else None
        last_inkass = terminal[21] if len(terminal) > 21 else None
        if len(terminal) > 23:
            license_expires = terminal[23]
        elif len(terminal) > 22:
            license_expires = terminal[22]
        else:
            license_expires = None

        if tgs_slots_bitmask is not None:
            bitmask = int(tgs_slots_bitmask)
            last_epoch_tick = int(tgs_last_tick_epoch or 0)
            updated_at = tgs_updated_at
            slots = decode_slots_bitmask(
                bitmask,
                last_epoch_tick,
                now_epoch_tick,
                updated_at=updated_at,
                now_dt=now,
            )
            gauge = tgs_gauge_data or {}
        else:
            slots = [False] * 12
            gauge = {}

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
