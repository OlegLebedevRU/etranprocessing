import json
from contextlib import suppress
from datetime import UTC, datetime, tzinfo

from fastapi import APIRouter, Depends, HTTPException, Query
from pydantic import BaseModel
from sqlalchemy import select, text, update

from app.auth import require_tenant_context, resolve_org_id
from app.database import async_session
from app.models import Org, TechGateRecord
from app.utils.timezone import (
    DEFAULT_TIMEZONE,
    get_date_range_bounds_utc,
    get_local_datetime,
    get_timezone_name,
    resolve_tz,
    to_utc_iso,
)

router = APIRouter(prefix="/api/reports", tags=["reports"])

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


def _parse_device_ids(device_ids: str | None) -> list[int]:
    if not device_ids:
        return []
    return [
        int(value.strip()) for value in device_ids.split(",") if value.strip().isdigit()
    ]


def _add_device_filter(
    conditions: list[str], params: dict, device_ids: str | None
) -> None:
    ids = _parse_device_ids(device_ids)
    if not ids:
        return
    placeholders = ",".join(f":did_{index}" for index in range(len(ids)))
    conditions.append(f"t.device_id IN ({placeholders})")
    params.update({f"did_{index}": device_id for index, device_id in enumerate(ids)})


def _parse_int_day(date_str: str) -> int | None:
    try:
        parts = date_str.split("-")
        if len(parts) == 3:
            return int(parts[0]) * 10000 + int(parts[1]) * 100 + int(parts[2])
    except ValueError, IndexError:
        pass
    return None


def _json_int(data: dict, key: str) -> int:
    try:
        return int(data.get(key, "0"))
    except ValueError, TypeError:
        return 0


def _parse_inkass_datetime(value: str) -> str:
    if not value:
        return ""
    try:
        parsed = datetime.strptime(value, "%d.%m.%Y %H:%M:%S").replace(tzinfo=UTC)
        return parsed.strftime("%Y-%m-%d %H:%M:%S")
    except ValueError:
        return value


def _parse_inkass_datetime_utc(
    value: str | datetime | None,
    tz: tzinfo | str = DEFAULT_TIMEZONE,
) -> datetime | None:
    if not value:
        return None
    if isinstance(value, datetime):
        if value.tzinfo is None:
            return value.replace(tzinfo=UTC)
        return value.astimezone(UTC)

    val_str = str(value).strip()
    if not val_str:
        return None

    target_zone = resolve_tz(tz)

    for fmt in ("%d.%m.%Y %H:%M:%S", "%d.%m.%Y %H:%M", "%Y-%m-%d %H:%M:%S"):
        try:
            local_dt = datetime.strptime(val_str, fmt).replace(tzinfo=target_zone)
            return local_dt.astimezone(UTC)
        except ValueError:
            continue

    try:
        dt = datetime.fromisoformat(val_str)
        if dt.tzinfo is None:
            dt = dt.replace(tzinfo=target_zone)
        return dt.astimezone(UTC)
    except ValueError:
        return None


def ext_id_to_canonical(ext_id: str) -> str:
    parts = ext_id.strip().split("_")
    if len(parts) >= 3 and len(parts[1]) == 6 and len(parts[2]) >= 8:
        d, t = parts[1], parts[2]
        return f"20{d[4:6]}{d[2:4]}{d[0:2]}{t[:8]}"
    return ""


async def _resolve_org_tz(session, org_id: int | None) -> tzinfo:
    if not org_id:
        return resolve_tz(DEFAULT_TIMEZONE)
    org_tz_row = (
        await session.execute(select(Org.timezone).where(Org.org_id == org_id))
    ).scalar_one_or_none()
    return resolve_tz(org_tz_row)


async def _calculated_inkass_sum(
    session,
    record_id: int,
    device_id: int,
    terminal_id: int,
    created_at: datetime,
    request_data: dict,
) -> int | None:
    # If already calculated in request_data, return directly
    if request_data.get("calc_status") == "needs_calc":
        return None
    if "calc_cash_sum" in request_data and request_data["calc_cash_sum"] is not None:
        return int(request_data["calc_cash_sum"])
    if "calculated_sum" in request_data and request_data["calculated_sum"] is not None:
        return int(request_data["calculated_sum"])

    if created_at and created_at.tzinfo is None:
        created_at = created_at.replace(tzinfo=UTC)

    current_ext_id = str(request_data.get("PaymExtId") or "").strip()
    previous_inkass = (
        await session.execute(
            text("""
                SELECT request_data, created_at
                FROM tech_gate_records
                WHERE device_id = :device_id AND function_name = 'inkass'
                  AND (created_at < :created_at OR (created_at = :created_at AND id < :id))
                ORDER BY created_at DESC, id DESC
                LIMIT 1
            """),
            {"device_id": device_id, "created_at": created_at, "id": record_id},
        )
    ).fetchone()
    previous_ext_id = ""
    previous_created_at = None
    if previous_inkass:
        previous_data = previous_inkass[0] or {}
        previous_ext_id = str(previous_data.get("PaymExtId") or "").strip()
        previous_created_at = previous_inkass[1]
        if previous_created_at and previous_created_at.tzinfo is None:
            previous_created_at = previous_created_at.replace(tzinfo=UTC)

    async def find_payment(external_id: str):
        if not external_id:
            return None
        return (
            await session.execute(
                text("""
                    SELECT paym_id, paym_datetime
                    FROM payments
                    WHERE terminal_id = :term_id AND paym_ext_id = :ext_id
                    ORDER BY paym_id DESC
                    LIMIT 1
                """),
                {"term_id": terminal_id, "ext_id": external_id},
            )
        ).fetchone()

    current_payment = await find_payment(current_ext_id)
    previous_payment = await find_payment(previous_ext_id)
    if current_payment and previous_payment:
        query = """
            SELECT COALESCE(SUM(paym_amount), 0) FROM payments
            WHERE terminal_id = :term_id AND paym_state = 2 AND pay_type_id IN (0, 1)
              AND paym_amount != 100
              AND paym_id > :prev_id AND paym_id <= :curr_id
        """
        params = {
            "term_id": terminal_id,
            "prev_id": previous_payment[0],
            "curr_id": current_payment[0],
        }
    elif current_payment and previous_created_at:
        query = """
            SELECT COALESCE(SUM(paym_amount), 0) FROM payments
            WHERE terminal_id = :term_id AND paym_state = 2 AND pay_type_id IN (0, 1)
              AND paym_amount != 100
              AND paym_datetime > :prev_created_at
              AND paym_id <= :curr_id
        """
        params = {
            "term_id": terminal_id,
            "prev_created_at": previous_created_at,
            "curr_id": current_payment[0],
        }
    elif current_payment:
        query = """
            SELECT COALESCE(SUM(paym_amount), 0) FROM payments
            WHERE terminal_id = :term_id AND paym_state = 2 AND pay_type_id IN (0, 1)
              AND paym_amount != 100
              AND paym_id <= :curr_id
        """
        params = {"term_id": terminal_id, "curr_id": current_payment[0]}
    elif previous_created_at:
        query = """
            SELECT COALESCE(SUM(paym_amount), 0) FROM payments
            WHERE terminal_id = :term_id AND paym_state = 2 AND pay_type_id IN (0, 1)
              AND paym_amount != 100
              AND paym_datetime > :prev_created_at
              AND paym_datetime <= :curr_created_at
        """
        params = {
            "term_id": terminal_id,
            "prev_created_at": previous_created_at,
            "curr_created_at": created_at,
        }
    else:
        query = """
            SELECT COALESCE(SUM(paym_amount), 0) FROM payments
            WHERE terminal_id = :term_id AND paym_state = 2 AND pay_type_id IN (0, 1)
              AND paym_amount != 100
              AND paym_datetime <= :curr_created_at
        """
        params = {"term_id": terminal_id, "curr_created_at": created_at}
    amount = (await session.execute(text(query), params)).scalar()
    return (amount or 0) // 100


class ApplyCalculationRequest(BaseModel):
    strategy_id: str


async def _calculate_strategies_preview(
    session,
    record_id: int,
    device_id: int,
    terminal_id: int,
    created_at: datetime,
    request_data: dict,
    org_tz: tzinfo | str = DEFAULT_TIMEZONE,
) -> dict:
    if created_at and created_at.tzinfo is None:
        created_at = created_at.replace(tzinfo=UTC)

    fact_total_sum = _json_int(request_data, "TotalSum") or _json_int(
        request_data, "TotalNoteSum"
    )
    curr_paym_ext_id = str(request_data.get("PaymExtId") or "").strip()
    curr_inkass_ext_id = str(request_data.get("InkassExtId") or "").strip()
    curr_inkass_id_raw = request_data.get("InkassId") or request_data.get("cntInkass")
    curr_inkass_id = (
        int(curr_inkass_id_raw)
        if curr_inkass_id_raw and str(curr_inkass_id_raw).isdigit()
        else None
    )

    # Find previous inkassation
    prev_record_row = None
    if curr_inkass_id and curr_inkass_id > 1:
        prev_res = await session.execute(
            text("""
                SELECT id, request_data, created_at
                FROM tech_gate_records
                WHERE device_id = :dev_id AND function_name = 'inkass'
                  AND COALESCE((request_data->>'InkassId')::bigint, (request_data->>'cntInkass')::bigint, 0) < :curr_ink_id
                ORDER BY COALESCE((request_data->>'InkassId')::bigint, (request_data->>'cntInkass')::bigint, 0) DESC, id DESC
                LIMIT 1
            """),
            {"dev_id": device_id, "curr_ink_id": curr_inkass_id},
        )
        prev_record_row = prev_res.fetchone()

    if not prev_record_row:
        prev_res = await session.execute(
            text("""
                SELECT id, request_data, created_at
                FROM tech_gate_records
                WHERE device_id = :dev_id AND function_name = 'inkass'
                  AND (created_at < :created_at OR (created_at = :created_at AND id < :id))
                ORDER BY created_at DESC, id DESC
                LIMIT 1
            """),
            {"dev_id": device_id, "created_at": created_at, "id": record_id},
        )
        prev_record_row = prev_res.fetchone()

    prev_data = prev_record_row[1] or {} if prev_record_row else {}
    prev_created_at = prev_record_row[2] if prev_record_row else None
    if prev_created_at and prev_created_at.tzinfo is None:
        prev_created_at = prev_created_at.replace(tzinfo=UTC)

    prev_upper_ext_id = str(
        prev_data.get("calc_upper_paym_ext_id") or prev_data.get("PaymExtId") or ""
    ).strip()

    async def find_payment_exact(ext_id: str):
        if not ext_id:
            return None
        res = await session.execute(
            text("""
                SELECT paym_id, paym_ext_id, paym_datetime
                FROM payments
                WHERE terminal_id = :term_id AND paym_ext_id = :ext_id
                ORDER BY paym_id DESC
                LIMIT 1
            """),
            {"term_id": terminal_id, "ext_id": ext_id},
        )
        return res.fetchone()

    async def find_payment_nearest_canonical(ext_id: str):
        if not ext_id:
            return None
        canon = ext_id_to_canonical(ext_id)
        if not canon:
            return None
        res = await session.execute(
            text("""
                SELECT paym_id, paym_ext_id, paym_datetime
                FROM payments
                WHERE terminal_id = :term_id
                  AND ('20' || substring(paym_ext_id from 10 for 2) || substring(paym_ext_id from 8 for 2) ||
                       substring(paym_ext_id from 6 for 2) || substring(paym_ext_id from 13 for 8)) <= :canon_key
                ORDER BY ('20' || substring(paym_ext_id from 10 for 2) || substring(paym_ext_id from 8 for 2) ||
                          substring(paym_ext_id from 6 for 2) || substring(paym_ext_id from 13 for 8)) DESC,
                         paym_id DESC
                LIMIT 1
            """),
            {"term_id": terminal_id, "canon_key": canon},
        )
        return res.fetchone()

    curr_p_exact = (
        await find_payment_exact(curr_paym_ext_id) if curr_paym_ext_id else None
    )
    prev_p_exact = (
        await find_payment_exact(prev_upper_ext_id) if prev_upper_ext_id else None
    )

    curr_p_nearest = curr_p_exact or (
        await find_payment_nearest_canonical(curr_paym_ext_id or curr_inkass_ext_id)
    )
    prev_p_nearest = prev_p_exact or (
        await find_payment_nearest_canonical(prev_upper_ext_id)
    )

    strategies = []

    # Strategy 1: exact_paym_ext_id (Эталонная по точному PaymExtId, только наличные pay_type_id IN (0, 1))
    sum1 = None
    if curr_p_exact:
        prev_bound_id = (
            prev_p_exact[0]
            if prev_p_exact
            else (prev_p_nearest[0] if prev_p_nearest else None)
        )
        if prev_bound_id:
            q1 = """
                SELECT COALESCE(SUM(paym_amount), 0) FROM payments
                WHERE terminal_id = :term_id AND paym_state = 2 AND pay_type_id IN (0, 1)
                  AND paym_amount != 100
                  AND paym_id > :prev_id AND paym_id <= :curr_id
            """
            p1 = {
                "term_id": terminal_id,
                "prev_id": prev_bound_id,
                "curr_id": curr_p_exact[0],
            }
        else:
            q1 = """
                SELECT COALESCE(SUM(paym_amount), 0) FROM payments
                WHERE terminal_id = :term_id AND paym_state = 2 AND pay_type_id IN (0, 1)
                  AND paym_amount != 100
                  AND paym_id <= :curr_id
            """
            p1 = {"term_id": terminal_id, "curr_id": curr_p_exact[0]}
        amt1 = (await session.execute(text(q1), p1)).scalar() or 0
        sum1 = int(amt1) // 100
        delta1 = fact_total_sum - sum1
        strategies.append(
            {
                "id": "exact_paym_ext_id",
                "name": "Эталонная по точному PaymExtId (только наличные)",
                "lower_bound": prev_upper_ext_id or "Начало работы",
                "upper_bound": curr_paym_ext_id,
                "calculated_cash": sum1,
                "delta": delta1,
                "is_matched": (sum1 == fact_total_sum),
                "description": "Расчет строго по точному внешнему идентификатору платежа (pay_type_id IN (0, 1))",
            }
        )
    else:
        strategies.append(
            {
                "id": "exact_paym_ext_id",
                "name": "Эталонная по точному PaymExtId (только наличные)",
                "lower_bound": prev_upper_ext_id or None,
                "upper_bound": curr_paym_ext_id or "Не указан",
                "calculated_cash": None,
                "delta": None,
                "is_matched": False,
                "description": f"Платеж {curr_paym_ext_id or 'PaymExtId'} пока не найден в базе данных (возможно, задерживается в сети)",
            }
        )

    # Strategy 2: nearest_paym_ext_id (По ближайшему PaymExtId до среза инкассации)
    if curr_p_nearest and (not curr_p_exact or curr_p_nearest[0] != curr_p_exact[0]):
        prev_bound_id = prev_p_nearest[0] if prev_p_nearest else None
        if prev_bound_id:
            qn = """
                SELECT COALESCE(SUM(paym_amount), 0) FROM payments
                WHERE terminal_id = :term_id AND paym_state = 2 AND pay_type_id IN (0, 1)
                  AND paym_amount != 100
                  AND paym_id > :prev_id AND paym_id <= :curr_id
            """
            pn = {
                "term_id": terminal_id,
                "prev_id": prev_bound_id,
                "curr_id": curr_p_nearest[0],
            }
        else:
            qn = """
                SELECT COALESCE(SUM(paym_amount), 0) FROM payments
                WHERE terminal_id = :term_id AND paym_state = 2 AND pay_type_id IN (0, 1)
                  AND paym_amount != 100
                  AND paym_id <= :curr_id
            """
            pn = {"term_id": terminal_id, "curr_id": curr_p_nearest[0]}
        amtn = (await session.execute(text(qn), pn)).scalar() or 0
        sumn = int(amtn) // 100
        deltan = fact_total_sum - sumn
        strategies.append(
            {
                "id": "nearest_paym_ext_id",
                "name": "По ближайшему PaymExtId (до среза инкассации)",
                "lower_bound": (
                    prev_p_nearest[1] if prev_p_nearest else prev_upper_ext_id
                )
                or "Начало работы",
                "upper_bound": curr_p_nearest[1],
                "calculated_cash": sumn,
                "delta": deltan,
                "is_matched": (sumn == fact_total_sum),
                "description": f"Расчет по ближайшему проведенному платежу {curr_p_nearest[1]} (до {curr_paym_ext_id or curr_inkass_ext_id})",
            }
        )

    # Strategy 3: terminal_time (По дате/времени инкассации, только наличные pay_type_id IN (0, 1))
    dt_end = (
        _parse_inkass_datetime_utc(request_data.get("InkassDateTime"), org_tz)
        or created_at
    )
    dt_start = (
        _parse_inkass_datetime_utc(prev_data.get("InkassDateTime"), org_tz)
        or prev_created_at
    )
    if dt_end and dt_end.tzinfo is None:
        dt_end = dt_end.replace(tzinfo=UTC)
    if dt_start and dt_start.tzinfo is None:
        dt_start = dt_start.replace(tzinfo=UTC)

    if dt_start:
        q2 = """
            SELECT COALESCE(SUM(paym_amount), 0) FROM payments
            WHERE terminal_id = :term_id AND paym_state = 2 AND pay_type_id IN (0, 1)
              AND paym_amount != 100
              AND paym_datetime > :dt_start AND paym_datetime <= :dt_end
        """
        p2 = {"term_id": terminal_id, "dt_start": dt_start, "dt_end": dt_end}
    else:
        q2 = """
            SELECT COALESCE(SUM(paym_amount), 0) FROM payments
            WHERE terminal_id = :term_id AND paym_state = 2 AND pay_type_id IN (0, 1)
              AND paym_amount != 100
              AND paym_datetime <= :dt_end
        """
        p2 = {"term_id": terminal_id, "dt_end": dt_end}
    amt2 = (await session.execute(text(q2), p2)).scalar() or 0
    sum2 = int(amt2) // 100
    delta2 = fact_total_sum - sum2

    dt_start_display = (
        get_local_datetime(dt_start, org_tz).strftime("%Y-%m-%d %H:%M:%S")
        if dt_start
        else "Начало работы"
    )
    dt_end_display = (
        get_local_datetime(dt_end, org_tz).strftime("%Y-%m-%d %H:%M:%S")
        if dt_end
        else ""
    )

    strategies.append(
        {
            "id": "terminal_time",
            "name": "По времени инкассации (только наличные)",
            "lower_bound": dt_start_display,
            "upper_bound": dt_end_display,
            "calculated_cash": sum2,
            "delta": delta2,
            "is_matched": (sum2 == fact_total_sum),
            "description": "Расчет по временному окну между инкассациями терминала",
        }
    )

    cur_status = request_data.get("calc_status")
    if not cur_status:
        if curr_p_exact and sum1 is not None and sum1 == fact_total_sum:
            cur_status = "matched"
        elif not curr_p_exact:
            cur_status = "needs_calc"
        else:
            cur_status = "mismatch"

    return {
        "record_id": record_id,
        "device_id": device_id,
        "report_number": str(curr_inkass_id_raw or ""),
        "fact_total_sum": fact_total_sum,
        "current_status": cur_status,
        "strategies": strategies,
    }


@router.get("/inkass")
async def get_inkass_report(
    user: dict = Depends(require_tenant_context),
    date_from: str | None = None,
    date_to: str | None = None,
    device_ids: str | None = None,
    page: int = Query(1, ge=1),
    size: int = Query(100, ge=10, le=500),
    org_id: int | None = Query(None),
):
    effective_org_id = resolve_org_id(user, org_id)

    async with async_session() as session:
        tz = resolve_tz("Europe/Moscow")
        if effective_org_id:
            org_tz_row = (
                await session.execute(
                    select(Org.timezone).where(Org.org_id == effective_org_id)
                )
            ).scalar_one_or_none()
            if org_tz_row:
                tz = resolve_tz(org_tz_row)
        tz_name = get_timezone_name(tz)

        conditions = ["r.function_name = 'inkass'"]
        params: dict = {}
        if effective_org_id > 0:
            conditions.append("t.org_id = :org_id")
            params["org_id"] = effective_org_id
        dt_from_utc, dt_to_utc = get_date_range_bounds_utc(date_from, date_to, tz=tz)
        if dt_from_utc:
            params["dt_from"] = dt_from_utc
            conditions.append("r.created_at >= :dt_from")
        if dt_to_utc:
            params["dt_to"] = dt_to_utc
            conditions.append("r.created_at < :dt_to")
        _add_device_filter(conditions, params, device_ids)
        where = " AND ".join(conditions)
        count_row = (
            await session.execute(
                text(f"""
                    SELECT count(*) FROM tech_gate_records r
                    JOIN terminals t ON t.device_id = r.device_id WHERE {where}
                """),
                params,
            )
        ).scalar()
        rows = (
            await session.execute(
                text(f"""
                    SELECT r.id, r.device_id, r.sn, r.created_at, r.request_data,
                           t.org_id, t.id AS terminal_id
                    FROM tech_gate_records r
                    JOIN terminals t ON t.device_id = r.device_id
                    WHERE {where} ORDER BY r.created_at DESC
                    LIMIT :limit OFFSET :offset
                """),
                {**params, "limit": size, "offset": (page - 1) * size},
            )
        ).fetchall()
        calculated_sums = {
            row[0]: await _calculated_inkass_sum(
                session,
                row[0],
                row[1],
                row[6] if len(row) > 6 else row[0],
                row[3],
                row[4] or {},
            )
            for row in rows
        }

    items = []
    for row in rows:
        data = row[4] or {}
        notes = [_json_int(data, f"Note{index}") for index in range(10)]
        coins = [_json_int(data, f"Coin{index}") for index in range(10)]
        denominations = {
            amount: _json_int(data, indexed)
            or _json_int(data, lower)
            or _json_int(data, named)
            for amount, indexed, lower, named in (
                ("n10", "Note2", "note10", "Note10"),
                ("n50", "Note3", "note50", "Note50"),
                ("n100", "Note4", "note100", "Note100"),
                ("n200", "Note8", "note200", "Note200"),
                ("n500", "Note5", "note500", "Note500"),
                ("n1000", "Note6", "note1000", "Note1000"),
                ("n2000", "Note9", "note2000", "Note2000"),
                ("n5000", "Note7", "note5000", "Note5000"),
            )
        }
        total_note_count = (
            _json_int(data, "TotalNoteCount")
            or _json_int(data, "TotalCount")
            or sum(denominations.values())
        )
        transact_count = (
            _json_int(data, "TransactCount")
            or _json_int(data, "cntTransact")
            or _json_int(data, "TotalCount")
        )
        report_number = data.get("InkassId") or data.get("cntInkass") or ""
        server_dt_str = to_utc_iso(row[3]) if row[3] else ""
        total_sum_val = _json_int(data, "TotalSum") or _json_int(data, "TotalNoteSum")
        calc_cash_sum = calculated_sums.get(row[0])
        calc_status = data.get("calc_status")
        if calc_status is None:
            if calc_cash_sum is None:
                calc_status = "needs_calc"
            elif calc_cash_sum == total_sum_val:
                calc_status = "matched"
            else:
                calc_status = "mismatch"

        calc_delta = (
            (total_sum_val - calc_cash_sum)
            if (calc_cash_sum is not None and total_sum_val is not None)
            else None
        )

        items.append(
            {
                "id": row[0],
                "device_id": row[1],
                "sn": row[2] or "",
                "org_id": row[5] or 0,
                "inkass_datetime": _parse_inkass_datetime(
                    data.get("InkassDateTime", "")
                )
                or server_dt_str,
                "server_datetime": server_dt_str,
                "total_sum": total_sum_val,
                "calculated_sum": calc_cash_sum,
                "calc_status": calc_status,
                "calc_delta": calc_delta,
                "calc_strategy": data.get("calc_strategy_applied"),
                "calc_lower_paym_ext_id": data.get("calc_lower_paym_ext_id"),
                "calc_upper_paym_ext_id": data.get("calc_upper_paym_ext_id"),
                "total_count": _json_int(data, "TotalCount"),
                "total_note_sum": _json_int(data, "TotalNoteSum"),
                "total_note_count": total_note_count,
                "total_coin_sum": _json_int(data, "TotalCoinSum"),
                "total_coin_count": _json_int(data, "TotalCoinCount"),
                "notes": notes,
                "coins": coins,
                "banknotes": denominations,
                "inkassator": data.get("Inkassator", ""),
                "inkass_ext_id": data.get("InkassExtId", ""),
                "paym_ext_id": data.get("PaymExtId", ""),
                "inkass_id": data.get("InkassId", ""),
                "report_number": str(report_number),
                "cassette_num": data.get("cassetteNum", "")
                or data.get("CassetteNum", ""),
                "cnt_inkass": _json_int(data, "cntInkass"),
                "cnt_inkass_sum": _json_int(data, "cntInkassSum"),
                "cnt_transact": _json_int(data, "cntTransact"),
                "cnt_total_sum": _json_int(data, "cntTotalSum"),
                "transact_count": transact_count,
                "last_sum_inkass": _json_int(data, "LastSumInkass"),
                "currency": _json_int(data, "Currency"),
            }
        )
    return {"items": items, "total": count_row or 0, "timezone": tz_name}


@router.post("/inkass/{record_id}/recalculate-preview")
async def recalculate_inkass_preview(
    record_id: int,
    user: dict = Depends(require_tenant_context),
):
    effective_org_id = resolve_org_id(user)
    async with async_session() as session:
        record_row = (
            await session.execute(
                text("""
                    SELECT r.id, r.device_id, r.created_at, r.request_data, t.id AS terminal_id, t.org_id
                    FROM tech_gate_records r
                    JOIN terminals t ON t.device_id = r.device_id
                    WHERE r.id = :record_id AND r.function_name = 'inkass'
                """),
                {"record_id": record_id},
            )
        ).fetchone()
        if not record_row:
            raise HTTPException(status_code=404, detail="Инкассация не найдена")
        if effective_org_id > 0 and record_row[5] != effective_org_id:
            raise HTTPException(status_code=403, detail="Доступ запрещен")

        org_tz = await _resolve_org_tz(session, record_row[5])

        return await _calculate_strategies_preview(
            session,
            record_row[0],
            record_row[1],
            record_row[4],
            record_row[2],
            record_row[3] or {},
            org_tz=org_tz,
        )


@router.post("/inkass/{record_id}/apply-calculation")
async def apply_inkass_calculation(
    record_id: int,
    body: ApplyCalculationRequest,
    user: dict = Depends(require_tenant_context),
):
    effective_org_id = resolve_org_id(user)
    async with async_session() as session:
        record_row = (
            await session.execute(
                text("""
                    SELECT r.id, r.device_id, r.created_at, r.request_data, t.id AS terminal_id, t.org_id
                    FROM tech_gate_records r
                    JOIN terminals t ON t.device_id = r.device_id
                    WHERE r.id = :record_id AND r.function_name = 'inkass'
                """),
                {"record_id": record_id},
            )
        ).fetchone()
        if not record_row:
            raise HTTPException(status_code=404, detail="Инкассация не найдена")
        if effective_org_id > 0 and record_row[5] != effective_org_id:
            raise HTTPException(status_code=403, detail="Доступ запрещен")

        request_data = dict(record_row[3] or {})

        if body.strategy_id == "no_change":
            request_data["calc_status"] = "mismatch"
            request_data["calc_applied_by_user"] = user.get("username", "user")
            request_data["calc_applied_at"] = datetime.now(UTC).isoformat()
            await session.execute(
                update(TechGateRecord)
                .where(TechGateRecord.id == record_id)
                .values(request_data=request_data)
            )
            await session.commit()
            return {
                "status": "ok",
                "calc_status": "mismatch",
                "calculated_sum": request_data.get("calc_cash_sum"),
                "delta": request_data.get("calc_delta"),
            }

        org_tz = await _resolve_org_tz(session, record_row[5])

        preview = await _calculate_strategies_preview(
            session,
            record_row[0],
            record_row[1],
            record_row[4],
            record_row[2],
            request_data,
            org_tz=org_tz,
        )
        st = next(
            (s for s in preview["strategies"] if s["id"] == body.strategy_id),
            None,
        )
        if not st:
            raise HTTPException(
                status_code=400,
                detail=f"Неизвестная стратегия: {body.strategy_id}",
            )

        calculated_cash = st["calculated_cash"]
        delta = st["delta"]
        is_matched = st["is_matched"]

        request_data["calc_status"] = (
            "matched"
            if is_matched
            else ("needs_calc" if calculated_cash is None else "mismatch")
        )
        request_data["calc_cash_sum"] = calculated_cash
        request_data["calculated_sum"] = calculated_cash
        request_data["calc_delta"] = delta
        request_data["calc_strategy_applied"] = body.strategy_id
        request_data["calc_lower_paym_ext_id"] = st["lower_bound"]
        request_data["calc_upper_paym_ext_id"] = st["upper_bound"]
        request_data["calc_applied_by_user"] = user.get("username", "user")
        request_data["calc_applied_at"] = datetime.now(UTC).isoformat()

        await session.execute(
            update(TechGateRecord)
            .where(TechGateRecord.id == record_id)
            .values(request_data=request_data)
        )
        await session.commit()

        return {
            "status": "ok",
            "calc_status": request_data["calc_status"],
            "calculated_sum": calculated_cash,
            "delta": delta,
        }


@router.get("/payments")
async def get_payments_report(
    user: dict = Depends(require_tenant_context),
    date_from: str | None = None,
    date_to: str | None = None,
    device_ids: str | None = None,
    tsp_code: int | None = None,
    paym_state: int | None = None,
    top: int = Query(100, ge=10, le=1000),
    org_id: int | None = Query(None),
):
    effective_org_id = resolve_org_id(user, org_id)

    async with async_session() as session:
        org_tz_row = (
            await session.execute(
                select(Org.timezone).where(Org.org_id == effective_org_id)
            )
        ).scalar_one_or_none()
        tz = resolve_tz(org_tz_row)
        tz_name = get_timezone_name(tz)

        if not date_from and not date_to:
            date_from = date_to = get_local_datetime(datetime.now(UTC), tz=tz).strftime(
                "%Y-%m-%d"
            )

        conditions = ["p.org_id = :org_id"]
        params: dict = {"org_id": effective_org_id}
        dt_from_utc, dt_to_utc = get_date_range_bounds_utc(date_from, date_to, tz=tz)
        if dt_from_utc:
            params["dt_from"] = dt_from_utc
            conditions.append("p.paym_datetime >= :dt_from")
        if dt_to_utc:
            params["dt_to"] = dt_to_utc
            conditions.append("p.paym_datetime < :dt_to")
        _add_device_filter(conditions, params, device_ids)
        if tsp_code is not None:
            conditions.append("p.paym_tsp_code = :tsp_code")
            params["tsp_code"] = tsp_code
        if paym_state is not None:
            conditions.append("p.paym_state = :paym_state")
            params["paym_state"] = paym_state
        where = " AND ".join(conditions)
        count_row = (
            await session.execute(
                text(f"""
                    SELECT count(*) FROM payments p
                    JOIN terminals t ON t.id = p.terminal_id WHERE {where}
                """),
                params,
            )
        ).scalar()
        rows = (
            await session.execute(
                text(f"""
                    SELECT p.paym_id, p.paym_datetime, p.paym_amount, p.paym_ext_id,
                           p.paym_tsp_code, p.paym_state, p.pay_type_id,
                           t.device_id, t.sn, p.menu_snapshot_id, p.menu_version
                    FROM payments p JOIN terminals t ON t.id = p.terminal_id
                    WHERE {where} ORDER BY p.paym_datetime DESC LIMIT :top
                """),
                {**params, "top": top},
            )
        ).fetchall()
        if not rows:
            return {"items": [], "total": count_row or 0, "timezone": tz_name}

        payment_ids = [row[0] for row in rows]
        device_id_values = list({row[7] for row in rows})
        tsp_codes = list({row[4] for row in rows})
        snapshot_ids = list({row[9] for row in rows if row[9] is not None})
        snapshots_map: dict[int, dict] = {}
        if snapshot_ids:
            snapshot_rows = (
                await session.execute(
                    text("""
                        SELECT id, snapshot_data FROM menu_variant_snapshots
                        WHERE id = ANY(:s_ids)
                    """),
                    {"s_ids": snapshot_ids},
                )
            ).fetchall()
            for snapshot_row in snapshot_rows:
                snapshot_data = snapshot_row[1]
                if isinstance(snapshot_data, str):
                    parsed = {}
                    with suppress(Exception):
                        parsed = json.loads(snapshot_data)
                    snapshot_data = parsed
                snapshots_map[snapshot_row[0]] = snapshot_data

        binding_rows = (
            await session.execute(
                text("""
                    SELECT b.device_id, s.tsp_code, s.name
                    FROM terminal_menu_bindings b
                    JOIN services s ON s.menu_variant_id = b.menu_variant_id
                    WHERE b.device_id = ANY(:d_ids) AND s.tsp_code = ANY(:tsp_codes)
                """),
                {"d_ids": device_id_values, "tsp_codes": tsp_codes},
            )
        ).fetchall()
        binding_tsp_map = {(row[0], row[1]): row[2] for row in binding_rows}
        org_service_rows = (
            await session.execute(
                text("""
                    SELECT DISTINCT ON (s.tsp_code) s.tsp_code, s.name
                    FROM services s JOIN menu_variants mv ON mv.id = s.menu_variant_id
                    WHERE mv.org_id = :org_id AND s.tsp_code = ANY(:tsp_codes)
                """),
                {"org_id": effective_org_id, "tsp_codes": tsp_codes},
            )
        ).fetchall()
        org_tsp_map = {row[0]: row[1] for row in org_service_rows}
        tsp_rows = (
            await session.execute(
                text(
                    "SELECT tsp_code, tsp_name FROM tsp WHERE tsp_code = ANY(:tsp_codes)"
                ),
                {"tsp_codes": tsp_codes},
            )
        ).fetchall()
        global_tsp_map = {row[0]: row[1] for row in tsp_rows}
        placeholders = ",".join(f":pid_{index}" for index in range(len(payment_ids)))
        parameter_rows = (
            await session.execute(
                text(f"""
                    SELECT pp.paym_id, tpc.parameter_code, tpc.code_description,
                           pp.param_value
                    FROM payment_params pp
                    JOIN tsp_parameter_codes tpc ON tpc.param_id = pp.param_id
                    WHERE pp.paym_id IN ({placeholders})
                    ORDER BY pp.paym_id, tpc.parameter_code
                """),
                {
                    f"pid_{index}": payment_id
                    for index, payment_id in enumerate(payment_ids)
                },
            )
        ).fetchall()
        params_map: dict[int, list[dict]] = {}
        for parameter_row in parameter_rows:
            params_map.setdefault(parameter_row[0], []).append(
                {
                    "code": parameter_row[1],
                    "description": parameter_row[2] or "",
                    "value": parameter_row[3] or "",
                }
            )

    items = []
    for row in rows:
        payment_id, device_id, current_tsp_code, snapshot_id = (
            row[0],
            row[7],
            row[4],
            row[9],
        )
        row_menu_version = row[10] if len(row) > 10 else None
        tsp_name = None
        menu_version = row_menu_version
        if snapshot_id and snapshot_id in snapshots_map:
            snapshot_data = snapshots_map[snapshot_id]
            if isinstance(snapshot_data, dict):
                if menu_version is None:
                    menu_version = snapshot_data.get("version")
                service_info = snapshot_data.get("services_by_tsp", {}).get(
                    str(current_tsp_code)
                ) or snapshot_data.get("services_by_tsp", {}).get(current_tsp_code)
                if isinstance(service_info, dict):
                    tsp_name = service_info.get("name")
        tsp_name = tsp_name or (
            binding_tsp_map.get((device_id, current_tsp_code))
            or org_tsp_map.get(current_tsp_code)
            or global_tsp_map.get(current_tsp_code)
            or str(current_tsp_code)
        )
        items.append(
            {
                "paym_id": payment_id,
                "paym_datetime": to_utc_iso(row[1]) if row[1] else "",
                "paym_amount": row[2],
                "paym_ext_id": (row[3] or "").strip() or str(payment_id),
                "paym_tsp_code": current_tsp_code,
                "tsp_name": tsp_name,
                "menu_version": menu_version,
                "paym_state": row[5],
                "paym_state_label": PAYM_STATE_LABELS.get(row[5], str(row[5])),
                "pay_type_id": row[6],
                "pay_type_label": PAY_TYPE_LABELS.get(row[6], str(row[6])),
                "device_id": device_id,
                "sn": row[8] or "",
                "params": params_map.get(payment_id, []),
            }
        )
    return {"items": items, "total": count_row or 0, "timezone": tz_name}


@router.get("/balance-by-terminal")
async def get_balance_by_terminal(
    user: dict = Depends(require_tenant_context),
    date_from: str | None = None,
    date_to: str | None = None,
    device_ids: str | None = None,
    tsp_code: int | None = None,
    org_id: int | None = Query(None),
):
    effective_org_id = resolve_org_id(user, org_id)
    async with async_session() as session:
        org_tz_row = (
            await session.execute(
                select(Org.timezone).where(Org.org_id == effective_org_id)
            )
        ).scalar_one_or_none()
        tz = resolve_tz(org_tz_row)
        tz_name = get_timezone_name(tz)

        conditions = ["b.org_id = :org_id"]
        params: dict = {"org_id": effective_org_id}
        if date_from and (day_from := _parse_int_day(date_from)) is not None:
            conditions.append("b.int_day >= :day_from")
            params["day_from"] = day_from
        if date_to and (day_to := _parse_int_day(date_to)) is not None:
            conditions.append("b.int_day <= :day_to")
            params["day_to"] = day_to
        _add_device_filter(conditions, params, device_ids)
        if tsp_code is not None:
            conditions.append("ts.tsp_code = :tsp_code")
            params["tsp_code"] = tsp_code
        where = " AND ".join(conditions)
        rows = (
            await session.execute(
                text(f"""
                    SELECT t.device_id, t.sn, t.id AS terminal_id,
                           COUNT(*) AS tsp_count, SUM(b.count) AS total_count,
                           SUM(b.amount) AS total_amount
                    FROM balance_terminal_tsp b
                    JOIN terminals t ON t.id = b.terminal_id
                    JOIN tsp ts ON ts.tsp_id = b.tsp_id
                    WHERE {where} GROUP BY t.device_id, t.sn, t.id
                    ORDER BY total_amount DESC
                """),
                params,
            )
        ).fetchall()
    return {
        "items": [
            {
                "device_id": row[0],
                "sn": (row[1] or "").strip(),
                "terminal_id": row[2],
                "tsp_count": row[3],
                "total_count": row[4],
                "total_amount": row[5],
            }
            for row in rows
        ],
        "timezone": tz_name,
    }


@router.get("/balance-by-tsp")
async def get_balance_by_tsp(
    user: dict = Depends(require_tenant_context),
    date_from: str | None = None,
    date_to: str | None = None,
    device_ids: str | None = None,
    org_id: int | None = Query(None),
):
    effective_org_id = resolve_org_id(user, org_id)
    async with async_session() as session:
        org_tz_row = (
            await session.execute(
                select(Org.timezone).where(Org.org_id == effective_org_id)
            )
        ).scalar_one_or_none()
        tz = resolve_tz(org_tz_row)
        tz_name = get_timezone_name(tz)

        conditions = ["b.org_id = :org_id"]
        params: dict = {"org_id": effective_org_id}
        if date_from and (day_from := _parse_int_day(date_from)) is not None:
            conditions.append("b.int_day >= :day_from")
            params["day_from"] = day_from
        if date_to and (day_to := _parse_int_day(date_to)) is not None:
            conditions.append("b.int_day <= :day_to")
            params["day_to"] = day_to
        _add_device_filter(conditions, params, device_ids)
        where = " AND ".join(conditions)
        rows = (
            await session.execute(
                text(f"""
                    SELECT ts.tsp_code, ts.tsp_name, b.menu_version, b.menu_snapshot_id,
                           COUNT(DISTINCT b.terminal_id) AS terminal_count,
                           SUM(b.count) AS total_count,
                           SUM(b.amount) AS total_amount
                    FROM balance_terminal_tsp b
                    JOIN terminals t ON t.id = b.terminal_id
                    JOIN tsp ts ON ts.tsp_id = b.tsp_id
                    WHERE {where}
                    GROUP BY ts.tsp_code, ts.tsp_name, b.menu_version, b.menu_snapshot_id
                    ORDER BY ts.tsp_code, b.menu_version
                """),
                params,
            )
        ).fetchall()
        if not rows:
            return {"items": [], "timezone": tz_name}
        snapshot_ids = list(
            {
                (row[3] if len(row) >= 7 else row[2])
                for row in rows
                if (row[3] if len(row) >= 7 else row[2]) is not None
            }
        )
        snapshots_map: dict[int, dict] = {}
        if snapshot_ids:
            snapshot_rows = (
                await session.execute(
                    text("""
                        SELECT id, snapshot_data FROM menu_variant_snapshots
                        WHERE id = ANY(:s_ids)
                    """),
                    {"s_ids": snapshot_ids},
                )
            ).fetchall()
            for snapshot_row in snapshot_rows:
                snapshot_data = snapshot_row[1]
                if isinstance(snapshot_data, str):
                    parsed = {}
                    with suppress(Exception):
                        parsed = json.loads(snapshot_data)
                    snapshot_data = parsed
                snapshots_map[snapshot_row[0]] = snapshot_data
        tsp_codes = list({row[0] for row in rows})
        org_service_rows = (
            await session.execute(
                text("""
                    SELECT DISTINCT ON (s.tsp_code) s.tsp_code, s.name
                    FROM services s JOIN menu_variants mv ON mv.id = s.menu_variant_id
                    WHERE mv.org_id = :org_id AND s.tsp_code = ANY(:tsp_codes)
                """),
                {"org_id": effective_org_id, "tsp_codes": tsp_codes},
            )
        ).fetchall()
        org_tsp_map = {row[0]: row[1] for row in org_service_rows}

    grouped: dict[tuple[int, int | None], dict] = {}
    for row in rows:
        if len(row) >= 7:
            (
                tsp_code,
                global_name,
                menu_version,
                snapshot_id,
                terminal_count,
                count,
                amount,
            ) = row[:7]
        else:
            tsp_code, global_name, snapshot_id, _term_id, count, amount = row[:6]
            menu_version = None
            terminal_count = 1
        tsp_name = None
        if snapshot_id and snapshot_id in snapshots_map:
            snapshot_data = snapshots_map[snapshot_id]
            if isinstance(snapshot_data, dict):
                if menu_version is None:
                    menu_version = snapshot_data.get("version")
                service_info = snapshot_data.get("services_by_tsp", {}).get(
                    str(tsp_code)
                ) or snapshot_data.get("services_by_tsp", {}).get(tsp_code)
                if isinstance(service_info, dict):
                    tsp_name = service_info.get("name")
        tsp_name = (
            tsp_name
            or org_tsp_map.get(tsp_code)
            or (global_name.strip() if global_name else "")
            or str(tsp_code)
        )
        key = (tsp_code, menu_version)
        if key not in grouped:
            grouped[key] = {
                "tsp_code": tsp_code,
                "version": menu_version,
                "tsp_name": tsp_name,
                "terminal_count": terminal_count or 0,
                "total_count": int(count or 0),
                "total_amount": int(amount or 0),
            }
        else:
            grouped[key]["terminal_count"] += terminal_count or 0
            grouped[key]["total_count"] += int(count or 0)
            grouped[key]["total_amount"] += int(amount or 0)
            if tsp_name and snapshot_id:
                grouped[key]["tsp_name"] = tsp_name
    return {
        "items": [
            {
                "tsp_code": item["tsp_code"],
                "version": item["version"],
                "tsp_name": item["tsp_name"],
                "terminal_count": item["terminal_count"],
                "total_count": item["total_count"],
                "total_amount": item["total_amount"],
            }
            for item in sorted(
                grouped.values(), key=lambda value: value["total_amount"], reverse=True
            )
        ],
        "timezone": tz_name,
    }
