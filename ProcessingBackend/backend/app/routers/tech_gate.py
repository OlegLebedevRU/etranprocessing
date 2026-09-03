import logging

from fastapi import APIRouter, Depends, HTTPException, Request
from fastapi.responses import Response
from sqlalchemy import select, text
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.dependencies import get_current_terminal
from app.models import Payment, TechGateRecord, Terminal

logger = logging.getLogger(__name__)

router = APIRouter()

FUNCTION_MAP = {
    "devicestatus": "devicestatus",
    "inkass": "inkass",
    "closeshift": "closeshift",
    "closeshift2": "closeshift2",
    "getshiftreport": "getshiftreport",
    "getinkassreport": "getinkassreport",
    "kiosk": "kiosk",
    "tsplist": "tsplist",
}


def get_params_from_request(request: Request, body: bytes) -> dict:
    """Extract params from query string and/or POST body."""
    params = dict(request.query_params)
    if body:
        raw = body.decode("windows-1251", errors="replace")
        for param in raw.split("&"):
            if "=" in param:
                k, v = param.split("=", 1)
                params[k] = v
    return params


@router.get("")
@router.post("")
@router.get("/etran.ashx")
@router.post("/etran.ashx")
async def techgate_dispatch(
    request: Request,
    terminal: Terminal = Depends(get_current_terminal),
    db: AsyncSession = Depends(get_db),
):
    """
    Legacy dispatcher endpoint.
    Reads 'function' query param and routes to the correct handler.
    Handles: /api/techgate/etran.ashx?function=inkass&...
    """
    body = await request.body()
    params = get_params_from_request(request, body)
    function_name = params.get("function", "").lower()

    if function_name not in FUNCTION_MAP:
        raise HTTPException(
            status_code=400, detail=f"Unknown function: {function_name}"
        )

    await save_tech_gate_record(db, terminal, function_name, params, str(params))

    if function_name == "devicestatus":
        return xml_response("<Response><Result>OK</Result></Response>")
    elif function_name in ("inkass",):
        return xml_response(
            "<Response>"
            "<Result>OK</Result>"
            f"<PaymExtId>{params.get('InkassExtId', '')}</PaymExtId>"
            "<Description>1</Description>"
            "</Response>"
        )
    elif function_name in ("closeshift", "closeshift2"):
        return xml_response(
            "<Response>"
            "<Result>OK</Result>"
            f"<PaymExtId>{params.get('LastPaymExtId', '')}</PaymExtId>"
            "<Description>1</Description>"
            "</Response>"
        )
    elif function_name == "getshiftreport":
        return xml_response(
            "<Response>"
            "<Result>OK</Result>"
            "<ShiftReport>"
            f"<KioskNumber>{terminal.sn}</KioskNumber>"
            "<Records/>"
            "</ShiftReport>"
            "</Response>"
        )
    elif function_name == "getinkassreport":
        return xml_response(
            "<Response>"
            "<Result>OK</Result>"
            "<InkassReport>"
            f"<KioskNumber>{terminal.sn}</KioskNumber>"
            "<Records/>"
            "</InkassReport>"
            "</Response>"
        )
    elif function_name == "kiosk":
        return xml_response(
            "<Response><Result>OK</Result><Kiosk24>0</Kiosk24></Response>"
        )
    elif function_name == "tsplist":
        return xml_response("<Response><Result>OK</Result><TspList/></Response>")
    else:
        return xml_response("<Response><Result>OK</Result></Response>")


def xml_response(content: str) -> Response:
    return Response(
        content=f"<?xml version='1.0' encoding='UTF-8'?>\n{content}",
        media_type="application/xml",
    )


async def _process_inkass_at_receipt(
    db: AsyncSession,
    terminal: Terminal,
    params: dict,
) -> None:
    """Exact calculation of inkassation at receipt time.

    Rules:
    - Only the exact strategy is attempted automatically.
    - If PaymExtId is not provided or not yet committed in payments:
      calc_status = 'needs_calc', needs_calc = True, calc_cash_sum = None.
    - If PaymExtId is found in payments:
      Find previous inkassation:
        - Priority 1: InkassId / cntInkass < curr_inkass_id
        - Priority 2: created_at
      Calculate cash sum (pay_type_id IN (0, 1), paym_state = 2).
      Compare with TotalSum: if match, 'matched', else 'mismatch'.
    """
    curr_paym_ext_id = str(params.get("PaymExtId") or "").strip()
    total_sum_fact = 0
    try:
        total_sum_fact = int(params.get("TotalSum") or params.get("TotalNoteSum") or 0)
    except ValueError, TypeError:
        total_sum_fact = 0

    if not curr_paym_ext_id:
        params["calc_status"] = "needs_calc"
        params["needs_calc"] = True
        params["calc_cash_sum"] = None
        params["calculated_sum"] = None
        params["calc_delta"] = None
        params["calc_strategy_applied"] = None
        params["calc_upper_paym_ext_id"] = None
        params["calc_lower_paym_ext_id"] = None
        return

    # Check if curr_paym_ext_id exists in payments for this terminal
    curr_payment_res = await db.execute(
        select(Payment.paym_id, Payment.paym_datetime)
        .where(
            Payment.terminal_id == terminal.id,
            Payment.paym_ext_id == curr_paym_ext_id,
        )
        .order_by(Payment.paym_id.desc())
        .limit(1)
    )
    curr_payment_row = curr_payment_res.fetchone() if curr_payment_res else None

    if not curr_payment_row:
        # PaymExtId not in payments yet (delayed network packet)
        params["calc_status"] = "needs_calc"
        params["needs_calc"] = True
        params["calc_cash_sum"] = None
        params["calculated_sum"] = None
        params["calc_delta"] = None
        params["calc_strategy_applied"] = None
        params["calc_upper_paym_ext_id"] = curr_paym_ext_id
        params["calc_lower_paym_ext_id"] = None
        return

    curr_paym_id = curr_payment_row[0]

    # Search for previous inkassation
    curr_inkass_id_raw = params.get("InkassId") or params.get("cntInkass")
    curr_inkass_id = None
    if curr_inkass_id_raw and str(curr_inkass_id_raw).isdigit():
        curr_inkass_id = int(curr_inkass_id_raw)

    prev_record_row = None
    # Priority 1: By InkassId
    if curr_inkass_id and curr_inkass_id > 1:
        prev_res = await db.execute(
            text("""
                SELECT id, request_data, created_at
                FROM tech_gate_records
                WHERE device_id = :dev_id AND function_name = 'inkass'
                  AND COALESCE((request_data->>'InkassId')::bigint, (request_data->>'cntInkass')::bigint, 0) < :curr_ink_id
                ORDER BY COALESCE((request_data->>'InkassId')::bigint, (request_data->>'cntInkass')::bigint, 0) DESC, id DESC
                LIMIT 1
            """),
            {"dev_id": terminal.device_id, "curr_ink_id": curr_inkass_id},
        )
        prev_record_row = prev_res.fetchone() if prev_res else None

    # Priority 2 / 3: By created_at (or id)
    if not prev_record_row:
        prev_res = await db.execute(
            text("""
                SELECT id, request_data, created_at
                FROM tech_gate_records
                WHERE device_id = :dev_id AND function_name = 'inkass'
                ORDER BY created_at DESC, id DESC
                LIMIT 1
            """),
            {"dev_id": terminal.device_id},
        )
        prev_record_row = prev_res.fetchone() if prev_res else None

    lower_paym_id = None
    lower_bound_ext_id = None

    if prev_record_row:
        prev_data = prev_record_row[1] or {}
        lower_bound_ext_id = str(
            prev_data.get("calc_upper_paym_ext_id") or prev_data.get("PaymExtId") or ""
        ).strip()
        if lower_bound_ext_id:
            prev_payment_res = await db.execute(
                select(Payment.paym_id)
                .where(
                    Payment.terminal_id == terminal.id,
                    Payment.paym_ext_id == lower_bound_ext_id,
                )
                .order_by(Payment.paym_id.desc())
                .limit(1)
            )
            prev_payment_row = prev_payment_res.fetchone() if prev_payment_res else None
            if prev_payment_row:
                lower_paym_id = prev_payment_row[0]

    # If previous inkassation was found, but its lower payment was not found:
    if prev_record_row and not lower_paym_id:
        params["calc_status"] = "needs_calc"
        params["needs_calc"] = True
        params["calc_cash_sum"] = None
        params["calculated_sum"] = None
        params["calc_delta"] = None
        params["calc_strategy_applied"] = None
        params["calc_upper_paym_ext_id"] = curr_paym_ext_id
        params["calc_lower_paym_ext_id"] = lower_bound_ext_id
        return

    # Calculate sum (cash payments: pay_type_id IN (0, 1), paym_state == 2, excluding 1 ruble / 100 kopecks)
    if lower_paym_id is not None:
        sum_query = """
            SELECT COALESCE(SUM(paym_amount), 0) FROM payments
            WHERE terminal_id = :term_id AND paym_state = 2 AND pay_type_id IN (0, 1)
              AND paym_amount != 100
              AND paym_id > :prev_id AND paym_id <= :curr_id
        """
        q_params = {
            "term_id": terminal.id,
            "prev_id": lower_paym_id,
            "curr_id": curr_paym_id,
        }
    else:
        # First inkassation on terminal
        sum_query = """
            SELECT COALESCE(SUM(paym_amount), 0) FROM payments
            WHERE terminal_id = :term_id AND paym_state = 2 AND pay_type_id IN (0, 1)
              AND paym_amount != 100
              AND paym_id <= :curr_id
        """
        q_params = {
            "term_id": terminal.id,
            "curr_id": curr_paym_id,
        }

    sum_res = await db.execute(text(sum_query), q_params)
    sum_kopecks = (sum_res.scalar() if sum_res else 0) or 0
    calc_cash_sum = int(sum_kopecks) // 100

    calc_status = "matched" if calc_cash_sum == total_sum_fact else "mismatch"
    calc_delta = total_sum_fact - calc_cash_sum

    params["calc_status"] = calc_status
    params["needs_calc"] = False
    params["calc_cash_sum"] = calc_cash_sum
    params["calculated_sum"] = calc_cash_sum
    params["calc_delta"] = calc_delta
    params["calc_strategy_applied"] = "exact_paym_ext_id"
    params["calc_upper_paym_ext_id"] = curr_paym_ext_id
    params["calc_lower_paym_ext_id"] = lower_bound_ext_id


async def save_tech_gate_record(
    db: AsyncSession,
    terminal: Terminal,
    function_name: str,
    request_data: dict,
    raw_params: str,
    response_status: str = "ok",
):
    if function_name == "inkass":
        try:
            await _process_inkass_at_receipt(db, terminal, request_data)
        except Exception as e:  # noqa: BLE001
            logger.warning("Error during receipt inkass calculation: %s", e)
            request_data.setdefault("calc_status", "needs_calc")
            request_data.setdefault("needs_calc", True)
            request_data.setdefault("calculated_sum", None)

    record = TechGateRecord(
        device_id=terminal.device_id or 0,
        sn=terminal.sn,
        function_name=function_name,
        request_data=request_data,
        raw_params=raw_params,
        response_status=response_status,
    )
    db.add(record)
    await db.commit()


@router.get("/devicestatus")
@router.post("/devicestatus")
async def devicestatus(
    request: Request,
    terminal: Terminal = Depends(get_current_terminal),
    db: AsyncSession = Depends(get_db),
):
    params = dict(request.query_params)
    if request.method == "POST":
        body = await request.body()
        raw = body.decode("windows-1251", errors="replace")
        for param in raw.split("&"):
            if "=" in param:
                k, v = param.split("=", 1)
                params[k] = v

    await save_tech_gate_record(db, terminal, "devicestatus", params, str(params))

    return xml_response("<Response><Result>OK</Result></Response>")


@router.get("/inkass")
@router.post("/inkass")
async def inkass(
    request: Request,
    terminal: Terminal = Depends(get_current_terminal),
    db: AsyncSession = Depends(get_db),
):
    params = dict(request.query_params)
    if request.method == "POST":
        body = await request.body()
        raw = body.decode("windows-1251", errors="replace")
        for param in raw.split("&"):
            if "=" in param:
                k, v = param.split("=", 1)
                params[k] = v

    await save_tech_gate_record(db, terminal, "inkass", params, str(params))

    return xml_response(
        "<Response>"
        "<Result>OK</Result>"
        f"<PaymExtId>{params.get('InkassExtId', '')}</PaymExtId>"
        "<Description>1</Description>"
        "</Response>"
    )


@router.get("/closeshift")
@router.post("/closeshift")
async def closeshift(
    request: Request,
    terminal: Terminal = Depends(get_current_terminal),
    db: AsyncSession = Depends(get_db),
):
    params = dict(request.query_params)
    if request.method == "POST":
        body = await request.body()
        raw = body.decode("windows-1251", errors="replace")
        for param in raw.split("&"):
            if "=" in param:
                k, v = param.split("=", 1)
                params[k] = v

    await save_tech_gate_record(db, terminal, "closeshift", params, str(params))

    return xml_response(
        "<Response>"
        "<Result>OK</Result>"
        f"<PaymExtId>{params.get('LastPaymExtId', '')}</PaymExtId>"
        "<Description>1</Description>"
        "</Response>"
    )


@router.get("/closeshift2")
@router.post("/closeshift2")
async def closeshift2(
    request: Request,
    terminal: Terminal = Depends(get_current_terminal),
    db: AsyncSession = Depends(get_db),
):
    params = dict(request.query_params)
    if request.method == "POST":
        body = await request.body()
        raw = body.decode("windows-1251", errors="replace")
        for param in raw.split("&"):
            if "=" in param:
                k, v = param.split("=", 1)
                params[k] = v

    await save_tech_gate_record(db, terminal, "closeshift2", params, str(params))

    return xml_response(
        "<Response>"
        "<Result>OK</Result>"
        f"<PaymExtId>{params.get('LastPaymExtId', '')}</PaymExtId>"
        "<Description>1</Description>"
        "</Response>"
    )


@router.get("/getshiftreport")
@router.post("/getshiftreport")
async def getshiftreport(
    request: Request,
    terminal: Terminal = Depends(get_current_terminal),
    db: AsyncSession = Depends(get_db),
):
    params = dict(request.query_params)
    if request.method == "POST":
        body = await request.body()
        raw = body.decode("windows-1251", errors="replace")
        for param in raw.split("&"):
            if "=" in param:
                k, v = param.split("=", 1)
                params[k] = v

    await save_tech_gate_record(db, terminal, "getshiftreport", params, str(params))

    # Return empty shift report (stub)
    return xml_response(
        "<Response>"
        "<Result>OK</Result>"
        "<ShiftReport>"
        f"<KioskNumber>{terminal.sn}</KioskNumber>"
        "<Records/>"
        "</ShiftReport>"
        "</Response>"
    )


@router.get("/getinkassreport")
@router.post("/getinkassreport")
async def getinkassreport(
    request: Request,
    terminal: Terminal = Depends(get_current_terminal),
    db: AsyncSession = Depends(get_db),
):
    params = dict(request.query_params)
    if request.method == "POST":
        body = await request.body()
        raw = body.decode("windows-1251", errors="replace")
        for param in raw.split("&"):
            if "=" in param:
                k, v = param.split("=", 1)
                params[k] = v

    await save_tech_gate_record(db, terminal, "getinkassreport", params, str(params))

    return xml_response(
        "<Response>"
        "<Result>OK</Result>"
        "<InkassReport>"
        f"<KioskNumber>{terminal.sn}</KioskNumber>"
        "<Records/>"
        "</InkassReport>"
        "</Response>"
    )


@router.get("/kiosk")
@router.post("/kiosk")
async def kiosk(
    request: Request,
    terminal: Terminal = Depends(get_current_terminal),
    db: AsyncSession = Depends(get_db),
):
    params = dict(request.query_params)
    if request.method == "POST":
        body = await request.body()
        raw = body.decode("windows-1251", errors="replace")
        for param in raw.split("&"):
            if "=" in param:
                k, v = param.split("=", 1)
                params[k] = v

    await save_tech_gate_record(db, terminal, "kiosk", params, str(params))

    return xml_response("<Response><Result>OK</Result><Kiosk24>0</Kiosk24></Response>")


@router.get("/tsplist")
@router.post("/tsplist")
async def tsplist(
    request: Request,
    terminal: Terminal = Depends(get_current_terminal),
    db: AsyncSession = Depends(get_db),
):
    params = dict(request.query_params)
    if request.method == "POST":
        body = await request.body()
        raw = body.decode("windows-1251", errors="replace")
        for param in raw.split("&"):
            if "=" in param:
                k, v = param.split("=", 1)
                params[k] = v

    await save_tech_gate_record(db, terminal, "tsplist", params, str(params))

    return xml_response("<Response><Result>OK</Result><TspList/></Response>")
