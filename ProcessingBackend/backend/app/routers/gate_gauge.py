from fastapi import APIRouter, Depends, Request
from fastapi.responses import Response
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.dependencies import get_current_terminal
from app.models import GateGaugeRecord, Terminal

router = APIRouter()


def xml_response(content: str) -> Response:
    return Response(
        content=f"<?xml version='1.0' encoding='UTF-8'?>\n{content}",
        media_type="application/xml",
    )


def parse_gauge_pack(raw: str) -> dict:
    """Parse semicolon-delimited resource=value pairs into JSON dict."""
    result = {}
    for pair in raw.split(";"):
        pair = pair.strip()
        if "=" in pair:
            key, value = pair.split("=", 1)
            try:
                result[int(key)] = value
            except ValueError:
                result[key] = value
    return result


@router.post("")
async def post_gauge(
    request: Request,
    terminal: Terminal = Depends(get_current_terminal),
    db: AsyncSession = Depends(get_db),
):
    """
    GateGauge endpoint - receives telemetry data from terminals.
    Compatible with legacy GateGauge main.ashx format.
    Stores in JSONB with circular buffer (24 records per terminal).
    """
    body = await request.body()
    raw_text = body.decode("windows-1251", errors="replace")

    # Parse GaugePack from form data
    gauge_raw = ""
    for param in raw_text.split("&"):
        if param.startswith("GaugePack="):
            gauge_raw = param[len("GaugePack="):]
            break

    if not gauge_raw:
        return xml_response(
            "<Response>"
            "<Result>ERROR</Result>"
            "<description>Missing GaugePack parameter</description>"
            "</Response>"
        )

    gauge_data = parse_gauge_pack(gauge_raw)

    record = GateGaugeRecord(
        device_id=terminal.device_id,
        sn=terminal.sn,
        gauge_data=gauge_data,
        raw_data=gauge_raw,
    )
    db.add(record)
    await db.commit()

    return xml_response(
        "<Response>"
        "<Result>OK</Result>"
        "</Response>"
    )
