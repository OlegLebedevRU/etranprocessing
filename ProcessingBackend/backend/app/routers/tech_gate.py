from fastapi import APIRouter, Depends, HTTPException, Request
from fastapi.responses import Response
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.dependencies import get_current_terminal
from app.models import TechGateRecord, Terminal

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


async def save_tech_gate_record(
    db: AsyncSession,
    terminal: Terminal,
    function_name: str,
    request_data: dict,
    raw_params: str,
    response_status: str = "ok",
):
    record = TechGateRecord(
        device_id=terminal.device_id,
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
